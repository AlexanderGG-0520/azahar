// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Originally MIT-licensed code from The Pixellizer Group

// Copyright 2022 The Pixellizer Group
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <cstring>
#include <boost/serialization/weak_ptr.hpp>
#include <fmt/format.h>
#include "common/archives.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/plugin_3gx.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/plgldr/level256_lfcs.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/hle/service/plgldr/plugin_path.h"
#include "core/hw/aes/key.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "video_core/gpu.h"

SERIALIZE_EXPORT_IMPL(Service::PLGLDR::PLG_LDR)
SERVICE_CONSTRUCT_IMPL(Service::PLGLDR::PLG_LDR)

namespace Service::PLGLDR {

static const Kernel::CoreVersion plgldr_version = Kernel::CoreVersion(1, 0, 2);

PluginPathSource ChoosePluginPathSource(u64 title_id, bool level256_file_exists,
                                        bool select_pressed, bool title_plugin_exists,
                                        bool default_plugin_exists) {
    if (level256_file_exists && !select_pressed && IsLevel256SupportedTitle(title_id)) {
        return PluginPathSource::Level256;
    }
    if (title_plugin_exists) {
        return PluginPathSource::TitleSpecific;
    }
    if (default_plugin_exists) {
        return PluginPathSource::Default;
    }
    return PluginPathSource::None;
}

std::string MakeGuestPluginPath(std::string plugin_path, const std::string& sdmc_root) {
    std::string normalized_root = sdmc_root;
    std::replace(plugin_path.begin(), plugin_path.end(), '\\', '/');
    std::replace(normalized_root.begin(), normalized_root.end(), '\\', '/');
    const auto root = plugin_path.find(normalized_root);
    if (root != std::string::npos) {
        plugin_path.erase(root, normalized_root.size());
    }
    if (plugin_path.empty() || plugin_path.front() != '/') {
        plugin_path.insert(plugin_path.begin(), '/');
    }
    return plugin_path;
}

bool IsLevel256SupportedTitle(u64 title_id) {
    return IsLevel256PolicyTitle(title_id);
}

Kernel::Luma3DS::PluginAuthorization PLG_LDR::GetLumaExtensionAuthorization(u64 title_id) const {
    const bool verified_level256_plugin =
        IsLevel256SupportedTitle(title_id) && plgldr_context.level256_plugin_identity_verified;
    return {
        .plugin_loaded = plgldr_context.plugin_loaded,
        .plugin_integrity_verified = verified_level256_plugin,
        .owner_process_id = plgldr_context.plugin_process_id,
        .plugin_code_begin = plgldr_context.plugin_code_begin,
        .plugin_code_end = plgldr_context.plugin_code_end,
        .bootloader_code_begin = plgldr_context.plugin_bootloader_begin,
        .bootloader_code_end = plgldr_context.plugin_bootloader_end,
        .capabilities = Kernel::Luma3DS::CapabilitiesForLevel256Plugin(
            verified_level256_plugin, plgldr_context.level256_official_plugin_hash_matches),
    };
}

Kernel::Luma3DS::Level256VerificationPolicy PLG_LDR::GetLevel256VerificationPolicy(
    bool enabled, u64 title_id) const {
    return {
        .enabled = enabled,
        .supported_title = IsLevel256SupportedTitle(title_id),
        .capabilities = Kernel::Luma3DS::CapabilitiesForLevel256Plugin(
            IsLevel256SupportedTitle(title_id) && plgldr_context.level256_plugin_identity_verified,
            plgldr_context.level256_official_plugin_hash_matches),
        .plugin_loaded = plgldr_context.plugin_loaded,
        .owner_process_id = plgldr_context.plugin_process_id,
        .plugin_code_begin = plgldr_context.plugin_code_begin,
        .plugin_code_end = plgldr_context.plugin_code_end,
    };
}

PLG_LDR::PLG_LDR(Core::System& system_) : ServiceFramework{"plg:ldr", 1}, system(system_) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "LoadPlugin"},
        {0x0002, &PLG_LDR::IsEnabled, "IsEnabled"},
        {0x0003, &PLG_LDR::SetEnabled, "SetEnabled"},
        {0x0004, &PLG_LDR::SetLoadSettings, "SetLoadSettings"},
        {0x0005, nullptr, "DisplayMenu"},
        {0x0006, nullptr, "DisplayMessage"},
        {0x0007, &PLG_LDR::DisplayErrorMessage, "DisplayErrorMessage"},
        {0x0008, &PLG_LDR::GetPLGLDRVersion, "GetPLGLDRVersion"},
        {0x0009, &PLG_LDR::GetArbiter, "GetArbiter"},
        {0x000A, &PLG_LDR::GetPluginPath, "GetPluginPath"},
        {0x000B, nullptr, "SetRosalinaMenuBlock"},
        {0x000C, nullptr, "SetSwapParam"},
        {0x000D, nullptr, "SetLoadExeParam"},
        {0x2560, &PLG_LDR::GetLevel256LFCS, "GetLevel256LFCS"},
        // clang-format on
    };
    RegisterHandlers(functions);
    plgldr_context.memory_changed_handle = 0;
    plgldr_context.plugin_loaded = false;
    plgldr_context.plugin_code_reached = false;
    plgldr_context.plugin_code_begin = 0;
    plgldr_context.plugin_code_end = 0;
    plgldr_context.plugin_bootloader_begin = 0;
    plgldr_context.plugin_bootloader_end = 0;
    plgldr_context.plugin_trace_count = 0;
    plgldr_context.plugin_get_system_info_seen = false;
    plgldr_context.plugin_trace_thread_id = UINT32_MAX;
    plgldr_context.plugin_followup_trace_count = 0;
    plgldr_context.plugin_last_return_pc = 0;
    plgldr_context.plugin_instruction_trace_started = false;
    plgldr_context.plugin_service_trace_count = 0;
    plgldr_context.plugin_query_process_memory_failures = 0;
    plgldr_context.level256_custom_backdoor_patterns = {};
    plgldr_context.level256_custom_backdoor_pattern_count = 0;
    plgldr_context.level256_open_process_patterns = {};
    plgldr_context.level256_open_process_pattern_count = 0;
    plgldr_context.ResetTransientLevel256Authorization();
    plgldr_context.level256_open_process_trace_started = false;
    plgldr_context.level256_thread_create_log_count = 0;
    plgldr_context.ctrpf_osd_scan = {};
    plgldr_context.ctrpf_osd_candidate_initial_words = {};
    plgldr_context.ctrpf_osd_post_init_compared = false;
    plgldr_context.ctrpf_arbiter_watch_started = false;
    plgldr_context.ctrpf_arbiter_watch_handle = 0;
    plgldr_context.ctrpf_arbiter_watch_address = 0;
    plgldr_context.ctrpf_arbiter_watch_object = 0;
    plgldr_context.ctrpf_arbiter_trace_count = 0;
    plgldr_context.ctrpf_hook_trace_started = false;
    plgldr_context.ctrpf_hook_execution_trace_armed = false;
    plgldr_context.ctrpf_hook_trace_process_id = UINT32_MAX;
    plgldr_context.ctrpf_hook_target_instruction_executed = false;
    plgldr_context.ctrpf_hook_wrapper_instruction_executed = false;
    plgldr_context.ctrpf_hook_callback_instruction_executed = false;
    plgldr_context.ctrpf_hook_wrapper_observed = false;
    plgldr_context.ctrpf_hook_asm_wrapper_observed = false;
    plgldr_context.ctrpf_hook_asm_wrapper_executed = false;
    plgldr_context.ctrpf_hook_callback_target_observed = false;
    plgldr_context.ctrpf_hook_trace_thread_id = UINT32_MAX;
    plgldr_context.ctrpf_hook_trace_runs = 0;
    plgldr_context.ctrpf_hook_asm_wrapper = 0;
    plgldr_context.ctrpf_hook_callback_target = 0;
    plgldr_context.ctrpf_hook_arbiter_trace_count = 0;
}

