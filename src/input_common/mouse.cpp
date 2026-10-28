// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <atomic>
#include <list>
#include <mutex>
#include <utility>
#include "common/param_package.h"
#include "input_common/mouse.h"

namespace InputCommon {

namespace {

constexpr std::size_t ButtonIndex(MouseButton button) {
    return static_cast<std::size_t>(button);
}

} // namespace

MouseButtonTarget SelectMouseButtonTarget(MouseButton button, bool inside_touchscreen,
                                          bool button_mapped, bool prefer_touch) {
    if (button == MouseButton::Left && inside_touchscreen && (!button_mapped || prefer_touch)) {
        return MouseButtonTarget::Touch;
    }
    return button_mapped ? MouseButtonTarget::Button : MouseButtonTarget::None;
}

MouseButtonTarget MouseButtonRoutingState::Press(MouseButton button, MouseButtonTarget target) {
    auto& current = targets[ButtonIndex(button)];
    if (current == MouseButtonTarget::None) {
        current = target;
    }
    return current;
}

MouseButtonTarget MouseButtonRoutingState::Release(MouseButton button) {
    auto& current = targets[ButtonIndex(button)];
    const auto target = current;
    current = MouseButtonTarget::None;
    return target;
}

std::array<MouseButtonTarget, static_cast<std::size_t>(MouseButton::Count)>
MouseButtonRoutingState::ReleaseAll() {
    const auto released = targets;
    targets.fill(MouseButtonTarget::None);
    return released;
}

MouseButtonTarget MouseButtonRoutingState::GetTarget(MouseButton button) const {
    return targets[ButtonIndex(button)];
}

class Mouse::ButtonList {
public:
    struct ButtonPair {
        explicit ButtonPair(MouseButton button_) : button(button_) {}
        MouseButton button;
        std::atomic<bool> status{false};
    };

    ButtonPair& AddButton(MouseButton button) {
        std::lock_guard guard{mutex};
        auto it = std::find_if(list.begin(), list.end(),
                               [button](const ButtonPair& pair) { return pair.button == button; });
        if (it == list.end()) {
            return list.emplace_back(button);
        }
        return *it;
    }

    void ChangeButtonStatus(MouseButton button, bool pressed) {
        std::lock_guard guard{mutex};
        for (ButtonPair& pair : list) {
            if (pair.button == button) {
                pair.status.store(pressed);
            }
        }
    }

    void ChangeAllButtonStatus(bool pressed) {
        std::lock_guard guard{mutex};
        for (ButtonPair& pair : list) {
            pair.status.store(pressed);
        }
    }

private:
    std::mutex mutex;
    std::list<ButtonPair> list;
};

namespace {

class MouseButtonDevice final : public Input::ButtonDevice {
public:
    MouseButtonDevice(std::atomic<bool>& status_, std::shared_ptr<void> owner_)
        : status(status_), owner(std::move(owner_)) {}

    bool GetStatus() const override {
        return status.load();
    }

private:
    std::atomic<bool>& status;
    std::shared_ptr<void> owner;
};

} // namespace

Mouse::Mouse() : button_list{std::make_shared<ButtonList>()} {}
Mouse::~Mouse() = default;

std::unique_ptr<Input::ButtonDevice> Mouse::Create(const Common::ParamPackage& params) {
    const int button_index = params.Get("button", -1);
    if (button_index < 0 || button_index >= static_cast<int>(MouseButton::Count)) {
        return {};
    }
    auto& pair = button_list->AddButton(static_cast<MouseButton>(button_index));
    return std::make_unique<MouseButtonDevice>(pair.status, button_list);
}

void Mouse::PressButton(MouseButton button) {
    button_list->ChangeButtonStatus(button, true);
}

void Mouse::ReleaseButton(MouseButton button) {
    button_list->ChangeButtonStatus(button, false);
}

void Mouse::ReleaseAllButtons() {
    button_list->ChangeAllButtonStatus(false);
}

} // namespace InputCommon
