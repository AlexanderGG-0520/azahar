// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include "common/settings.h"

namespace Service::Level256RegistrationDiagnostics {

inline constexpr u32 kMaxEvents = 4096;
inline std::atomic<u32> event_count{};
inline std::mutex open_files_mutex;
inline std::unordered_map<std::string, u32> open_files;

inline bool IsEnabled() {
    return Settings::values.level256_registration_diagnostics_enabled.GetValue();
}

// Event IDs make records from worker threads comparable across platforms. Return zero once the
// cap is reached; callers should then skip the diagnostic record entirely.
inline u32 NextEventId() {
    if (!IsEnabled()) {
        return 0;
    }
    const u32 event_id = event_count.fetch_add(1, std::memory_order_relaxed) + 1;
    return event_id <= kMaxEvents ? event_id : 0;
}

inline bool IsAccountPath(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (const unsigned char ch : path) {
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (normalized.find("level256") == std::string::npos) {
        return false;
    }
    return normalized.find("account.") != std::string::npos || normalized.ends_with("level256") ||
           normalized.ends_with("level256/");
}

// Keep logs independent of host paths and user names. File identifiers are guest-visible relative
// names only, and this function rejects all paths outside the registration directory.
inline std::string_view SafePathLabel(std::string_view path) {
    if (!IsAccountPath(path)) {
        return "filtered";
    }
    const auto slash = path.find_last_of("/\\");
    const std::string_view basename = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (basename.empty()) {
        return "Level256/";
    }
    return basename;
}

inline std::string NormalizeGuestPath(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (const unsigned char ch : path) {
        normalized.push_back(ch == '\\' ? '/' : static_cast<char>(std::tolower(ch)));
    }
    return normalized;
}

inline void TrackOpenFile(std::string_view path) {
    if (!IsEnabled() || !IsAccountPath(path)) {
        return;
    }
    std::scoped_lock lock{open_files_mutex};
    ++open_files[NormalizeGuestPath(path)];
}

inline void UntrackOpenFile(std::string_view path) {
    std::scoped_lock lock{open_files_mutex};
    const auto key = NormalizeGuestPath(path);
    const auto it = open_files.find(key);
    if (it == open_files.end()) {
        return;
    }
    if (it->second <= 1) {
        open_files.erase(it);
    } else {
        --it->second;
    }
}

inline u32 OpenFileCount(std::string_view path) {
    std::scoped_lock lock{open_files_mutex};
    const auto it = open_files.find(NormalizeGuestPath(path));
    return it == open_files.end() ? 0 : it->second;
}

} // namespace Service::Level256RegistrationDiagnostics