template <class Archive>
void PLG_LDR::PluginLoaderContext::serialize(Archive& ar, const unsigned int) {
    ar & is_enabled;
    ar & allow_game_change;
    ar & plugin_loaded;
    ar & is_default_path;
    ar & plugin_path;
    ar & memory_region;
    ar & memory_block;
    ar & plugin_process_id;
    ar & use_user_load_parameters;
    ar & user_load_parameters;
    ar & plg_event;
    ar & plg_reply;
    ar & memory_changed_handle;
    ar & is_exe_load_function_set;
    ar & exe_load_checksum;
    ar & load_exe_func;
    ar & load_exe_args;
    if constexpr (Archive::is_loading::value) {
        // A savestate does not preserve trust in the loaded plugin bytes. The compatibility gate
        // is re-established only after a fresh file hash during plugin loading.
        ResetTransientLevel256Authorization();
        // A restored process owns any restored VM mapping. The transient manager deliberately
        // does not inherit a raw Process pointer or plugin trust across savestate loading.
        level256_open_process_trace_started = false;
        level256_thread_create_log_count = 0;
        level256_custom_backdoor_patterns = {};
        level256_custom_backdoor_pattern_count = 0;
        level256_open_process_patterns = {};
        level256_open_process_pattern_count = 0;
        ctrpf_hook_execution_trace_armed = false;
        ctrpf_hook_trace_process_id = UINT32_MAX;
        ctrpf_hook_target_instruction_executed = false;
        ctrpf_hook_wrapper_instruction_executed = false;
        ctrpf_hook_callback_instruction_executed = false;
    }
}
SERIALIZE_IMPL(PLG_LDR::PluginLoaderContext)

static const char* ThreadStatusName(Kernel::ThreadStatus status) {
    switch (status) {
    case Kernel::ThreadStatus::Running:
        return "Running";
    case Kernel::ThreadStatus::Ready:
        return "Ready";
    case Kernel::ThreadStatus::WaitArb:
        return "WaitArb";
    case Kernel::ThreadStatus::WaitSleep:
        return "WaitSleep";
    case Kernel::ThreadStatus::WaitIPC:
        return "WaitIPC";
    case Kernel::ThreadStatus::WaitSynchAny:
        return "WaitSynchAny";
    case Kernel::ThreadStatus::WaitSynchAll:
        return "WaitSynchAll";
    case Kernel::ThreadStatus::WaitHleEvent:
        return "WaitHleEvent";
    case Kernel::ThreadStatus::Dormant:
        return "Dormant";
    case Kernel::ThreadStatus::Dead:
        return "Dead";
    }
    return "Unknown";
}

bool PLG_LDR::TracePluginSVC(const Kernel::Process& process, const Kernel::Thread& thread, u32 pc,
                             u32 svc_id, const char* svc_name, const char* service_name, u32 type,
                             s32 param, const std::array<u32, 8>& registers) {
    const bool in_plugin_code =
        pc >= plgldr_context.plugin_code_begin && pc < plgldr_context.plugin_code_end;
    const bool in_bootloader =
        pc >= plgldr_context.plugin_bootloader_begin && pc < plgldr_context.plugin_bootloader_end;
    if (!plgldr_context.plugin_loaded || plgldr_context.plugin_process_id != process.process_id ||
        (!in_plugin_code && !in_bootloader)) {
        return false;
    }

    if (in_plugin_code && !plgldr_context.plugin_code_reached) {
        plgldr_context.plugin_code_reached = true;
        LOG_INFO(Service_PLGLDR,
                 "3GX guest code reached pid={} title_id={:016X} thread={} pc=0x{:08X}",
                 process.process_id, process.codeset->program_id, thread.GetThreadId(), pc);
    }

    if (svc_id == 0x2A && !plgldr_context.plugin_get_system_info_seen) {
        plgldr_context.plugin_get_system_info_seen = true;
        plgldr_context.plugin_trace_thread_id = thread.GetThreadId();
        plgldr_context.plugin_followup_trace_count = 0;
        plgldr_context.plugin_last_return_pc = pc;
    } else if (plgldr_context.plugin_get_system_info_seen &&
               plgldr_context.plugin_trace_thread_id == thread.GetThreadId() &&
               plgldr_context.plugin_followup_trace_count < 8) {
        LOG_INFO(Service_PLGLDR,
                 "3GX post-GetSystemInfo SVC boundary pid={} thread={} step={} "
                 "previous_post_hle_pc=0x{:08X} next_svc_pc=0x{:08X} svc=0x{:02X} name={} "
                 "status_before={}",
                 process.process_id, thread.GetThreadId(),
                 plgldr_context.plugin_followup_trace_count + 1,
                 plgldr_context.plugin_last_return_pc, pc, svc_id, svc_name,
                 ThreadStatusName(thread.status));
    }

    if (svc_id == 0xA0 && process.codeset &&
        process.codeset->program_id == Kernel::LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID &&
        Settings::values.level256_compatibility_enabled.GetValue() &&
        Settings::values.level256_bootloader_diagnostics_enabled.GetValue() &&
        plgldr_context.level256_plugin_identity_verified &&
        plgldr_context.level256_map_process_memory_ex_trace_count < 8) {
        ++plgldr_context.level256_map_process_memory_ex_trace_count;
        const auto request = Kernel::Luma3DS::DecodeMapProcessMemoryExRequest(registers);
        const auto destination_process = process.handle_table.Get<Kernel::Process>(
            static_cast<Kernel::Handle>(request.destination_process_handle));
        const auto source_process = process.handle_table.Get<Kernel::Process>(
            static_cast<Kernel::Handle>(request.source_process_handle));
        const auto describe_vma = [](const std::shared_ptr<Kernel::Process>& target_process,
                                     u32 address) {
            if (!target_process || address >= Kernel::VMManager::MAX_ADDRESS) {
                return std::string{"unresolved"};
            }
            const auto vma = target_process->vm_manager.FindVMA(address);
            if (vma == target_process->vm_manager.vma_map.end()) {
                return std::string{"unmapped"};
            }
            return fmt::format("base=0x{:08X} size=0x{:X} type={} state={} permissions=0x{:X}",
                               vma->second.base, vma->second.size,
                               static_cast<u32>(vma->second.type),
                               static_cast<u32>(vma->second.meminfo_state),
                               static_cast<u32>(vma->second.permissions));
        };
        const u32 page_size = ~Memory::CITRA_PAGE_MASK + 1;
        const u32 rounded_size = request.size & ~Memory::CITRA_PAGE_MASK;
        const u64 destination_end = static_cast<u64>(request.destination_address) + rounded_size;
        const u64 source_end = static_cast<u64>(request.source_address) + rounded_size;
        const u32 destination_last =
            destination_end != 0 && destination_end <= Kernel::VMManager::MAX_ADDRESS
                ? static_cast<u32>(destination_end - 1)
                : request.destination_address;
        const u32 source_last = source_end != 0 && source_end <= Kernel::VMManager::MAX_ADDRESS
                                    ? static_cast<u32>(source_end - 1)
                                    : request.source_address;
        LOG_INFO(Service_PLGLDR,
                 "Level256 MapProcessMemoryEx request pid={} thread={} pc=0x{:08X} "
                 "abi={} r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} r3=0x{:08X} "
                 "r4=0x{:08X} r5=0x{:08X} r6=0x{:08X} r7=0x{:08X} "
                 "dst_handle=0x{:08X} dst_pid={} dst=0x{:08X} src_handle=0x{:08X} "
                 "src_pid={} src=0x{:08X} size=0x{:X} mapped_size=0x{:X} flags=0x{:X} "
                 "page_size=0x{:X} dst_vma_first=[{}] dst_vma_last=[{}] "
                 "src_vma_first=[{}] src_vma_last=[{}] request_index={}",
                 process.process_id, thread.GetThreadId(), pc, request.abi_v2 ? "v2" : "legacy",
                 registers[0], registers[1], registers[2], registers[3], registers[4], registers[5],
                 registers[6], registers[7], request.destination_process_handle,
                 destination_process ? destination_process->process_id : UINT32_MAX,
                 request.destination_address, request.source_process_handle,
                 source_process ? source_process->process_id : UINT32_MAX, request.source_address,
                 request.size, rounded_size, request.flags, page_size,
                 describe_vma(destination_process, request.destination_address),
                 describe_vma(destination_process, destination_last),
                 describe_vma(source_process, request.source_address),
                 describe_vma(source_process, source_last),
                 plgldr_context.level256_map_process_memory_ex_trace_count);
    }

    if (svc_id == 0x2A && type == Kernel::LEVEL256_DIAGNOSTIC_TYPE &&
        param == Kernel::LEVEL256_DIAGNOSTIC_PARAM && process.codeset &&
        Kernel::ShouldCaptureLevel256BootloaderDiagnostic(
            Settings::values.level256_compatibility_enabled.GetValue(),
            Settings::values.level256_bootloader_diagnostics_enabled.GetValue(),
            plgldr_context.level256_plugin_identity_verified, process.codeset->program_id,
            process.process_id, plgldr_context.plugin_process_id, plgldr_context.plugin_loaded,
            thread.GetThreadId(), thread.GetThreadId(), pc, plgldr_context.plugin_code_begin,
            plgldr_context.plugin_code_end, plgldr_context.plugin_bootloader_begin,
            plgldr_context.plugin_bootloader_end)) {
        plgldr_context.plugin_trace_thread_id = thread.GetThreadId();
        plgldr_context.plugin_last_return_pc = pc;
        TraceLevel256BootloaderSnapshot(process, "before_get_system_info", thread.GetThreadId());
    }

    // Keep this diagnostic bounded and avoid logging SVC arguments or IPC contents.
    constexpr u32 max_plugin_svc_traces = 32;
    if (plgldr_context.plugin_trace_count >= max_plugin_svc_traces) {
        return true;
    }
    ++plgldr_context.plugin_trace_count;
    LOG_INFO(Service_PLGLDR,
             "3GX guest SVC pid={} title_id={:016X} thread={} pc=0x{:08X} svc=0x{:02X} "
             "name={} service={} status={}",
             process.process_id, process.codeset->program_id, thread.GetThreadId(), pc, svc_id,
             svc_name, service_name ? service_name : "", ThreadStatusName(thread.status));
    return true;
}

