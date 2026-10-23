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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <boost/serialization/export.hpp>
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/luma3ds/compatibility.h"
#include "core/hle/kernel/luma3ds/level256_process_view.h"
#include "core/hle/kernel/luma3ds/level256_system_info.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class Process;
class Thread;
} // namespace Kernel

namespace Service::PLGLDR {

class PLG_LDR final : public ServiceFramework<PLG_LDR> {
public:
    enum class PluginMemoryStrategy : u8 {
        PLG_STRATEGY_NONE = 2,
        PLG_STRATEGY_SWAP = 0,
        PLG_STRATEGY_MODE3 = 1,
    };

    struct PluginLoaderContext {
        struct PluginLoadParameters {
            u8 no_flash = 0;
            PluginMemoryStrategy plugin_memory_strategy = PluginMemoryStrategy::PLG_STRATEGY_SWAP;
            u32_le low_title_Id = 0;
            char path[256] = {0};
            u32_le config[32] = {0};

            template <class Archive>
            void serialize(Archive& ar, const unsigned int) {
                ar & no_flash;
                ar & plugin_memory_strategy;
                ar & low_title_Id;
                ar & path;
                ar & config;
            }
            friend class boost::serialization::access;
        };
        bool is_enabled = true;
        bool allow_game_change = true;
        bool plugin_loaded = false;
        bool plugin_code_reached = false;
        u32 plugin_code_begin = 0;
        u32 plugin_code_end = 0;
        // Runtime guest range for the 3GX .data and .bss sections. Not serialized as authority;
        // it is populated again only when the plugin is loaded from its verified artifact.
        u32 plugin_data_begin = 0;
        u32 plugin_data_end = 0;
        u32 plugin_bootloader_begin = 0;
        u32 plugin_bootloader_end = 0;
        u32 plugin_trace_count = 0;
        bool plugin_get_system_info_seen = false;
        u32 plugin_trace_thread_id = UINT32_MAX;
        u32 plugin_followup_trace_count = 0;
        u32 plugin_last_return_pc = 0;
        bool plugin_instruction_trace_started = false;
        u32 plugin_service_trace_count = 0;
        // Read-only, one-shot bootloader return diagnostics; transient, never savestated.
        bool level256_bootloader_map_snapshot_logged = false;
        bool level256_bootloader_pre_svc_snapshot_logged = false;
        bool level256_bootloader_return_snapshot_logged = false;
        bool level256_bootloader_return_trace_started = false;
        // Transient diagnostics; intentionally omitted from savestates.
        u32 plugin_query_process_memory_failures = 0;
        // Distinct register patterns only, bounded to eight calls per SVC and plugin load.
        std::array<Kernel::Level256SvcDiagnosticPattern,
                   Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT>
            level256_custom_backdoor_patterns{};
        std::size_t level256_custom_backdoor_pattern_count = 0;
        std::array<Kernel::Level256SvcDiagnosticPattern,
                   Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT>
            level256_open_process_patterns{};
        std::size_t level256_open_process_pattern_count = 0;
        // A one-shot, bounded trace for the verified Level256 worker's OpenProcess path.
        // Transient diagnostic state; intentionally omitted from savestates.
        bool level256_open_process_trace_started = false;
        // Low-frequency CreateThread result records for the verified Level256 worker.
        // Transient diagnostic state; intentionally omitted from savestates.
        u32 level256_thread_create_log_count = 0;
        // One-shot, hash-gated CTRPF OSD and frame-arbiter diagnostics. Transient by design.
        Kernel::Level256CtrpfOSDScanResult ctrpf_osd_scan{};
        std::array<u32, 3> ctrpf_osd_candidate_initial_words{};
        bool ctrpf_osd_post_init_compared = false;
        // One-shot OSD candidate cache-flush observation per guest process.
        // Transient diagnostic state; intentionally omitted from savestates.
        std::unordered_set<u32> ctrpf_osd_candidate_flush_observed_pids;
        bool ctrpf_arbiter_watch_started = false;
        u32 ctrpf_arbiter_watch_handle = 0;
        u32 ctrpf_arbiter_watch_address = 0;
        std::uintptr_t ctrpf_arbiter_watch_object = 0;
        u32 ctrpf_arbiter_trace_count = 0;
        // One-run observations of official CTRPF startup strings and its discovered OSD target.
        // Watchpoints alter only page dispatch for this opt-in diagnostic and are transient.
        bool ctrpf_startup_watchpoints_armed = false;
        std::array<u32, Kernel::LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT>
            ctrpf_startup_watch_counts{};
        bool ctrpf_osd_candidate_execution_seen = false;
        // Bounded runtime path from the verified game hook through its CTRPF wrapper.
        // Transient diagnostic state; intentionally omitted from savestates.
        bool ctrpf_hook_trace_started = false;
        bool ctrpf_hook_execution_trace_armed = false;
        u32 ctrpf_hook_trace_process_id = UINT32_MAX;
        bool ctrpf_hook_target_instruction_executed = false;
        bool ctrpf_hook_wrapper_instruction_executed = false;
        bool ctrpf_hook_callback_instruction_executed = false;
        bool ctrpf_hook_wrapper_observed = false;
        bool ctrpf_hook_asm_wrapper_observed = false;
        bool ctrpf_hook_asm_wrapper_executed = false;
        bool ctrpf_hook_callback_target_observed = false;
        u32 ctrpf_hook_trace_thread_id = UINT32_MAX;
        u32 ctrpf_hook_trace_runs = 0;
        u32 ctrpf_hook_asm_wrapper = 0;
        u32 ctrpf_hook_callback_target = 0;
        u32 ctrpf_hook_arbiter_trace_count = 0;
        u32 ctrpf_top_callback_count = 0;
        u32 ctrpf_bottom_callback_count = 0;
        u32 ctrpf_top_render_path_count = 0;
        bool ctrpf_hook_current_is_bottom = false;
        u32 ctrpf_hook_invocation_count = 0;
        u32 ctrpf_guest_wait_count = 0;
        u32 ctrpf_guest_event_notification_count = 0;
        u32 ctrpf_guest_wakeup_count = 0;
        u32 ctrpf_guest_wait_log_count = 0;
        u32 ctrpf_guest_arbiter_wait_count = 0;
        u32 ctrpf_guest_arbiter_signal_count = 0;
        u32 ctrpf_guest_arbiter_wakeup_count = 0;
        // Exact legacy file identity, retained for the previously reviewed plugin.
        bool level256_official_plugin_hash_matches = false;
        // Publisher identity from the appended Ed25519 manifest, transient across savestates.
        bool level256_official_plugin_signature_verified = false;
        // Privileged HLE profile authorization; stricter than publisher identity.
        bool level256_plugin_identity_verified = false;
        u32 level256_signed_plugin_artifact_version = 0;
        // Rejections are diagnostic only and bounded per plugin-load attempt.
        u32 level256_capability_rejection_log_count = 0;
        // Detailed MapProcessMemoryEx request/result records are opt-in and bounded.
        u32 level256_map_process_memory_ex_trace_count = 0;
        u32 level256_map_process_memory_ex_result_trace_count = 0;
        // Count of 0x2560 requests for the current plugin load; deliberately transient.
        u32 level256_lfcs_request_count = 0;
        bool is_default_path = false;
        std::string plugin_path = "";
        u32 plugin_process_id = UINT32_MAX;
        Kernel::MemoryRegion memory_region{};
        std::pair<u32, u32> memory_block{};

