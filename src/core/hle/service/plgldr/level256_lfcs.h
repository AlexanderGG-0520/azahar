// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <optional>
#include "core/hle/ipc.h"

namespace Service::PLGLDR::Level256LFCS {

inline constexpr u16 kCommandId = 0x2560;
inline constexpr std::size_t kLfcsSize = 0x110;
inline constexpr u32 kMaxRequests = 8;

enum class Source : u32 {
    None = 0,
    LFCS_B = 1,
    LFCS_A = 2,
};

using LfcsPayload = std::array<u8, kLfcsSize>;

struct Selection {
    Source source{Source::None};
    LfcsPayload payload{};
};

constexpr u32 RequestHeader() {
    // One CallingPid descriptor and one writable mapped buffer (2 words each).
    return IPC::MakeHeader(kCommandId, 0, 4);
}

constexpr u32 ResponseHeader() {
    // Result + source, followed by the mapped output buffer.
    return IPC::MakeHeader(kCommandId, 2, 2);
}

constexpr bool IsRequestShapeValid(u32 raw_header, u32 pid_descriptor, u32 buffer_descriptor,
                                   std::size_t buffer_size) {
    if (raw_header != RequestHeader() || pid_descriptor != IPC::CallingPidDesc() ||
        IPC::GetDescriptorType(buffer_descriptor) != IPC::MappedBuffer) {
        return false;
    }

    const IPC::MappedBufferDescInfo buffer_info{buffer_descriptor};
    return (static_cast<u32>(buffer_info.perms) & IPC::W) != 0 && buffer_info.size == kLfcsSize &&
           buffer_size == kLfcsSize;
}

constexpr bool IsAuthorized(bool compatibility_mode, bool supported_title, bool plugin_loaded,
                            bool capability_authorized, u32 calling_process_id,
                            u32 plugin_process_id, u32 descriptor_process_id) {
    return compatibility_mode && supported_title && plugin_loaded && capability_authorized &&
           calling_process_id != 0 && calling_process_id == plugin_process_id &&
           descriptor_process_id == calling_process_id;
}

constexpr bool ConsumeRequestSlot(u32& request_count) {
    if (request_count >= kMaxRequests) {
        return false;
    }
    ++request_count;
    return true;
}

inline Selection SelectLfcs(std::optional<LfcsPayload> lfcs_b, std::optional<LfcsPayload> lfcs_a) {
    if (lfcs_b) {
        return {Source::LFCS_B, *lfcs_b};
    }
    if (lfcs_a) {
        return {Source::LFCS_A, *lfcs_a};
    }
    return {};
}

template <typename Read, typename Verify>
std::optional<LfcsPayload> ReadVerifiedLfcs(std::size_t file_size, Read&& read, Verify&& verify) {
    if (file_size != kLfcsSize) {
        return std::nullopt;
    }
    LfcsPayload payload{};
    if (!read(payload.data(), payload.size()) || !verify(payload)) {
        return std::nullopt;
    }
    return payload;
}

} // namespace Service::PLGLDR::Level256LFCS