void PLG_LDR::TracePluginSVCResult(const Kernel::Process& process, const Kernel::Thread& thread,
                                   u32 svc_id, const char* svc_name, u32 svc_pc, u32 post_hle_pc,
                                   u32 type, s32 param, u32 result, u64 value) {
    const bool in_plugin_code =
        svc_pc >= plgldr_context.plugin_code_begin && svc_pc < plgldr_context.plugin_code_end;
    const bool in_bootloader = svc_pc >= plgldr_context.plugin_bootloader_begin &&
                               svc_pc < plgldr_context.plugin_bootloader_end;
    if (svc_id == 0xA0 && in_plugin_code && process.codeset &&
        process.codeset->program_id == Kernel::LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID &&
        plgldr_context.plugin_loaded && plgldr_context.plugin_process_id == process.process_id &&
        Settings::values.level256_compatibility_enabled.GetValue() &&
        Settings::values.level256_bootloader_diagnostics_enabled.GetValue() &&
        plgldr_context.level256_plugin_identity_verified &&
        plgldr_context.level256_map_process_memory_ex_result_trace_count <
            plgldr_context.level256_map_process_memory_ex_trace_count) {
        ++plgldr_context.level256_map_process_memory_ex_result_trace_count;
        LOG_INFO(Service_PLGLDR,
                 "Level256 MapProcessMemoryEx result pid={} thread={} svc_pc=0x{:08X} "
                 "post_hle_pc=0x{:08X} result=0x{:08X} value=0x{:016X} status_after={}",
                 process.process_id, thread.GetThreadId(), svc_pc, post_hle_pc, result, value,
                 ThreadStatusName(thread.status));
    }
    if (!plgldr_context.plugin_loaded || plgldr_context.plugin_process_id != process.process_id ||
        (!in_plugin_code && !in_bootloader) || !plgldr_context.plugin_get_system_info_seen ||
        plgldr_context.plugin_trace_thread_id != thread.GetThreadId()) {
        return;
    }

    if (svc_id == 0x2A) {
        LOG_INFO(Service_PLGLDR,
                 "3GX GetSystemInfo return pid={} title_id={:016X} thread={} svc_pc=0x{:08X} "
                 "post_hle_pc=0x{:08X} type={} param={} result=0x{:08X} value=0x{:016X} "
                 "status_after={}",
                 process.process_id, process.codeset->program_id, thread.GetThreadId(), svc_pc,
                 post_hle_pc, type, param, result, value, ThreadStatusName(thread.status));
        plgldr_context.plugin_last_return_pc = post_hle_pc;
        if (type == Kernel::LEVEL256_DIAGNOSTIC_TYPE &&
            param == Kernel::LEVEL256_DIAGNOSTIC_PARAM) {
            TraceLevel256BootloaderSnapshot(process, "after_get_system_info", thread.GetThreadId());
        }
        if (type == Kernel::LEVEL256_DIAGNOSTIC_TYPE &&
            param == Kernel::LEVEL256_DIAGNOSTIC_PARAM && process.codeset &&
            !plgldr_context.level256_bootloader_return_trace_started &&
            Kernel::ShouldCaptureLevel256BootloaderDiagnostic(
                Settings::values.level256_compatibility_enabled.GetValue(),
                Settings::values.level256_bootloader_diagnostics_enabled.GetValue(),
                plgldr_context.level256_plugin_identity_verified, process.codeset->program_id,
                process.process_id, plgldr_context.plugin_process_id, plgldr_context.plugin_loaded,
                thread.GetThreadId(), plgldr_context.plugin_trace_thread_id, svc_pc,
                plgldr_context.plugin_code_begin, plgldr_context.plugin_code_end,
                plgldr_context.plugin_bootloader_begin, plgldr_context.plugin_bootloader_end)) {
            plgldr_context.level256_bootloader_return_trace_started = true;
            system.GetRunningCore().StartPluginInstructionTrace(
                process.process_id, thread.GetThreadId(), plgldr_context.plugin_code_begin,
                plgldr_context.plugin_code_end, plgldr_context.plugin_bootloader_begin,
                plgldr_context.plugin_bootloader_end,
                Kernel::LEVEL256_BOOTLOADER_TRACE_INSTRUCTION_LIMIT, false, false, true);
        }
        return;
    }

    if (plgldr_context.plugin_followup_trace_count < 8) {
        LOG_INFO(Service_PLGLDR,
                 "3GX post-GetSystemInfo SVC return pid={} thread={} step={} svc=0x{:02X} "
                 "name={} svc_pc=0x{:08X} post_hle_pc=0x{:08X} status_after={}",
                 process.process_id, thread.GetThreadId(),
                 plgldr_context.plugin_followup_trace_count + 1, svc_id, svc_name, svc_pc,
                 post_hle_pc, ThreadStatusName(thread.status));
        plgldr_context.plugin_last_return_pc = post_hle_pc;
        ++plgldr_context.plugin_followup_trace_count;
    }
}

