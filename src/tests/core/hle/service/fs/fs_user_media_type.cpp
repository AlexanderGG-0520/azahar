// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "common/file_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/service/fs/fs_user.h"
#include "core/hle/service/sm/sm.h"
#include "core/memory.h"

namespace {

struct TestEnvironment {
    Core::Timing timing{1, 100};
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel{memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1};
    Service::SM::ServiceManager manager{system, kernel};
    Service::FS::ArchiveManager archives{system};

    std::shared_ptr<Kernel::Process> MakeProcess() {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("fs-media-type-test", 0));
        process->resource_limit =
            kernel.ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);
        process->status = Kernel::ProcessStatus::Running;
        auto code_mem = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
        const MemoryRef code_ref{code_mem};
        if (process->vm_manager
                .MapBackingMemory(0x00100000, code_ref, Memory::CITRA_PAGE_SIZE,
                                  Kernel::MemoryState::Code)
                .Failed()) {
            throw std::runtime_error("failed to map mock process code");
        }
        return process;
    }

    std::shared_ptr<Kernel::Thread> MakeThread(const std::shared_ptr<Kernel::Process>& process) {
        auto thread = kernel.CreateThread("fs-media-type-test", 0x00100000, 0x30, 0, 0, 0x08000000,
                                          process, false);
        if (thread.Failed()) {
            throw std::runtime_error("failed to create mock process thread");
        }
        auto thread_ptr = thread.Unwrap();
        thread_ptr->status = Kernel::ThreadStatus::Running;
        return thread_ptr;
    }

    void Send(const std::shared_ptr<Kernel::Process>& process,
              const std::shared_ptr<Kernel::Thread>& thread,
              const std::shared_ptr<Kernel::ClientSession>& session,
              const std::array<u32, IPC::COMMAND_BUFFER_LENGTH>& request) {
        memory.WriteBlock(*process, thread->GetCommandBufferAddress(), request.data(),
                          request.size() * sizeof(u32));
        thread->status = Kernel::ThreadStatus::Running;
        REQUIRE(session->SendSyncRequest(thread) == ResultSuccess);
    }

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> ReadCommand(
        const std::shared_ptr<Kernel::Process>& process,
        const std::shared_ptr<Kernel::Thread>& thread) {
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> result{};
        memory.ReadBlock(*process, thread->GetCommandBufferAddress(), result.data(),
                         result.size() * sizeof(u32));
        return result;
    }
};

struct MediaTypeClient {
    std::shared_ptr<Kernel::Process> process;
    std::shared_ptr<Kernel::Thread> thread;
};

MediaTypeClient MakeClient(TestEnvironment& env, Service::FS::FS_USER& fs_user,
                           const std::string& filepath) {
    auto process = env.MakeProcess();
    auto thread = env.MakeThread(process);
    fs_user.RegisterProgramInfo(process->process_id, process->codeset->program_id, filepath);
    return {std::move(process), std::move(thread)};
}

} // namespace

TEST_CASE("FS_USER GetMediaType returns the registered calling process media type",
          "[core][service][fs][media_type]") {
    TestEnvironment env;
    auto fs_user = std::make_shared<Service::FS::FS_USER>(env.system, env.archives);
    fs_user->InstallAsService(env.manager);
    std::shared_ptr<Kernel::ClientSession> session;
    REQUIRE(env.manager.ConnectToService(&session, "fs:USER") == ResultSuccess);

    const std::string nand_path =
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "title/00040000/0016c700";
    const std::string sdmc_path = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                                  "Nintendo 3DS/test/title/00040000/0016c600";
    const auto nand = MakeClient(env, *fs_user, nand_path);
    const auto sdmc = MakeClient(env, *fs_user, sdmc_path);
    // Paths outside NAND and the Nintendo 3DS SD tree are classified as GameCard.
    const auto gamecard = MakeClient(env, *fs_user, "/tmp/test-cartridge.3ds");

    const auto request_media_type = [&env, &session](const MediaTypeClient& client, u32 expected) {
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
        request[0] = 0x08680000;
        env.Send(client.process, client.thread, session, request);
        const auto response = env.ReadCommand(client.process, client.thread);
        REQUIRE(response[0] == 0x08680080);
        REQUIRE(response[1] == 0);
        REQUIRE(response[2] == expected);
    };

    SECTION("NAND, SDMC, and GameCard results stay associated with each caller") {
        request_media_type(nand, 0);
        request_media_type(sdmc, 1);
        request_media_type(gamecard, 2);
        request_media_type(sdmc, 1);
        request_media_type(nand, 0);
    }

    SECTION("a process without registered launch information gets an error and zero media") {
        auto unregistered_process = env.MakeProcess();
        auto thread = env.MakeThread(unregistered_process);
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
        request[0] = 0x08680000;
        env.Send(unregistered_process, thread, session, request);
        const auto response = env.ReadCommand(unregistered_process, thread);

        REQUIRE(response[0] == 0x08680080);
        REQUIRE(response[1] == Result(FileSys::ErrCodes::ArchiveNotMounted, ErrorModule::FS,
                                      ErrorSummary::NotFound, ErrorLevel::Status)
                                   .raw);
        REQUIRE(response[2] == 0);
    }
}
