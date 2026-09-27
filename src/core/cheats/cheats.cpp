// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fstream>
#include <functional>
#include <fmt/format.h>
#include "common/file_util.h"
#include "core/cheats/cheats.h"
#include "core/cheats/gateway_cheat.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Cheats {

// Luma3DS uses this interval for applying cheats, so to keep consistent behavior
// we use the same value
constexpr u64 run_interval_ticks = 50'000'000;

namespace {

constexpr u64 YW2_SHINUCHI_TITLE_ID = 0x0004000000155100;
constexpr VAddr YW2_SCRIPT_SCAN_BEGIN = 0x08A00000;
constexpr VAddr YW2_SCRIPT_SCAN_END = 0x08B00000;

void ApplyYW2Shinuchi120FpsSoultimateFix(Core::System& system, u32 process_id) {
    const auto process = system.Kernel().GetProcessById(process_id);
    if (!process || !process->codeset ||
        process->codeset->program_id != YW2_SHINUCHI_TITLE_ID) {
        return;
    }

    auto& memory = system.Memory();

    // Apply only while YW2 is using the 120 FPS B=2 / P=1 configuration.
    const auto timing = memory.Read32OrNullopt(*process, 0x00857BCC);
    if (!timing || ((*timing & 0xFFFF) != 0x0102)) {
        return;
    }

    // Shinuchi Ver.1.2 guard.
    const auto handler_prologue = memory.Read32OrNullopt(*process, 0x001E6374);
    if (!handler_prologue || *handler_prologue != 0xE92D41F0) {
        return;
    }

    static u32 cached_process_id = 0;
    static VAddr cached_base = 0;
    static u32 scan_divider = 0;

    if (cached_process_id != process_id) {
        cached_process_id = process_id;
        cached_base = 0;
        scan_divider = 0;
    }

    const auto matches = [&](VAddr address, u32 expected) {
        const auto value = memory.Read32OrNullopt(*process, address);
        return value && *value == expected;
    };

    const auto sequence_loaded = [&](VAddr base) {
        return matches(base + 0x00, 0x010024B6) &&
               matches(base + 0x04, 0x084803EB) &&
               matches(base + 0x08, 0x001C1F14) &&
               matches(base + 0x0C, 0x000224B6) &&
               matches(base + 0x14, 0x001E6374) &&
               matches(base + 0x18, 0x000224B8) &&
               matches(base + 0x1C, 0x001E03E9) &&
               matches(base + 0x20, 0x001D4060);
    };

    // If the previously found battle script still exists, only verify the patched word.
    if (cached_base != 0) {
        if (sequence_loaded(cached_base)) {
            const auto current = memory.Read32OrNullopt(*process, cached_base + 0x10);

            if (current && *current == 0x008703E9) {
                return;
            }

            if (current && *current == 0x008703EA) {
                memory.Write32(*process, cached_base + 0x10, 0x008703E9);
                return;
            }
        }

        // Battle ended or the script was rebuilt elsewhere.
        cached_base = 0;
    }

    // The battle script is dynamically expanded.
    // Search periodically until the exact 4823 -> 4824 -> 4825 sequence appears.
    if (++scan_divider < 5) {
        return;
    }
    scan_divider = 0;

    for (VAddr base = YW2_SCRIPT_SCAN_BEGIN;
         base + 0x24 <= YW2_SCRIPT_SCAN_END;
         base += sizeof(u32)) {

        if (!sequence_loaded(base)) {
            continue;
        }

        const auto target = memory.Read32OrNullopt(*process, base + 0x10);
        if (!target) {
            continue;
        }

        if (*target == 0x008703E9) {
            cached_base = base;
            return;
        }

        if (*target != 0x008703EA) {
            continue;
        }

        memory.Write32(*process, base + 0x10, 0x008703E9);
        cached_base = base;

        LOG_INFO(Core_Cheats,
                 "YW2 Shinuchi 120 FPS Soultimate workaround: patched script record at "
                 "0x{:08X}",
                 base + 0x10);
        return;
    }
}

} // namespace