void PLG_LDR::TraceLevel256BootloaderSnapshot(const Kernel::Process& process, const char* phase,
                                              u32 thread_id) {
    if (!process.codeset || !Settings::values.level256_compatibility_enabled.GetValue() ||
        !Settings::values.level256_bootloader_diagnostics_enabled.GetValue() ||
        !plgldr_context.level256_plugin_identity_verified ||
        process.codeset->program_id != Kernel::LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID) {
        return;
    }

    const auto* current_thread = system.Kernel().GetCurrentThreadManager().GetCurrentThread();
    if (!current_thread || current_thread->GetThreadId() != thread_id ||
        current_thread->owner_process.lock().get() != &process) {
        return;
    }

    bool* logged = nullptr;
    if (std::strcmp(phase, "after_map_bootloader") == 0) {
        if (!Kernel::ShouldCaptureLevel256BootloaderMapDiagnostic(
                Settings::values.level256_compatibility_enabled.GetValue(),
                Settings::values.level256_bootloader_diagnostics_enabled.GetValue(),
                plgldr_context.level256_plugin_identity_verified, process.codeset->program_id,
                process.process_id, thread_id, true)) {
            return;
        }
        logged = &plgldr_context.level256_bootloader_map_snapshot_logged;
    } else if ((std::strcmp(phase, "before_get_system_info") == 0 ||
                std::strcmp(phase, "after_get_system_info") == 0) &&
               Kernel::ShouldCaptureLevel256BootloaderDiagnostic(
                   Settings::values.level256_compatibility_enabled.GetValue(),
                   Settings::values.level256_bootloader_diagnostics_enabled.GetValue(),
                   plgldr_context.level256_plugin_identity_verified, process.codeset->program_id,
                   process.process_id, plgldr_context.plugin_process_id,
                   plgldr_context.plugin_loaded, thread_id, plgldr_context.plugin_trace_thread_id,
                   plgldr_context.plugin_last_return_pc, plgldr_context.plugin_code_begin,
                   plgldr_context.plugin_code_end, plgldr_context.plugin_bootloader_begin,
                   plgldr_context.plugin_bootloader_end)) {
        if (std::strcmp(phase, "before_get_system_info") == 0) {
            logged = &plgldr_context.level256_bootloader_pre_svc_snapshot_logged;
        } else {
            logged = &plgldr_context.level256_bootloader_return_snapshot_logged;
        }
    }
    if (!logged || *logged) {
        return;
    }
    *logged = true;

    std::array<u32, Kernel::LEVEL256_BOOTLOADER_SNAPSHOT_ADDRESSES.size()> words{};
    std::array<bool, Kernel::LEVEL256_BOOTLOADER_SNAPSHOT_ADDRESSES.size()> valid{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        const VAddr address = Kernel::LEVEL256_BOOTLOADER_SNAPSHOT_ADDRESSES[i];
        valid[i] = system.Memory().IsValidVirtualAddress(process, address);
        if (valid[i]) {
            words[i] = system.Memory().Read32(process, address);
        }
    }
    std::array<u32, Kernel::LEVEL256_GAME_ENTRY_SNAPSHOT_ADDRESSES.size()> game_entry_words{};
    std::array<bool, Kernel::LEVEL256_GAME_ENTRY_SNAPSHOT_ADDRESSES.size()> game_entry_valid{};
    for (std::size_t i = 0; i < game_entry_words.size(); ++i) {
        const VAddr address = Kernel::LEVEL256_GAME_ENTRY_SNAPSHOT_ADDRESSES[i];
        game_entry_valid[i] = system.Memory().IsValidVirtualAddress(process, address);
        if (game_entry_valid[i]) {
            game_entry_words[i] = system.Memory().Read32(process, address);
        }
    }
    const bool guest_execution_state = std::strcmp(phase, "after_map_bootloader") != 0;
    const auto& core = system.GetRunningCore();
    const u32 pc = guest_execution_state ? core.GetReg(15) : 0;
    const u32 cpsr = guest_execution_state ? core.GetCPSR() : 0;
    const u32 sp = guest_execution_state ? core.GetReg(13) : 0;
    const u32 lr = guest_execution_state ? core.GetReg(14) : 0;
    LOG_INFO(Service_PLGLDR,
             "Level256 bootloader snapshot phase={} pid={} title_id={:016X} thread={} "
             "pc=0x{:08X} cpsr=0x{:08X} thumb={} sp=0x{:08X} lr=0x{:08X} "
             "0x00100000={} 0x00100004={} 0x06FFF0E8={} 0x06FFF0EC={} 0x06FFF0F0={} "
             "expected_return=0x{:08X}",
             phase, process.process_id, process.codeset->program_id, thread_id, pc, cpsr,
             guest_execution_state && (cpsr & (1U << 5)) != 0, sp, lr,
             game_entry_valid[0] ? fmt::format("0x{:08X}", game_entry_words[0]) : "invalid",
             game_entry_valid[1] ? fmt::format("0x{:08X}", game_entry_words[1]) : "invalid",
             valid[0] ? fmt::format("0x{:08X}", words[0]) : "invalid",
             valid[1] ? fmt::format("0x{:08X}", words[1]) : "invalid",
             valid[2] ? fmt::format("0x{:08X}", words[2]) : "invalid",
             Kernel::LEVEL256_BOOTLOADER_EXPECTED_RETURN_TARGET);
}

