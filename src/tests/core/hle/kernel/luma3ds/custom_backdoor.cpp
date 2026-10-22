// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/custom_backdoor.h"
#include "core/hle/kernel/luma3ds/level256_backend.h"

TEST_CASE("CustomBackdoor validates ABI and leaves guest callback dispatch to a backend",
          "[core][kernel][luma3ds][custom-backdoor]") {
    using namespace Kernel::Luma3DS;
    const CompatibilityContext context{.enabled = true};
    const PluginAuthorization authorization{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 42,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07100000,
    };
    SvcRequest request{
        .svc_id = 0x80,
        .process_id = 42,
        .entry_pc = 0x07001000,
        .arguments = {0x07002000, 1, 2, 3},
    };

    REQUIRE(ValidateCustomBackdoorRequest(request, context, authorization) ==
            CustomBackdoorStatus::Unsupported);
    REQUIRE(ValidateCustomBackdoorRequest(request, context, authorization) ==
            CustomBackdoorStatus::Unsupported);

    request.arguments[0] = 0x07002001;
    REQUIRE(ValidateCustomBackdoorRequest(request, context, authorization) ==
            CustomBackdoorStatus::Rejected);
    request.svc_id = 0x2A;
    REQUIRE(ValidateCustomBackdoorRequest(request, context, authorization) ==
            CustomBackdoorStatus::NotApplicable);
}

TEST_CASE("Level256 instruction patch range is confined to verified plugin RAM",
          "[core][kernel][luma3ds][custom-backdoor]") {
    using Kernel::Luma3DS::IsPluginRamPatchRange;
    REQUIRE(IsPluginRamPatchRange(0x07001000, 4, 0x07000000, 0x07100000));
    REQUIRE_FALSE(IsPluginRamPatchRange(0x07001001, 4, 0x07000000, 0x07100000));
    REQUIRE_FALSE(IsPluginRamPatchRange(0x00100000, 4, 0x07000000, 0x07100000));
    REQUIRE_FALSE(IsPluginRamPatchRange(0x070FFFFF, 4, 0x07000000, 0x07100000));
    REQUIRE_FALSE(IsPluginRamPatchRange(0xFFFFFFFC, 8, 0x07000000, 0xFFFFFFFF));
}