        void ResetTransientLevel256Authorization() {
            level256_official_plugin_hash_matches = false;
            level256_official_plugin_signature_verified = false;
            level256_plugin_identity_verified = false;
            level256_signed_plugin_artifact_version = 0;
            level256_capability_rejection_log_count = 0;
            level256_map_process_memory_ex_trace_count = 0;
            level256_map_process_memory_ex_result_trace_count = 0;
            level256_lfcs_request_count = 0;
        }

        bool use_user_load_parameters = false;
        PluginLoadParameters user_load_parameters;

        VAddr plg_event = 0;
        VAddr plg_reply = 0;
        Kernel::Handle memory_changed_handle = 0;

        bool is_exe_load_function_set = false;
        u32 exe_load_checksum = 0;

        std::vector<u32> load_exe_func;
        u32_le load_exe_args[4] = {0};

        template <class Archive>
        void serialize(Archive& ar, const unsigned int);
        friend class boost::serialization::access;
    };

    PLG_LDR(Core::System& system_);
    ~PLG_LDR() {}

    void OnProcessRun(Kernel::Process& process, Kernel::KernelSystem& kernel);
    void OnProcessExit(Kernel::Process& process, Kernel::KernelSystem& kernel);
    void OnProcessDestroyed(Kernel::Process& process);
    bool TracePluginSVC(const Kernel::Process& process, const Kernel::Thread& thread, u32 pc,
                        u32 svc_id, const char* svc_name, const char* service_name, u32 type,
                        s32 param, const std::array<u32, 8>& registers);
    void TracePluginSVCResult(const Kernel::Process& process, const Kernel::Thread& thread,
                              u32 svc_id, const char* svc_name, u32 svc_pc, u32 post_hle_pc,
                              u32 type, s32 param, u32 result, u64 value);
    void TraceLevel256BootloaderSnapshot(const Kernel::Process& process, const char* phase,
                                         u32 thread_id);
    void TraceCtrpfStartupMemoryAccess(const Kernel::Process& process, const Kernel::Thread& thread,
                                       VAddr address, u32 size, bool is_write, u64 value, u32 pc,
                                       u32 lr);
    void TraceLevel256GuestWait(const Kernel::Process& process, u32 thread_id, u32 pc,
                                const std::string& phase, const std::string& object_type,
                                u32 object_id, const std::string& object_name, u32 resume_pc);
    ResultVal<Kernel::Handle> GetMemoryChangedHandle(Kernel::KernelSystem& kernel);
    void OnMemoryChanged(Kernel::Process& process, Kernel::KernelSystem& kernel);

