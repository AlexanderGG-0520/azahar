// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/level256_start_scan_diagnostic.h"
#include "core/hle/kernel/luma3ds/level256_memory_operations.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/plgldr/plugin_path.h"

namespace {

constexpr bool Matches(bool enabled = true, u64 title_id = Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID,
                       u32 pid = 16, u32 plugin_pid = 16, bool plugin_loaded = true,
                       u32 code_begin = Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                       u32 code_end = Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                       u32 pc = Kernel::LEVEL256_DIAGNOSTIC_SVC_PC,
                       u32 svc_id = Kernel::LEVEL256_DIAGNOSTIC_SVC_ID,
                       u32 type = Kernel::LEVEL256_VERIFICATION_TYPE,
                       s32 param = Kernel::LEVEL256_VERIFICATION_PARAM,
                       bool official_plugin_hash_matches = true) {
    return Kernel::ShouldProvideLevel256VerificationResponse(
        enabled, official_plugin_hash_matches, title_id, pid, plugin_pid, plugin_loaded, code_begin,
        code_end, pc, svc_id, type, param);
}

} // namespace

TEST_CASE("Level256 supported-title set includes Red Cat Corps and excludes nearby IDs",
          "[core][kernel][level256]") {
    REQUIRE(Kernel::IsLevel256SupportedTitleId(0x0004000000155100ULL));
    REQUIRE(Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL));
    REQUIRE_FALSE(Kernel::IsLevel256SupportedTitleId(0x000400000016C701ULL));

    constexpr auto begin = Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN;
    constexpr auto end = Kernel::LEVEL256_DIAGNOSTIC_CODE_END;
    constexpr auto pc = Kernel::LEVEL256_WORKER_THREAD_ENTRY;
    REQUIRE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), true, 16, 16, true, begin,
        end, pc));
    REQUIRE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), true, 16, 16, true,
        0x07000100, 0x0710A000, 0x0709D290));
    REQUIRE_FALSE(Kernel::IsVerifiedLevel256PluginContext(
        false, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), true, 16, 16, true, begin,
        end, pc));
    REQUIRE_FALSE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C701ULL), true, 16, 16, true, begin,
        end, pc));
    REQUIRE_FALSE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), false, 16, 16, true, begin,
        end, pc));
    REQUIRE_FALSE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), true, 16, 17, true, begin,
        end, pc));
    REQUIRE_FALSE(Kernel::IsVerifiedLevel256PluginContext(
        true, Kernel::IsLevel256SupportedTitleId(0x000400000016C700ULL), true, 16, 16, true,
        0x07000100, 0x0710A000, 0x0710A000));
}

