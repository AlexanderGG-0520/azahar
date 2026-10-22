// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "common/common_types.h"
#include "core/hle/kernel/thread.h"

namespace GDBStub {

struct WaitObjectDebugInfo {
    u32 object_id{};
    std::string type_name;
};

struct ThreadDebugInfo {
    u32 thread_id{};
    u32 pc{};
    Kernel::ThreadStatus status{};
    VAddr wait_address{};
    std::size_t wait_object_count{};
    std::vector<WaitObjectDebugInfo> wait_objects;
};

inline std::string_view ThreadStatusName(Kernel::ThreadStatus status) {
    using Kernel::ThreadStatus;
    switch (status) {
    case ThreadStatus::Running:
        return "Running";
    case ThreadStatus::Ready:
        return "Ready";
    case ThreadStatus::WaitArb:
        return "WaitArb";
    case ThreadStatus::WaitSleep:
        return "WaitSleep";
    case ThreadStatus::WaitIPC:
        return "WaitIPC";
    case ThreadStatus::WaitSynchAny:
        return "WaitSynchAny";
    case ThreadStatus::WaitSynchAll:
        return "WaitSynchAll";
    case ThreadStatus::WaitHleEvent:
        return "WaitHleEvent";
    case ThreadStatus::Dormant:
        return "Dormant";
    case ThreadStatus::Dead:
        return "Dead";
    }
    return "Unknown";
}

inline std::string EscapeXmlAttribute(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '\"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

inline std::string FormatThreadDebugName(const ThreadDebugInfo& info) {
    std::string name = fmt::format("Thread {} [{} pc={:08X}", info.thread_id,
                                   ThreadStatusName(info.status), info.pc);
    if (info.status == Kernel::ThreadStatus::WaitArb) {
        name += fmt::format(" wait={:08X}", info.wait_address);
    }
    constexpr std::size_t MaxWaitObjects = 4;
    const auto count = std::min(info.wait_objects.size(), MaxWaitObjects);
    for (std::size_t i = 0; i < count; ++i) {
        name += fmt::format(" object{}={}:{}", i,
                            EscapeXmlAttribute(info.wait_objects[i].type_name),
                            info.wait_objects[i].object_id);
    }
    if (info.wait_object_count > count) {
        name += fmt::format(" objects_truncated={}", info.wait_object_count - count);
    }
    name += ']';
    return name;
}

} // namespace GDBStub