void PLG_LDR::TraceCtrpfStartupMemoryAccess(const Kernel::Process& process,
                                            const Kernel::Thread& thread, VAddr address, u32 size,
                                            bool is_write, u64 value, u32 pc, u32 lr) {
    if (!plgldr_context.ctrpf_startup_watchpoints_armed || !process.codeset ||
        !Kernel::ShouldCaptureLevel256CtrpfStartupDiagnostic(
            Settings::values.level256_compatibility_enabled.GetValue(),
            plgldr_context.level256_plugin_identity_verified, process.codeset->program_id,
            process.process_id, plgldr_context.plugin_process_id, plgldr_context.plugin_loaded)) {
        return;
    }

    const std::size_t target = Kernel::GetLevel256CtrpfStartupWatchTarget(
        address, size, is_write, plgldr_context.ctrpf_osd_scan.selected_candidate);
    if (target >= plgldr_context.ctrpf_startup_watch_counts.size()) {
        return;
    }

    u32& count = plgldr_context.ctrpf_startup_watch_counts[target];
    if (count != UINT32_MAX) {
        ++count;
    }
    const VAddr candidate = plgldr_context.ctrpf_osd_scan.selected_candidate;
    const bool candidate_write =
        target == Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size() && is_write &&
        address == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS && size == sizeof(u32) &&
        static_cast<u32>(value) == Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH &&
        system.Memory().IsValidVirtualAddress(process, candidate) &&
        system.Memory().Read32(process, candidate) == Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH;
    if (candidate_write) {
        plgldr_context.ctrpf_hook_execution_trace_armed = true;
        plgldr_context.ctrpf_hook_trace_process_id = process.process_id;
        plgldr_context.ctrpf_hook_target_instruction_executed = false;
        plgldr_context.ctrpf_hook_wrapper_instruction_executed = false;
        plgldr_context.ctrpf_hook_callback_instruction_executed = false;
        plgldr_context.ctrpf_hook_trace_started = false;
        plgldr_context.ctrpf_hook_trace_thread_id = UINT32_MAX;
        plgldr_context.ctrpf_hook_trace_runs = 0;
        plgldr_context.ctrpf_hook_invocation_count = 0;
        plgldr_context.ctrpf_hook_asm_wrapper_executed = false;
        plgldr_context.ctrpf_hook_callback_target_observed = false;
        plgldr_context.ctrpf_hook_callback_target = 0;
        LOG_INFO(Service_PLGLDR,
                 "Level256 OSD hook execution trace armed after guest write pid={} "
                 "thread={} address=0x{:08X} instruction=0x{:08X} pc=0x{:08X}",
                 process.process_id, thread.GetThreadId(), address, static_cast<u32>(value), pc);
    }
    if (!Kernel::ShouldLogLevel256CtrpfStartupWatch(count)) {
        return;
    }

    constexpr std::array<const char*, Kernel::LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT> names = {
        "game_title_text",   "create_backup_text", "creating_backup_text", "backup_created_text",
        "plugin_ready_text", "title_config_path",  "start_as_a_path",      "osd_candidate_write"};
    auto& cpu = system.GetRunningCore();
    const u32 current_word = target == Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size() &&
                                     system.Memory().IsValidVirtualAddress(process, candidate)
                                 ? system.Memory().Read32(process, candidate)
                                 : 0;
    LOG_INFO(Service_PLGLDR,
             "Level256 CTRPF startup memory access pid={} title_id={:016X} thread={} "
             "target={} kind={} pc=0x{:08X} lr=0x{:08X} address=0x{:08X} size={} "
             "value=0x{:016X} current_word=0x{:08X} cpsr=0x{:08X} "
             "r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} r3=0x{:08X} sp=0x{:08X} "
             "occurrence={}",
             process.process_id, process.codeset->program_id, thread.GetThreadId(), target,
             names[target], pc, lr, address, size, value, current_word, cpu.GetCPSR(),
             cpu.GetReg(0), cpu.GetReg(1), cpu.GetReg(2), cpu.GetReg(3), cpu.GetReg(13), count);
}

void PLG_LDR::TraceLevel256GuestWait(const Kernel::Process& process, u32 thread_id, u32 pc,
                                     const std::string& phase, const std::string& object_type,
                                     u32 object_id, const std::string& object_name, u32 resume_pc) {
    auto& context = plgldr_context;
    if (!Settings::values.level256_compatibility_enabled.GetValue() || !context.plugin_loaded ||
        !context.level256_plugin_identity_verified ||
        process.process_id != context.plugin_process_id || !process.codeset ||
        !IsLevel256SupportedTitle(process.codeset->program_id)) {
        return;
    }
    if (phase == "wait") {
        ++context.ctrpf_guest_wait_count;
    } else if (phase == "notify") {
        ++context.ctrpf_guest_event_notification_count;
    } else {
        ++context.ctrpf_guest_wakeup_count;
    }
    if (context.ctrpf_guest_wait_log_count++ >= 24) {
        return;
    }
    LOG_INFO(Service_PLGLDR,
             "Level256 CTRPF guest synchronization phase={} pid={} thread={} pc=0x{:08X} "
             "object_type={} object_id={} object_name={} resume_pc=0x{:08X} "
             "waits={} event_notifications={} wakeups={}",
             phase, process.process_id, thread_id, pc, object_type, object_id,
             object_name.empty() ? "<unnamed>" : object_name, resume_pc,
             context.ctrpf_guest_wait_count, context.ctrpf_guest_event_notification_count,
             context.ctrpf_guest_wakeup_count);
}

template <class Archive>
void PLG_LDR::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & plgldr_context;
    if constexpr (Archive::is_loading::value) {
        process_view_manager.ResetAfterSavestate();
    }
}
SERIALIZE_IMPL(PLG_LDR)