TEST_CASE("Level256 bootloader diagnostics require explicit opt-in and exact owner scope",
          "[core][kernel][level256]") {
    using Kernel::LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID;
    using Kernel::LEVEL256_BOOTLOADER_TRACE_ADD_LR_PC;
    using Kernel::ShouldCaptureLevel256BootloaderDiagnostic;
    using Kernel::ShouldCaptureLevel256BootloaderExceptionDiagnostic;
    using Kernel::ShouldCaptureLevel256BootloaderMapDiagnostic;

    REQUIRE(ShouldCaptureLevel256BootloaderMapDiagnostic(
        true, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 38, true));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderMapDiagnostic(
        false, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 38, true));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderMapDiagnostic(
        true, false, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 38, true));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderMapDiagnostic(
        true, true, false, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 38, true));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderMapDiagnostic(
        true, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 38, false));

    constexpr u32 code_begin = 0x07000100;
    constexpr u32 code_end = 0x0710A000;
    constexpr u32 bootloader_begin = 0x06FFF000;
    constexpr u32 bootloader_end = 0x07000000;
    const auto runtime_capture =
        [&](u64 title_id = LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, u32 pid = 16,
            u32 plugin_pid = 16, bool loaded = true, u32 tid = 38, u32 plugin_tid = 38,
            u32 pc = LEVEL256_BOOTLOADER_TRACE_ADD_LR_PC, bool enabled = true,
            bool diagnostics = true, bool identity = true) {
            return ShouldCaptureLevel256BootloaderDiagnostic(
                enabled, diagnostics, identity, title_id, pid, plugin_pid, loaded, tid, plugin_tid,
                pc, code_begin, code_end, bootloader_begin, bootloader_end);
        };
    REQUIRE(runtime_capture());
    REQUIRE_FALSE(runtime_capture(Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID));
    REQUIRE_FALSE(runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 17));
    REQUIRE_FALSE(runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, false));
    REQUIRE_FALSE(runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 37));
    REQUIRE_FALSE(
        runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38, 0xEB000FB0));
    REQUIRE_FALSE(runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38,
                                  LEVEL256_BOOTLOADER_TRACE_ADD_LR_PC, false));
    REQUIRE_FALSE(runtime_capture(LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38,
                                  LEVEL256_BOOTLOADER_TRACE_ADD_LR_PC, true, true, false));

    REQUIRE(ShouldCaptureLevel256BootloaderExceptionDiagnostic(
        true, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderExceptionDiagnostic(
        false, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderExceptionDiagnostic(
        true, true, true, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 39, 38));
    REQUIRE_FALSE(ShouldCaptureLevel256BootloaderExceptionDiagnostic(
        true, true, false, LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID, 16, 16, true, 38, 38));
}

TEST_CASE("Level256 network IPC tracing is opt-in, authenticated, owner-scoped, and bounded",
          "[core][kernel][level256][diagnostic]") {
    using Kernel::ConsumeLevel256NetworkIpcTraceSlot;
    using Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID;
    using Kernel::LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID;
    using Kernel::LEVEL256_NETWORK_IPC_TRACE_LIMIT;
    using Kernel::ShouldCaptureLevel256NetworkIpcDiagnostic;

    const auto eligible = [&](bool compatibility = true, bool diagnostics = true,
                              bool identity = true,
                              u64 title_id = LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID, u32 pid = 16,
                              u32 owner_pid = 16, bool loaded = true) {
        return ShouldCaptureLevel256NetworkIpcDiagnostic(compatibility, diagnostics, identity,
                                                         title_id, pid, owner_pid, loaded);
    };
    REQUIRE(eligible());
    REQUIRE_FALSE(eligible(false));
    REQUIRE_FALSE(eligible(true, false));
    REQUIRE_FALSE(eligible(true, true, false));
    REQUIRE_FALSE(eligible(true, true, true, LEVEL256_DIAGNOSTIC_TITLE_ID));
    REQUIRE_FALSE(eligible(true, true, true, LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID, 17, 16));
    REQUIRE_FALSE(
        eligible(true, true, true, LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID, 16, 16, false));

    u32 trace_count = 0;
    REQUIRE_FALSE(ConsumeLevel256NetworkIpcTraceSlot(false, trace_count));
    REQUIRE(trace_count == 0);
    for (u32 i = 0; i < LEVEL256_NETWORK_IPC_TRACE_LIMIT; ++i) {
        REQUIRE(ConsumeLevel256NetworkIpcTraceSlot(true, trace_count));
    }
    REQUIRE_FALSE(ConsumeLevel256NetworkIpcTraceSlot(true, trace_count));
    REQUIRE(trace_count == LEVEL256_NETWORK_IPC_TRACE_LIMIT);
}

TEST_CASE("Level256 StartScan output diagnostics decode and validate read-only buffer ranges",
          "[core][kernel][level256][diagnostic]") {
    using Kernel::DecodeLevel256StartScanBuffer;
    using Kernel::Level256StartScanBufferStatus;
    using Kernel::ValidateLevel256BeaconReplyRange;

    std::array<u32, 21> request{};
    request[0] = IPC::MakeHeader(15, 16, 4);
    request[1] = 0x4000;
    request[19] = IPC::MappedBufferDesc(0x4000, IPC::W);
    request[20] = 0x0010FFF8;

    const auto original = DecodeLevel256StartScanBuffer(request);
    REQUIRE(original.status == Level256StartScanBufferStatus::Valid);
    REQUIRE(original.request_max_size == 0x4000);
    REQUIRE(original.descriptor == request[19]);
    REQUIRE(original.mapped_size == 0x4000);
    REQUIRE(original.output_address == 0x0010FFF8);

    // Level256 moves the mapped-buffer descriptor and pointer into normal parameters without
    // changing their word positions. The diagnostic must still decode that forwarded request.
    request[0] = IPC::MakeHeader(15, 20, 0);
    REQUIRE(request[0] == 0x000F0500);
    const auto forwarded = DecodeLevel256StartScanBuffer(request);
    REQUIRE(forwarded.status == Level256StartScanBufferStatus::Valid);
    REQUIRE(forwarded.descriptor == original.descriptor);
    REQUIRE(forwarded.output_address == original.output_address);

    request[19] = 0;
    REQUIRE(DecodeLevel256StartScanBuffer(request).status ==
            Level256StartScanBufferStatus::InvalidDescriptor);

    request[19] = IPC::MappedBufferDesc(8, IPC::W);
    REQUIRE(DecodeLevel256StartScanBuffer(request).status ==
            Level256StartScanBufferStatus::BufferTooSmall);

    request[19] = IPC::MappedBufferDesc(0x4000, IPC::R);
    REQUIRE(DecodeLevel256StartScanBuffer(request).status ==
            Level256StartScanBufferStatus::InvalidDescriptor);

    request[1] = 8;
    request[19] = IPC::MappedBufferDesc(0x4000, IPC::W);
    REQUIRE(DecodeLevel256StartScanBuffer(request).status ==
            Level256StartScanBufferStatus::BufferTooSmall);

    const auto readable = [](VAddr page) { return page == 0x0010F000 || page == 0x00110000; };
    REQUIRE(ValidateLevel256BeaconReplyRange(0x0010FFF8, 0x4000, 0x1000, readable) ==
            Level256StartScanBufferStatus::Valid);
    REQUIRE(ValidateLevel256BeaconReplyRange(0x0010FFF8, 8, 0x1000, readable) ==
            Level256StartScanBufferStatus::BufferTooSmall);
    REQUIRE(ValidateLevel256BeaconReplyRange(0, 0x4000, 0x1000, readable) ==
            Level256StartScanBufferStatus::NullPointer);
    REQUIRE(ValidateLevel256BeaconReplyRange(0xFFFFFFFC, 0x4000, 0x1000, readable) ==
            Level256StartScanBufferStatus::AddressOverflow);
    REQUIRE(ValidateLevel256BeaconReplyRange(0x00102000, 0x4000, 0x1000, readable) ==
            Level256StartScanBufferStatus::UnreadableAddress);
}

TEST_CASE("Level256 D4 LDR decode identifies the saved game entry pointer",
          "[core][kernel][level256]") {
    std::array<u32, 16> registers{};
    registers[14] = 0x06FFF0F0;
    const auto decoded = Kernel::DecodeLevel256ArmLdrAddress(
        0xE59EF000, Kernel::LEVEL256_BOOTLOADER_TRACE_LOAD_PC, registers);
    REQUIRE(decoded.valid);
    REQUIRE(decoded.rn == 14);
    REQUIRE(decoded.rd == 15);
    REQUIRE(decoded.effective_address == 0x06FFF0F0);
}

TEST_CASE("Level256 CreateThread diagnostics are bounded and require an authenticated title",
          "[core][kernel][level256]") {
    u32 count = 0;
    REQUIRE_FALSE(Kernel::ConsumeLevel256ThreadCreateLogSlot(false, count));
    REQUIRE(count == 0);
    for (u32 i = 0; i < Kernel::LEVEL256_THREAD_CREATE_LOG_LIMIT; ++i) {
        REQUIRE(Kernel::ConsumeLevel256ThreadCreateLogSlot(true, count));
    }
    REQUIRE(count == Kernel::LEVEL256_THREAD_CREATE_LOG_LIMIT);
    REQUIRE_FALSE(Kernel::ConsumeLevel256ThreadCreateLogSlot(true, count));
    REQUIRE(count == Kernel::LEVEL256_THREAD_CREATE_LOG_LIMIT);
}

TEST_CASE("Level256 verification response mirrors Luma only for the verified plugin",
          "[core][kernel][level256]") {
    REQUIRE(Matches());
    REQUIRE(Matches(true, 0x000400000016C700ULL));
    REQUIRE_FALSE(Matches(false));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END, Kernel::LEVEL256_DIAGNOSTIC_SVC_PC,
                          Kernel::LEVEL256_DIAGNOSTIC_SVC_ID, Kernel::LEVEL256_VERIFICATION_TYPE,
                          Kernel::LEVEL256_VERIFICATION_PARAM, false));
    REQUIRE_FALSE(Matches(true, 0x0004000000155101ULL));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 17));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, false));
    REQUIRE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true, 0x07000104));
    REQUIRE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                    Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN, 0x07104000));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END, 0x06FFFFFF));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END, Kernel::LEVEL256_DIAGNOSTIC_SVC_PC,
                          0x32));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END, Kernel::LEVEL256_DIAGNOSTIC_SVC_PC,
                          Kernel::LEVEL256_DIAGNOSTIC_SVC_ID, 0x30001));
    REQUIRE_FALSE(Matches(true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END, Kernel::LEVEL256_DIAGNOSTIC_SVC_PC,
                          Kernel::LEVEL256_DIAGNOSTIC_SVC_ID, Kernel::LEVEL256_DIAGNOSTIC_TYPE,
                          Kernel::LEVEL256_VERIFICATION_PARAM));

    u32 result = 0;
    u64 output = 2;
    REQUIRE(Kernel::ApplyLevel256VerificationResponse(Matches(), result, output));
    REQUIRE(result == Kernel::LEVEL256_VERIFICATION_RESULT);
    REQUIRE(output == Kernel::LEVEL256_VERIFICATION_OUTPUT);

    result = 0x12345678;
    output = 0xAABBCCDDEEFF0011ULL;
    REQUIRE_FALSE(Kernel::ApplyLevel256VerificationResponse(Matches(false), result, output));
    REQUIRE(result == 0x12345678);
    REQUIRE(output == 0xAABBCCDDEEFF0011ULL);
}

