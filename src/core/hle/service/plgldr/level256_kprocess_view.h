// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include "common/common_types.h"

namespace Service::PLGLDR::Level256 {

/// Narrow guest ABI projection consumed by the verified Level256 CustomBackdoor callbacks.
/// This is not an Azahar KProcess layout and must never be used as a generic kernel structure.
struct KProcessViewV1 {
    static constexpr u32 Size = 0x1000;
    static constexpr u32 ResourceLimitN3dsOffset = 0x84;
    static constexpr u32 ResourceLimitO3dsOffset = 0x7C;
    static constexpr u32 CodeSetN3dsOffset = 0xB8;
    static constexpr u32 ProcessIdN3dsOffset = 0xBC;
    static constexpr u32 CodeSetO3dsOffset = 0xB0;
    static constexpr u32 ProcessIdO3dsOffset = 0xB4;
    static constexpr u32 ResourceObjectOffset = 0x100;
    static constexpr u32 PriorityLimitOffset = 0x08;
    static constexpr u32 ThreadLimitOffset = 0x10;
    static constexpr u32 CodeSetObjectOffset = 0x200;
    static constexpr u32 CodeSetTextPagesOffset = 0x44;

    struct CodeSetFields {
        u32 text_address{};
        u32 text_size{};
        u32 rodata_address{};
        u32 rodata_size{};
        u32 data_address{};
        u32 data_size{};
        u64 program_id{};
    };

    static constexpr u32 PageCount(u32 size) {
        return static_cast<u32>((static_cast<u64>(size) + 0xFFF) >> 12);
    }

    static constexpr std::array<u32, Size / sizeof(u32)> Build(u32 guest_base, u32 process_id,
                                                               u32 priority_limit,
                                                               u32 thread_limit,
                                                               CodeSetFields code_set,
                                                               bool is_n3ds) {
        std::array<u32, Size / sizeof(u32)> view{};
        const u32 resource_offset = is_n3ds ? ResourceLimitN3dsOffset : ResourceLimitO3dsOffset;
        const u32 process_id_offset = is_n3ds ? ProcessIdN3dsOffset : ProcessIdO3dsOffset;
        const u32 code_set_offset = is_n3ds ? CodeSetN3dsOffset : CodeSetO3dsOffset;
        view[resource_offset / sizeof(u32)] = guest_base + ResourceObjectOffset;
        view[code_set_offset / sizeof(u32)] = guest_base + CodeSetObjectOffset;
        view[process_id_offset / sizeof(u32)] = process_id;
        view[(ResourceObjectOffset + PriorityLimitOffset) / sizeof(u32)] = priority_limit;
        view[(ResourceObjectOffset + ThreadLimitOffset) / sizeof(u32)] = thread_limit;

        const u32 base = CodeSetObjectOffset / sizeof(u32);
        view[base + 0x08 / sizeof(u32)] = code_set.text_address;
        view[base + 0x0C / sizeof(u32)] = PageCount(code_set.text_size);
        view[base + 0x1C / sizeof(u32)] = code_set.rodata_address;
        view[base + 0x20 / sizeof(u32)] = PageCount(code_set.rodata_size);
        view[base + 0x30 / sizeof(u32)] = code_set.data_address;
        view[base + 0x34 / sizeof(u32)] = PageCount(code_set.data_size);
        view[base + CodeSetTextPagesOffset / sizeof(u32)] = PageCount(code_set.text_size);
        view[base + 0x48 / sizeof(u32)] = PageCount(code_set.rodata_size);
        view[base + 0x4C / sizeof(u32)] = PageCount(code_set.data_size);
        view[base + 0x5C / sizeof(u32)] = static_cast<u32>(code_set.program_id);
        view[base + 0x60 / sizeof(u32)] = static_cast<u32>(code_set.program_id >> 32);
        return view;
    }

    static constexpr bool HasIdentity(const std::array<u32, Size / sizeof(u32)>& view,
                                      u32 guest_base, u32 process_id, bool is_n3ds) {
        const u32 resource_offset = is_n3ds ? ResourceLimitN3dsOffset : ResourceLimitO3dsOffset;
        const u32 process_id_offset = is_n3ds ? ProcessIdN3dsOffset : ProcessIdO3dsOffset;
        return view[resource_offset / sizeof(u32)] == guest_base + ResourceObjectOffset &&
               view[process_id_offset / sizeof(u32)] == process_id;
    }
};

} // namespace Service::PLGLDR::Level256
