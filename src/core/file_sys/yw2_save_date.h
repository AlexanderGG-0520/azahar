// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

#pragma once

#include <ctime>
#include <string>

namespace FileSys {

// Repairs future-dated save metadata in Japanese Yo-kai Watch 2 Ganso/Honke/Shinuchi.
// Called only after the game's writable save handle has been closed.
// No modification occurs if the file fails authentication or is not future-dated.
bool NormalizeYw2SaveDate(const std::string& filename);

// Deterministic clock-injection seam for offline regression tests. Production callers use
// NormalizeYw2SaveDate() and the emulated 3DS clock, not this overload.
bool NormalizeYw2SaveDateAt(const std::string& filename, const std::tm& emulated_local_time);
bool IsYw2ObservedSaveFile(const std::string& mount_point, const std::string& filename);
void NormalizeYw2SaveDateOnCommit(const std::string& filename);

} // namespace FileSys
