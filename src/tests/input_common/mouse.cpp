// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <string>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "common/param_package.h"
#include "common/settings.h"
#include "input_common/mouse.h"

TEST_CASE("Mouse button devices report press and release state", "[input_common][mouse]") {
    InputCommon::Mouse mouse;
    const auto create_button = [&mouse](InputCommon::MouseButton button) {
        Common::ParamPackage params{
            {"engine", "mouse"},
            {"button", std::to_string(static_cast<int>(button))},
        };
        return mouse.Create(params);
    };

    auto left = create_button(InputCommon::MouseButton::Left);
    auto back = create_button(InputCommon::MouseButton::Back);
    auto forward = create_button(InputCommon::MouseButton::Forward);
    REQUIRE_FALSE(left->GetStatus());
    REQUIRE_FALSE(back->GetStatus());
    REQUIRE_FALSE(forward->GetStatus());

    mouse.PressButton(InputCommon::MouseButton::Left);
    mouse.PressButton(InputCommon::MouseButton::Back);
    mouse.PressButton(InputCommon::MouseButton::Forward);
    REQUIRE(left->GetStatus());
    REQUIRE(back->GetStatus());
    REQUIRE(forward->GetStatus());

    mouse.ReleaseButton(InputCommon::MouseButton::Left);
    REQUIRE_FALSE(left->GetStatus());
    REQUIRE(back->GetStatus());
    mouse.ReleaseAllButtons();
    REQUIRE_FALSE(back->GetStatus());
    REQUIRE_FALSE(forward->GetStatus());
}

TEST_CASE("Mouse button target stays latched until release", "[input_common][mouse]") {
    InputCommon::MouseButtonRoutingState routing;

    REQUIRE(routing.Press(InputCommon::MouseButton::Left, InputCommon::MouseButtonTarget::Touch) ==
            InputCommon::MouseButtonTarget::Touch);
    // Cursor movement does not retarget an already pressed button.
    REQUIRE(routing.GetTarget(InputCommon::MouseButton::Left) ==
            InputCommon::MouseButtonTarget::Touch);
    REQUIRE(routing.Press(InputCommon::MouseButton::Left, InputCommon::MouseButtonTarget::Button) ==
            InputCommon::MouseButtonTarget::Touch);
    REQUIRE(routing.Release(InputCommon::MouseButton::Left) ==
            InputCommon::MouseButtonTarget::Touch);
    REQUIRE(routing.GetTarget(InputCommon::MouseButton::Left) ==
            InputCommon::MouseButtonTarget::None);

    routing.Press(InputCommon::MouseButton::Left, InputCommon::MouseButtonTarget::Button);
    REQUIRE(routing.Press(InputCommon::MouseButton::Left, InputCommon::MouseButtonTarget::Touch) ==
            InputCommon::MouseButtonTarget::Button);
    auto released = routing.ReleaseAll();
    REQUIRE(released[static_cast<std::size_t>(InputCommon::MouseButton::Left)] ==
            InputCommon::MouseButtonTarget::Button);
    REQUIRE(routing.GetTarget(InputCommon::MouseButton::Left) ==
            InputCommon::MouseButtonTarget::None);
}

TEST_CASE("Left mouse routing honors touchscreen priority and unmapped touch",
          "[input_common][mouse]") {
    using InputCommon::MouseButton;
    using InputCommon::MouseButtonTarget;
    using InputCommon::SelectMouseButtonTarget;

    REQUIRE(SelectMouseButtonTarget(MouseButton::Left, true, true, true) ==
            MouseButtonTarget::Touch);
    REQUIRE(SelectMouseButtonTarget(MouseButton::Left, false, true, true) ==
            MouseButtonTarget::Button);
    REQUIRE(SelectMouseButtonTarget(MouseButton::Left, true, true, false) ==
            MouseButtonTarget::Button);
    REQUIRE(SelectMouseButtonTarget(MouseButton::Left, true, false, false) ==
            MouseButtonTarget::Touch);
    REQUIRE(SelectMouseButtonTarget(MouseButton::Right, true, true, true) ==
            MouseButtonTarget::Button);
    REQUIRE(SelectMouseButtonTarget(MouseButton::Middle, false, false, true) ==
            MouseButtonTarget::None);
}

TEST_CASE("Mouse button bindings round-trip through an input profile", "[input_common][mouse]") {
    struct RestoreSettings {
        std::vector<Settings::InputProfile> profiles = Settings::values.input_profiles;
        Settings::InputProfile profile = Settings::values.current_input_profile;
        int profile_index = Settings::values.current_input_profile_index;
        ~RestoreSettings() {
            Settings::values.input_profiles = std::move(profiles);
            Settings::values.current_input_profile = std::move(profile);
            Settings::values.current_input_profile_index = profile_index;
        }
    } restore_settings;

    Settings::InputProfile profile{};
    profile.buttons[Settings::NativeButton::A] =
        Common::ParamPackage{{"engine", "mouse"}, {"button", "0"}}.Serialize();
    profile.buttons[Settings::NativeButton::X] =
        Common::ParamPackage{{"engine", "mouse"}, {"button", "3"}}.Serialize();
    profile.prefer_touchscreen_over_mouse_buttons = false;

    Settings::values.input_profiles = {profile};
    Settings::values.current_input_profile = profile;
    Settings::SaveProfile(0);
    Settings::values.current_input_profile = {};
    Settings::LoadProfile(0);
    const auto& restored = Settings::values.current_input_profile;
    const Common::ParamPackage restored_a{restored.buttons[Settings::NativeButton::A]};
    const Common::ParamPackage restored_x{restored.buttons[Settings::NativeButton::X]};
    REQUIRE(restored_a.Get("engine", "") == "mouse");
    REQUIRE(restored_a.Get("button", -1) == static_cast<int>(InputCommon::MouseButton::Left));
    REQUIRE(restored_x.Get("button", -1) == static_cast<int>(InputCommon::MouseButton::Back));
    REQUIRE_FALSE(restored.prefer_touchscreen_over_mouse_buttons);
}
