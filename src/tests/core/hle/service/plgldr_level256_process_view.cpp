// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/plgldr/level256_kprocess_view.h"

TEST_CASE("Level256 KProcess view is an explicit guest ABI projection", "[core][plgldr][level256]") {
    using View = Service::PLGLDR::Level256::KProcessViewV1;
    constexpr u32 base = 0x08000000;
    constexpr u32 pid = 27;
    constexpr View::CodeSetFields code{
        .text_address = 0x00100000,
        .text_size = 0x2345,
        .rodata_address = 0x00200000,
        .rodata_size = 0x1000,
        .data_address = 0x00300000,
        .data_size = 0x2100,
        .program_id = 0x000400000016C700ULL,
    };

    constexpr auto n3ds = View::Build(base, pid, 0x18, 0x24, code, true);
    constexpr auto o3ds = View::Build(base, pid, 0x1A, 0x22, code, false);
    REQUIRE(View::HasIdentity(n3ds, base, pid, true));
    REQUIRE(View::HasIdentity(o3ds, base, pid, false));
    REQUIRE(n3ds[View::ResourceLimitN3dsOffset / sizeof(u32)] == base + View::ResourceObjectOffset);
    REQUIRE(n3ds[(View::ResourceObjectOffset + View::PriorityLimitOffset) / sizeof(u32)] == 0x18);
    REQUIRE(n3ds[(View::ResourceObjectOffset + View::ThreadLimitOffset) / sizeof(u32)] == 0x24);
    REQUIRE(n3ds[(View::CodeSetObjectOffset + 0x0C) / sizeof(u32)] == 3);
    REQUIRE(n3ds[(View::CodeSetObjectOffset + 0x20) / sizeof(u32)] == 1);
    REQUIRE(n3ds[(View::CodeSetObjectOffset + 0x34) / sizeof(u32)] == 3);
    REQUIRE_FALSE(View::HasIdentity(n3ds, base, pid + 1, true));
}
