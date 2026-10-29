// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "common/common_types.h"

namespace Service::APT {

// Application and HOME slots define the visible title independently of CPU scheduling.
inline u64 SelectPresenceProgramId(u64 application, u64 home_menu, u64 fallback) {
    return application ? application : (home_menu ? home_menu : fallback);
}

} // namespace Service::APT
