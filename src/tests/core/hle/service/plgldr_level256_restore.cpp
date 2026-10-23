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
