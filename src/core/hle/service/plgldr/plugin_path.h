// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <string>
#include "common/common_types.h"

namespace Service::PLGLDR {

enum class PluginPathSource {
    Level256,
    TitleSpecific,
    Default,
    None,
};

PluginPathSource ChoosePluginPathSource(u64 title_id, bool level256_file_exists,
                                        bool select_pressed, bool title_plugin_exists,
                                        bool default_plugin_exists);
bool IsLevel256SupportedTitle(u64 title_id);
std::string MakeGuestPluginPath(std::string plugin_path, const std::string& sdmc_root);

/// Level256 title selection policy. This is intentionally owned by PLGLDR, not by the generic
/// Luma kernel-extension context or its SVC handlers.
constexpr bool IsLevel256PolicyTitle(u64 title_id) {
    switch (title_id) {
    case 0x000400000016C700ULL:
    case 0x000400000016C600ULL:
    case 0x00040000001CF000ULL:
    case 0x00040000001CEC00ULL:
    case 0x0004000000191000ULL:
    case 0x0004000000191100ULL:
    case 0x00040000001AF400ULL:
    case 0x000400000012F900ULL:
    case 0x000400000012F800ULL:
    case 0x0004000000155100ULL:
    case 0x00040000001B2900ULL:
    case 0x000400000019AB00ULL:
    case 0x000400000019AC00ULL:
    case 0x000400000019AA00ULL:
    case 0x000400000019A900ULL:
    case 0x00040000001B2700ULL:
    case 0x00040000001D6800ULL:
    case 0x00040000001D6900ULL:
    case 0x00040000001D6A00ULL:
    case 0x00040000001D6B00ULL:
    case 0x00040000001D6C00ULL:
    case 0x00040000001D6700ULL:
    case 0x00040000001CEB00ULL:
    case 0x00040000001CEF00ULL:
    case 0x000400000019B000ULL:
    case 0x000400000019B100ULL:
    case 0x00040000001B2800ULL:
    case 0x000400000019AE00ULL:
    case 0x000400000019AF00ULL:
    case 0x00040000001B2A00ULL:
    case 0x00040000001CF200ULL:
    case 0x00040000001CED00ULL:
    case 0x00040000000CF400ULL:
    case 0x0004000000167800ULL:
    case 0x0004000000167700ULL:
    case 0x000400000017C200ULL:
    case 0x0004000000167600ULL:
    case 0x0004000000086200ULL:
    case 0x0004000000198D00ULL:
    case 0x0004000000086400ULL:
    case 0x0004000000198F00ULL:
    case 0x0004000000086300ULL:
    case 0x0004000000198E00ULL:
    case 0x0004000000086500ULL:
    case 0x0004000000199000ULL:
    case 0x00040000001C9400ULL:
    case 0x00040000001C9C00ULL:
    case 0x000400000018B000ULL:
    case 0x0004000000197100ULL:
        return true;
    default:
        return false;
    }
}

} // namespace Service::PLGLDR
