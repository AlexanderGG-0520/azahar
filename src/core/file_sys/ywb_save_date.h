// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace FileSys {

// Authenticates and reads the timestamp at offset 0x4C in the decrypted body of a
// Yo-kai Watch Busters Getto-gumi game1.yw. This is read-only and is not called by
// save commit handling.
std::optional<std::tm> ReadYwbGettoSaveDate(const std::string& game_filename,
                                            const std::string& head_filename,
                                            const char** diagnostic_stage = nullptr);

enum class YwbSaveDateNormalizationResult {
    Invalid,
    Unchanged,
    Updated,
};

YwbSaveDateNormalizationResult NormalizeYwbGettoSaveDateAt(
    const std::string& game_filename, const std::string& head_filename,
    const std::tm& emulated_local_time, std::vector<std::uint8_t>& updated_game_file,
    const char** diagnostic_stage = nullptr);

// Reads and normalizes the independently encrypted timestamp at offset 0x3A20 in head.yw.
YwbSaveDateNormalizationResult NormalizeYwbHeadDateAt(const std::string& head_filename,
                                                      const std::tm& emulated_local_time,
                                                      std::vector<std::uint8_t>& updated_head_file);

// Production save-commit hook. Recognizes Busters ExtData and title save locations only.
bool IsYwbObservedSaveFile(const std::string& mount_point, const std::string& filename);
// True only for a Moon Rabbit game1.yw below its canonical ExtData title directory.
bool IsYwbGettoGameSavePath(const std::string& filename);

class YwbSaveCommitCoordinator {
public:
    void OnFileClose(const std::string& filename, bool has_write, bool write_succeeded,
                     bool close_succeeded,
                     const std::function<void(const std::string&)>& normalize);

private:
    std::mutex mutex;
    std::unordered_set<std::string> pending_getto_game_slots;
};

void NormalizeYwbSaveDateOnCommit(const std::string& filename);

} // namespace FileSys
