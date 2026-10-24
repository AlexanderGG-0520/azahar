// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/compatibility.h"

TEST_CASE("Luma kernel compatibility context authenticates only the loaded owner code",
          "[core][kernel][luma3ds]") {
    using Kernel::Luma3DS::CompatibilityContext;
    using Kernel::Luma3DS::PluginAuthorization;
    const CompatibilityContext context{.enabled = true};
    const PluginAuthorization authorization{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 42,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07103000,
        .bootloader_code_begin = 0x06FFF000,
        .bootloader_code_end = 0x07000000,
        .capabilities = Kernel::Luma3DS::CapabilitiesForVerifiedPlugin(true),
    };

    REQUIRE(authorization.OwnsCode(42, 0x07000100));
    REQUIRE(authorization.Authorizes(42, 0x07000100,
                                     Kernel::Luma3DS::Level256Capability::CustomBackdoor));
    REQUIRE(
        Kernel::Luma3DS::CanDispatchExtension(context, authorization, 42, 0x07000100,
                                              Kernel::Luma3DS::Level256Capability::CustomBackdoor));
    REQUIRE(Kernel::Luma3DS::IsValidCustomBackdoorCallback(context, authorization, 42, 0x07000100));
    REQUIRE_FALSE(
        Kernel::Luma3DS::IsValidCustomBackdoorCallback(context, authorization, 42, 0x07000101));
    REQUIRE(authorization.OwnsCode(42, 0x07102FFC));
    REQUIRE(authorization.OwnsCode(42, 0x06FFF000));
    REQUIRE_FALSE(
        Kernel::Luma3DS::IsValidCustomBackdoorCallback(context, authorization, 42, 0x06FFF000));
    REQUIRE_FALSE(authorization.OwnsCode(41, 0x07000100));
    REQUIRE_FALSE(
        Kernel::Luma3DS::CanDispatchExtension(context, authorization, 41, 0x07000100,
                                              Kernel::Luma3DS::Level256Capability::CustomBackdoor));
    REQUIRE_FALSE(authorization.OwnsCode(42, 0x070000FC));
    REQUIRE_FALSE(authorization.OwnsCode(42, 0x07103000));

    auto disabled = context;
    disabled.enabled = false;
    REQUIRE_FALSE(
        Kernel::Luma3DS::CanDispatchExtension(disabled, authorization, 42, 0x07000100,
                                              Kernel::Luma3DS::Level256Capability::CustomBackdoor));
    auto unverified = authorization;
    unverified.plugin_integrity_verified = false;
    REQUIRE_FALSE(
        unverified.Authorizes(42, 0x07000100, Kernel::Luma3DS::Level256Capability::CustomBackdoor));
}

TEST_CASE("Luma cache operation classes preserve separate data and instruction semantics",
          "[core][kernel][luma3ds]") {
    REQUIRE(Kernel::Luma3DS::IsDataCacheOperation(0x91));
    REQUIRE(Kernel::Luma3DS::IsDataCacheOperation(0x92));
    REQUIRE_FALSE(Kernel::Luma3DS::IsDataCacheOperation(0x93));
    REQUIRE(Kernel::Luma3DS::IsInstructionCacheOperation(0x93));
    REQUIRE(Kernel::Luma3DS::IsInstructionCacheOperation(0x94));
    REQUIRE_FALSE(Kernel::Luma3DS::IsInstructionCacheOperation(0x92));
}

TEST_CASE("Luma extension SVC classification excludes ordinary SVCs", "[core][kernel][luma3ds]") {
    REQUIRE(Kernel::Luma3DS::IsExtensionSVC(0x80));
    REQUIRE(Kernel::Luma3DS::IsExtensionSVC(0x90));
    REQUIRE(Kernel::Luma3DS::IsExtensionSVC(0x94));
    REQUIRE(Kernel::Luma3DS::IsExtensionSVC(0xA0));
    REQUIRE(Kernel::Luma3DS::IsExtensionSVC(0xA3));
    REQUIRE_FALSE(Kernel::Luma3DS::IsExtensionSVC(0x08));
    REQUIRE_FALSE(Kernel::Luma3DS::IsExtensionSVC(0x2A));
    REQUIRE_FALSE(Kernel::Luma3DS::IsExtensionSVC(0x95));
}

TEST_CASE("Luma GetSystemInfo marker does not alter ordinary Azahar information",
          "[core][kernel][luma3ds]") {
    u32 result = 0xDEADBEEF;
    u64 output = 0x0123456789ABCDEF;

    Kernel::Luma3DS::SystemInfoResponse response{result, output};
    REQUIRE_FALSE(Kernel::Luma3DS::TryGetLumaSystemInfoResponse(false, 0x20000, 0, response));
    REQUIRE(response.result == 0xDEADBEEF);
    REQUIRE(response.value == 0x0123456789ABCDEF);

    REQUIRE(Kernel::Luma3DS::TryGetLumaSystemInfoResponse(true, 0x20000, 0, response));
    REQUIRE(response.result == 1);
    REQUIRE(response.value == 0);

    response = {0xDEADBEEF, 0x0123456789ABCDEF};
    REQUIRE_FALSE(Kernel::Luma3DS::TryGetLumaSystemInfoResponse(true, 0x20000, 1, response));
    REQUIRE_FALSE(Kernel::Luma3DS::TryGetLumaSystemInfoResponse(true, 0x20001, 0, response));
    REQUIRE(response.result == 0xDEADBEEF);
    REQUIRE(response.value == 0x0123456789ABCDEF);
}
