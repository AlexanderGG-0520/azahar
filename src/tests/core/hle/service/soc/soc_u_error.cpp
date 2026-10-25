// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cerrno>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/soc/soc_u.h"

namespace {

constexpr int WSA_WOULDBLOCK = 10035;
constexpr int WSA_INPROGRESS = 10036;
constexpr int WSA_CONNREFUSED = 10061;

int TranslateSyntheticWinsockError(int error) {
    switch (error) {
    case WSA_WOULDBLOCK:
        return -6;
    case WSA_INPROGRESS:
        return -26;
    case WSA_CONNREFUSED:
        return -14;
    default:
        return error;
    }
}

int TranslatePosixError(int error) {
    if (error == EINPROGRESS) {
        return -26;
    }
    if (error == EAGAIN) {
        return -6;
    }
    return error;
}

TEST_CASE("SOC nonblocking connect maps Winsock would-block to in-progress") {
    CHECK(TranslateSyntheticWinsockError(WSA_WOULDBLOCK) == -6);
    CHECK(Service::SOC::Detail::TranslateConnectError(WSA_WOULDBLOCK, false, WSA_WOULDBLOCK,
                                                      WSA_INPROGRESS,
                                                      &TranslateSyntheticWinsockError) == -26);
}

TEST_CASE("SOC blocking connect preserves Winsock would-block mapping") {
    CHECK(Service::SOC::Detail::TranslateConnectError(WSA_WOULDBLOCK, true, WSA_WOULDBLOCK,
                                                      WSA_INPROGRESS,
                                                      &TranslateSyntheticWinsockError) == -6);
}

TEST_CASE("SOC connect preserves other Winsock failures") {
    const int result = Service::SOC::Detail::TranslateConnectError(
        WSA_CONNREFUSED, false, WSA_WOULDBLOCK, WSA_INPROGRESS, &TranslateSyntheticWinsockError);
    CHECK(result == -14);
    CHECK(result != 0);
}

TEST_CASE("SOC POSIX EINPROGRESS keeps its existing guest result") {
    CHECK(Service::SOC::Detail::TranslateConnectError(EINPROGRESS, false, EWOULDBLOCK, EINPROGRESS,
                                                      &TranslatePosixError) == -26);
}

} // namespace
