// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/level256_registration_diagnostics.h"

TEST_CASE("Level256 registration diagnostics only identify account paths", "[core][plgldr]") {
    using Service::Level256RegistrationDiagnostics::IsAccountPath;
    using Service::Level256RegistrationDiagnostics::SafePathLabel;

    REQUIRE(IsAccountPath("/Level256/account.key.new"));
    REQUIRE(IsAccountPath("sdmc:\\Level256\\account.id"));
    REQUIRE(IsAccountPath("/Level256"));
    REQUIRE_FALSE(IsAccountPath("/Level256/plugin/Level256.3gx"));
    REQUIRE_FALSE(IsAccountPath("/Nintendo 3DS/other.bin"));
    REQUIRE(SafePathLabel("C:\\private-user\\sdmc\\Level256\\account.key") == "account.key");
    REQUIRE(SafePathLabel("C:\\private-user\\sdmc\\Nintendo 3DS\\other.bin") == "filtered");
}