void PLG_LDR::OnProcessRun(Kernel::Process& process, Kernel::KernelSystem& kernel) {
    constexpr u32 TITLE_ID_APP_MASK = 0xFFFFFFED;
    constexpr u32 TITLE_ID_APP_VALUE = 0x00040000;
    if (!plgldr_context.is_enabled || plgldr_context.plugin_loaded ||
        (static_cast<u32>(process.codeset->program_id >> 32) & TITLE_ID_APP_MASK) !=
            TITLE_ID_APP_VALUE) {
        return;
    }
    plgldr_context.plugin_code_reached = false;
    plgldr_context.plugin_code_begin = 0;
    plgldr_context.plugin_code_end = 0;
    plgldr_context.plugin_bootloader_begin = 0;
    plgldr_context.plugin_bootloader_end = 0;
    plgldr_context.plugin_trace_count = 0;
    plgldr_context.plugin_get_system_info_seen = false;
    plgldr_context.plugin_trace_thread_id = UINT32_MAX;
    plgldr_context.plugin_followup_trace_count = 0;
    plgldr_context.plugin_last_return_pc = 0;
    plgldr_context.plugin_instruction_trace_started = false;
    plgldr_context.plugin_service_trace_count = 0;
    plgldr_context.plugin_query_process_memory_failures = 0;
    plgldr_context.level256_custom_backdoor_patterns = {};
    plgldr_context.level256_custom_backdoor_pattern_count = 0;
    plgldr_context.level256_open_process_patterns = {};
    plgldr_context.level256_open_process_pattern_count = 0;
    plgldr_context.level256_lfcs_request_count = 0;
    if (!process_view_manager.ResetForNewPlugin(process)) {
        LOG_ERROR(Service_PLGLDR,
                  "Cannot start plugin while previous Level256 process view is still mapped: "
                  "owner_pid={} address=0x{:08X} pending={}",
                  process_view_manager.GetOwnerProcessId(), process_view_manager.GetOwnedAddress(),
                  process_view_manager.IsReleasePending(process));
        return;
    }
    plgldr_context.level256_open_process_trace_started = false;
    plgldr_context.level256_thread_create_log_count = 0;
    plgldr_context.ctrpf_startup_watchpoints_armed = false;
    plgldr_context.ctrpf_startup_watch_counts = {};
    plgldr_context.ctrpf_osd_candidate_execution_seen = false;
    plgldr_context.ctrpf_hook_trace_started = false;
    plgldr_context.ctrpf_hook_execution_trace_armed = false;
    plgldr_context.ctrpf_hook_trace_process_id = UINT32_MAX;
    plgldr_context.ctrpf_hook_target_instruction_executed = false;
    plgldr_context.ctrpf_hook_wrapper_instruction_executed = false;
    plgldr_context.ctrpf_hook_callback_instruction_executed = false;
    plgldr_context.ctrpf_hook_wrapper_observed = false;
    plgldr_context.ctrpf_hook_asm_wrapper_observed = false;
    plgldr_context.ctrpf_hook_asm_wrapper_executed = false;
    plgldr_context.ctrpf_hook_callback_target_observed = false;
    plgldr_context.ctrpf_hook_trace_thread_id = UINT32_MAX;
    plgldr_context.ctrpf_hook_trace_runs = 0;
    plgldr_context.ctrpf_hook_asm_wrapper = 0;
    plgldr_context.ctrpf_hook_callback_target = 0;
    plgldr_context.ctrpf_hook_arbiter_trace_count = 0;
    plgldr_context.ctrpf_top_callback_count = 0;
    plgldr_context.ctrpf_bottom_callback_count = 0;
    plgldr_context.ctrpf_top_render_path_count = 0;
    plgldr_context.ctrpf_hook_current_is_bottom = false;
    plgldr_context.ctrpf_hook_invocation_count = 0;
    plgldr_context.ctrpf_guest_wait_count = 0;
    plgldr_context.ctrpf_guest_event_notification_count = 0;
    plgldr_context.ctrpf_guest_wakeup_count = 0;
    plgldr_context.ctrpf_guest_wait_log_count = 0;
    plgldr_context.ctrpf_guest_arbiter_wait_count = 0;
    plgldr_context.ctrpf_guest_arbiter_signal_count = 0;
    plgldr_context.ctrpf_guest_arbiter_wakeup_count = 0;
    plgldr_context.ctrpf_osd_scan = {};
    plgldr_context.ctrpf_osd_candidate_initial_words = {};
    plgldr_context.ctrpf_osd_post_init_compared = false;
    plgldr_context.ctrpf_arbiter_watch_started = false;
    plgldr_context.ctrpf_arbiter_watch_handle = 0;
    plgldr_context.ctrpf_arbiter_watch_address = 0;
    plgldr_context.ctrpf_arbiter_watch_object = 0;
    plgldr_context.ctrpf_arbiter_trace_count = 0;
    plgldr_context.ctrpf_startup_watchpoints_armed = false;
    plgldr_context.ctrpf_startup_watch_counts = {};
    plgldr_context.ctrpf_osd_candidate_execution_seen = false;
    {
        // Same check as original plugin loader, plugins are not supported in homebrew apps
        u32 value1, value2;
        kernel.memory.ReadBlock(process, process.codeset->CodeSegment().addr, &value1, 4);
        kernel.memory.ReadBlock(process, process.codeset->CodeSegment().addr + 32, &value2, 4);
        // Check for "B #0x20" and "MOV R4, LR" instructions
        bool is_homebrew = u32_le(value1) == 0xEA000006 && u32_le(value2) == 0xE1A0400E;
        if (is_homebrew) {
            return;
        }
    }
    FileSys::Plugin3GXLoader plugin_loader;
    const u64 title_id = process.codeset->program_id;
    auto attempt_load = [&](const std::string& path, bool is_default) {
        plgldr_context.is_default_path = is_default;
        plgldr_context.plugin_path = path;
        LOG_INFO(Service_PLGLDR, "3GX load attempt pid={} title={:016X} source={} path={}",
                 process.process_id, title_id, is_default ? "default" : "selected", path);
        const auto result = plugin_loader.Load(plgldr_context, process, kernel, *this);
        LOG_INFO(Service_PLGLDR,
                 "3GX load result pid={} title={:016X} status={} plugin_loaded={} owner_pid={}",
                 process.process_id, title_id, static_cast<u32>(result),
                 plgldr_context.plugin_loaded, plgldr_context.plugin_process_id);
        return result;
    };
    const auto low_title_Id = plgldr_context.user_load_parameters.low_title_Id;
    if (plgldr_context.use_user_load_parameters &&
        (low_title_Id == static_cast<u32>(process.codeset->program_id) ||
         low_title_Id == 0 /* Should load for any title */) &&
        plgldr_context.user_load_parameters.path[0]) {
        std::string plugin_file = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                                  std::string(plgldr_context.user_load_parameters.path + 1);
        attempt_load(plugin_file, false);
    } else {
        const std::string plugin_root =
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) + "luma/plugins/";
        const std::string plugin_tid =
            plugin_root + fmt::format("{:016X}", process.codeset->program_id);
        FileUtil::FSTEntry entry;
        FileUtil::ScanDirectoryTree(plugin_tid, entry);
        std::vector<std::string> title_plugins;
        for (const auto& child : entry.children) {
            if (!child.isDirectory && child.physicalName.ends_with(".3gx")) {
                title_plugins.emplace_back(child.physicalName);
            }
        }

        const std::string sdmc_root = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir);
        const std::string level256_path = sdmc_root + "Level256/plugin/Level256.3gx";
        const std::string default_path = plugin_root + "default.3gx";
        const auto hid = Service::HID::GetModule(system);
        const bool select_pressed = hid && hid->GetState().select;
        const auto path_source =
            ChoosePluginPathSource(title_id, FileUtil::Exists(level256_path), select_pressed,
                                   !title_plugins.empty(), FileUtil::Exists(default_path));

        if (path_source == PluginPathSource::Level256 &&
            attempt_load(level256_path, false) == Loader::ResultStatus::Success) {
            return;
        }

        // A held SELECT bypasses Level256's dedicated plugin path, matching Rosalina's
        // loader behavior. Azahar retains its existing title-directory/default selection flow.
        if (path_source == PluginPathSource::TitleSpecific ||
            path_source == PluginPathSource::Level256) {
            for (const auto& path : title_plugins) {
                if (attempt_load(path, false) == Loader::ResultStatus::Success) {
                    return;
                }
            }
        }

        if ((path_source == PluginPathSource::Default ||
             path_source == PluginPathSource::TitleSpecific ||
             path_source == PluginPathSource::Level256) &&
            FileUtil::Exists(default_path)) {
            attempt_load(default_path, true);
        }
    }
}

