// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/level256_system_info.h"

TEST_CASE("Level256 verification policy feeds GetSystemInfo without owning SVC dispatch",
          "[core][kernel][luma3ds][level256]") {
    using Kernel::Luma3DS::Level256VerificationPolicy;
    using Kernel::Luma3DS::SystemInfoResponse;

    const Level256VerificationPolicy policy{
        .enabled = true,
        .supported_title = true,
        .official_plugin_verified = true,
        .plugin_loaded = true,
        .owner_process_id = 16,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07103000,
    };
    SystemInfoResponse response{0xDEADBEEF, 0x0123456789ABCDEF};
    REQUIRE(Kernel::Luma3DS::TryGetLevel256VerificationResponse(
        policy, 16, 0x0700D500, 0x2A, 0x30000, 0, response));
    REQUIRE(response.result == 0x9A7FC03E);
    REQUIRE(response.value == 0x12FAADCC);

    const auto rejected = [&](Level256VerificationPolicy candidate, u32 pid, u32 pc, u32 type,
                              s32 param) {
        SystemInfoResponse untouched{0xDEADBEEF, 0x0123456789ABCDEF};
        REQUIRE_FALSE(Kernel::Luma3DS::TryGetLevel256VerificationResponse(
            candidate, pid, pc, 0x2A, type, param, untouched));
        REQUIRE(untouched.result == 0xDEADBEEF);
        REQUIRE(untouched.value == 0x0123456789ABCDEF);
    };

    auto disabled = policy;
    disabled.enabled = false;
    rejected(disabled, 16, 0x0700D500, 0x30000, 0);
    auto wrong_title = policy;
    wrong_title.supported_title = false;
    rejected(wrong_title, 16, 0x0700D500, 0x30000, 0);
    auto unverified = policy;
    unverified.official_plugin_verified = false;
    rejected(unverified, 16, 0x0700D500, 0x30000, 0);
    rejected(policy, 17, 0x0700D500, 0x30000, 0);
    rejected(policy, 16, 0x070000FC, 0x30000, 0);
    rejected(policy, 16, 0x0700D500, 0x20000, 0);
}
