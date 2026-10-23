// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/kernel/luma3ds/compatibility.h"

TEST_CASE("Level256 SVC capabilities are independent and unknown extensions fail closed",
          "[core][kernel][luma3ds][level256][capability]") {
    using namespace Kernel::Luma3DS;

    const CompatibilityContext enabled{.enabled = true};
    const PluginAuthorization plugin{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 16,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07100000,
        .bootloader_code_begin = 0x06FFF000,
        .bootloader_code_end = 0x07000000,
        .capabilities = CapabilityBit(Level256Capability::CustomBackdoor),
    };

    REQUIRE(DispatchExtensionSvc({.svc_id = 0x80, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, plugin)
                .kind == SvcDispatchKind::DelegateToRegisteredHandler);
    REQUIRE(DispatchExtensionSvc({.svc_id = 0xA0, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, plugin)
                .kind == SvcDispatchKind::Rejected);
    REQUIRE(DispatchExtensionSvc({.svc_id = 0xA2, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, plugin)
                .kind == SvcDispatchKind::Rejected);
    REQUIRE(DispatchExtensionSvc({.svc_id = 0xB4, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, plugin)
                .kind == SvcDispatchKind::NotExtension);
}

TEST_CASE("Verified Level256 identity advertises only reviewed ABI capabilities",
          "[core][kernel][luma3ds][level256][capability]") {
    using namespace Kernel::Luma3DS;

    const auto capabilities = CapabilitiesForVerifiedPlugin(true);
    REQUIRE(HasCapability(capabilities, Level256Capability::SystemInfo));
    REQUIRE(HasCapability(capabilities, Level256Capability::CustomBackdoor));
    REQUIRE(HasCapability(capabilities, Level256Capability::ControlMemoryUnsafe));
    REQUIRE(HasCapability(capabilities, Level256Capability::Level256LFCS));
    REQUIRE(HasCapability(capabilities, Level256Capability::MapProcessMemoryEx));
    REQUIRE_FALSE(HasCapability(capabilities, Level256Capability::UnmapProcessMemoryEx));
    REQUIRE_FALSE(HasCapability(capabilities, static_cast<Level256Capability>(0xFF)));
    REQUIRE(HasCapability(capabilities, Level256Capability::PluginUdsControlIPC));
    REQUIRE(HasCapability(capabilities, Level256Capability::PluginUdsDataIPC));
    REQUIRE(HasCapability(capabilities, Level256Capability::NdmControlIPC));
    REQUIRE(HasCapability(capabilities, Level256Capability::SocAttachIPC));
    REQUIRE_FALSE(
        HasCapability(CapabilitiesForVerifiedPlugin(false), Level256Capability::SystemInfo));

    const auto legacy_capabilities = CapabilitiesForLevel256Plugin(true, true);
    REQUIRE(HasCapability(legacy_capabilities, Level256Capability::MapProcessMemoryEx));
    REQUIRE(HasCapability(legacy_capabilities, Level256Capability::UnmapProcessMemoryEx));
    REQUIRE(HasCapability(CapabilitiesForLevel256Plugin(true, false),
                          Level256Capability::MapProcessMemoryEx));
}

TEST_CASE("Level256 IPC adapters require their own capability and owner code scope",
          "[core][kernel][luma3ds][level256][capability]") {
    using namespace Kernel::Luma3DS;

    const CompatibilityContext enabled{.enabled = true};
    const PluginAuthorization auth{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 16,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07100000,
        .capabilities = CapabilityBit(Level256Capability::PluginUdsDataIPC),
    };
    REQUIRE(CanDispatchLevel256IPC(enabled, auth, 16, 0x07001000,
                                   Level256Capability::PluginUdsDataIPC));
    REQUIRE_FALSE(CanDispatchLevel256IPC(enabled, auth, 16, 0x07001000,
                                         Level256Capability::PluginUdsControlIPC));
    REQUIRE_FALSE(CanDispatchLevel256IPC(enabled, auth, 17, 0x07001000,
                                         Level256Capability::PluginUdsDataIPC));
    REQUIRE_FALSE(CanDispatchLevel256IPC(enabled, auth, 16, 0x00100000,
                                         Level256Capability::PluginUdsDataIPC));
    REQUIRE_FALSE(CanDispatchLevel256IPC({.enabled = false}, auth, 16, 0x07001000,
                                         Level256Capability::PluginUdsDataIPC));
}

TEST_CASE("Level256 extension dispatch checks each supported SVC capability independently",
          "[core][kernel][luma3ds][level256][capability]") {
    using namespace Kernel::Luma3DS;

    const CompatibilityContext enabled{.enabled = true};
    const PluginAuthorization signed_plugin{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = 16,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07100000,
        .capabilities = CapabilitiesForLevel256Plugin(true, false),
    };
    for (const u32 svc_id : std::array<u32, 7>{0x90, 0x91, 0x92, 0x93, 0x94, 0xA0, 0xA3}) {
        REQUIRE(DispatchExtensionSvc({.svc_id = svc_id, .process_id = 16, .entry_pc = 0x07001000},
                                     enabled, signed_plugin)
                    .kind == SvcDispatchKind::DelegateToRegisteredHandler);
    }
    for (const u32 svc_id : std::array<u32, 2>{0xA1, 0xA2}) {
        REQUIRE(DispatchExtensionSvc({.svc_id = svc_id, .process_id = 16, .entry_pc = 0x07001000},
                                     enabled, signed_plugin)
                    .kind == SvcDispatchKind::Rejected);
    }

    auto legacy_plugin = signed_plugin;
    legacy_plugin.capabilities = CapabilitiesForLevel256Plugin(true, true);
    REQUIRE(DispatchExtensionSvc({.svc_id = 0xA0, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, legacy_plugin)
                .kind == SvcDispatchKind::DelegateToRegisteredHandler);
    REQUIRE(DispatchExtensionSvc({.svc_id = 0xA1, .process_id = 16, .entry_pc = 0x07001000},
                                 enabled, legacy_plugin)
                .kind == SvcDispatchKind::DelegateToRegisteredHandler);
}

TEST_CASE("MapProcessMemoryEx decodes both official wrapper ABIs and validates flags",
          "[core][kernel][luma3ds][level256][capability]") {
    using namespace Kernel::Luma3DS;

    const auto legacy = DecodeMapProcessMemoryExRequest(
        {0xFFFF8001, 0x08000000, 0x00000041, 0x00100000, 0x2000, 1, 0x1234, 0x5678});
    REQUIRE_FALSE(legacy.abi_v2);
    CHECK(legacy.destination_process_handle == 0xFFFF8001);
    CHECK(legacy.destination_address == 0x08000000);
    CHECK(legacy.source_process_handle == 0x41);
    CHECK(legacy.source_address == 0x00100000);
    CHECK(legacy.size == 0x2000);
    CHECK(legacy.flags == 0); // Legacy wrapper deliberately clears r5.

    const auto current =
        DecodeMapProcessMemoryExRequest({MAP_PROCESS_MEMORY_EX_ABI_V2_MAGIC, 0x08000000, 0x00000041,
                                         0x00100000, 0x2000, 1, 0xFFFF8001, 0});
    REQUIRE(current.abi_v2);
    CHECK(current.destination_process_handle == 0xFFFF8001);
    CHECK(current.destination_address == 0x08000000);
    CHECK(current.source_process_handle == 0x41);
    CHECK(current.source_address == 0x00100000);
    CHECK(current.size == 0x2000);
    CHECK(current.flags == MAP_PROCESS_MEMORY_EX_FLAG_PRIVATE);
    CHECK(IsValidMapProcessMemoryExFlags(current.flags));
    CHECK_FALSE(IsValidMapProcessMemoryExFlags(2));
}