TEST_CASE("Level256 emulator marker applies to supported titles without changing the emulator ID",
          "[core][kernel][level256]") {
    using Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN;
    using Kernel::LEVEL256_DIAGNOSTIC_CODE_END;
    using Kernel::LEVEL256_DIAGNOSTIC_SVC_ID;
    using Kernel::LEVEL256_DIAGNOSTIC_SVC_PC;
    using Kernel::LEVEL256_DIAGNOSTIC_TYPE;
    using Kernel::LEVEL256_LUMA_MARKER_RESULT;
    constexpr u64 red_cat_corps_title_id = 0x000400000016C700ULL;
    constexpr u64 unsupported_title_id = 0x000400000016C701ULL;

    REQUIRE(Service::PLGLDR::IsLevel256SupportedTitle(red_cat_corps_title_id));
    REQUIRE_FALSE(Service::PLGLDR::IsLevel256SupportedTitle(unsupported_title_id));

    const auto matches = [](bool enabled, bool supported_title, u32 pid, u32 plugin_pid,
                            bool plugin_loaded, bool hash_matches, u32 pc, u32 type, s32 param,
                            u32 code_begin = LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                            u32 code_end = LEVEL256_DIAGNOSTIC_CODE_END) {
        return Kernel::ShouldOverrideLevel256GetSystemInfo(
            enabled, supported_title, hash_matches, pid, plugin_pid, plugin_loaded, code_begin,
            code_end, pc, LEVEL256_DIAGNOSTIC_SVC_ID, type, param);
    };

    REQUIRE(matches(true, true, 16, 16, true, true, LEVEL256_DIAGNOSTIC_SVC_PC,
                    LEVEL256_DIAGNOSTIC_TYPE, 0));
    REQUIRE(matches(true, true, 16, 16, true, true, 0x0709D290, LEVEL256_DIAGNOSTIC_TYPE, 0,
                    0x07000100, 0x0710A000));
    REQUIRE_FALSE(matches(true, false, 16, 16, true, true, LEVEL256_DIAGNOSTIC_SVC_PC,
                          LEVEL256_DIAGNOSTIC_TYPE, 0));
    REQUIRE_FALSE(matches(false, true, 16, 16, true, true, LEVEL256_DIAGNOSTIC_SVC_PC,
                          LEVEL256_DIAGNOSTIC_TYPE, 0));
    REQUIRE_FALSE(matches(true, true, 16, 16, true, false, LEVEL256_DIAGNOSTIC_SVC_PC,
                          LEVEL256_DIAGNOSTIC_TYPE, 0));
    REQUIRE_FALSE(matches(true, true, 16, 17, true, true, LEVEL256_DIAGNOSTIC_SVC_PC,
                          LEVEL256_DIAGNOSTIC_TYPE, 0));

    u32 result = ResultSuccess.raw;
    u64 output = 2; // Azahar's normal emulator ID remains unchanged outside this exact hook.
    REQUIRE(Kernel::ApplyLevel256GetSystemInfoResult(true, result, output));
    REQUIRE(result == LEVEL256_LUMA_MARKER_RESULT);
    REQUIRE(output == 0);

    result = ResultSuccess.raw;
    output = 2;
    REQUIRE_FALSE(Kernel::ApplyLevel256GetSystemInfoResult(false, result, output));
    REQUIRE(result == ResultSuccess.raw);
    REQUIRE(output == 2);
}

TEST_CASE("Level256 OpenProcess trace uses the active plugin thread entry range",
          "[core][kernel][level256]") {
    using Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN;
    using Kernel::LEVEL256_DIAGNOSTIC_CODE_END;
    using Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID;
    using Kernel::LEVEL256_WORKER_THREAD_ENTRY;
    using Kernel::ShouldStartLevel256OpenProcessTrace;

    const auto matches =
        [&](bool enabled = true, bool official_hash = true,
            u64 title_id = LEVEL256_DIAGNOSTIC_TITLE_ID, u32 pid = 16, u32 owner_pid = 16,
            bool loaded = true, u32 code_begin = LEVEL256_DIAGNOSTIC_CODE_BEGIN,
            u32 code_end = LEVEL256_DIAGNOSTIC_CODE_END, u32 entry = LEVEL256_WORKER_THREAD_ENTRY,
            bool already_started = false) {
            return ShouldStartLevel256OpenProcessTrace(enabled, official_hash, title_id, pid,
                                                       owner_pid, loaded, code_begin, code_end,
                                                       entry, already_started);
        };

    REQUIRE(matches());
    REQUIRE(matches(true, true, 0x000400000016C700ULL));
    REQUIRE(matches(true, true, 0x000400000016C700ULL, 16, 16, true, 0x07000100, 0x0710A000,
                    0x07016E00));
    REQUIRE_FALSE(matches(false));
    REQUIRE_FALSE(matches(true, false));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID + 1));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 17));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 17));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, false));
    REQUIRE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                    LEVEL256_DIAGNOSTIC_CODE_BEGIN + 4, LEVEL256_DIAGNOSTIC_CODE_END + 0x1000,
                    0x07016E00));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_WORKER_THREAD_ENTRY,
                          LEVEL256_WORKER_THREAD_ENTRY));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_DIAGNOSTIC_CODE_END,
                          LEVEL256_DIAGNOSTIC_CODE_END));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_DIAGNOSTIC_CODE_END,
                          LEVEL256_WORKER_THREAD_ENTRY, true));
}

TEST_CASE("CTRPF startup watchpoints match only known text reads and OSD candidate writes",
          "[core][kernel][level256]") {
    using Kernel::GetLevel256CtrpfStartupWatchTarget;
    using Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS;
    using Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES;
    using Kernel::LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT;

    for (std::size_t i = 0; i < LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size(); ++i) {
        REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[i], 1,
                                                   false,
                                                   LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) == i);
        REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[i], 1,
                                                   true, LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
                LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT);
    }

    REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS, 4, true,
                                               LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
            LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size());
    REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS + 1, 1, true,
                                               LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
            LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size());
    REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS, 4, false,
                                               LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
            LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT);
    REQUIRE(GetLevel256CtrpfStartupWatchTarget(UINT32_MAX, 2, false,
                                               LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
            LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT);
    REQUIRE(GetLevel256CtrpfStartupWatchTarget(LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[0] - 1, 0,
                                               false, LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) ==
            LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT);
}

TEST_CASE("CTRPF startup memory watch logging is bounded per target", "[core][kernel][level256]") {
    using Kernel::ShouldLogLevel256CtrpfStartupWatch;
    REQUIRE(ShouldLogLevel256CtrpfStartupWatch(1));
    REQUIRE(ShouldLogLevel256CtrpfStartupWatch(2));
    REQUIRE(ShouldLogLevel256CtrpfStartupWatch(4));
    REQUIRE(ShouldLogLevel256CtrpfStartupWatch(8));
    REQUIRE(ShouldLogLevel256CtrpfStartupWatch(16));
    REQUIRE_FALSE(ShouldLogLevel256CtrpfStartupWatch(3));
    REQUIRE_FALSE(ShouldLogLevel256CtrpfStartupWatch(17));
    REQUIRE_FALSE(ShouldLogLevel256CtrpfStartupWatch(32));
}

TEST_CASE("Level256 OSD branch destination uses ARM PC plus eight", "[core][kernel][level256]") {
    u32 destination = 0;
    REQUIRE(Kernel::DecodeLevel256ArmBranchTarget(Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS,
                                                  Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH,
                                                  destination));
    REQUIRE(destination == Kernel::LEVEL256_CTRPF_HOOK_WRAPPER_ADDRESS);
    REQUIRE(Kernel::DecodeLevel256ArmBranchTarget(0x1000, 0xEAFFFFFE, destination));
    REQUIRE(destination == 0x1000);
    REQUIRE_FALSE(Kernel::DecodeLevel256ArmBranchTarget(0x003788C4, 0xE92D5FF0, destination));
    REQUIRE_FALSE(Kernel::DecodeLevel256ArmBranchTarget(0x003788C4, 0xFA000000, destination));
}

TEST_CASE("Level256 OpenProcess trace computes stack addresses at the actual LDR SP",
          "[core][kernel][level256]") {
    const auto c4 = Kernel::GetLevel256SnapshotReadLocation(0x10000, 0xC4);
    REQUIRE(c4.valid);
    REQUIRE(c4.read_address == 0x100C4);
    REQUIRE(c4.snapshot_base == 0x10010);
    REQUIRE(c4.snapshot_offset == 0xB4);

    const auto c8 = Kernel::GetLevel256SnapshotReadLocation(0x10000, 0xC8);
    REQUIRE(c8.valid);
    REQUIRE(c8.read_address == 0x100C8);
    REQUIRE(c8.snapshot_base == 0x10010);
    REQUIRE(c8.snapshot_offset == 0xB8);

    REQUIRE_FALSE(Kernel::GetLevel256SnapshotReadLocation(0xFFFFFFF0, 0xC8).valid);
    REQUIRE_FALSE(Kernel::GetLevel256SnapshotReadLocation(0x10000, 0x0C).valid);
}

