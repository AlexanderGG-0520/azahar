// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <boost/serialization/access.hpp>
#include <boost/serialization/version.hpp>
#include "common/common_types.h"

namespace Service::SM {

struct UdsCommandShape {
    u16 command_id;
    u8 normal_params;
    u8 translate_params;
};

struct PlgUdsControlResult {
    bool handled = false;
    bool forward_to_server = true;
    u16 response_command = 0;
    u32 response_value = 0;
};

inline bool operator==(const UdsCommandShape& lhs, const UdsCommandShape& rhs) {
    return lhs.command_id == rhs.command_id && lhs.normal_params == rhs.normal_params &&
           lhs.translate_params == rhs.translate_params;
}

/// IPC shape changes confirmed in Luma3DS-level256 SendSyncRequest.c.
inline std::optional<UdsCommandShape> TranslateLevel256UdsCommand(u16 command_id, u8 normal_params,
                                                                  u8 translate_params) {
    const UdsCommandShape input{command_id, normal_params, translate_params};
    if (input == UdsCommandShape{14, 0, 6} || input == UdsCommandShape{31, 0, 6}) {
        return UdsCommandShape{31, 7, 0};
    }
    if (input == UdsCommandShape{15, 16, 4})
        return UdsCommandShape{15, 20, 0};
    if (input == UdsCommandShape{16, 1, 2})
        return UdsCommandShape{16, 3, 0};
    if (input == UdsCommandShape{17, 1, 0})
        return UdsCommandShape{17, 2, 0};
    if (input == UdsCommandShape{20, 3, 0})
        return UdsCommandShape{20, 4, 0};
    if (input == UdsCommandShape{23, 6, 2})
        return UdsCommandShape{23, 8, 0};
    if (input == UdsCommandShape{27, 12, 2})
        return UdsCommandShape{27, 14, 0};
    if (input == UdsCommandShape{29, 1, 4})
        return UdsCommandShape{29, 5, 0};
    if (input == UdsCommandShape{30, 2, 4})
        return UdsCommandShape{30, 6, 0};
    return std::nullopt;
}

/// Process-scoped service aliases. An empty result means normal global lookup applies.
class ProcessServiceRedirect final {
public:
    struct State {
        bool is_log_mode = false;
        bool is_paused = false;
        bool signal_on_attach_process = false;
        bool signal_on_detach_process = false;
        bool dlp_enabled = false;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & dlp_enabled;
            if (version > 0) {
                ar & is_log_mode;
                ar & is_paused;
                ar & signal_on_attach_process;
                ar & signal_on_detach_process;
            }
        }
        friend class boost::serialization::access;
    };

    void SetLevel256(u32 process_id, bool enabled, bool dlp_enabled) {
        if (!enabled || process_id == 0) {
            states.erase(process_id);
            return;
        }
        auto& state = states[process_id];
        state.dlp_enabled = dlp_enabled;
    }

    PlgUdsControlResult ProcessPlgUdsControl(u32 process_id, u16 command_id, u8 normal_params,
                                             u8 translate_params, u32 arg1, u32 arg2) {
        constexpr u32 not_authorized = 0xD90007EA;
        constexpr u32 out_of_range = 0xD90007FD;
        const auto response = [command_id](u32 value) {
            return PlgUdsControlResult{true, false, command_id, value};
        };

        if (command_id == 100 && normal_params == 2 && translate_params == 0) {
            if (arg1 != process_id) {
                return response(not_authorized);
            }
            states[process_id] = State{.is_log_mode = arg2 == 1};
            return {true, true, command_id, 0};
        }
        if (command_id == 101 && normal_params == 0 && translate_params == 0) {
            const auto it = states.find(process_id);
            if (it == states.end()) {
                return response(not_authorized);
            }
            states.erase(it);
            return {true, true, command_id, 0};
        }
        if ((command_id == 104 || command_id == 105 || command_id == 106) && normal_params == 1 &&
            translate_params == 0) {
            auto it = states.find(process_id);
            if (it == states.end()) {
                return response(not_authorized);
            }
            State& state = it->second;
            switch (command_id) {
            case 104:
                state.signal_on_attach_process = arg1 != 0;
                break;
            case 105:
                state.is_paused = arg1 != 0;
                break;
            case 106:
                if (arg1 > 1) {
                    return response(out_of_range);
                }
                state.dlp_enabled = arg1 != 0;
                break;
            }
            return response(0);
        }
        return {};
    }

    std::optional<std::string> Resolve(u32 process_id, const std::string& service_name) const {
        const auto it = states.find(process_id);
        if (it == states.end()) {
            return std::nullopt;
        }
        if (it->second.is_log_mode || it->second.is_paused) {
            return std::nullopt;
        }
        if (service_name == "nwm::UDS") {
            return "plg:UDS";
        }
        if (!it->second.dlp_enabled) {
            return std::nullopt;
        }
        if (service_name == "dlp:SRVR") {
            return "plg:DLP";
        }
        if (service_name == "dlp:CLNT" || service_name == "dlp:FKCL") {
            return "plg:FKCL";
        }
        return std::nullopt;
    }

    void OnProcessExit(u32 process_id) {
        states.erase(process_id);
    }

    bool IsConnectedProcess(u32 process_id) const {
        const auto it = states.find(process_id);
        return it != states.end() && !it->second.is_log_mode;
    }

    std::optional<State> GetState(u32 process_id) const {
        const auto it = states.find(process_id);
        if (it == states.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool IsPaused(u32 process_id) const {
        const auto it = states.find(process_id);
        return it != states.end() && it->second.is_paused;
    }

    bool SignalOnAttach(u32 process_id) const {
        const auto it = states.find(process_id);
        return it != states.end() && it->second.signal_on_attach_process;
    }

    bool SignalOnDetach(u32 process_id) const {
        const auto it = states.find(process_id);
        return it != states.end() && it->second.signal_on_detach_process;
    }

    void SetSignalOnDetach(u32 process_id, bool enabled) {
        const auto it = states.find(process_id);
        if (it != states.end()) {
            it->second.signal_on_detach_process = enabled;
        }
    }

private:
    std::unordered_map<u32, State> states;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & states;
    }
    friend class boost::serialization::access;
};

} // namespace Service::SM

BOOST_CLASS_VERSION(Service::SM::ProcessServiceRedirect::State, 1)
