// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include "core/hle/ipc.h"

namespace Service::PLGLDR::Level256Random {

inline constexpr u16 kCommandId = 0x2561;
inline constexpr std::size_t kMaxRandomBytes = 64;

constexpr u32 RequestHeader() {
    return IPC::MakeHeader(kCommandId, 1, 2);
}

constexpr u32 ResponseHeader() {
    return IPC::MakeHeader(kCommandId, 1, 2);
}

constexpr u32 ErrorResponseHeader() {
    return IPC::MakeHeader(kCommandId, 1, 0);
}

constexpr bool IsRequestShapeValid(u32 raw_header, u32 size, u32 buffer_descriptor,
                                   std::size_t buffer_size) {
    if (raw_header != RequestHeader() || size == 0 || size > kMaxRandomBytes ||
        IPC::GetDescriptorType(buffer_descriptor) != IPC::MappedBuffer) {
        return false;
    }

    const IPC::MappedBufferDescInfo buffer_info{buffer_descriptor};
    return buffer_info.perms == IPC::W && buffer_info.size == size && buffer_size == size;
}

constexpr bool IsAuthorized(bool compatibility_mode, bool supported_title, bool plugin_loaded,
                            bool capability_authorized, u32 calling_process_id,
                            u32 plugin_process_id) {
    return compatibility_mode && supported_title && plugin_loaded && capability_authorized &&
           calling_process_id != 0 && calling_process_id == plugin_process_id;
}

template <typename Fill>
bool FillRandomBytes(std::span<u8> output, Fill&& fill) {
    std::fill(output.begin(), output.end(), 0);
    if (!std::forward<Fill>(fill)(output.data(), output.size())) {
        std::fill(output.begin(), output.end(), 0);
        return false;
    }
    return true;
}

} // namespace Service::PLGLDR::Level256Random
