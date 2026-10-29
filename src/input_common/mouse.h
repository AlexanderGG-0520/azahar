// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <memory>
#include <optional>
#include "core/frontend/input.h"

namespace Common {
class ParamPackage;
}

namespace InputCommon {

enum class MouseButton : unsigned {
    Left,
    Right,
    Middle,
    Back,
    Forward,
    Count = 27,
};

// Qt exposes 27 single-bit buttons. Stored indices remain zero based.
inline std::optional<MouseButton> MouseButtonFromBit(unsigned bit) {
    if (!bit || (bit & (bit - 1)))
        return std::nullopt;
    for (unsigned index = 0; index < static_cast<unsigned>(MouseButton::Count); ++index) {
        if (bit == (1U << index))
            return static_cast<MouseButton>(index);
    }
    return std::nullopt;
}

enum class MouseButtonTarget : unsigned char {
    None,
    Button,
    Touch,
};

MouseButtonTarget SelectMouseButtonTarget(MouseButton button, bool inside_touchscreen,
                                          bool button_mapped, bool prefer_touch);

class MouseButtonRoutingState {
public:
    MouseButtonTarget Press(MouseButton button, MouseButtonTarget target);
    MouseButtonTarget Release(MouseButton button);
    std::array<MouseButtonTarget, static_cast<std::size_t>(MouseButton::Count)> ReleaseAll();
    MouseButtonTarget GetTarget(MouseButton button) const;

private:
    std::array<MouseButtonTarget, static_cast<std::size_t>(MouseButton::Count)> targets{};
};

class Mouse final : public Input::Factory<Input::ButtonDevice> {
public:
    Mouse();
    ~Mouse() override;

    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override;
    void PressButton(MouseButton button);
    void ReleaseButton(MouseButton button);
    void ReleaseAllButtons();

private:
    class ButtonList;
    std::shared_ptr<ButtonList> button_list;
};

} // namespace InputCommon