TEST_CASE("Level256 ARM LDR diagnostic decodes all architectural registers safely",
          "[core][kernel][level256]") {
    std::array<u32, 16> registers{};
    registers[0] = 0x1000;
    registers[7] = 0x7000;
    registers[8] = 0x8000;
    registers[12] = 0xC000;
    registers[13] = 0x070F3BC8;
    registers[14] = 0xE000;
    registers[15] = 0xF000;

    const auto sp_load = Kernel::DecodeLevel256ArmLdrAddress(0xE59D10C8, 0x07046C70, registers);
    REQUIRE(sp_load.valid);
    REQUIRE(sp_load.rn == 13);
    REQUIRE(sp_load.rd == 1);
    REQUIRE(sp_load.offset == 0xC8);
    REQUIRE(sp_load.base == 0x070F3BC8);
    REQUIRE(sp_load.effective_address == 0x070F3C90);

    for (const u32 reg : {0U, 7U, 8U, 12U, 13U, 14U}) {
        const u32 instruction = 0xE5900000 | (reg << 16);
        const auto decoded = Kernel::DecodeLevel256ArmLdrAddress(instruction, 0x1000, registers);
        REQUIRE(decoded.valid);
        REQUIRE(decoded.rn == reg);
        REQUIRE(decoded.base == registers[reg]);
    }

    const auto pc_load = Kernel::DecodeLevel256ArmLdrAddress(0xE59F0004, 0x07001000, registers);
    REQUIRE(pc_load.valid);
    REQUIRE(pc_load.rn == 15);
    REQUIRE(pc_load.base == 0x07001008);
    REQUIRE(pc_load.effective_address == 0x0700100C);

    REQUIRE_FALSE(Kernel::ReadLevel256ArmRegister(16, 0x1000, registers).valid);
    REQUIRE_FALSE(Kernel::DecodeLevel256ArmLdrAddress(0xE79D10C8, 0x07046C70, registers).valid);
}

TEST_CASE("Level256 OpenProcess trace separates instruction and log budgets",
          "[core][kernel][level256]") {
    using Kernel::IsLevel256OpenProcessCriticalPc;
    using Kernel::IsLevel256OpenProcessTraceContext;
    using Kernel::IsLevel256OpenProcessTracePc;
    using Kernel::LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT;
    using Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT;
    using Kernel::LEVEL256_OPEN_PROCESS_TRACE_LIMIT;
    using Kernel::LEVEL256_OPEN_PROCESS_TRACE_LOG_LIMIT;
    using Kernel::ShouldLogLevel256OpenProcessTrace;

    REQUIRE(LEVEL256_OPEN_PROCESS_TRACE_LIMIT == 262144);
    REQUIRE(LEVEL256_OPEN_PROCESS_TRACE_LOG_LIMIT == 64);
    REQUIRE(LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT == 32);
    REQUIRE(LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT == 32);
    REQUIRE(IsLevel256OpenProcessCriticalPc(0x07046A30));
    REQUIRE(IsLevel256OpenProcessCriticalPc(0x07046A34));
    REQUIRE(IsLevel256OpenProcessCriticalPc(0x07046A3C));
    REQUIRE(IsLevel256OpenProcessCriticalPc(0x07046C70));
    REQUIRE(IsLevel256OpenProcessCriticalPc(0x070A131C));
    REQUIRE(IsLevel256OpenProcessTraceContext(16, 16, 33, 33));
    REQUIRE_FALSE(IsLevel256OpenProcessTraceContext(16, 16, 31, 33));
    REQUIRE_FALSE(IsLevel256OpenProcessTraceContext(17, 16, 33, 33));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046A3C));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046A2C));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046A38));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046A40));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046C10));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046C14));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046C34));
    REQUIRE(IsLevel256OpenProcessTracePc(0x07046C70));
    REQUIRE_FALSE(IsLevel256OpenProcessTracePc(0x07046C24));
    REQUIRE_FALSE(ShouldLogLevel256OpenProcessTrace(true, 0x07046C24, 0, 0));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(true, 0x07046C10, 0, 0));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(true, 0x070A131C, 0, 0));
    REQUIRE_FALSE(ShouldLogLevel256OpenProcessTrace(
        true, 0x07046C10, LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT, 0));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(true, 0x07046C10, 0,
                                              LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(true, 0x07046A2C,
                                              LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT, 0));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(true, 0x07046C2C,
                                              LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT,
                                              LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT - 1));
    REQUIRE_FALSE(ShouldLogLevel256OpenProcessTrace(true, 0x07046C2C, 0,
                                                    LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT));
    REQUIRE(ShouldLogLevel256OpenProcessTrace(false, 0x001060E0,
                                              LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT,
                                              LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT));
}

TEST_CASE("OpenProcess failure clears its SVC output handle", "[core][kernel][level256]") {
    u32 output_handle = 0xDFB6C720;
    Kernel::ClearOpenProcessOutput(output_handle);
    REQUIRE(output_handle == 0);
}

TEST_CASE("Level256 QueryProcessMemory failures log exponentially and aggregate total",
          "[core][kernel][level256]") {
    u32 failures = 0;
    REQUIRE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
    REQUIRE(failures == 1);
    REQUIRE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
    REQUIRE(failures == 2);
    REQUIRE_FALSE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
    REQUIRE(failures == 3);
    REQUIRE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
    REQUIRE(failures == 4);
    for (u32 expected = 5; expected < 8; ++expected) {
        REQUIRE_FALSE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
        REQUIRE(failures == expected);
    }
    REQUIRE(Kernel::RecordLevel256QueryProcessMemoryFailure(failures));
    REQUIRE(failures == 8);
}

TEST_CASE("Level256 duplicate SVC errors are suppressed only for exact QPM failures",
          "[core][kernel][level256]") {
    using namespace Kernel;
    const auto matches = [](bool enabled = true, bool hash = true,
                            u64 title = LEVEL256_DIAGNOSTIC_TITLE_ID, u32 pid = 16, u32 owner = 16,
                            bool loaded = true, u32 begin = LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                            u32 end = LEVEL256_DIAGNOSTIC_CODE_END,
                            u32 pc = LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC,
                            u32 svc = LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID,
                            u32 result = LEVEL256_QUERY_PROCESS_MEMORY_FAILURE) {
        return ShouldSuppressDuplicateLevel256SVCError(enabled, hash, title, pid, owner, loaded,
                                                       begin, end, pc, svc, result);
    };
    REQUIRE(matches());
    REQUIRE(matches(true, true, 0x000400000016C700ULL));
    REQUIRE(matches(true, true, 0x000400000016C700ULL, 16, 16, true, 0x07000100, 0x0710A000,
                    0x0709D244));
    REQUIRE_FALSE(matches(false));
    REQUIRE_FALSE(matches(true, false));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID + 1));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 17));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 17));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, false));
    REQUIRE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                    LEVEL256_DIAGNOSTIC_CODE_BEGIN + 4, LEVEL256_DIAGNOSTIC_CODE_END + 0x1000));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC));
    REQUIRE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                    LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_DIAGNOSTIC_CODE_END,
                    LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC - 4));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_DIAGNOSTIC_CODE_END,
                          LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC, 0x33));
    REQUIRE_FALSE(matches(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          LEVEL256_DIAGNOSTIC_CODE_BEGIN, LEVEL256_DIAGNOSTIC_CODE_END,
                          LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC,
                          LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID, 0xD8E007F6));
}

TEST_CASE("Level256 SVC diagnostics record distinct argument patterns at dynamic PCs",
          "[core][kernel][level256]") {
    std::array<Kernel::Level256SvcDiagnosticPattern, Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT>
        patterns{};
    std::size_t count = 0;
    const Kernel::Level256SvcDiagnosticPattern first{0x80, 0x0709D240, {0x070519B4, 1, 2, 3}};

    REQUIRE(Kernel::RecordLevel256SvcDiagnosticPattern(patterns, count, first));
    REQUIRE(count == 1);
    REQUIRE_FALSE(Kernel::RecordLevel256SvcDiagnosticPattern(patterns, count, first));
    REQUIRE(Kernel::RecordLevel256SvcDiagnosticPattern(patterns, count,
                                                       {0x80, 0x0709D244, {0x0706263C, 4, 5, 6}}));
    REQUIRE(count == 2);
    for (u32 i = 2; i < Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT; ++i) {
        REQUIRE(Kernel::RecordLevel256SvcDiagnosticPattern(
            patterns, count, {0x80, 0x0709D240 + i * 4, {0x07050000 + i, i, i, i}}));
    }
    REQUIRE(count == Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT);
    REQUIRE_FALSE(Kernel::RecordLevel256SvcDiagnosticPattern(
        patterns, count, {0x80, 0x0709D300, {0x07053000, 7, 8, 9}}));
    REQUIRE_FALSE(Kernel::RecordLevel256SvcDiagnosticPattern(patterns, count, first));
    REQUIRE(count == Kernel::LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT);
}

