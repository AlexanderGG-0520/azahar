// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <cstring>
#include <optional>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include "core/hle/service/soc/soc_u_address.h"

#ifndef _WIN32
namespace {

TEST_CASE("SOC socket address query preserves IPv4 getpeername results") {
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        SKIP("Host socket creation is unavailable in this sandbox");
    }
    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        ::close(listener);
        SKIP("Host socket creation is unavailable in this sandbox");
    }
    int accepted = -1;

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(listener, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == 0);
    REQUIRE(::listen(listener, 1) == 0);
    socklen_t bind_len = sizeof(bind_addr);
    REQUIRE(::getsockname(listener, reinterpret_cast<sockaddr*>(&bind_addr), &bind_len) == 0);
    REQUIRE(::connect(client, reinterpret_cast<sockaddr*>(&bind_addr), bind_len) == 0);
    accepted = ::accept(listener, nullptr, nullptr);
    REQUIRE(accepted >= 0);

    auto result = Service::SOC::Detail::QuerySocketAddress<
        Service::SOC::Detail::CTRSockAddr, sockaddr_storage, socklen_t>(
        sizeof(sockaddr_storage), EAFNOSUPPORT,
        [client](sockaddr_storage& address, socklen_t& length) {
            return ::getpeername(client, reinterpret_cast<sockaddr*>(&address), &length);
        }, [] { return errno; }, [](int error) { return -error; },
        [](sockaddr_storage const& address) {
            return Service::SOC::Detail::CTRSockAddr::FromPlatform(address);
        });

    CHECK(result.result == 0);
    CHECK(result.address.in.sin_family == 2);
    CHECK(result.address.raw.len == sizeof(Service::SOC::Detail::CTRSockAddr::CTRSockAddrIn));

    ::close(accepted);
    ::close(client);
    ::close(listener);
}

TEST_CASE("SOC unconnected getpeername returns translated error and a zero address") {
    bool conversion_called = false;
    auto result = Service::SOC::Detail::QuerySocketAddress<
        Service::SOC::Detail::CTRSockAddr, sockaddr_storage, socklen_t>(
        sizeof(sockaddr_storage), EAFNOSUPPORT,
        [](sockaddr_storage&, socklen_t&) {
            errno = ENOTCONN;
            return -1;
        }, [] { return errno; }, [](int error) { return -error; },
        [&conversion_called](sockaddr_storage const& address)
            -> std::optional<Service::SOC::Detail::CTRSockAddr> {
            conversion_called = true;
            return Service::SOC::Detail::CTRSockAddr::FromPlatform(address);
        });

    CHECK(result.result == -ENOTCONN);
    CHECK_FALSE(conversion_called);
    Service::SOC::Detail::CTRSockAddr zero_address{};
    CHECK(std::memcmp(&result.address, &zero_address, sizeof(zero_address)) == 0);

}

TEST_CASE("SOC real unconnected getpeername fails with the host not-connected error") {
    int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) {
        SKIP("Host socket creation is unavailable in this sandbox");
    }
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    CHECK(::getpeername(socket, reinterpret_cast<sockaddr*>(&address), &length) == -1);
    const int socket_error = errno;
    CHECK((socket_error == ENOTCONN || socket_error == EINVAL));
    ::close(socket);
}

TEST_CASE("SOC unsupported host address family is rejected without a valid address") {
    sockaddr_storage address{};
    address.ss_family = AF_UNIX;
    CHECK_FALSE(Service::SOC::Detail::CTRSockAddr::FromPlatform(address).has_value());
}

TEST_CASE("SOC IPv4 address conversion preserves endpoint fields") {
    sockaddr_storage address{};
    auto& address4 = *reinterpret_cast<sockaddr_in*>(&address);
    address4.sin_family = AF_INET;
    address4.sin_port = htons(0x1234);
    address4.sin_addr.s_addr = htonl(0x7F000001);

    auto converted = Service::SOC::Detail::CTRSockAddr::FromPlatform(address);
    REQUIRE(converted.has_value());
    CHECK(converted->in.sin_family == 2);
    CHECK(converted->in.sin_port == address4.sin_port);
    CHECK(converted->in.sin_addr == address4.sin_addr.s_addr);

    auto [platform_address, platform_length] =
        Service::SOC::Detail::CTRSockAddr::ToPlatform(*converted);
    CHECK(platform_length == sizeof(sockaddr_in));
    auto const* round_trip = reinterpret_cast<sockaddr_in const*>(&platform_address);
    CHECK(round_trip->sin_family == AF_INET);
    CHECK(round_trip->sin_port == address4.sin_port);
    CHECK(round_trip->sin_addr.s_addr == address4.sin_addr.s_addr);
}

TEST_CASE("SOC IPv6 address conversion preserves endpoint fields") {
    sockaddr_storage address{};
    auto& address6 = *reinterpret_cast<sockaddr_in6*>(&address);
    address6.sin6_family = AF_INET6;
    address6.sin6_port = htons(0x1234);
    address6.sin6_flowinfo = 0x55667788;
    address6.sin6_scope_id = 0x99;
    address6.sin6_addr.s6_addr[0] = 0x20;
    address6.sin6_addr.s6_addr[15] = 0x01;

    auto converted = Service::SOC::Detail::CTRSockAddr::FromPlatform(address);
    REQUIRE(converted.has_value());
    CHECK(converted->in6.sin6_family == 23);
    CHECK(converted->in6.sin6_port == address6.sin6_port);
    CHECK(converted->in6.sin6_flowinfo == address6.sin6_flowinfo);
    CHECK(converted->in6.sin6_scope_id == address6.sin6_scope_id);
    CHECK(converted->in6.sin6_addr[0] == 0x20);
    CHECK(converted->in6.sin6_addr[15] == 0x01);
}

} // namespace
#endif
