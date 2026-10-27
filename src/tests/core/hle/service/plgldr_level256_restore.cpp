// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>

#include "core/hle/service/plgldr/plgldr.h"

TEST_CASE("Restored plugin mappings do not inherit Level256 identity or capabilities",
          "[core][plgldr][level256][savestate]") {
    Service::PLGLDR::PLG_LDR::PluginLoaderContext context{};
    context.plugin_loaded = true;
    context.level256_official_plugin_hash_matches = true;
    context.level256_official_plugin_signature_verified = true;
    context.level256_plugin_identity_verified = true;
    context.level256_signed_plugin_artifact_version = 20031;
    context.level256_capability_rejection_log_count = 4;
    context.level256_lfcs_request_count = 3;

    context.ResetTransientLevel256Authorization();

    REQUIRE(context.plugin_loaded);
    REQUIRE_FALSE(context.level256_official_plugin_hash_matches);
    REQUIRE_FALSE(context.level256_official_plugin_signature_verified);
    REQUIRE_FALSE(context.level256_plugin_identity_verified);
    REQUIRE(context.level256_signed_plugin_artifact_version == 0);
    REQUIRE(context.level256_capability_rejection_log_count == 0);
    REQUIRE(context.level256_lfcs_request_count == 0);
}

TEST_CASE("Plugin process state is discarded after a game exits", "[core][plgldr][lifecycle]") {
    Service::PLGLDR::PLG_LDR::PluginLoaderContext context{};
    context.is_enabled = false;
    context.allow_game_change = false;
    context.plugin_loaded = true;
    context.plugin_process_id = 42;
    context.plugin_path = "/Level256/plugin/Level256.3gx";
    context.plugin_code_begin = 0x07000100;
    context.plugin_code_end = 0x07100000;
    context.memory_block = {0x1000, 0x2000};
    context.use_user_load_parameters = true;
    context.user_load_parameters.path[0] = '/';
    context.level256_plugin_identity_verified = true;
    context.level256_signed_plugin_artifact_version = 20031;
    context.plg_event = 0x070000FC;
    context.memory_changed_handle = 7;

    context.ResetProcessState();

    REQUIRE_FALSE(context.is_enabled);
    REQUIRE_FALSE(context.allow_game_change);
    REQUIRE_FALSE(context.plugin_loaded);
    REQUIRE(context.plugin_process_id == UINT32_MAX);
    REQUIRE(context.plugin_path.empty());
    REQUIRE(context.plugin_code_begin == 0);
    REQUIRE(context.plugin_code_end == 0);
    REQUIRE(context.memory_block == (std::pair<u32, u32>{}));
    REQUIRE_FALSE(context.use_user_load_parameters);
    REQUIRE(context.user_load_parameters.path[0] == '\0');
    REQUIRE_FALSE(context.level256_plugin_identity_verified);
    REQUIRE(context.level256_signed_plugin_artifact_version == 0);
    REQUIRE(context.plg_event == 0);
    REQUIRE(context.memory_changed_handle == 0);
}
