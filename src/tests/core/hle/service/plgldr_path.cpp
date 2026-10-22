// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/service/plgldr/plugin_path.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Level256 plugin path is preferred only for supported titles", "[core][plgldr]") {
    using Service::PLGLDR::ChoosePluginPathSource;
    using Service::PLGLDR::PluginPathSource;

    REQUIRE(ChoosePluginPathSource(0x0004000000155100ULL, true, false, true, true) ==
            PluginPathSource::Level256);
    REQUIRE(ChoosePluginPathSource(0x00040000001B2900ULL, true, false, false, true) ==
            PluginPathSource::Level256);
    REQUIRE(Service::PLGLDR::IsLevel256SupportedTitle(0x000400000016C700ULL)); // Red Cat
    REQUIRE(Service::PLGLDR::IsLevel256SupportedTitle(0x000400000016C600ULL)); // White Dog
    REQUIRE(Service::PLGLDR::IsLevel256SupportedTitle(0x0004000000155100ULL)); // Shinuchi
    REQUIRE_FALSE(Service::PLGLDR::IsLevel256SupportedTitle(0x000400000016C701ULL));
    REQUIRE(ChoosePluginPathSource(0x0004000000155100ULL, false, false, true, true) ==
            PluginPathSource::TitleSpecific);
    REQUIRE(ChoosePluginPathSource(0x0004000000155100ULL, false, false, false, true) ==
            PluginPathSource::Default);
    REQUIRE(ChoosePluginPathSource(0x0004000000999900ULL, true, false, true, true) ==
            PluginPathSource::TitleSpecific);
    REQUIRE(ChoosePluginPathSource(0x0004000000155100ULL, true, true, true, true) ==
            PluginPathSource::TitleSpecific);
}

TEST_CASE("GetPluginPath exposes the selected SDMC path to the guest", "[core][plgldr]") {
    REQUIRE(Service::PLGLDR::MakeGuestPluginPath(
                "/emulator/sdmc/Level256/plugin/Level256.3gx", "/emulator/sdmc/") ==
            "/Level256/plugin/Level256.3gx");
    REQUIRE(Service::PLGLDR::MakeGuestPluginPath(
                "\\emulator\\sdmc\\luma\\plugins\\default.3gx", "/emulator/sdmc/") ==
            "/luma/plugins/default.3gx");
}
