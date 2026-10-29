// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fmt/format.h>

#include "common/common_types.h"

namespace DiscordRPC {

inline std::string ResolvePresenceTitle(u64 running_program_id, u64 initial_program_id,
                                        std::string_view initial_title,
                                        std::string_view running_title) {
    if (running_program_id == 0) {
        return initial_title.empty() ? "Nintendo 3DS" : std::string{initial_title};
    }
    if (running_program_id == initial_program_id && !initial_title.empty()) {
        return std::string{initial_title};
    }
    if (!running_title.empty()) {
        return std::string{running_title};
    }
    return fmt::format("Title {:016X}", running_program_id);
}

class PresenceState {
public:
    bool NeedsTitleRefresh(bool powered_on, std::optional<u64> program_id) const {
        if (!initialized || powered_on != last_powered_on || program_id != last_program_id) {
            return true;
        }
        return powered_on && program_id &&
               last_title == fmt::format("Playing: Title {:016X}", *program_id);
    }

    bool NeedsUpdate(bool powered_on, std::optional<u64> program_id, std::string_view title) const {
        return !initialized || powered_on != last_powered_on || program_id != last_program_id ||
               title != last_title;
    }

    void Set(bool powered_on, std::optional<u64> program_id, std::string title) {
        initialized = true;
        last_powered_on = powered_on;
        last_program_id = program_id;
        last_title = std::move(title);
    }

    void Reset() {
        initialized = false;
        last_program_id.reset();
        last_title.clear();
    }

private:
    bool initialized = false;
    bool last_powered_on = false;
    std::optional<u64> last_program_id;
    std::string last_title;
};

} // namespace DiscordRPC
