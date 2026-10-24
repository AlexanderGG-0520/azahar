// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <dynarmic/interface/A32/a32.h>
#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/arm/dynarmic/arm_dynarmic_cp15.h"

namespace Memory {
struct PageTable;
class MemorySystem;
} // namespace Memory

namespace Core {

class DynarmicUserCallbacks;
class DynarmicExclusiveMonitor;
class ExclusiveMonitor;
class System;

class ARM_Dynarmic final : public ARM_Interface {
public:
    explicit ARM_Dynarmic(Core::System& system_, Memory::MemorySystem& memory_, u32 core_id_,
                          std::shared_ptr<Core::Timing::Timer> timer,
                          Core::ExclusiveMonitor& exclusive_monitor_);
    ~ARM_Dynarmic() override;

    void Run() override;
    void Step() override;
    void StartPluginInstructionTrace(u32 process_id, u32 thread_id, u32 code_begin, u32 code_end,
                                     u32 bootloader_begin, u32 bootloader_end,
                                     u32 instruction_limit, bool focus_open_process = false,
                                     bool wait_for_target = false,
                                     bool trace_bootloader_return = false) override;

    void SetPC(u32 pc) override;
    u32 GetPC() const override;
    u32 GetReg(int index) const override;
    void SetReg(int index, u32 value) override;
    u32 GetVFPReg(int index) const override;
    void SetVFPReg(int index, u32 value) override;
    u32 GetVFPSystemReg(VFPSystemRegister reg) const override;
    void SetVFPSystemReg(VFPSystemRegister reg, u32 value) override;
    u32 GetCPSR() const override;
    void SetCPSR(u32 cpsr) override;
    u32 GetCP15Register(CP15Register reg) const override;
    void SetCP15Register(CP15Register reg, u32 value) override;

    void SaveContext(ThreadContext& ctx) override;
    void LoadContext(const ThreadContext& ctx) override;

    void PrepareReschedule() override;

    void ClearInstructionCache() override;
    void InvalidateCacheRange(u32 start_address, std::size_t length) override;
    void ClearExclusiveState() override;
    void SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) override;

    bool HasSingleInstructionBreakAccuracy() override {
        return false;
    }

protected:
    std::shared_ptr<Memory::PageTable> GetPageTable() const override;

private:
    void ServeBreak(int signal);

    friend class DynarmicUserCallbacks;
    Core::System& system;
    Memory::MemorySystem& memory;
    std::unique_ptr<DynarmicUserCallbacks> cb;
    std::unique_ptr<Dynarmic::A32::Jit> MakeJit();

    u32 fpexc = 0;
    CP15State cp15_state;
    Core::DynarmicExclusiveMonitor& exclusive_monitor;

    Dynarmic::A32::Jit* jit = nullptr;
    std::shared_ptr<Memory::PageTable> current_page_table = nullptr;
    std::map<std::shared_ptr<Memory::PageTable>, std::unique_ptr<Dynarmic::A32::Jit>> jits;
    u32 plugin_trace_process_id = UINT32_MAX;
    u32 plugin_trace_thread_id = UINT32_MAX;
    u32 plugin_trace_code_begin = 0;
    u32 plugin_trace_code_end = 0;
    u32 plugin_trace_bootloader_begin = 0;
    u32 plugin_trace_bootloader_end = 0;
    u32 plugin_trace_instruction_limit = 0;
    u32 plugin_trace_instruction_index = 0;
    u32 plugin_trace_log_count = 0;
    u32 plugin_trace_critical_log_count = 0;
    u32 plugin_trace_context_log_count = 0;
    u32 plugin_trace_wait_count = 0;
    bool plugin_trace_focus_open_process = false;
    bool plugin_trace_wait_for_target = false;
    bool plugin_trace_waiting_reported = false;
    struct PluginTraceInstruction {
        u32 pc{};
        u32 instruction{};
        u32 cpsr{};
        u32 sp{};
        u32 lr{};
    };
    std::array<PluginTraceInstruction, 16> plugin_trace_instruction_ring{};
    std::size_t plugin_trace_instruction_ring_next{};
    std::size_t plugin_trace_instruction_ring_count{};
    bool plugin_trace_bootloader_return = false;
    bool plugin_trace_bootloader_d0_logged = false;
    bool plugin_trace_bootloader_d4_logged = false;
    u32 plugin_trace_entry_pointer_change_log_count{};
};

} // namespace Core