void PLG_LDR::OnProcessExit(Kernel::Process& process, Kernel::KernelSystem& kernel) {
    if (plgldr_context.plugin_loaded && process.process_id == plgldr_context.plugin_process_id) {
        if (plgldr_context.level256_plugin_identity_verified && process.codeset &&
            IsLevel256SupportedTitle(process.codeset->program_id)) {
            LOG_INFO(
                Service_PLGLDR,
                "Level256 CTRPF frame diagnostic summary title={:016X} pid={} "
                "callback=0x{:08X} "
                "top_r0_zero={} bottom_r0_one={} top_render_entry=0x{:08X} "
                "top_render_path={} hook_invocations={} sample_cap=32 "
                "guest_waits={} event_notifications_with_waiter={} guest_wakeups={} "
                "arbiter_waits={} arbiter_signals={} arbiter_wakeups={}",
                process.codeset->program_id, process.process_id,
                Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS,
                plgldr_context.ctrpf_top_callback_count, plgldr_context.ctrpf_bottom_callback_count,
                Kernel::LEVEL256_CTRPF_OSD_RENDER_ENTRY_ADDRESS,
                plgldr_context.ctrpf_top_render_path_count,
                plgldr_context.ctrpf_hook_invocation_count, plgldr_context.ctrpf_guest_wait_count,
                plgldr_context.ctrpf_guest_event_notification_count,
                plgldr_context.ctrpf_guest_wakeup_count,
                plgldr_context.ctrpf_guest_arbiter_wait_count,
                plgldr_context.ctrpf_guest_arbiter_signal_count,
                plgldr_context.ctrpf_guest_arbiter_wakeup_count);
        }
        if (plgldr_context.ctrpf_startup_watchpoints_armed) {
            for (std::size_t i = 0; i < Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size(); ++i) {
                const VAddr address = Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[i];
                if (kernel.memory.IsValidVirtualAddress(process, address)) {
                    kernel.memory.UnregisterWatchpoint(process, address, 1);
                }
            }
            if (plgldr_context.ctrpf_osd_scan.selected_candidate ==
                    Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS &&
                kernel.memory.IsValidVirtualAddress(process,
                                                    Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS)) {
                kernel.memory.UnregisterWatchpoint(process,
                                                   Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS,
                                                   Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_SIZE);
            }
            LOG_INFO(Service_PLGLDR,
                     "Level256 CTRPF startup memory watch summary pid={} accesses="
                     "title:{},create_backup:{},creating_backup:{},backup_created:{},"
                     "plugin_ready:{},title_config_path:{},start_as_a_path:{},"
                     "osd_candidate_writes:{}",
                     process.process_id, plgldr_context.ctrpf_startup_watch_counts[0],
                     plgldr_context.ctrpf_startup_watch_counts[1],
                     plgldr_context.ctrpf_startup_watch_counts[2],
                     plgldr_context.ctrpf_startup_watch_counts[3],
                     plgldr_context.ctrpf_startup_watch_counts[4],
                     plgldr_context.ctrpf_startup_watch_counts[5],
                     plgldr_context.ctrpf_startup_watch_counts[6],
                     plgldr_context.ctrpf_startup_watch_counts[7]);
            plgldr_context.ctrpf_startup_watchpoints_armed = false;
            plgldr_context.ctrpf_osd_candidate_execution_seen = false;
        }
        u32 status = kernel.memory.Read32(FileSys::Plugin3GXLoader::_3GX_exe_load_addr - 0xC);
        LOG_INFO(Service_PLGLDR, "3GX process exit pid={} title={:016X} bootloader_status=0x{:08X}",
                 process.process_id, process.codeset->program_id, status);
        if (plgldr_context.plugin_query_process_memory_failures != 0) {
            LOG_INFO(Service_PLGLDR,
                     "Level256 QueryProcessMemory failure summary: pid={} failures={}",
                     process.process_id, plgldr_context.plugin_query_process_memory_failures);
        }
        if (process_view_manager.HasOwner()) {
            const Result free_result = process_view_manager.Release(process);
            if (free_result.IsError()) {
                LOG_WARNING(Service_PLGLDR,
                            "Failed to unmap Level256 process view; ownership retained: "
                            "pid={} address=0x{:08X} result=0x{:08X}",
                            process.process_id, process_view_manager.GetOwnedAddress(),
                            free_result.raw);
            }
        }
        if (status == 0) {
            LOG_CRITICAL(Service_PLGLDR, "Failed to launch {}: Checksum failed",
                         plgldr_context.plugin_path);
        }
        // Rasterizer surfaces can retain this physical framebuffer. Remove them while the
        // physical-to-plugin-VA alias is still available so RasterizerMarkRegionCached can
        // restore every page table before the backing allocation is returned to the allocator.
        kernel.memory.ClearPlugin3GXFramebufferAddress([&] { system.GPU().ClearAll(false); });
        if (plgldr_context.memory_region != static_cast<Kernel::MemoryRegion>(0)) {
            kernel.GetMemoryRegion(plgldr_context.memory_region)
                ->Free(plgldr_context.memory_block.first, plgldr_context.memory_block.second);
            plgldr_context.memory_region = static_cast<Kernel::MemoryRegion>(0);
        }
        plgldr_context.plugin_loaded = false;
        plgldr_context.plugin_process_id = UINT32_MAX;
        plgldr_context.ResetTransientLevel256Authorization();
        plgldr_context.ctrpf_osd_scan = {};
        plgldr_context.ctrpf_osd_candidate_initial_words = {};
        plgldr_context.ctrpf_osd_post_init_compared = false;
        plgldr_context.ctrpf_arbiter_watch_started = false;
        plgldr_context.ctrpf_arbiter_watch_handle = 0;
        plgldr_context.ctrpf_arbiter_watch_address = 0;
        plgldr_context.ctrpf_arbiter_watch_object = 0;
        plgldr_context.ctrpf_arbiter_trace_count = 0;
        plgldr_context.ctrpf_startup_watch_counts = {};
        plgldr_context.ctrpf_osd_candidate_execution_seen = false;
        plgldr_context.plugin_code_reached = false;
        plgldr_context.plugin_code_begin = 0;
        plgldr_context.plugin_code_end = 0;
        plgldr_context.plugin_bootloader_begin = 0;
        plgldr_context.plugin_bootloader_end = 0;
        plgldr_context.plugin_trace_count = 0;
        plgldr_context.plugin_get_system_info_seen = false;
        plgldr_context.plugin_trace_thread_id = UINT32_MAX;
        plgldr_context.plugin_followup_trace_count = 0;
        plgldr_context.plugin_last_return_pc = 0;
        plgldr_context.plugin_instruction_trace_started = false;
        plgldr_context.plugin_service_trace_count = 0;
        plgldr_context.plugin_query_process_memory_failures = 0;
        plgldr_context.level256_open_process_trace_started = false;
        plgldr_context.level256_thread_create_log_count = 0;
        plgldr_context.memory_changed_handle = 0;
        LOG_INFO(Service_PLGLDR, "Plugin unloaded successfully.");
    }
}

void PLG_LDR::OnProcessDestroyed(Kernel::Process& process) {
    process_view_manager.OnProcessDestroyed(process);
}

ResultVal<Kernel::Handle> PLG_LDR::GetMemoryChangedHandle(Kernel::KernelSystem& kernel) {
    if (plgldr_context.memory_changed_handle) {
        return plgldr_context.memory_changed_handle;
    }

    std::shared_ptr<Kernel::Event> evt =
        kernel.CreateEvent(Kernel::ResetType::OneShot,
                           fmt::format("event-{:08x}", system.GetRunningCore().GetReg(14)));
    R_TRY(kernel.GetCurrentProcess()->handle_table.Create(
        std::addressof(plgldr_context.memory_changed_handle), std::move(evt)));
    return plgldr_context.memory_changed_handle;
}

void PLG_LDR::OnMemoryChanged(Kernel::Process& process, Kernel::KernelSystem& kernel) {
    if (!plgldr_context.plugin_loaded || !plgldr_context.memory_changed_handle) {
        return;
    }

    std::shared_ptr<Kernel::Event> evt =
        kernel.GetCurrentProcess()->handle_table.Get<Kernel::Event>(
            plgldr_context.memory_changed_handle);
    if (evt == nullptr)
        return;

    evt->Signal();
}

void PLG_LDR::IsEnabled(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(plgldr_context.is_enabled);
}

void PLG_LDR::SetEnabled(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    bool enabled = rp.Pop<u32>() == 1;

    bool can_change = enabled == plgldr_context.is_enabled || plgldr_context.allow_game_change;
    if (can_change) {
        plgldr_context.is_enabled = enabled;
        Settings::values.plugin_loader_enabled.SetValue(enabled);
    }
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push((can_change) ? ResultSuccess : Kernel::ResultNotAuthorized);
}