TEST_CASE("CTRPF diagnostics are opt-in, identity-gated and scoped to the target title owner",
          "[core][kernel][level256]") {
    using namespace Kernel;
    constexpr u64 title = LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID;
    REQUIRE(ShouldScanLevel256CtrpfOSD(true, true, title));
    REQUIRE_FALSE(ShouldScanLevel256CtrpfOSD(false, true, title));
    REQUIRE_FALSE(ShouldScanLevel256CtrpfOSD(true, false, title));
    REQUIRE_FALSE(ShouldScanLevel256CtrpfOSD(true, true, LEVEL256_DIAGNOSTIC_TITLE_ID));
    REQUIRE(ShouldCaptureLevel256CtrpfStartupDiagnostic(true, true, title, 16, 16, true));
    REQUIRE_FALSE(ShouldCaptureLevel256CtrpfStartupDiagnostic(true, true, title, 16, 17, true));
    REQUIRE_FALSE(ShouldCaptureLevel256CtrpfStartupDiagnostic(true, true, title, 16, 16, false));
}

TEST_CASE("Level256 field update callback mirrors verified ARM bit operations",
          "[core][kernel][level256]") {
    const u32 old_value = 0xE5C23013;
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(old_value) == 0);
    const u32 first_value = Kernel::ApplyLevel256FieldUpdate(old_value, 0x300);
    REQUIRE(first_value == 0xE5C23313);
    const u32 next_value = Kernel::ApplyLevel256FieldUpdate(first_value, 0x07046440);
    REQUIRE(next_value == 0xE7C67453);
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(first_value) == 0x300);
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(next_value) == 0x400);
}

TEST_CASE("Level256 process view candidate uses the heap intersection and page alignment",
          "[core][kernel][level256]") {
    constexpr u32 heap_begin = 0x08000000;
    constexpr u32 heap_end = 0x10000000;
    constexpr u32 page_size = Kernel::LEVEL256_PROCESS_VIEW_PAGE_ALIGNMENT;
    const u32 candidate = Kernel::FindLevel256GuestRangeCandidate(
        heap_begin - page_size, page_size * 3, heap_begin, heap_end,
        Kernel::LEVEL256_PROCESS_VIEW_SIZE, page_size);
    REQUIRE(candidate == heap_begin);

    REQUIRE(Kernel::FindLevel256GuestRangeCandidate(heap_begin - page_size, page_size, heap_begin,
                                                    heap_end, Kernel::LEVEL256_PROCESS_VIEW_SIZE,
                                                    page_size) == 0);
    REQUIRE(Kernel::FindLevel256GuestRangeCandidate(heap_begin + 1, page_size * 2, heap_begin,
                                                    heap_end, Kernel::LEVEL256_PROCESS_VIEW_SIZE,
                                                    page_size) == heap_begin + page_size);
    REQUIRE(Kernel::FindLevel256GuestRangeCandidate(heap_begin, page_size, heap_begin, heap_end,
                                                    Kernel::LEVEL256_PROCESS_VIEW_SIZE,
                                                    page_size) == heap_begin);

    REQUIRE(Kernel::FindLevel256GuestRangeCandidateFromHigh(0, 0x40000000, 0x07103000, heap_begin,
                                                            Kernel::LEVEL256_PROCESS_VIEW_SIZE,
                                                            page_size) == 0x07FFF000);
}

