// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <string>
#include <discord_rpc.h>
#include "citra_qt/discord_impl.h"
#include "citra_qt/uisettings.h"
#include "common/common_types.h"
#include "core/core.h"
#include "core/hle/service/am/am.h"
#include "core/loader/loader.h"

namespace DiscordRPC {

DiscordImpl::DiscordImpl(const Core::System& system_) : system{system_} {
    DiscordEventHandlers handlers{};

    // The number is the client ID for Citra, it's used for images and the
    // application name
    Discord_Initialize("1345366770436800533", &handlers, 1, nullptr);
}

DiscordImpl::~DiscordImpl() {
    Discord_ClearPresence();
    Discord_Shutdown();
}

void DiscordImpl::Pause() {
    Discord_ClearPresence();
    presence_state.Reset();
}

void DiscordImpl::Update(bool is_powered_on) {
    s64 start_time = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    auto truncate = [](const std::string& str, std::size_t maxLen = 128) -> std::string {
        if (str.length() <= maxLen) {
            return str;
        }
        return str.substr(0, maxLen - 3) + "...";
    };

    std::string title;
    std::optional<u64> title_id;
    if (is_powered_on) {
        const auto program_id = system.Kernel().GetCurrentProcessProgramId();
        if (program_id != 0)
            title_id = program_id;
        if (!presence_state.NeedsTitleRefresh(is_powered_on, title_id)) {
            return;
        }

        std::string initial_title;
        u64 initial_title_id{};
        if (system.GetAppLoader().ReadProgramId(initial_title_id) !=
            Loader::ResultStatus::Success) {
            initial_title_id = 0;
        }
        if (initial_title_id == program_id) {
            system.GetAppLoader().ReadTitle(initial_title);
        }

        std::string running_title;
        if (title_id && initial_title_id != *title_id) {
            const auto media_type = Service::AM::GetTitleMediaType(*title_id);
            const auto path = Service::AM::GetTitleContentPath(media_type, *title_id);
            auto loader = Loader::GetLoader(path);
            u64 loaded_id{};
            if (loader && loader->ReadProgramId(loaded_id) == Loader::ResultStatus::Success &&
                loaded_id == *title_id) {
                loader->ReadTitle(running_title);
            }
            if (running_title.empty() && !system.GetCartridge().empty()) {
                loader = Loader::GetLoader(system.GetCartridge());
                if (loader && loader->ReadProgramId(loaded_id) == Loader::ResultStatus::Success &&
                    loaded_id == *title_id) {
                    loader->ReadTitle(running_title);
                }
            }
        }
        title = ResolvePresenceTitle(program_id, initial_title_id, initial_title, running_title);
        title = truncate("Playing: " + title);
    }

    if (!presence_state.NeedsUpdate(is_powered_on, title_id, title)) {
        return;
    }
    presence_state.Set(is_powered_on, title_id, title);

    DiscordRichPresence presence{};
    presence.largeImageKey = "logo";
    presence.largeImageText = "An open source emulator for the Nintendo 3DS";
    if (is_powered_on) {
        presence.state = title.c_str();
    }
    presence.startTimestamp = start_time;
    Discord_UpdatePresence(&presence);
}
} // namespace DiscordRPC
