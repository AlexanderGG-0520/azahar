// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <algorithm>
#include <span>
#include "common/common_types.h"
#include "core/hle/ipc.h"

namespace Kernel {

class Process;

} // namespace Kernel

namespace Memory {
class MemorySystem;
}

namespace Kernel {

constexpr std::size_t LEVEL256_BEACON_REPLY_HEADER_SIZE = 12;
constexpr std::size_t LEVEL256_START_SCAN_DESCRIPTOR_WORD = 19;
constexpr std::size_t LEVEL256_START_SCAN_POINTER_WORD = 20;

enum class Level256StartScanBufferStatus {
    Valid,
    InvalidRequestShape,
    InvalidDescriptor,
    BufferTooSmall,
    NullPointer,
    AddressOverflow,
    UnreadableAddress,
};

struct Level256StartScanBuffer {
    Level256StartScanBufferStatus status{Level256StartScanBufferStatus::InvalidRequestShape};
    u32 request_max_size{};
    u32 descriptor{};
    u32 mapped_size{};
    VAddr output_address{};
};

struct Level256BeaconReplySnapshot {
    Level256StartScanBufferStatus status{Level256StartScanBufferStatus::InvalidRequestShape};
    u32 max_size{};
    u32 size{};
    u32 total_entries{};
};

Level256BeaconReplySnapshot ReadLevel256BeaconReplySnapshot(
    Memory::MemorySystem& memory, const Process& process,
    const Level256StartScanBuffer& output_buffer);

/// Decodes the StartScan output buffer from either the guest's original IPC layout or the
/// Level256-forwarded layout (where the four translation words have been made normal parameters).
constexpr Level256StartScanBuffer DecodeLevel256StartScanBuffer(std::span<const u32> words) {
    Level256StartScanBuffer result{};
    if (words.size() <= LEVEL256_START_SCAN_POINTER_WORD) {
        return result;
    }

    const IPC::Header header{words[0]};
    const bool original_shape = header.command_id == 15 && header.normal_params_size == 16 &&
                                header.translate_params_size == 4;
    const bool forwarded_shape = header.command_id == 15 && header.normal_params_size == 20 &&
                                 header.translate_params_size == 0;
    if (!original_shape && !forwarded_shape) {
        return result;
    }

    result.request_max_size = words[1];
    result.descriptor = words[LEVEL256_START_SCAN_DESCRIPTOR_WORD];
    result.output_address = words[LEVEL256_START_SCAN_POINTER_WORD];

    if (IPC::GetDescriptorType(result.descriptor) != IPC::MappedBuffer) {
        result.status = Level256StartScanBufferStatus::InvalidDescriptor;
        return result;
    }

    const IPC::MappedBufferDescInfo info{result.descriptor};
    if (info.perms != IPC::W && info.perms != IPC::RW) {
        result.status = Level256StartScanBufferStatus::InvalidDescriptor;
        return result;
    }
    result.mapped_size = info.size;
    result.status =
        std::min(result.mapped_size, result.request_max_size) < LEVEL256_BEACON_REPLY_HEADER_SIZE
            ? Level256StartScanBufferStatus::BufferTooSmall
            : Level256StartScanBufferStatus::Valid;
    return result;
}

/// Checks the exact 12-byte BeaconDataReply header range without reading or changing guest memory.
/// The callback must validate a single guest page address for read access.
template <typename IsReadablePage>
constexpr Level256StartScanBufferStatus ValidateLevel256BeaconReplyRange(
    VAddr address, u32 mapped_size, u32 page_size, IsReadablePage&& is_readable_page) {
    if (mapped_size < LEVEL256_BEACON_REPLY_HEADER_SIZE) {
        return Level256StartScanBufferStatus::BufferTooSmall;
    }
    if (address == 0) {
        return Level256StartScanBufferStatus::NullPointer;
    }

    const u64 end = static_cast<u64>(address) + LEVEL256_BEACON_REPLY_HEADER_SIZE - 1;
    if (end > UINT32_MAX) {
        return Level256StartScanBufferStatus::AddressOverflow;
    }

    const u32 first_page = address - (address % page_size);
    const u32 last_page = static_cast<u32>(end) - (static_cast<u32>(end) % page_size);
    for (u64 page = first_page; page <= last_page; page += page_size) {
        if (!is_readable_page(static_cast<VAddr>(page))) {
            return Level256StartScanBufferStatus::UnreadableAddress;
        }
    }
    return Level256StartScanBufferStatus::Valid;
}

} // namespace Kernel