TEST_CASE("Level256 KProcess compatibility view uses guest addresses and copies through VM",
          "[core][kernel][level256][integration]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    const u32 pid = 0x1234;
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("level256-mock", pid));

    const auto& first_vma = process->vm_manager.vma_map.begin()->second;
    REQUIRE(first_vma.type == Kernel::VMAType::Free);
    REQUIRE(first_vma.base < Memory::HEAP_VADDR);
    const VAddr view_address = Kernel::FindLevel256GuestRangeCandidateFromHigh(
        first_vma.base, first_vma.size, Kernel::LEVEL256_DIAGNOSTIC_CODE_END, Memory::HEAP_VADDR,
        Kernel::LEVEL256_PROCESS_VIEW_SIZE, Kernel::LEVEL256_PROCESS_VIEW_PAGE_ALIGNMENT);
    REQUIRE(view_address == 0x07FFF000);
    REQUIRE(view_address < Memory::HEAP_VADDR);
    constexpr VAddr copy_destination = Kernel::LEVEL256_PROCESS_VIEW_COPY_DESTINATION;
    constexpr VAddr copy_destination_page =
        copy_destination & ~(static_cast<VAddr>(Memory::CITRA_PAGE_SIZE) - 1);
    auto view_mem = std::make_shared<BufferMem>(Kernel::LEVEL256_PROCESS_VIEW_SIZE);
    auto destination_mem = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
    constexpr VAddr codeset_destination = Kernel::LEVEL256_CODESET_COPY_DESTINATION;
    constexpr VAddr codeset_destination_page =
        codeset_destination & ~(static_cast<VAddr>(Memory::CITRA_PAGE_SIZE) - 1);
    auto codeset_destination_mem = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
    REQUIRE(process->vm_manager
                .MapBackingMemory(view_address, MemoryRef{view_mem},
                                  Kernel::LEVEL256_PROCESS_VIEW_SIZE, Kernel::MemoryState::Private)
                .Succeeded());
    REQUIRE(process->vm_manager
                .MapBackingMemory(copy_destination_page, MemoryRef{destination_mem},
                                  Memory::CITRA_PAGE_SIZE, Kernel::MemoryState::Private)
                .Succeeded());
    REQUIRE(process->vm_manager
                .MapBackingMemory(codeset_destination_page, MemoryRef{codeset_destination_mem},
                                  Memory::CITRA_PAGE_SIZE, Kernel::MemoryState::Private)
                .Succeeded());

    std::array<u32, Kernel::LEVEL256_PROCESS_VIEW_SIZE / sizeof(u32)> view{};
    const Kernel::Level256CodeSetViewInfo code_info{0x00100000,
                                                    0x005EC000,
                                                    0x006EC000,
                                                    0x00020000,
                                                    0x0070C000,
                                                    0x00010000,
                                                    Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID};
    Kernel::BuildLevel256ProcessView(view, view_address, pid, 0x18, 0x20, code_info, false);
    memory.WriteBlock(*process, view_address, view.data(), sizeof(view));
    REQUIRE(
        memory.Read32(*process,
                      view_address + Kernel::LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET) ==
        view_address + Kernel::LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET);
    REQUIRE(memory.Read32(*process,
                          view_address + Kernel::LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET) ==
            pid);
    REQUIRE(memory.Read32(*process, view_address +
                                        Kernel::LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET +
                                        Kernel::LEVEL256_PROCESS_VIEW_THREAD_LIMIT_OFFSET) == 0x20);

    std::array<u8, Kernel::LEVEL256_PROCESS_VIEW_COPY_SIZE> copied{};
    memory.ReadBlock(*process, view_address, copied.data(), copied.size());
    memory.WriteBlock(*process, copy_destination, copied.data(), copied.size());
    std::array<u8, Kernel::LEVEL256_PROCESS_VIEW_COPY_SIZE> observed{};
    memory.ReadBlock(*process, copy_destination, observed.data(), observed.size());
    REQUIRE(observed == copied);

    // The official O3DS 8.x path copies a KProcess snapshot to SP+0x10 and reads its PID from
    // [SP+0xC4], which is snapshot+0xB4. Verify that layout through guest memory after the copy.
    const VAddr caller_sp = copy_destination - 0x10;
    REQUIRE(memory.Read32(*process, caller_sp + 0xC4) == pid);

    std::array<u32, Kernel::LEVEL256_PROCESS_VIEW_SIZE / sizeof(u32)> n3ds_view{};
    Kernel::BuildLevel256ProcessView(n3ds_view, view_address, pid, 0x18, 0x20, code_info, true);
    REQUIRE(n3ds_view[Kernel::LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET / sizeof(u32)] ==
            view_address + Kernel::LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET);
    REQUIRE(n3ds_view[Kernel::LEVEL256_PROCESS_VIEW_N3DS_PROCESS_ID_OFFSET / sizeof(u32)] == pid);
    REQUIRE(n3ds_view[Kernel::LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET / sizeof(u32)] == 0);

    // The unmodified N3DS guest instructions load KCodeSet* from snapshot+0xB8 and PID from
    // snapshot+0xBC. The former drives Process::GetTextSize and SearchOSD.
    Kernel::BuildLevel256ProcessView(view, view_address, pid, 0x18, 0x20, code_info, true);
    memory.WriteBlock(*process, view_address, view.data(), sizeof(view));
    memory.ReadBlock(*process, view_address, copied.data(), copied.size());
    memory.WriteBlock(*process, copy_destination, copied.data(), copied.size());
    std::array<u32, 16> registers{};
    registers[13] = caller_sp;
    const auto pid_load = Kernel::DecodeLevel256ArmLdrAddress(0xE59D20CC, 0x07046C7C, registers);
    REQUIRE(pid_load.valid);
    REQUIRE(pid_load.rn == 13);
    REQUIRE(pid_load.rd == 2);
    REQUIRE(pid_load.effective_address == caller_sp + 0xCC);
    const u32 loaded_pid = memory.Read32(*process, static_cast<VAddr>(pid_load.effective_address));
    REQUIRE(loaded_pid == pid);

    // Exercise the following guest-memory handoff as well: STR r2,[r4,#0x48], then
    // LDR r1,[r4,#0x48] immediately before the OpenProcess wrapper.
    const VAddr process_state = copy_destination + 0x200;
    memory.Write32(*process, process_state + 0x48, loaded_pid);
    const u32 open_process_r1 = memory.Read32(*process, process_state + 0x48);
    REQUIRE(open_process_r1 == pid);

    // The distinct copy-source LDR reads the embedded KCodeSet, not the PID slot.
    const auto copy_source_load =
        Kernel::DecodeLevel256ArmLdrAddress(0xE59D10C8, 0x07046C70, registers);
    REQUIRE(copy_source_load.valid);
    REQUIRE(copy_source_load.rd == 1);
    REQUIRE(copy_source_load.effective_address == caller_sp + 0xC8);
    const VAddr codeset_source =
        memory.Read32(*process, static_cast<VAddr>(copy_source_load.effective_address));
    REQUIRE(codeset_source == view_address + Kernel::LEVEL256_PROCESS_VIEW_CODESET_OFFSET);
    std::array<u8, Kernel::LEVEL256_CODESET_SNAPSHOT_SIZE> codeset_snapshot{};
    memory.ReadBlock(*process, codeset_source, codeset_snapshot.data(), codeset_snapshot.size());
    memory.WriteBlock(*process, codeset_destination, codeset_snapshot.data(),
                      codeset_snapshot.size());
    REQUIRE(memory.Read32(*process, codeset_destination +
                                        Kernel::LEVEL256_CODESET_TEXT_PAGES_OFFSET) == 0x5EC);

    constexpr VAddr unsafe_commit_address = 0x07500000;
    constexpr u32 unsafe_commit_size = 0x10000;
    auto unsafe_commit_memory = std::make_shared<BufferMem>(unsafe_commit_size);
    REQUIRE(process->vm_manager
                .MapBackingMemory(unsafe_commit_address, MemoryRef{unsafe_commit_memory},
                                  unsafe_commit_size, Kernel::MemoryState::Private)
                .Succeeded());
    const auto unsafe_commit_vma = process->vm_manager.FindVMA(unsafe_commit_address);
    REQUIRE(unsafe_commit_vma != process->vm_manager.vma_map.end());
    REQUIRE(unsafe_commit_vma->second.meminfo_state == Kernel::MemoryState::Private);
    REQUIRE(unsafe_commit_vma->second.permissions == Kernel::VMAPermission::ReadWrite);
    auto conflicting_memory = std::make_shared<BufferMem>(unsafe_commit_size);
    REQUIRE(process->vm_manager
                .MapBackingMemory(unsafe_commit_address, MemoryRef{conflicting_memory},
                                  unsafe_commit_size, Kernel::MemoryState::Private)
                .Failed());
    REQUIRE(process->vm_manager.UnmapRange(unsafe_commit_address, unsafe_commit_size) ==
            ResultSuccess);

    REQUIRE(process->vm_manager.UnmapRange(view_address, Kernel::LEVEL256_PROCESS_VIEW_SIZE) ==
            ResultSuccess);
    REQUIRE(process->vm_manager.UnmapRange(copy_destination_page, Memory::CITRA_PAGE_SIZE) ==
            ResultSuccess);
}

