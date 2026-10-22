// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include "core/hle/service/plgldr/level256_lfcs.h"

TEST_CASE("Level256 LFCS PLGLDR request and response use the official IPC shape",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256LFCS;

    CHECK(RequestHeader() == IPC::MakeHeader(0x2560, 0, 4));
    CHECK(ResponseHeader() == IPC::MakeHeader(0x2560, 2, 2));
    CHECK(IsRequestShapeValid(RequestHeader(), IPC::CallingPidDesc(),
                              IPC::MappedBufferDesc(kLfcsSize, IPC::W), kLfcsSize));
    CHECK_FALSE(IsRequestShapeValid(IPC::MakeHeader(0x2560, 1, 3), IPC::CallingPidDesc(),
                                    IPC::MappedBufferDesc(kLfcsSize, IPC::W), kLfcsSize));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), 0, IPC::MappedBufferDesc(kLfcsSize, IPC::W),
                                    kLfcsSize));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), IPC::CallingPidDesc(),
                                    IPC::MappedBufferDesc(kLfcsSize, IPC::R), kLfcsSize));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), IPC::CallingPidDesc(),
                                    IPC::MappedBufferDesc(kLfcsSize - 1, IPC::W), kLfcsSize - 1));
}

TEST_CASE("Level256 LFCS requires the verified plugin caller and matching PID",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256LFCS;

    CHECK(IsAuthorized(true, true, true, true, 42, 42, 42));
    CHECK_FALSE(IsAuthorized(false, true, true, true, 42, 42, 42));
    CHECK_FALSE(IsAuthorized(true, false, true, true, 42, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, false, true, 42, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, true, false, 42, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, true, true, 42, 43, 42));
    CHECK_FALSE(IsAuthorized(true, true, true, true, 42, 42, 43));
}

TEST_CASE("Level256 LFCS lookup prefers B, falls back to A, and zeroes no-source output",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256LFCS;
    LfcsPayload b{};
    LfcsPayload a{};
    b[0] = 0xB1;
    a[0] = 0xA2;

    const auto both = SelectLfcs(b, a);
    CHECK(both.source == Source::LFCS_B);
    CHECK(both.payload == b);

    const auto only_a = SelectLfcs(std::nullopt, a);
    CHECK(only_a.source == Source::LFCS_A);
    CHECK(only_a.payload == a);

    const auto none = SelectLfcs(std::nullopt, std::nullopt);
    CHECK(none.source == Source::None);
    CHECK(none.payload == LfcsPayload{});
}

TEST_CASE("Level256 LFCS request limit allows exactly eight calls", "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256LFCS;
    u32 count = 0;
    for (u32 i = 0; i < kMaxRequests; ++i) {
        CHECK(ConsumeRequestSlot(count));
    }
    CHECK(count == kMaxRequests);
    CHECK_FALSE(ConsumeRequestSlot(count));
    CHECK(count == kMaxRequests);
}

TEST_CASE("Level256 LFCS file reads reject short, failed, and unverified data",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256LFCS;
    LfcsPayload expected{};
    expected[0] = 0xA5;

    CHECK_FALSE(ReadVerifiedLfcs(kLfcsSize - 1,
                                 [](u8*, std::size_t) { return true; },
                                 [](const LfcsPayload&) { return true; }));
    CHECK_FALSE(ReadVerifiedLfcs(kLfcsSize,
                                 [](u8*, std::size_t) { return false; },
                                 [](const LfcsPayload&) { return true; }));
    CHECK_FALSE(ReadVerifiedLfcs(kLfcsSize,
                                 [&expected](u8* out, std::size_t size) {
                                     std::memcpy(out, expected.data(), size);
                                     return true;
                                 },
                                 [](const LfcsPayload&) { return false; }));
    const auto result = ReadVerifiedLfcs(
        kLfcsSize,
        [&expected](u8* out, std::size_t size) {
            std::memcpy(out, expected.data(), size);
            return true;
        },
        [&expected](const LfcsPayload& payload) { return payload == expected; });
    REQUIRE(result.has_value());
    CHECK(*result == expected);
}