CheatEngine::CheatEngine(Core::System& system_) : system{system_} {}

CheatEngine::~CheatEngine() {
    if (system.IsPoweredOn()) {
        system.CoreTiming().UnscheduleEvent(event, 0);
    }
}

void CheatEngine::Connect(u32 process_id) {
    this->process_id = process_id;
    event = system.CoreTiming().RegisterEvent(
        "CheatCore::run_event",
        [this](u64 thread_id, s64 cycle_late) { RunCallback(thread_id, cycle_late); });
    system.CoreTiming().ScheduleEvent(run_interval_ticks, event);
}

std::span<const std::shared_ptr<CheatBase>> CheatEngine::GetCheats() const {
    std::shared_lock lock{cheats_list_mutex};
    return cheats_list;
}

void CheatEngine::AddCheat(std::shared_ptr<CheatBase>&& cheat) {
    std::unique_lock lock{cheats_list_mutex};
    cheats_list.push_back(std::move(cheat));
}

void CheatEngine::RemoveCheat(std::size_t index) {
    std::unique_lock lock{cheats_list_mutex};
    if (index < 0 || index >= cheats_list.size()) {
        LOG_ERROR(Core_Cheats, "Invalid index {}", index);
        return;
    }
    cheats_list.erase(cheats_list.begin() + index);
}

void CheatEngine::UpdateCheat(std::size_t index, std::shared_ptr<CheatBase>&& new_cheat) {
    std::unique_lock lock{cheats_list_mutex};
    if (index < 0 || index >= cheats_list.size()) {
        LOG_ERROR(Core_Cheats, "Invalid index {}", index);
        return;
    }
    cheats_list[index] = std::move(new_cheat);
}

void CheatEngine::SaveCheatFile(u64 title_id) const {
    const std::string cheat_dir = FileUtil::GetUserPath(FileUtil::UserPath::CheatsDir);
    const std::string filepath = fmt::format("{}{:016X}.txt", cheat_dir, title_id);

    LOG_INFO(Core_Cheats, "Attempting to save cheats file: {}", filepath);

    if (!FileUtil::IsDirectory(cheat_dir)) {
        FileUtil::CreateDir(cheat_dir);
    }
    FileUtil::IOFile file(filepath, "w");

    auto cheats = GetCheats();
    for (const auto& cheat : cheats) {
        file.WriteString(cheat->ToString());
    }
}

void CheatEngine::LoadCheatFile(u64 title_id) {
    {
        std::unique_lock lock{cheats_list_mutex};
        if (loaded_title_id.has_value() && loaded_title_id == title_id) {
            return;
        }
    }

    const std::string cheat_dir = FileUtil::GetUserPath(FileUtil::UserPath::CheatsDir);
    const std::string filepath = fmt::format("{}{:016X}.txt", cheat_dir, title_id);

    LOG_INFO(Core_Cheats, "Attempting to load cheats file: {}", filepath);

    if (!FileUtil::IsDirectory(cheat_dir)) {
        FileUtil::CreateDir(cheat_dir);
    }

    auto gateway_cheats = GatewayCheat::LoadFile(filepath);
    {
        std::unique_lock lock{cheats_list_mutex};
        loaded_title_id = title_id;
        cheats_list = std::move(gateway_cheats);
    }
}

void CheatEngine::RunCallback([[maybe_unused]] std::uintptr_t user_data, s64 cycles_late) {
    {
        std::shared_lock lock{cheats_list_mutex};
        for (const auto& cheat : cheats_list) {
            if (cheat->IsEnabled()) {
                cheat->Execute(system, process_id);
            }
        }
    }
    ApplyYW2Shinuchi120FpsSoultimateFix(system, process_id);
    system.CoreTiming().ScheduleEvent(run_interval_ticks - cycles_late, event);
}

} // namespace Cheats