    PluginLoaderContext& GetPluginLoaderContext() {
        return plgldr_context;
    }
    Kernel::Luma3DS::CompatibilityContext GetLumaCompatibilityContext(bool enabled) const {
        return {.enabled = enabled};
    }
    Kernel::Luma3DS::PluginAuthorization GetLumaExtensionAuthorization(u64 title_id) const;
    Kernel::Luma3DS::Level256VerificationPolicy GetLevel256VerificationPolicy(bool enabled,
                                                                              u64 title_id) const;
    Kernel::Luma3DS::Level256ProcessViewManager& GetProcessViewManager() {
        return process_view_manager;
    }
    const Kernel::Luma3DS::Level256ProcessViewManager& GetProcessViewManager() const {
        return process_view_manager;
    }
    void SetPluginLoaderContext(PluginLoaderContext& context) {
        plgldr_context = context;
    }
    void SetEnabled(bool enabled) {
        plgldr_context.is_enabled = enabled;
    }
    bool GetEnabled() {
        return plgldr_context.is_enabled;
    }
    void SetAllowGameChangeState(bool allow) {
        plgldr_context.allow_game_change = allow;
    }
    bool GetAllowGameChangeState() {
        return plgldr_context.allow_game_change;
    }

private:
    Core::System& system;

    PluginLoaderContext plgldr_context;
    Kernel::Luma3DS::Level256ProcessViewManager process_view_manager;

    void IsEnabled(Kernel::HLERequestContext& ctx);
    void SetEnabled(Kernel::HLERequestContext& ctx);
    void SetLoadSettings(Kernel::HLERequestContext& ctx);
    void DisplayErrorMessage(Kernel::HLERequestContext& ctx);
    void GetPLGLDRVersion(Kernel::HLERequestContext& ctx);
    void GetArbiter(Kernel::HLERequestContext& ctx);
    void GetPluginPath(Kernel::HLERequestContext& ctx);
    void GetLevel256LFCS(Kernel::HLERequestContext& ctx);

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

std::shared_ptr<PLG_LDR> GetService(Core::System& system);

void InstallInterfaces(Core::System& system);

} // namespace Service::PLGLDR

BOOST_CLASS_EXPORT_KEY(Service::PLGLDR::PLG_LDR)
SERVICE_CONSTRUCT(Service::PLGLDR::PLG_LDR)
