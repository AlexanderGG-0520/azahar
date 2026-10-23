// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>

#include "core/hle/kernel/luma3ds/level256_memory_operations.h"

TEST_CASE("Level256 ControlMemoryUnsafe authorization follows the loaded plugin range",
          "[core][kernel][luma3ds][level256][memory-operations]") {
    using namespace Kernel;
    using namespace Kernel::Luma3DS;

    const auto authorized = [](bool enabled = true, bool capability = true,
                               u64 title = 0x000400000016C700ULL, u32 pid = 16, u32 owner_pid = 16,
                               bool loaded = true, u32 begin = 0x07000100, u32 end = 0x0710A000,
                               u32 pc = 0x0709D290, u32 svc = 0xA3) {
        return ShouldHandleLevel256ControlMemoryUnsafe(enabled, capability, title, pid, owner_pid,
                                                       loaded, begin, end, pc, svc);
    };

    // Runtime-observed v2.0.1 call and a future signed artifact with shifted code layout/call PC.
    REQUIRE(authorized());
    REQUIRE(authorized(true, true, 0x000400000016C700ULL, 16, 16, true, 0x07000100, 0x0710C000,
                       0x0709E1A0));
    REQUIRE_FALSE(authorized(false));
    REQUIRE_FALSE(authorized(true, false));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C701ULL));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C700ULL, 16, 17));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C700ULL, 16, 16, false));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C700ULL, 16, 16, true, 0x07000100,
                             0x0710A000, 0x0710A000));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C700ULL, 16, 16, true, 0x0710A000,
                             0x07000100, 0x0709D290));
    REQUIRE_FALSE(authorized(true, true, 0x000400000016C700ULL, 16, 16, true, 0x07000100,
                             0x0710A000, 0x0709D290, 0xA2));
}

TEST_CASE("Level256 ControlMemoryUnsafe validates the observed SYSTEM COMMIT ABI",
          "[core][kernel][luma3ds][level256][memory-operations]") {
    using namespace Kernel::Luma3DS;

    REQUIRE(ValidateLevel256UnsafeCommitRequest(0x07500000, 0x10000, 0x203, 0x3) ==
            Level256UnsafeCommitRequestStatus::Valid);
    REQUIRE(ValidateLevel256UnsafeCommitRequest(0xFFFFF000, 0x10000, 0x203, 0x3) ==
            Level256UnsafeCommitRequestStatus::InvalidAddress);
    REQUIRE(ValidateLevel256UnsafeCommitRequest(0x07501000, 0x10000, 0x203, 0x3) ==
            Level256UnsafeCommitRequestStatus::InvalidAddress);
    REQUIRE(ValidateLevel256UnsafeCommitRequest(0x07500000, 0x0F000, 0x203, 0x3) ==
            Level256UnsafeCommitRequestStatus::InvalidSize);
    REQUIRE(ValidateLevel256UnsafeCommitRequest(0x07500000, 0x10000, 0x203, 0x7) ==
            Level256UnsafeCommitRequestStatus::InvalidPermissions);
    REQUIRE(ValidateLevel256UnsafeCommitRequest(0x07500000, 0x10000, 0x103, 0x3) ==
            Level256UnsafeCommitRequestStatus::UnsupportedOperation);
}

TEST_CASE("Level256 ControlMemoryUnsafe requires writable output and a free protected range",
          "[core][kernel][luma3ds][level256][memory-operations]") {
    using namespace Kernel::Luma3DS;
    using Status = Level256UnsafeCommitMappingStatus;

    REQUIRE(ValidateLevel256UnsafeCommitMapping(true, true, false, false) == Status::Valid);
    REQUIRE(ValidateLevel256UnsafeCommitMapping(false, true, false, false) ==
            Status::InvalidOutputPointer);
    REQUIRE(ValidateLevel256UnsafeCommitMapping(true, false, false, false) ==
            Status::DestinationUnavailable);
    REQUIRE(ValidateLevel256UnsafeCommitMapping(true, true, true, false) ==
            Status::DestinationUnavailable);
    REQUIRE(ValidateLevel256UnsafeCommitMapping(true, true, false, true) ==
            Status::DestinationUnavailable);
}