TEST_CASE("Level256 CustomBackdoor process-view callbacks require the exact signed plugin ABI",
          "[core][kernel][level256]") {
    using namespace Kernel;
    constexpr bool on = true;
    constexpr bool trusted = true;
    constexpr u64 title = LEVEL256_DIAGNOSTIC_TITLE_ID;
    constexpr u32 pid = 16;
    constexpr u32 begin = LEVEL256_DIAGNOSTIC_CODE_BEGIN;
    constexpr u32 end = LEVEL256_DIAGNOSTIC_CODE_END;
    constexpr u32 pc = LEVEL256_CUSTOM_BACKDOOR_SVC_PC;
    constexpr u32 svc = LEVEL256_CUSTOM_BACKDOOR_SVC_ID;
    constexpr u32 view = 0x08000000;

    REQUIRE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true, begin,
                                                       end, pc, svc, true, 0, 0, 0));
    REQUIRE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, 0x000400000016C700ULL, pid, pid,
                                                       true, begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(false, trusted, title, pid, pid, true,
                                                             begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, false, title, pid, pid, true,
                                                             begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title + 1, pid, pid, true,
                                                             begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid + 1, pid, true,
                                                             begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid + 1, true,
                                                             begin, end, pc, svc, true, 0, 0, 0));
    REQUIRE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true,
                                                       begin + 4, end, pc, svc, true, 0, 0, 0));
    REQUIRE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true, begin,
                                                       end, pc + 4, svc, true, 0, 0, 0));
    REQUIRE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true, begin,
                                                       0x0710A000, 0x0709D290, svc, true, 0, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true,
                                                             begin, end, pc, svc, true, 1, 0, 0));
    REQUIRE_FALSE(ShouldHandleLevel256CurrentProcessCallback(on, trusted, title, pid, pid, true,
                                                             begin, end, pc, svc, false, 0, 0, 0));

    REQUIRE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, view, view,
        LEVEL256_PROCESS_VIEW_RESOURCE_LIMIT_OFFSET, 1));
    REQUIRE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, 0x000400000016C700ULL, pid, pid, true, begin, end, pc, svc, true, view, view,
        LEVEL256_PROCESS_VIEW_RESOURCE_LIMIT_OFFSET, 1));
    REQUIRE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, view, view,
        LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET, 1,
        LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET));
    REQUIRE_FALSE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, view, view, 0x88, 1));
    REQUIRE_FALSE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, view, view, 0x84, 2));
    REQUIRE_FALSE(ShouldHandleLevel256ResourceLimitCallback(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, false, view, view, 0x84, 1));

    REQUIRE(ShouldHandleLevel256ProcessSnapshotCopy(on, trusted, title, pid, pid, true, begin, end,
                                                    pc, svc, true, 0x070F3BD8, view, view,
                                                    LEVEL256_PROCESS_VIEW_COPY_SIZE, true));
    REQUIRE(ShouldHandleLevel256ProcessSnapshotCopy(
        on, trusted, 0x000400000016C700ULL, pid, pid, true, begin, end, pc, svc, true, 0x070F3BD8,
        view, view, LEVEL256_PROCESS_VIEW_COPY_SIZE, true));
    REQUIRE(ShouldHandleLevel256ProcessSnapshotCopy(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, 0x070F5664,
        view + LEVEL256_PROCESS_VIEW_CODESET_OFFSET, view, LEVEL256_CODESET_SNAPSHOT_SIZE, true));
    REQUIRE_FALSE(ShouldHandleLevel256ProcessSnapshotCopy(on, trusted, title, pid, pid, true, begin,
                                                          end, pc, svc, true, 0x070F3BD8, 0, view,
                                                          LEVEL256_PROCESS_VIEW_COPY_SIZE, true));
    REQUIRE_FALSE(ShouldHandleLevel256ProcessSnapshotCopy(on, trusted, title, pid, pid, true, begin,
                                                          end, pc, svc, true, 0x070F3BD8, view,
                                                          view, 0x64, true));
    REQUIRE_FALSE(ShouldHandleLevel256ProcessSnapshotCopy(on, trusted, title, pid, pid, true, begin,
                                                          end, pc, svc, true, 0x070F5664, 0, view,
                                                          LEVEL256_CODESET_SNAPSHOT_SIZE, true));
    REQUIRE_FALSE(ShouldHandleLevel256ProcessSnapshotCopy(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, false, 0x070F3BD8, view, view,
        LEVEL256_PROCESS_VIEW_COPY_SIZE, true));
    REQUIRE_FALSE(ShouldHandleLevel256ProcessSnapshotCopy(
        on, trusted, title, pid, pid, true, begin, end, pc, svc, true, 0x070F3BD8, view, view,
        LEVEL256_PROCESS_VIEW_COPY_SIZE, false));

    REQUIRE(IsLevel256PluginDataRange(0x070F3BD8, 0x100, 0x070F3000, 0x10000));
    REQUIRE_FALSE(IsLevel256PluginDataRange(0x070F3BD8, 0x100, 0x070F4000, 0x1000));
    REQUIRE_FALSE(IsLevel256PluginDataRange(0xFFFFFFF0, 0x100, 0xFFFFFF00, 0x100));

    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewFlag, 0x07047C68, 0, 1, 0));
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewFlag, 0x07047C68, 16, 1, 0));
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewFlag, 0x07047C68, 0, 1, 1));
    REQUIRE_FALSE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewFlag, 0x07047C68, 16, 0, 0));
    REQUIRE(Kernel::ApplyLevel256ViewFlagCallback(Kernel::Level256BackdoorCallbackKind::ViewFlag, 0,
                                                  0) == 0x2000);
    const u32 observed_flag_update = Kernel::ApplyLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewFlag, 0, 16);
    REQUIRE(observed_flag_update == 0x2000);
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0x300, 0xB0,
        observed_flag_update));
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0x300, 0xB0, 0x2000));
    REQUIRE_FALSE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0x300, 0xB0, 0));
    const u32 first_field_update = Kernel::ApplyLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x2000, 0x300);
    REQUIRE(first_field_update == 0x2300);
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(0x2000) == 0);
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07046440, 0x07046440, 0xB0, 0x2300));
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0, 0xB0, 0x2300));
    REQUIRE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0x07047C78, 0xB0, 0x2300));
    REQUIRE_FALSE(Kernel::ShouldHandleLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, 0x07047C78, 0x07046440, 0xB0, 0x2300));
    REQUIRE(Kernel::ApplyLevel256FieldUpdate(0x2300, 0) == 0x2000);
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(0x2300) == 0x300);
    const u32 second_field_update = Kernel::ApplyLevel256ViewFlagCallback(
        Kernel::Level256BackdoorCallbackKind::ViewField, first_field_update, 0x07046440);
    REQUIRE(second_field_update == 0x07046440);
    REQUIRE(Kernel::GetLevel256FieldUpdateResult(first_field_update) == 0x300);

    u32 guest_r0 = 0xAAAAAAAA;
    REQUIRE(ApplyLevel256BackdoorSVCResult(true, view, guest_r0));
    REQUIRE(guest_r0 == view);
    REQUIRE_FALSE(ApplyLevel256BackdoorSVCResult(false, 0, guest_r0));
    REQUIRE(guest_r0 == view);
}

TEST_CASE("Level256 v2 callbacks resolve from verified code signatures instead of fixed PCs",
          "[core][kernel][level256]") {
    using Kernel::Level256BackdoorCallbackKind;
    using Kernel::ResolveLevel256BackdoorCallback;

    constexpr u32 begin = 0x07000100;
    constexpr u32 end = 0x0710A000;
    constexpr std::array<u32, 3> current_process{
        0xE3E03A06,
        0xE5130FFB,
        0xE12FFF1E,
    };
    constexpr std::array<u32, 4> view_flag{
        0xE59030B0,
        0xE3833A02,
        0xE58030B0,
        0xE12FFF1E,
    };
    constexpr std::array<u32, 6> view_field{
        0xE790C002, 0xE3CC3C0F, 0xE1833001, 0xE7803002, 0xE20C0C0F, 0xE12FFF1E,
    };
    constexpr std::array<u32, 17> resource_limit{
        0xE7902001, 0xE5D2C011, 0xE5D23010, 0xE5D20012, 0xE5D21013, 0xE183340C,
        0xE1833800, 0xE1833C01, 0xE2833004, 0xE1A0C423, 0xE1A00823, 0xE1A01C23,
        0xE5C23010, 0xE5C2C011, 0xE5C20012, 0xE5C21013, 0xE12FFF1E,
    };
    constexpr std::array<u32, 16> priority_limit{
        0xE7903002, 0xE1A0C421, 0xE5D30009, 0xE5D32008, 0xE5C3C009, 0xE1822400,
        0xE5D3000A, 0xE1A0C821, 0xE1822800, 0xE5D3000B, 0xE5C31008, 0xE1A01C21,
        0xE5C3C00A, 0xE5C3100B, 0xE1820C00, 0xE12FFF1E,
    };
    constexpr std::array<u32, 6> process_snapshot_copy{
        0xE92D4010, 0xF10C01C0, 0xEB018BD1, 0xF10801C0, 0xE8BD8010, 0xE12FFF1E,
    };

    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C5C, current_process, begin, end, false) ==
            Level256BackdoorCallbackKind::CurrentProcess);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07080000, current_process, begin, end, false) ==
            Level256BackdoorCallbackKind::CurrentProcess);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C68, view_flag, begin, end, false) ==
            Level256BackdoorCallbackKind::ViewFlag);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C78, view_field, begin, end, false) ==
            Level256BackdoorCallbackKind::ViewField);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047CD0, resource_limit, begin, end, false) ==
            Level256BackdoorCallbackKind::ResourceLimit);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047CD0, priority_limit, begin, end, false) ==
            Level256BackdoorCallbackKind::PriorityLimit);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047D14, process_snapshot_copy, begin, end, false) ==
            Level256BackdoorCallbackKind::ProcessSnapshotCopy);
    auto relocated_copy = process_snapshot_copy;
    relocated_copy[2] = 0xEB018BD0;
    REQUIRE(ResolveLevel256BackdoorCallback(0x07090000, relocated_copy, begin, end, false) ==
            Level256BackdoorCallbackKind::ProcessSnapshotCopy);

    auto modified_current_process = current_process;
    modified_current_process[0] ^= 1;
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C5C, modified_current_process, begin, end,
                                            false) == Level256BackdoorCallbackKind::Unknown);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C5C, current_process, begin, 0x07047C67,
                                            false) == Level256BackdoorCallbackKind::Unknown);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07047C5C, current_process, begin, end, false) !=
            Level256BackdoorCallbackKind::Unknown);
    constexpr std::array<u32, 1> legacy_callback_word{0};
    REQUIRE(ResolveLevel256BackdoorCallback(0x07046424, legacy_callback_word, begin, end, false) ==
            Level256BackdoorCallbackKind::Unknown);
    REQUIRE(ResolveLevel256BackdoorCallback(0x07046424, legacy_callback_word, begin, end, true) ==
            Level256BackdoorCallbackKind::CurrentProcess);
}

