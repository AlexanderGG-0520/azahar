// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

#pragma once

#include <string>

namespace FileSys {

// Repairs the future-dated save metadata written by Japanese Yo-kai Watch 2 Shinuchi.
// Called only after the game's writable save handle has been closed.
// No modification occurs if the file fails authentication or is not future-dated.
bool NormalizeYw2ShinuchiSaveDate(const std::string& filename);

} // namespace FileSys