void PLG_LDR::SetLoadSettings(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    plgldr_context.use_user_load_parameters = true;
    u32_le flags = rp.Pop<u32>();
    plgldr_context.user_load_parameters.no_flash = (flags & 1) == 1;
    plgldr_context.user_load_parameters.plugin_memory_strategy =
        static_cast<PluginMemoryStrategy>((flags >> 8) & 0xF);
    plgldr_context.user_load_parameters.low_title_Id = rp.Pop<u32>();

    auto path = rp.PopMappedBuffer();

    path.Read(
        plgldr_context.user_load_parameters.path, 0,
        std::min(sizeof(PluginLoaderContext::PluginLoadParameters::path) - 1, path.GetSize()));
    plgldr_context.user_load_parameters.path[std::min(
        sizeof(PluginLoaderContext::PluginLoadParameters::path) - 1, path.GetSize())] = '\0';

    auto config = rp.PopMappedBuffer();
    config.Read(
        plgldr_context.user_load_parameters.config, 0,
        std::min(sizeof(PluginLoaderContext::PluginLoadParameters::config), config.GetSize()));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void PLG_LDR::DisplayErrorMessage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 error_code = rp.Pop<u32>();
    auto title = rp.PopMappedBuffer();
    auto desc = rp.PopMappedBuffer();

    std::vector<char> title_data(title.GetSize() + 1);
    std::vector<char> desc_data(desc.GetSize() + 1);

    title.Read(title_data.data(), 0, title.GetSize());
    title_data[title.GetSize()] = '\0';

    desc.Read(desc_data.data(), 0, desc.GetSize());
    desc_data[desc.GetSize()] = '\0';

    LOG_ERROR(Service_PLGLDR, "Plugin error - Code: {} - Title: {} - Description: {}", error_code,
              std::string(title_data.data()), std::string(desc_data.data()));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void PLG_LDR::GetPLGLDRVersion(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(plgldr_version.raw);
}

void PLG_LDR::GetArbiter(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    // NOTE: It doesn't make sense to send an arbiter in HLE, as it's used to
    // signal the plgldr service thread when a event is ready. Instead we just send
    // an error and the 3GX plugin will take care of it.
    // (We never send any events anyways)
    rb.Push(Kernel::ResultNotImplemented);
}

void PLG_LDR::GetPluginPath(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto path = rp.PopMappedBuffer();

    // Same behaviour as strncpy
    std::string plugin_path = MakeGuestPluginPath(
        plgldr_context.plugin_path, FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir));

    path.Write(plugin_path.c_str(), 0, std::min(path.GetSize(), plugin_path.length() + 1));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(path);
}

namespace {

std::optional<Level256LFCS::LfcsPayload> LoadVerifiedLocalFriendCodeSeedA() {
    const std::string path =
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/LocalFriendCodeSeed_A";
    if (!FileUtil::Exists(path)) {
        return std::nullopt;
    }

    FileUtil::IOFile file(path, "rb");
    if (!file.IsOpen()) {
        return std::nullopt;
    }

    return Level256LFCS::ReadVerifiedLfcs(
        file.GetSize(),
        [&file](u8* output, std::size_t size) { return file.ReadBytes(output, size) == size; },
        [](const Level256LFCS::LfcsPayload& payload) {
            HW::UniqueData::LocalFriendCodeSeedB seed{};
            std::memcpy(&seed, payload.data(), payload.size());
            if (!seed.IsValid()) {
                return false;
            }
            // Use the existing console RSA key material to accept only an authentic seed.
            HW::AES::InitKeys();
            return seed.VerifySignature();
        });
}

void ReplyLfcsWithoutBuffer(Kernel::HLERequestContext& ctx, Result result) {
    IPC::RequestBuilder rb(ctx, Level256LFCS::kCommandId, 2, 0);
    rb.Push(result);
    rb.Push(static_cast<u32>(Level256LFCS::Source::None));
}

} // namespace

void PLG_LDR::GetLevel256LFCS(Kernel::HLERequestContext& ctx) {
    const u32* const command_buffer = ctx.CommandBuffer();
    const u32 pid_descriptor = command_buffer[1];
    const u32 descriptor_process_id = command_buffer[2];
    const u32 buffer_descriptor = command_buffer[3];

    IPC::MappedBufferDescInfo buffer_info{buffer_descriptor};
    if (!Level256LFCS::IsRequestShapeValid(ctx.CommandHeader().raw, pid_descriptor,
                                           buffer_descriptor, buffer_info.size)) {
        ReplyLfcsWithoutBuffer(ctx, IPC::ResultInvalidBufferDescriptor);
        return;
    }

    // IPC translation has replaced the guest buffer address with its mapped-buffer index.
    Kernel::MappedBuffer& buffer = ctx.GetMappedBuffer(command_buffer[4]);
    if (buffer.GetSize() != Level256LFCS::kLfcsSize) {
        ReplyLfcsWithoutBuffer(ctx, IPC::ResultInvalidBufferDescriptor);
        return;
    }

    Level256LFCS::LfcsPayload zero_buffer{};
    buffer.Write(zero_buffer.data(), 0, zero_buffer.size());

    const auto caller_thread = ctx.ClientThread();
    const auto caller_process = caller_thread ? caller_thread->owner_process.lock() : nullptr;
    const u32 caller_process_id = caller_process ? caller_process->process_id : 0;
    const u64 caller_title_id =
        caller_process && caller_process->codeset ? caller_process->codeset->program_id : 0;
    const bool authorized = Level256LFCS::IsAuthorized(
        Settings::values.level256_compatibility_enabled.GetValue() && plgldr_context.is_enabled,
        IsLevel256SupportedTitle(caller_title_id), plgldr_context.plugin_loaded,
        Kernel::Luma3DS::HasCapability(Kernel::Luma3DS::CapabilitiesForLevel256Plugin(
                                           plgldr_context.level256_plugin_identity_verified,
                                           plgldr_context.level256_official_plugin_hash_matches),
                                       Kernel::Luma3DS::Level256Capability::Level256LFCS),
        caller_process_id, plgldr_context.plugin_process_id, descriptor_process_id);

    Result result = ResultSuccess;
    Level256LFCS::Selection selection{};
    if (!authorized) {
        result = Kernel::ResultNotAuthorized;
    } else if (!Level256LFCS::ConsumeRequestSlot(plgldr_context.level256_lfcs_request_count)) {
        result = Kernel::ResultOutOfRange;
    } else {
        std::optional<Level256LFCS::LfcsPayload> lfcs_b;
        if (HW::UniqueData::LoadLocalFriendCodeSeedB() ==
            HW::UniqueData::SecureDataLoadStatus::Loaded) {
            const auto& seed_b = HW::UniqueData::GetLocalFriendCodeSeedB();
            if (seed_b.IsValid()) {
                Level256LFCS::LfcsPayload payload{};
                std::memcpy(payload.data(), &seed_b, payload.size());
                lfcs_b = payload;
            }
        }
        auto lfcs_a = LoadVerifiedLocalFriendCodeSeedA();
        selection = Level256LFCS::SelectLfcs(std::move(lfcs_b), std::move(lfcs_a));
        if (selection.source == Level256LFCS::Source::None) {
            result = Kernel::ResultNotFound;
        } else {
            buffer.Write(selection.payload.data(), 0, selection.payload.size());
        }
    }

    IPC::RequestBuilder rb(ctx, Level256LFCS::kCommandId, 2, 2);
    rb.Push(result);
    rb.Push(static_cast<u32>(selection.source));
    rb.PushMappedBuffer(buffer);
}

std::shared_ptr<PLG_LDR> GetService(Core::System& system) {
    if (!system.KernelRunning())
        return nullptr;
    auto it = system.Kernel().named_ports.find("plg:ldr");
    if (it != system.Kernel().named_ports.end())
        return std::static_pointer_cast<PLG_LDR>(it->second->GetServerPort()->hle_handler);
    return nullptr;
}

void InstallInterfaces(Core::System& system) {
    std::make_shared<PLG_LDR>(system)->InstallAsNamedPort(system.Kernel());
}

} // namespace Service::PLGLDR
