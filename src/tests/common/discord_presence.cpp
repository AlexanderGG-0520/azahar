// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "citra_qt/discord_presence.h"
#include "core/hle/service/apt/presence.h"

TEST_CASE("Discord presence title follows the running application", "[discord]") {
    constexpr u64 home_menu_id = 0x0004003000008F02;
    constexpr u64 game_a_id = 0x000400000016C700;
    constexpr u64 game_b_id = 0x000400000016C600;

    REQUIRE(DiscordRPC::ResolvePresenceTitle(home_menu_id, home_menu_id, "HOME Menu", {}) ==
            "HOME Menu");
    REQUIRE(DiscordRPC::ResolvePresenceTitle(game_a_id, home_menu_id, "HOME Menu", "Red Cat") ==
            "Red Cat");
    REQUIRE(DiscordRPC::ResolvePresenceTitle(game_b_id, home_menu_id, "HOME Menu", "White Dog") ==
            "White Dog");
    REQUIRE(DiscordRPC::ResolvePresenceTitle(game_a_id, home_menu_id, "HOME Menu", {}) ==
            "Title 000400000016C700");
    REQUIRE(DiscordRPC::ResolvePresenceTitle(0, home_menu_id, "HOME Menu", {}) == "HOME Menu");
    REQUIRE(DiscordRPC::ResolvePresenceTitle(0, 0, {}, {}) == "Nintendo 3DS");
}

TEST_CASE("Presence follows APT application and HOME slots, not background processes",
          "[discord]") {
    constexpr u64 home = 0x0004003000008F02;
    constexpr u64 game_a = 0x000400000016C700;
    constexpr u64 game_b = 0x000400000016C600;
    constexpr u64 background = 0x0004003000009402;

    REQUIRE(Service::APT::SelectPresenceProgramId(0, home, background) == home);
    REQUIRE(Service::APT::SelectPresenceProgramId(game_a, home, background) == game_a);
    REQUIRE(Service::APT::SelectPresenceProgramId(0, home, game_a) == home);
    REQUIRE(Service::APT::SelectPresenceProgramId(game_b, home, game_a) == game_b);
    REQUIRE(Service::APT::SelectPresenceProgramId(0, 0, game_a) == game_a);
}

TEST_CASE("Discord presence state updates only when app or power state changes", "[discord]") {
    constexpr u64 home_menu_id = 0x0004003000008F02;
    constexpr u64 game_a_id = 0x000400000016C700;
    constexpr u64 game_b_id = 0x000400000016C600;
    DiscordRPC::PresenceState state;

    REQUIRE(state.NeedsUpdate(true, game_a_id, "Playing: Red Cat"));
    state.Set(true, game_a_id, "Playing: Red Cat");
    REQUIRE_FALSE(state.NeedsUpdate(true, game_a_id, "Playing: Red Cat"));
    REQUIRE(state.NeedsUpdate(true, home_menu_id, "Playing: HOME Menu"));
    state.Set(true, home_menu_id, "Playing: HOME Menu");
    REQUIRE_FALSE(state.NeedsUpdate(true, home_menu_id, "Playing: HOME Menu"));
    REQUIRE(state.NeedsUpdate(true, game_b_id, "Playing: White Dog"));
    state.Set(true, game_b_id, "Playing: White Dog");
    REQUIRE(state.NeedsUpdate(false, std::nullopt, {}));
    state.Set(false, std::nullopt, {});
    REQUIRE_FALSE(state.NeedsUpdate(false, std::nullopt, {}));
}

TEST_CASE("Discord retries unresolved title metadata without resending unchanged presence",
          "[discord]") {
    constexpr u64 game_id = 0x000400000016C700;
    DiscordRPC::PresenceState state;
    const std::string fallback = "Playing: Title 000400000016C700";

    state.Set(true, game_id, fallback);

    REQUIRE(state.NeedsTitleRefresh(true, game_id));
    REQUIRE_FALSE(state.NeedsUpdate(true, game_id, fallback));
    REQUIRE(state.NeedsUpdate(true, game_id, "Playing: Red Cat"));
}
