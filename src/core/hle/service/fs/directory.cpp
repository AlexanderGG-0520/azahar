// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include "common/archives.h"
#include "common/logging/log.h"
#include "core/file_sys/directory_backend.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/fs/directory.h"
#include "core/hle/service/level256_registration_diagnostics.h"

SERIALIZE_EXPORT_IMPL(Service::FS::Directory)

namespace Service::FS {

template <class Archive>
void Directory::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & path;
    ar & backend;
}

Directory::Directory(std::unique_ptr<FileSys::DirectoryBackend>&& backend,
                     const FileSys::Path& path)
    : Directory() {
    this->backend = std::move(backend);
    this->path = path;
}

Directory::Directory() : ServiceFramework("", 1), path(""), backend(nullptr) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0801, &Directory::Read, "Read"},
        {0x0802, &Directory::Close, "Close"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

Directory::~Directory() {}

void Directory::Read(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const std::string guest_path = path.DebugStr();
    const u32 diagnostic_event = Level256RegistrationDiagnostics::IsAccountPath(guest_path)
                                     ? Level256RegistrationDiagnostics::NextEventId()
                                     : 0;
    const auto caller_thread = ctx.ClientThread();
    const auto caller_process = caller_thread ? caller_thread->owner_process.lock() : nullptr;
    const u32 caller_pid = caller_process ? caller_process->process_id : 0;
    const u64 caller_title =
        caller_process && caller_process->codeset ? caller_process->codeset->program_id : 0;

    struct AsyncData {
        // Input
        u32 count;

        // Output
        Result ret{0};
        u32 read;
        Kernel::MappedBuffer* buffer;
    };

    auto async_data = std::make_shared<AsyncData>();
    async_data->count = rp.Pop<u32>();
    async_data->buffer = &rp.PopMappedBuffer();
    if (diagnostic_event != 0) {
        LOG_INFO(Service_FS,
                 "[L256-REG] event={} op=fs.read_directory.begin pid={} "
                 "title_id=0x{:016X} path={} requested_entries={}",
                 diagnostic_event, caller_pid, caller_title,
                 Level256RegistrationDiagnostics::SafePathLabel(guest_path), async_data->count);
    }

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            std::vector<FileSys::Entry> entries(async_data->count);
            LOG_TRACE(Service_FS, "Read {}: count={}", GetName(), async_data->count);
            // Number of entries actually read
            async_data->read = backend->Read(static_cast<u32>(entries.size()), entries.data());
            async_data->buffer->Write(entries.data(), 0, async_data->read * sizeof(FileSys::Entry));
            return 0;
        },
        [async_data, diagnostic_event, caller_pid, caller_title,
         guest_path](Kernel::HLERequestContext& ctx) {
            if (diagnostic_event != 0) {
                const u32 end_event = Level256RegistrationDiagnostics::NextEventId();
                if (end_event != 0) {
                    LOG_INFO(Service_FS,
                             "[L256-REG] event={} op=fs.read_directory.end pid={} "
                             "title_id=0x{:016X} path={} entries_read={} result=0x{:08X}",
                             end_event, caller_pid, caller_title,
                             Level256RegistrationDiagnostics::SafePathLabel(guest_path),
                             async_data->read, ResultSuccess.raw);
                }
            }
            IPC::RequestBuilder rb(ctx, 2, 2);

            rb.Push(ResultSuccess);
            rb.Push(async_data->read);
            rb.PushMappedBuffer(*async_data->buffer);
        },
        Settings::values.async_fs_operations.GetValue());
}

void Directory::Close(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const std::string guest_path = path.DebugStr();
    const u32 diagnostic_event = Level256RegistrationDiagnostics::IsAccountPath(guest_path)
                                     ? Level256RegistrationDiagnostics::NextEventId()
                                     : 0;
    const auto caller_thread = ctx.ClientThread();
    const auto caller_process = caller_thread ? caller_thread->owner_process.lock() : nullptr;
    const u32 caller_pid = caller_process ? caller_process->process_id : 0;
    const u64 caller_title =
        caller_process && caller_process->codeset ? caller_process->codeset->program_id : 0;
    LOG_TRACE(Service_FS, "Close {}", GetName());

    ctx.RunAsync(
        [this, diagnostic_event, caller_pid, caller_title,
         guest_path](Kernel::HLERequestContext& ctx) {
            backend->Close();
            if (diagnostic_event != 0) {
                const u32 end_event = Level256RegistrationDiagnostics::NextEventId();
                if (end_event != 0) {
                    LOG_INFO(Service_FS,
                             "[L256-REG] event={} op=fs.close_directory pid={} "
                             "title_id=0x{:016X} path={} backend_status=not_exposed "
                             "ipc_result=0x{:08X}",
                             end_event, caller_pid, caller_title,
                             Level256RegistrationDiagnostics::SafePathLabel(guest_path),
                             ResultSuccess.raw);
                }
            }
            return 0;
        },
        [](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 1, 0);
            rb.Push(ResultSuccess);
        },
        Settings::values.async_fs_operations.GetValue());
}

} // namespace Service::FS
