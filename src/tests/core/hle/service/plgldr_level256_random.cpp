// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/plgldr/level256_random.h"

TEST_CASE("Level256 random PLGLDR request uses the official IPC ABI",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256Random;

    CHECK(RequestHeader() == 0x25610042);
    CHECK(ResponseHeader() == 0x25610042);
    CHECK(ErrorResponseHeader() == IPC::MakeHeader(kCommandId, 1, 0));
    CHECK(IsRequestShapeValid(RequestHeader(), 1, IPC::MappedBufferDesc(1, IPC::W), 1));
    CHECK(IsRequestShapeValid(RequestHeader(), kMaxRandomBytes,
                              IPC::MappedBufferDesc(kMaxRandomBytes, IPC::W),
                              kMaxRandomBytes));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), 0, IPC::MappedBufferDesc(0, IPC::W), 0));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), kMaxRandomBytes + 1,
                                    IPC::MappedBufferDesc(kMaxRandomBytes + 1, IPC::W),
                                    kMaxRandomBytes + 1));
    CHECK_FALSE(IsRequestShapeValid(IPC::MakeHeader(kCommandId, 0, 4), 16,
                                    IPC::MappedBufferDesc(16, IPC::W), 16));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), 16, IPC::MappedBufferDesc(16, IPC::R), 16));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), 16, IPC::MappedBufferDesc(15, IPC::W), 16));
    CHECK_FALSE(IsRequestShapeValid(RequestHeader(), 16, IPC::MappedBufferDesc(16, IPC::W), 15));
}

TEST_CASE("Level256 random IPC is restricted to the authorized plugin process",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256Random;

    CHECK(IsAuthorized(true, true, true, true, 42, 42));
    CHECK_FALSE(IsAuthorized(false, true, true, true, 42, 42));
    CHECK_FALSE(IsAuthorized(true, false, true, true, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, false, true, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, true, false, 42, 42));
    CHECK_FALSE(IsAuthorized(true, true, true, true, 42, 43));
    CHECK_FALSE(IsAuthorized(true, true, true, true, 0, 0));
}

TEST_CASE("Level256 random IPC reports random-source failure and clears partial bytes",
          "[core][plgldr][level256]") {
    using namespace Service::PLGLDR::Level256Random;

    std::array<u8, 16> bytes{};
    bytes.fill(0xA5);
    CHECK(FillRandomBytes(bytes, [](u8* out, std::size_t size) {
        out[0] = 0x11;
        return size == 0;
    }) == false);
    CHECK(bytes == std::array<u8, 16>{});

    CHECK(FillRandomBytes(bytes, [](u8* out, std::size_t size) {
        std::fill_n(out, size, 0x5A);
        return true;
    }));
    CHECK(bytes == std::array<u8, 16>{0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A,
                                      0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A});
}
