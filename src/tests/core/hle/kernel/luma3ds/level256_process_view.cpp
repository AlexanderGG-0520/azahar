// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/level256_process_view.h"

TEST_CASE("Level256 process view ownership retains failed live releases",
          "[core][kernel][luma3ds][process-view]") {
    Kernel::Luma3DS::Level256ProcessViewRecord record;
    const auto* owner = reinterpret_cast<const Kernel::Process*>(0x1000);
    const auto* other = reinterpret_cast<const Kernel::Process*>(0x2000);

    REQUIRE(record.Empty());
    record.Claim(owner, 10, 0x070A0000);
    REQUIRE(record.Matches(owner, 10));
    REQUIRE(record.guest_address == 0x070A0000);
    REQUIRE(record.Overlaps(0x070A0000, 4));
    REQUIRE(record.Overlaps(0x070A0FFC, 4));
    REQUIRE_FALSE(record.Overlaps(0x070B0000, 4));
    REQUIRE_FALSE(record.Overlaps(0xFFFFFFFF, 2));
    REQUIRE_FALSE(record.CanClaim(other, 10));

    record.MarkReleasePending();
    REQUIRE(record.Matches(owner, 10));
    REQUIRE(record.release_pending);
    REQUIRE(record.guest_address == 0x070A0000);
    REQUIRE_FALSE(record.CanClaim(other, 11));

    record.ClearFor(other, 10);
    REQUIRE(record.Matches(owner, 10));
    record.ClearFor(owner, 10);
    REQUIRE(record.Empty());
    REQUIRE(record.guest_address == 0);
}

TEST_CASE("Level256 process view destruction cleanup is owner-specific",
          "[core][kernel][luma3ds][process-view]") {
    Kernel::Luma3DS::Level256ProcessViewRecord record;
    const auto* first_lifetime = reinterpret_cast<const Kernel::Process*>(0x1000);
    const auto* second_lifetime = reinterpret_cast<const Kernel::Process*>(0x2000);
    record.Claim(first_lifetime, 10, 0x070A0000);

    REQUIRE_FALSE(record.Matches(second_lifetime, 10));
    REQUIRE(record.Matches(first_lifetime, 10));

    record.ClearFor(first_lifetime, 10);
    REQUIRE(record.Empty());
    REQUIRE(record.CanClaim(second_lifetime, 10));
    record.Claim(second_lifetime, 10, 0x070A0000);
    REQUIRE(record.Matches(second_lifetime, 10));
    REQUIRE(record.guest_address == 0x070A0000);
}
