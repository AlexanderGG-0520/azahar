// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include "common/common_types.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace Service::SOC::Detail {

/// Union to represent the 3DS sockaddr structure.
union CTRSockAddr {
    struct {
        u8 len;
        u8 sa_family;
        std::array<u8, 0x1A> sa_data;
    } raw;

    struct CTRSockAddrIn {
        u8 len;
        u8 sin_family;
        u16 sin_port;
        u32 sin_addr;
    } in;
    static_assert(sizeof(CTRSockAddrIn) == 8);

    struct CTRSockAddrIn6 {
        u8 len;
        u8 sin6_family;
        u16 sin6_port;
        std::array<u8, 0x10> sin6_addr;
        u32 sin6_flowinfo;
        u32 sin6_scope_id;
    } in6;
    static_assert(sizeof(CTRSockAddrIn6) == 28);

    static std::optional<CTRSockAddr> FromPlatform(sockaddr_storage const& addr) {
        CTRSockAddr result{};
        switch (addr.ss_family) {
        case AF_INET: {
            auto const* addr_in = reinterpret_cast<sockaddr_in const*>(&addr);
            result.in.len = sizeof(CTRSockAddrIn);
            result.in.sin_family = 2;
            result.in.sin_port = addr_in->sin_port;
            result.in.sin_addr = addr_in->sin_addr.s_addr;
            return result;
        }
        case AF_INET6: {
            auto const* addr_in6 = reinterpret_cast<sockaddr_in6 const*>(&addr);
            result.in6.len = sizeof(CTRSockAddrIn6);
            result.in6.sin6_family = 23;
            result.in6.sin6_port = addr_in6->sin6_port;
            std::memcpy(result.in6.sin6_addr.data(), &addr_in6->sin6_addr,
                        sizeof(result.in6.sin6_addr));
            result.in6.sin6_flowinfo = addr_in6->sin6_flowinfo;
            result.in6.sin6_scope_id = addr_in6->sin6_scope_id;
            return result;
        }
        default:
            return std::nullopt;
        }
    }

    /// Invalid guest address families return a zero-length address for the caller to reject.
    static std::pair<sockaddr_storage, socklen_t> ToPlatform(CTRSockAddr const& ctr_addr) {
        sockaddr_storage result{};
        socklen_t result_len{};
        if (ctr_addr.raw.len != sizeof(CTRSockAddrIn) &&
            ctr_addr.raw.len != sizeof(CTRSockAddrIn6)) {
            return {result, result_len};
        }

        switch (ctr_addr.raw.sa_family) {
        case 2: {
            auto* result_in = reinterpret_cast<sockaddr_in*>(&result);
            result_in->sin_family = AF_INET;
            result_in->sin_port = ctr_addr.in.sin_port;
            result_in->sin_addr.s_addr = ctr_addr.in.sin_addr;
            result_len = sizeof(sockaddr_in);
            break;
        }
        case 23: {
            auto* result_in6 = reinterpret_cast<sockaddr_in6*>(&result);
            result_in6->sin6_family = AF_INET6;
            result_in6->sin6_port = ctr_addr.in6.sin6_port;
            std::memcpy(&result_in6->sin6_addr, ctr_addr.in6.sin6_addr.data(),
                        sizeof(result_in6->sin6_addr));
            result_in6->sin6_flowinfo = ctr_addr.in6.sin6_flowinfo;
            result_in6->sin6_scope_id = ctr_addr.in6.sin6_scope_id;
            result_len = sizeof(sockaddr_in6);
            break;
        }
        default:
            break;
        }
        return {result, result_len};
    }
};

template <typename Address>
struct SocketAddressQueryResult {
    s32 result{};
    Address address{};
};

/// Performs a host address query and only converts the output after a successful syscall.
template <typename Address, typename NativeAddress, typename Length, typename Query,
          typename ReadError, typename TranslateError, typename Convert>
SocketAddressQueryResult<Address> QuerySocketAddress(Length initial_length,
                                                     int unsupported_family_error, Query&& query,
                                                     ReadError&& read_error,
                                                     TranslateError&& translate_error,
                                                     Convert&& convert) {
    NativeAddress native_address{};
    Length address_length = initial_length;
    if (query(native_address, address_length) != 0) {
        // Read errno/WSAGetLastError before doing any other work.
        const int error = read_error();
        return {translate_error(error), {}};
    }

    auto converted = convert(native_address);
    if (!converted) {
        return {translate_error(unsupported_family_error), {}};
    }
    return {0, *converted};
}

} // namespace Service::SOC::Detail
