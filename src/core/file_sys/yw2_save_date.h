// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

#pragma once

#include <string>

namespace FileSys {

// Repairs future-dated save metadata in Japanese Yo-kai Watch 2 Ganso/Honke/Shinuchi.
// Called only after the game's writable save handle has been closed.
// No modification occurs if the file fails authentication or is not future-dated.
bool NormalizeYw2SaveDate(const std::string& filename);

} // namespace FileSys