TEST_CASE("Level256 process-view offset 0x08 mirrors maxPriority, not Event current value",
          "[core][kernel][level256]") {
    using namespace Kernel;
    constexpr u32 view_address = 0x07FFF000;
    constexpr u32 priority_limit = 0x18;
    constexpr u32 event_current = 9;
    const Level256CodeSetViewInfo code_info{};
    std::array<u32, LEVEL256_PROCESS_VIEW_SIZE / sizeof(u32)> view{};

    BuildLevel256ProcessView(view, view_address, 16, priority_limit, 0x20, code_info, true);

    REQUIRE(view[(LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET + 0x08) / sizeof(u32)] ==
            priority_limit);
    REQUIRE(view[(LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET + 0x08) / sizeof(u32)] !=
            event_current);

    u32 updated_limit = priority_limit;
    u32 callback_return = 0;
    REQUIRE(ShouldApplyLevel256PriorityLimitCallback(true, true, priority_limit, priority_limit, 0,
                                                     0x84, 0x84, updated_limit, callback_return));
    REQUIRE(updated_limit == 0);
    REQUIRE(callback_return == priority_limit);
    REQUIRE_FALSE(ShouldApplyLevel256PriorityLimitCallback(true, true, 1, priority_limit, 0, 0x84,
                                                           0x84, updated_limit, callback_return));
    REQUIRE(updated_limit == 0);
    REQUIRE(callback_return == priority_limit);
    REQUIRE_FALSE(CanCreateThreadWithPriority(priority_limit, 0, false));
    REQUIRE(CanCreateThreadWithPriority(updated_limit, 0, false));
    REQUIRE(CanCreateThreadWithPriority(priority_limit, 0, true));
}

TEST_CASE("Level256 CustomBackdoor patch requires exact plugin and call context",
          "[core][kernel][level256]") {
    const auto matches =
        [](bool enabled = true, bool hash_matches = true,
           u64 title_id = Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, u32 pid = 16, u32 plugin_pid = 16,
           bool plugin_loaded = true, u32 code_begin = Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
           u32 code_end = Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
           u32 svc_pc = Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_PC,
           u32 svc_id = Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID,
           u32 callback = Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK,
           u32 arg0 = Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK_ARGUMENT, u32 arg1 = 0,
           u32 arg2 = 1, u32 instruction = Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_BEFORE) {
            return Kernel::ShouldApplyLevel256BackdoorPatch(
                enabled, hash_matches, title_id, pid, plugin_pid, plugin_loaded, code_begin,
                code_end, svc_pc, svc_id, callback, arg0, arg1, arg2, instruction);
        };

    REQUIRE(matches());
    REQUIRE(matches(true, true, 0x000400000016C700ULL));
    REQUIRE_FALSE(matches(false));
    REQUIRE_FALSE(matches(true, false));
    REQUIRE_FALSE(matches(true, true, 0x0004000000155101ULL));
    REQUIRE_FALSE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 17));
    REQUIRE_FALSE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, false));
    REQUIRE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true, 0x07000104));
    REQUIRE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                    Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN, Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                    0x070991EC));
    REQUIRE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true, 0x07000100,
                    0x0710A000, 0x0709D290));
    REQUIRE_FALSE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                          Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_PC, 0x7F));
    REQUIRE_FALSE(matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN,
                          Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                          Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_PC,
                          Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID, 0x07046424));
    REQUIRE_FALSE(
        matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN, Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_PC, Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK_ARGUMENT, 0, 0));
    REQUIRE_FALSE(
        matches(true, true, Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true,
                Kernel::LEVEL256_DIAGNOSTIC_CODE_BEGIN, Kernel::LEVEL256_DIAGNOSTIC_CODE_END,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_PC, Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_CALLBACK_ARGUMENT, 0, 1,
                Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_AFTER));

    u32 instruction = Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_BEFORE;
    u32 callback_return = 0;
    REQUIRE(Kernel::ApplyLevel256BackdoorPatch(matches(), instruction, callback_return));
    REQUIRE(instruction == Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_AFTER);
    REQUIRE(callback_return == Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_RETURN);
    instruction = 0;
    callback_return = 0xDEADBEEF;
    REQUIRE_FALSE(Kernel::ApplyLevel256BackdoorPatch(true, instruction, callback_return));
    REQUIRE(instruction == 0);
    REQUIRE(callback_return == 0xDEADBEEF);
}

TEST_CASE("Level256 CTRPF OSD scan reports exact signatures and prologue candidates",
          "[core][kernel][level256]") {
    std::array<u8, 0x800> text{};
    constexpr std::array<u32, 5> signature = {
        0xE1833000, 0xE2044CFF, 0xE3C33CFF, 0xE1833004, 0xE1824F93,
    };
    constexpr u32 base = 0x00100000;
    constexpr std::size_t prologue_offset = 0x100;
    constexpr std::size_t signature_offset = 0x280;

    const auto write_word = [&](std::size_t offset, u32 word) {
        text[offset + 0] = static_cast<u8>(word);
        text[offset + 1] = static_cast<u8>(word >> 8);
        text[offset + 2] = static_cast<u8>(word >> 16);
        text[offset + 3] = static_cast<u8>(word >> 24);
    };
    write_word(prologue_offset, 0xE92D5FF0);
    for (std::size_t i = 0; i < signature.size(); ++i) {
        write_word(signature_offset + i * sizeof(u32), signature[i]);
    }

    const auto result = Kernel::ScanLevel256CtrpfOSDPatterns(text.data(), text.size(), base);
    REQUIRE(result.text_base == base);
    REQUIRE(result.text_size == text.size());
    REQUIRE(result.pattern0_offset == signature_offset);
    REQUIRE(result.candidate1 == base + prologue_offset);
    REQUIRE(result.selected_candidate == base + prologue_offset);
    REQUIRE(result.candidate2 == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
    REQUIRE(result.candidate3 == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);

    const std::array<u8, 0x800> empty_text{};
    const auto absent =
        Kernel::ScanLevel256CtrpfOSDPatterns(empty_text.data(), empty_text.size(), base);
    REQUIRE(absent.pattern0_offset == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
    REQUIRE(absent.pattern0_fallback_offset == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
    REQUIRE(absent.pattern3_offset == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
    REQUIRE(absent.pattern4_offset == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
    REQUIRE(absent.selected_candidate == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND);
}

TEST_CASE("Level256 CTRPF arbiter trace is bounded and opt-in to official plugin context",
          "[core][kernel][level256]") {
    REQUIRE(Kernel::ShouldLogLevel256ArbiterDiagnostic(true, 0));
    REQUIRE(Kernel::ShouldLogLevel256ArbiterDiagnostic(
        true, Kernel::LEVEL256_CTRPF_ARBITER_DIAGNOSTIC_LIMIT - 1));
    REQUIRE_FALSE(Kernel::ShouldLogLevel256ArbiterDiagnostic(
        true, Kernel::LEVEL256_CTRPF_ARBITER_DIAGNOSTIC_LIMIT));
    REQUIRE_FALSE(Kernel::ShouldLogLevel256ArbiterDiagnostic(false, 0));
}
