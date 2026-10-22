// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <limits>
#include "core/file_sys/plugin_3gx.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("3GX file section ranges are bounded", "[core][file_sys][plugin_3gx]") {
    using FileSys::IsValid3GXFileRange;

    REQUIRE(IsValid3GXFileRange(100, 0, 100));
    REQUIRE(IsValid3GXFileRange(100, 100, 0));
    REQUIRE(IsValid3GXFileRange(100, 20, 80));
    REQUIRE_FALSE(IsValid3GXFileRange(100, 101, 0));
    REQUIRE_FALSE(IsValid3GXFileRange(100, 99, 2));
    REQUIRE_FALSE(IsValid3GXFileRange(100, std::numeric_limits<u64>::max(), 4));
}
