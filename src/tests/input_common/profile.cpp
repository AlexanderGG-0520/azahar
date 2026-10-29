// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/param_package.h"
#include "common/settings.h"
#include "input_common/main.h"

TEST_CASE("Input profile map type updates nested analog directions without losing mappings",
          "[input_common][profile]") {
    const Common::ParamPackage up{{"engine", "sdl"}, {"button", "4"}, {"maptype", "guid"}};
    const Common::ParamPackage down{{"engine", "keyboard"}, {"code", "83"}};
    const Common::ParamPackage left{{"engine", "sdl"}, {"button", "7"}};
    const Common::ParamPackage right{{"engine", "gcpad"}, {"button", "2"}, {"port", "1"}};
    Common::ParamPackage analog{{"engine", "analog_from_button"}, {"up", up.Serialize()},
                                {"down", down.Serialize()},       {"left", left.Serialize()},
                                {"right", right.Serialize()},     {"modifier", ""},
                                {"modifier_scale", "0.5"}};
    const std::array<std::string, 4> directions{"up", "down", "left", "right"};

    InputCommon::ApplyInputProfileMapType(analog, "all", directions);

    const Common::ParamPackage serialized{analog.Serialize()};
    const Common::ParamPackage updated_up{serialized.Get("up", "")};
    const Common::ParamPackage updated_down{serialized.Get("down", "")};
    const Common::ParamPackage updated_left{serialized.Get("left", "")};
    const Common::ParamPackage updated_right{serialized.Get("right", "")};
    REQUIRE(updated_up.Get("engine", "") == "sdl");
    REQUIRE(updated_up.Get("button", "") == "4");
    REQUIRE(updated_up.Get("maptype", "") == "all");
    REQUIRE(updated_down.Get("engine", "") == "keyboard");
    REQUIRE(updated_down.Get("code", "") == "83");
    REQUIRE_FALSE(updated_down.Has("maptype"));
    REQUIRE(updated_left.Get("button", "") == "7");
    REQUIRE(updated_left.Get("maptype", "") == "all");
    REQUIRE(updated_right.Get("engine", "") == "gcpad");
    REQUIRE(updated_right.Get("button", "") == "2");
    REQUIRE(updated_right.Get("port", "") == "1");
    REQUIRE_FALSE(updated_right.Has("maptype"));
    REQUIRE(serialized.Get("modifier_scale", "") == "0.5");
    REQUIRE_FALSE(serialized.Has("maptype"));
}

TEST_CASE("Applying the current map type preserves existing analog direction entries",
          "[input_common][profile]") {
    const Common::ParamPackage direction{{"engine", "sdl"}, {"button", "6"}, {"maptype", "guid"}};
    Common::ParamPackage analog{
        {"engine", "analog_from_button"}, {"up", direction.Serialize()}, {"down", ""}};
    const auto before = analog.Serialize();
    const std::array<std::string, 2> directions{"up", "down"};

    InputCommon::ApplyInputProfileMapType(analog, "guid", directions);

    REQUIRE(analog.Get("up", "") == direction.Serialize());
    REQUIRE(analog.Get("down", "").empty());
    REQUIRE(analog.Serialize() == before);
}

TEST_CASE("Profile switching and saving preserves mixed input mappings",
          "[input_common][profile]") {
    struct SettingsProfileGuard {
        std::vector<Settings::InputProfile> profiles = Settings::values.input_profiles;
        Settings::InputProfile current = Settings::values.current_input_profile;
        int current_index = Settings::values.current_input_profile_index;
        ~SettingsProfileGuard() {
            Settings::values.input_profiles = std::move(profiles);
            Settings::values.current_input_profile = std::move(current);
            Settings::values.current_input_profile_index = current_index;
        }
    } guard;

    Settings::InputProfile mixed_profile{};
    mixed_profile.name = "Mixed mappings";
    mixed_profile.buttons[Settings::NativeButton::A] =
        Common::ParamPackage{{"engine", "keyboard"}, {"code", "65"}}.Serialize();
    mixed_profile.buttons[Settings::NativeButton::B] =
        Common::ParamPackage{{"engine", "mouse"}, {"button", "0"}}.Serialize();
    mixed_profile.buttons[Settings::NativeButton::X] =
        Common::ParamPackage{
            {"engine", "sdl"}, {"button", "4"}, {"guid", "pad-guid"}, {"maptype", "guid"}}
            .Serialize();
    mixed_profile.analogs[Settings::NativeAnalog::CirclePad] =
        Common::ParamPackage{
            {"engine", "analog_from_button"},
            {"up", Common::ParamPackage{{"engine", "keyboard"}, {"code", "87"}}.Serialize()},
            {"right",
             Common::ParamPackage{
                 {"engine", "sdl"}, {"button", "2"}, {"guid", "pad-guid"}, {"maptype", "guid"}}
                 .Serialize()},
            {"modifier_scale", "0.5"}}
            .Serialize();
    mixed_profile.prefer_touchscreen_over_mouse_buttons = false;

    Settings::values.input_profiles = {mixed_profile, Settings::InputProfile{}};
    Settings::LoadProfile(0);

    for (auto& binding : Settings::values.current_input_profile.buttons) {
        Common::ParamPackage button{binding};
        InputCommon::ApplyInputProfileMapType(button, "all");
        binding = button.Serialize();
    }
    auto analog = Common::ParamPackage{
        Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CirclePad]};
    const std::array<std::string, 4> directions{"up", "down", "left", "right"};
    InputCommon::ApplyInputProfileMapType(analog, "all", directions);
    Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CirclePad] =
        analog.Serialize();
    Settings::SaveProfile(0);
    Settings::LoadProfile(1);
    Settings::LoadProfile(0);

    const Common::ParamPackage restored_keyboard{
        Settings::values.current_input_profile.buttons[Settings::NativeButton::A]};
    const Common::ParamPackage restored_mouse{
        Settings::values.current_input_profile.buttons[Settings::NativeButton::B]};
    const Common::ParamPackage restored_gamepad{
        Settings::values.current_input_profile.buttons[Settings::NativeButton::X]};
    const Common::ParamPackage restored_analog{
        Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CirclePad]};
    const Common::ParamPackage restored_up{restored_analog.Get("up", "")};
    const Common::ParamPackage restored_right{restored_analog.Get("right", "")};

    REQUIRE(restored_keyboard.Get("engine", "") == "keyboard");
    REQUIRE(restored_keyboard.Get("code", "") == "65");
    REQUIRE(restored_mouse.Get("engine", "") == "mouse");
    REQUIRE(restored_mouse.Get("button", "") == "0");
    REQUIRE(restored_gamepad.Get("engine", "") == "sdl");
    REQUIRE(restored_gamepad.Get("maptype", "") == "all");
    REQUIRE(restored_up.Get("engine", "") == "keyboard");
    REQUIRE(restored_up.Get("code", "") == "87");
    REQUIRE(restored_right.Get("engine", "") == "sdl");
    REQUIRE(restored_right.Get("button", "") == "2");
    REQUIRE(restored_right.Get("maptype", "") == "all");
    REQUIRE(restored_analog.Get("modifier_scale", "") == "0.5");
    REQUIRE_FALSE(Settings::values.current_input_profile.prefer_touchscreen_over_mouse_buttons);
}
