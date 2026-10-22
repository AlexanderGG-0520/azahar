// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/compatibility.h"

TEST_CASE("Luma extension dispatch separates Luma mode from plugin authority",
          "[core][kernel][luma3ds][dispatch]") {
    using namespace Kernel::Luma3DS;

    const CompatibilityContext luma_context{.enabled = true};
    const PluginAuthorization verified_plugin{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 16,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07103000,
        .bootloader_code_begin = 0x06FFF000,
        .bootloader_code_end = 0x07000000,
    };
    const SvcRequest extension_request{
        .svc_id = 0x80,
        .process_id = 16,
        .entry_pc = 0x070991F0,
    };

    const auto authorized = DispatchExtensionSvc(extension_request, luma_context, verified_plugin);
    REQUIRE(authorized.kind == SvcDispatchKind::DelegateToRegisteredHandler);

    auto disabled_luma = luma_context;
    disabled_luma.enabled = false;
    REQUIRE(DispatchExtensionSvc(extension_request, disabled_luma, verified_plugin).kind ==
            SvcDispatchKind::Rejected);

    auto unverified_plugin = verified_plugin;
    unverified_plugin.plugin_integrity_verified = false;
    REQUIRE(DispatchExtensionSvc(extension_request, luma_context, unverified_plugin).kind ==
            SvcDispatchKind::Rejected);

    auto wrong_owner = verified_plugin;
    wrong_owner.owner_process_id = 17;
    REQUIRE(DispatchExtensionSvc(extension_request, luma_context, wrong_owner).kind ==
            SvcDispatchKind::Rejected);

    auto outside_plugin = extension_request;
    outside_plugin.entry_pc = 0x070000FC;
    REQUIRE(DispatchExtensionSvc(outside_plugin, luma_context, verified_plugin).kind ==
            SvcDispatchKind::Rejected);
}

TEST_CASE("ordinary SVCs bypass the Luma extension dispatcher",
          "[core][kernel][luma3ds][dispatch]") {
    using namespace Kernel::Luma3DS;

    const CompatibilityContext luma_context{.enabled = false};
    const PluginAuthorization no_plugin{};
    const SvcRequest ordinary_request{.svc_id = 0x2A, .process_id = 16, .entry_pc = 0x00100000};

    REQUIRE(DispatchExtensionSvc(ordinary_request, luma_context, no_plugin).kind ==
            SvcDispatchKind::NotExtension);
}
