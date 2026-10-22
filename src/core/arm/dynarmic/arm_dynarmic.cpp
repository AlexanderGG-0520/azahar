// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <csignal>
#include <cstring>
#include <optional>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/optimization_flags.h>
#include "common/assert.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/arm/dynarmic/arm_dynarmic.h"
#include "core/arm/dynarmic/arm_dynarmic_cp15.h"
#include "core/arm/dynarmic/arm_exclusive_monitor.h"
#include "core/arm/dynarmic/arm_tick_counts.h"
#include "core/core.h"
#include "core/core_timing.h"
#ifdef ENABLE_GDBSTUB
#include "core/gdbstub/gdbstub.h"
#endif
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/memory.h"

#ifndef SIGILL
constexpr u32 SIGILL = 4;
#endif

#ifndef SIGTRAP
constexpr u32 SIGTRAP = 5;
#endif

namespace Core {

static const char* PluginTraceThreadStatusName(Kernel::ThreadStatus status) {
    switch (status) {
    case Kernel::ThreadStatus::Running:
        return "Running";
    case Kernel::ThreadStatus::Ready:
        return "Ready";
    case Kernel::ThreadStatus::WaitArb:
        return "WaitArb";
    case Kernel::ThreadStatus::WaitSleep:
        return "WaitSleep";
    case Kernel::ThreadStatus::WaitIPC:
        return "WaitIPC";
    case Kernel::ThreadStatus::WaitSynchAny:
        return "WaitSynchAny";
    case Kernel::ThreadStatus::WaitSynchAll:
        return "WaitSynchAll";
    case Kernel::ThreadStatus::WaitHleEvent:
        return "WaitHleEvent";
    case Kernel::ThreadStatus::Dormant:
        return "Dormant";
    case Kernel::ThreadStatus::Dead:
        return "Dead";
    }
    return "Unknown";
}

class DynarmicUserCallbacks final : public Dynarmic::A32::UserCallbacks {
public:
    explicit DynarmicUserCallbacks(ARM_Dynarmic& parent)
        : parent(parent), svc_context(parent.system), memory(parent.memory) {}
    ~DynarmicUserCallbacks() = default;

    std::optional<std::uint32_t> MemoryReadCode(VAddr vaddr) override {
        return memory.Read32OrNullopt(vaddr);
    }

    std::uint8_t MemoryRead8(VAddr vaddr) override {
        const u8 value = memory.Read8(vaddr);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), false, value);
        return value;
    }
    std::uint16_t MemoryRead16(VAddr vaddr) override {
        const u16 value = memory.Read16(vaddr);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), false, value);
        return value;
    }
    std::uint32_t MemoryRead32(VAddr vaddr) override {
        const u32 value = memory.Read32(vaddr);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), false, value);
        return value;
    }
    std::uint64_t MemoryRead64(VAddr vaddr) override {
        const u64 value = memory.Read64(vaddr);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), false, value);
        return value;
    }

    void MemoryWrite8(VAddr vaddr, std::uint8_t value) override {
        memory.Write8(vaddr, value);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
    }
    void MemoryWrite16(VAddr vaddr, std::uint16_t value) override {
        memory.Write16(vaddr, value);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
    }
    void MemoryWrite32(VAddr vaddr, std::uint32_t value) override {
        memory.Write32(vaddr, value);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
    }
    void MemoryWrite64(VAddr vaddr, std::uint64_t value) override {
        memory.Write64(vaddr, value);
        TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
    }

    bool MemoryWriteExclusive8(u32 vaddr, u8 value, u8 expected) override {
        const bool written = memory.WriteExclusive8(vaddr, value, expected);
        if (written) {
            TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
        }
        return written;
    }
    bool MemoryWriteExclusive16(u32 vaddr, u16 value, u16 expected) override {
        const bool written = memory.WriteExclusive16(vaddr, value, expected);
        if (written) {
            TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
        }
        return written;
    }
    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected) override {
        const bool written = memory.WriteExclusive32(vaddr, value, expected);
        if (written) {
            TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
        }
        return written;
    }
    bool MemoryWriteExclusive64(u32 vaddr, u64 value, u64 expected) override {
        const bool written = memory.WriteExclusive64(vaddr, value, expected);
        if (written) {
            TraceCtrpfStartupAccess(vaddr, sizeof(value), true, value);
        }
        return written;
    }

    void InterpreterFallback(VAddr pc, std::size_t num_instructions) override {
        // Should never happen.
        UNREACHABLE_MSG("InterpeterFallback reached with pc = 0x{:08x}, code = 0x{:08x}, num = {}",
                        pc, MemoryReadCode(pc).value(), num_instructions);
    }

    void CallSVC(std::uint32_t swi) override {
        svc_context.CallSVC(swi);
    }

    void ExceptionRaised(VAddr pc, Dynarmic::A32::Exception exception) override {
        switch (exception) {
        case Dynarmic::A32::Exception::UndefinedInstruction:
        case Dynarmic::A32::Exception::UnpredictableInstruction:
        case Dynarmic::A32::Exception::DecodeError:
        case Dynarmic::A32::Exception::NoExecuteFault:
            break;
        case Dynarmic::A32::Exception::Breakpoint:
#ifdef ENABLE_GDBSTUB
            if (GDBStub::IsConnected()) {
                parent.SetPC(pc);
                parent.ServeBreak(SIGTRAP);
                return;
            }
#endif
            break;
        case Dynarmic::A32::Exception::SendEvent:
        case Dynarmic::A32::Exception::SendEventLocal:
        case Dynarmic::A32::Exception::WaitForInterrupt:
        case Dynarmic::A32::Exception::WaitForEvent:
        case Dynarmic::A32::Exception::Yield:
        case Dynarmic::A32::Exception::PreloadData:
        case Dynarmic::A32::Exception::PreloadDataWithIntentToWrite:
        case Dynarmic::A32::Exception::PreloadInstruction:
            return;
        }

        static constexpr auto ExceptionToString = [](Dynarmic::A32::Exception e) -> std::string {
            switch (e) {
            case Dynarmic::A32::Exception::UndefinedInstruction:
                return "UndefinedInstruction";
            case Dynarmic::A32::Exception::UnpredictableInstruction:
                return "UnpredictableInstruction";
            case Dynarmic::A32::Exception::DecodeError:
                return "DecodeError";
            case Dynarmic::A32::Exception::NoExecuteFault:
                return "NoExecuteFault";
            case Dynarmic::A32::Exception::Breakpoint:
                return "Breakpoint";
            default:
                return fmt::format("Unknown({})", e);
            }
        };

        const auto process = parent.system.Kernel().GetCurrentProcess();
        const auto thread = parent.system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        if (process && thread) {
            if (const auto plugin_loader = Service::PLGLDR::GetService(parent.system)) {
                const auto& context = plugin_loader->GetPluginLoaderContext();
                const bool in_plugin =
                    pc >= context.plugin_code_begin && pc < context.plugin_code_end;
                const bool in_bootloader =
                    pc >= context.plugin_bootloader_begin && pc < context.plugin_bootloader_end;
                if (context.plugin_loaded && context.plugin_process_id == process->process_id &&
                    (in_plugin || in_bootloader)) {
                    LOG_ERROR(Service_PLGLDR,
                              "3GX guest exception pid={} title_id={:016X} thread={} "
                              "thread_status={} pc=0x{:08X} exception={}",
                              process->process_id, process->codeset->program_id,
                              thread->GetThreadId(), static_cast<u32>(thread->status), pc,
                              ExceptionToString(exception));
                }
            }
        }

        parent.SetPC(pc);
#ifdef ENABLE_GDBSTUB
        if (GDBStub::IsConnected()) {
            parent.ServeBreak(SIGILL);
        } else
#endif
        {
            std::string error;
            for (int i = 0; i < 16; i++) {
                error += fmt::format("r{:02d} = {:08X}\n", i, parent.GetReg(i));
            }
            error += fmt::format("ExceptionRaised(exception = {}, pc = {:08X})",
                                 ExceptionToString(exception), pc);
            parent.system.SetStatus(Core::System::ResultStatus::ErrorCoreExceptionRaised,
                                    error.c_str());
        }
    }

    void AddTicks(std::uint64_t ticks) override {
        parent.GetTimer().AddTicks(ticks);
    }
    std::uint64_t GetTicksRemaining() override {
        s64 ticks = parent.GetTimer().GetDowncount();
        return static_cast<u64>(ticks <= 0 ? 0 : ticks);
    }
    std::uint64_t GetTicksForCode(bool is_thumb, VAddr, std::uint32_t instruction) override {
        return Core::TicksForInstruction(is_thumb, instruction);
    }

private:
    void TraceCtrpfStartupAccess(VAddr address, u32 size, bool is_write, u64 value) {
        if (Kernel::GetLevel256CtrpfStartupWatchTarget(
                address, size, is_write, Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) >=
            Kernel::LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT) {
            return;
        }
        const auto process = parent.system.Kernel().GetCurrentProcess();
        const auto thread = parent.system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        if (!process || !thread) {
            return;
        }
        const auto plugin_loader = Service::PLGLDR::GetService(parent.system);
        if (!plugin_loader) {
            return;
        }
        plugin_loader->TraceCtrpfStartupMemoryAccess(
            *process, *thread, address, size, is_write, value, parent.GetReg(15),
            parent.GetReg(14));
    }

    ARM_Dynarmic& parent;
    Kernel::SVCContext svc_context;
    Memory::MemorySystem& memory;
};

ARM_Dynarmic::ARM_Dynarmic(Core::System& system_, Memory::MemorySystem& memory_, u32 core_id_,
                           std::shared_ptr<Core::Timing::Timer> timer_,
                           Core::ExclusiveMonitor& exclusive_monitor_)
    : ARM_Interface(core_id_, timer_), system(system_), memory(memory_),
      cb(std::make_unique<DynarmicUserCallbacks>(*this)),
      exclusive_monitor{dynamic_cast<Core::DynarmicExclusiveMonitor&>(exclusive_monitor_)} {
    SetPageTable(memory.GetCurrentPageTable());
}

ARM_Dynarmic::~ARM_Dynarmic() = default;

MICROPROFILE_DEFINE(ARM_Jit, "ARM JIT", "ARM JIT", MP_RGB(255, 64, 64));

void ARM_Dynarmic::Run() {
    ASSERT(memory.GetCurrentPageTable() == current_page_table);
    MICROPROFILE_SCOPE(ARM_Jit);
    if (break_flag) [[unlikely]] {
        if (const auto plugin_loader = Service::PLGLDR::GetService(system)) {
            auto& context = plugin_loader->GetPluginLoaderContext();
            if (context.ctrpf_hook_trace_started) {
                LOG_INFO(Service_PLGLDR,
                         "Level256 CTRPF hook path trace stopped reason=emulator_break "
                         "pid={} thread={} runs={} pc=0x{:08X} wrapper_seen={} "
                         "asm_wrapper_executed={} callback_target_seen={} "
                         "callback_target=0x{:08X}",
                         context.plugin_process_id, context.ctrpf_hook_trace_thread_id,
                         context.ctrpf_hook_trace_runs, GetReg(15),
                         context.ctrpf_hook_wrapper_observed,
                         context.ctrpf_hook_asm_wrapper_executed,
                         context.ctrpf_hook_callback_target_observed,
                         context.ctrpf_hook_callback_target);
                context.ctrpf_hook_trace_started = false;
            }
        }
        if (plugin_trace_instruction_limit != 0) {
            LOG_INFO(Service_PLGLDR,
                     "Level256 OpenProcess trace stopped reason=emulator_break pid={} "
                     "thread={} instructions={} records={} critical_records={} "
                     "context_records={} pc=0x{:08X}",
                     plugin_trace_process_id, plugin_trace_thread_id,
                     plugin_trace_instruction_index, plugin_trace_log_count,
                     plugin_trace_critical_log_count, plugin_trace_context_log_count, GetReg(15));
            plugin_trace_instruction_limit = 0;
        }
        return;
    }

    // Observe the exact CTRPF OSD search candidate once. This does not install a breakpoint or
    // modify guest code; it records the guest instruction and the PC reached after its JIT block.
    const u32 osd_candidate_pc = GetReg(15);
    // After learning the live wrapper/callback destination once, sample a bounded number of
    // later hook invocations. Each keeps the existing 32-instruction per-invocation ceiling;
    // follow-up invocations aggregate counts without per-instruction log spam.
    if (osd_candidate_pc == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS &&
        Settings::values.level256_compatibility_enabled.GetValue()) {
        const auto process = system.Kernel().GetCurrentProcess();
        const auto thread = system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        if (process && thread && process->codeset &&
            process->codeset->program_id == Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID) {
            if (const auto plugin_loader = Service::PLGLDR::GetService(system)) {
                auto& context = plugin_loader->GetPluginLoaderContext();
                if (context.ctrpf_hook_execution_trace_armed &&
                    context.ctrpf_hook_trace_process_id == process->process_id &&
                    context.plugin_loaded && context.level256_official_plugin_hash_matches &&
                    context.plugin_process_id == process->process_id &&
                    memory.IsValidVirtualAddress(*process, osd_candidate_pc)) {
                    const u32 instruction = memory.Read32(*process, osd_candidate_pc);
                    const u32 cpsr_before = GetCPSR();
                    const std::array<u32, 6> regs_before = {GetReg(0), GetReg(1), GetReg(2),
                                                           GetReg(3), GetReg(13), GetReg(14)};
                    context.ctrpf_hook_execution_trace_armed = false;
                    jit->Step();
                    const u32 pc_after = thread->status == Kernel::ThreadStatus::Running
                                             ? GetReg(15)
                                             : thread->context.GetProgramCounter();
                    u32 branch_target = 0;
                    const bool branch_decoded = Kernel::DecodeLevel256ArmBranchTarget(
                        osd_candidate_pc, instruction, branch_target);
                    const bool wrapper_reached =
                        instruction == Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH && branch_decoded &&
                        pc_after == Kernel::LEVEL256_CTRPF_HOOK_WRAPPER_ADDRESS;
                    context.ctrpf_hook_target_instruction_executed = true;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 CTRPF OSD target instruction executed pid={} thread={} "
                             "pc_before=0x{:08X} instruction=0x{:08X} cpsr=0x{:08X} "
                             "r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} r3=0x{:08X} "
                             "sp=0x{:08X} lr=0x{:08X} pc_after=0x{:08X} "
                             "decoded_target=0x{:08X} wrapper_reached={}",
                             process->process_id, thread->GetThreadId(), osd_candidate_pc,
                             instruction, cpsr_before, regs_before[0], regs_before[1],
                             regs_before[2], regs_before[3], regs_before[4], regs_before[5],
                             pc_after, branch_decoded ? branch_target : 0, wrapper_reached);
                    if (wrapper_reached) {
                        context.ctrpf_hook_trace_started = true;
                        context.ctrpf_hook_trace_thread_id = thread->GetThreadId();
                        context.ctrpf_hook_trace_runs = 0;
                        context.ctrpf_hook_invocation_count = 1;
                        context.ctrpf_hook_asm_wrapper_executed = false;
                        context.ctrpf_hook_callback_target_observed = false;
                        context.ctrpf_hook_callback_target = 0;
                    }
                    return;
                }
                if (context.plugin_loaded && context.level256_official_plugin_hash_matches &&
                    context.plugin_process_id == process->process_id &&
                    context.ctrpf_osd_candidate_execution_seen &&
                    context.ctrpf_hook_callback_target ==
                        Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS &&
                    !context.ctrpf_hook_trace_started &&
                    context.ctrpf_hook_invocation_count <
                        Kernel::LEVEL256_CTRPF_HOOK_INVOCATION_LIMIT &&
                    memory.IsValidVirtualAddress(*process, osd_candidate_pc) &&
                    memory.Read32(*process, osd_candidate_pc) ==
                        Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH) {
                    context.ctrpf_hook_trace_started = true;
                    context.ctrpf_hook_trace_thread_id = thread->GetThreadId();
                    context.ctrpf_hook_trace_runs = 0;
                    ++context.ctrpf_hook_invocation_count;
                    jit->Step();
                    return;
                }
            }
        }
    }
    if (osd_candidate_pc == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS &&
        Settings::values.level256_compatibility_enabled.GetValue()) {
        const auto process = system.Kernel().GetCurrentProcess();
        const auto thread = system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        if (process && thread && process->codeset &&
            process->codeset->program_id == Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID) {
            if (const auto plugin_loader = Service::PLGLDR::GetService(system)) {
                auto& context = plugin_loader->GetPluginLoaderContext();
                const u32 candidate = context.ctrpf_osd_scan.selected_candidate;
                const u32 pc_before = osd_candidate_pc;
                if (context.plugin_loaded && context.level256_official_plugin_hash_matches &&
                    context.plugin_process_id == process->process_id &&
                    candidate == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS &&
                    context.ctrpf_osd_candidate_initial_words[0] == 0xE92D5FF0 &&
                    !context.ctrpf_osd_candidate_execution_seen && pc_before == candidate) {
                    context.ctrpf_osd_candidate_execution_seen = true;
                    const u32 instruction = memory.Read32(*process, candidate);
                    const u32 cpsr_before = GetCPSR();
                    const std::array<u32, 6> before = {GetReg(0), GetReg(1),  GetReg(2),
                                                       GetReg(3), GetReg(13), GetReg(14)};
                    LOG_INFO(Service_PLGLDR,
                             "Level256 CTRPF OSD candidate execute before pid={} "
                             "title_id={:016X} thread={} pc=0x{:08X} instruction=0x{:08X} "
                             "cpsr=0x{:08X} r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} "
                             "r3=0x{:08X} sp=0x{:08X} lr=0x{:08X}",
                             process->process_id, process->codeset->program_id,
                             thread->GetThreadId(), pc_before, instruction, cpsr_before, before[0],
                             before[1], before[2], before[3], before[4], before[5]);
                    // One instruction is needed here because Run() may continue through the
                    // dynamically generated wrapper before returning a PC to the dispatcher.
                    jit->Step();
                    u32 branch_target = 0;
                    const bool branch_decoded = Kernel::DecodeLevel256ArmBranchTarget(
                        pc_before, instruction, branch_target);
                    const bool branch_taken = branch_decoded && GetReg(15) == branch_target;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 CTRPF OSD candidate execute after pid={} "
                             "title_id={:016X} thread={} pc_before=0x{:08X} "
                             "pc_after=0x{:08X} instruction=0x{:08X} "
                             "decoded_branch_target=0x{:08X} branch_taken={} "
                             "r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} r3=0x{:08X} "
                             "sp=0x{:08X} lr=0x{:08X}",
                             process->process_id, process->codeset->program_id,
                             thread->GetThreadId(), pc_before, GetReg(15), instruction,
                             branch_decoded ? branch_target : 0, branch_taken, GetReg(0), GetReg(1),
                             GetReg(2), GetReg(3), GetReg(13), GetReg(14));
                    if (instruction == Kernel::LEVEL256_CTRPF_OSD_HOOK_BRANCH && branch_taken &&
                        branch_target == Kernel::LEVEL256_CTRPF_HOOK_WRAPPER_ADDRESS) {
                        context.ctrpf_hook_trace_started = true;
                        context.ctrpf_hook_trace_thread_id = thread->GetThreadId();
                        context.ctrpf_hook_trace_runs = 0;
                        context.ctrpf_hook_invocation_count = 1;
                        const auto wrapper_vma = process->vm_manager.FindVMA(branch_target);
                        const bool wrapper_mapped =
                            wrapper_vma != process->vm_manager.vma_map.end() &&
                            wrapper_vma->second.type != Kernel::VMAType::Free;
                        LOG_INFO(
                            Service_PLGLDR,
                            "Level256 CTRPF hook branch reached target pid={} thread={} "
                            "source=0x{:08X} target=0x{:08X} mapped={} "
                            "vma_base=0x{:08X} vma_size=0x{:08X} vma_type={} "
                            "permissions={} memory_state={} guest_words_valid={}",
                            process->process_id, thread->GetThreadId(), pc_before, branch_target,
                            wrapper_mapped, wrapper_mapped ? wrapper_vma->second.base : 0,
                            wrapper_mapped ? wrapper_vma->second.size : 0,
                            wrapper_mapped ? static_cast<u32>(wrapper_vma->second.type) : 0,
                            wrapper_mapped ? static_cast<u32>(wrapper_vma->second.permissions) : 0,
                            wrapper_mapped ? static_cast<u32>(wrapper_vma->second.meminfo_state)
                                           : 0,
                            memory.IsValidVirtualAddress(*process, branch_target));
                        if (memory.IsValidVirtualAddress(*process, branch_target)) {
                            std::array<u32, Kernel::LEVEL256_CTRPF_HOOK_WRAPPER_WORD_COUNT> words{};
                            for (u32 i = 0; i < words.size(); ++i) {
                                const VAddr address = branch_target + i * sizeof(u32);
                                if (!memory.IsValidVirtualAddress(*process, address)) {
                                    break;
                                }
                                words[i] = memory.Read32(*process, address);
                            }
                            LOG_INFO(Service_PLGLDR,
                                     "Level256 CTRPF HookWrapper words pid={} thread={} "
                                     "address=0x{:08X} jumpCode=0x{:08X} callback=0x{:08X} "
                                     "ctx=0x{:08X} words4=0x{:08X},0x{:08X},0x{:08X},0x{:08X},"
                                     "0x{:08X}",
                                     process->process_id, thread->GetThreadId(), branch_target,
                                     words[0], words[1], words[2], words[3], words[4], words[5],
                                     words[6], words[7]);
                            context.ctrpf_hook_asm_wrapper = words[1];
                            if (words[1] != 0 && memory.IsValidVirtualAddress(*process, words[1])) {
                                std::array<u32, Kernel::LEVEL256_CTRPF_ASM_WRAPPER_WORD_COUNT>
                                    asm_words{};
                                u32 valid_words = 0;
                                for (; valid_words < asm_words.size(); ++valid_words) {
                                    const u64 address =
                                        static_cast<u64>(words[1]) + valid_words * sizeof(u32);
                                    if (address > UINT32_MAX ||
                                        !memory.IsValidVirtualAddress(
                                            *process, static_cast<VAddr>(address))) {
                                        break;
                                    }
                                    asm_words[valid_words] =
                                        memory.Read32(*process, static_cast<VAddr>(address));
                                }
                                context.ctrpf_hook_wrapper_observed = true;
                                LOG_INFO(Service_PLGLDR,
                                         "Level256 CTRPF AsmWrapper guest code pid={} thread={} "
                                         "address=0x{:08X} valid_words={} "
                                         "w0=0x{:08X},0x{:08X},0x{:08X},0x{:08X},"
                                         "0x{:08X},0x{:08X},0x{:08X},0x{:08X} "
                                         "w8=0x{:08X},0x{:08X},0x{:08X},0x{:08X},"
                                         "0x{:08X},0x{:08X},0x{:08X},0x{:08X} "
                                         "w16=0x{:08X},0x{:08X},0x{:08X},0x{:08X},"
                                         "0x{:08X},0x{:08X},0x{:08X},0x{:08X}",
                                         process->process_id, thread->GetThreadId(), words[1],
                                         valid_words, asm_words[0], asm_words[1], asm_words[2],
                                         asm_words[3], asm_words[4], asm_words[5], asm_words[6],
                                         asm_words[7], asm_words[8], asm_words[9], asm_words[10],
                                         asm_words[11], asm_words[12], asm_words[13], asm_words[14],
                                         asm_words[15], asm_words[16], asm_words[17], asm_words[18],
                                         asm_words[19], asm_words[20], asm_words[21], asm_words[22],
                                         asm_words[23]);
                            }
                        }
                    }
                    return;
                }
            }
        }
    }

    // Observe the real guest path through CTRPF's generated wrapper. Run() boundaries are the
    // Dynarmic dispatcher boundaries; the hook and guest instructions execute normally.
    if (Settings::values.level256_compatibility_enabled.GetValue()) {
        const auto process = system.Kernel().GetCurrentProcess();
        const auto thread = system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        if (process && thread && process->codeset &&
            Kernel::IsLevel256SupportedTitleId(process->codeset->program_id)) {
            if (const auto plugin_loader = Service::PLGLDR::GetService(system)) {
                auto& context = plugin_loader->GetPluginLoaderContext();
                if (context.ctrpf_hook_trace_started && context.plugin_loaded &&
                    context.level256_official_plugin_hash_matches &&
                    context.plugin_process_id == process->process_id &&
                    context.ctrpf_hook_trace_thread_id == thread->GetThreadId() &&
                    context.ctrpf_hook_trace_runs < Kernel::LEVEL256_CTRPF_HOOK_TRACE_RUN_LIMIT) {
                    const u32 pc_before = GetReg(15);
                    const u32 instruction = memory.IsValidVirtualAddress(*process, pc_before)
                                                ? memory.Read32(*process, pc_before)
                                                : 0xFFFFFFFF;
                    const bool at_asm_wrapper =
                        context.ctrpf_hook_asm_wrapper != 0 &&
                        static_cast<u64>(pc_before) >= context.ctrpf_hook_asm_wrapper &&
                        static_cast<u64>(pc_before) <
                            static_cast<u64>(context.ctrpf_hook_asm_wrapper) + 0x100;
                    if (pc_before == context.ctrpf_hook_asm_wrapper &&
                        !context.ctrpf_hook_asm_wrapper_executed) {
                        context.ctrpf_hook_asm_wrapper_observed = true;
                        context.ctrpf_hook_asm_wrapper_executed = true;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF AsmWrapper entered pid={} thread={} pc=0x{:08X} "
                                 "instruction=0x{:08X}",
                                 process->process_id, thread->GetThreadId(), pc_before,
                                 instruction);
                    }
                    const u32 cpsr_before = GetCPSR();
                    const std::array<u32, 7> regs_before = {GetReg(0), GetReg(1),  GetReg(2),
                                                            GetReg(3), GetReg(12), GetReg(13),
                                                            GetReg(14)};
                    if (!context.ctrpf_hook_callback_target_observed &&
                        context.ctrpf_hook_callback_target != 0 &&
                        pc_before == context.ctrpf_hook_callback_target) {
                        context.ctrpf_hook_callback_target_observed = true;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF generated callback target entered pid={} "
                                 "thread={} target_pc=0x{:08X} instruction=0x{:08X} "
                                 "r0=0x{:08X} is_bottom={}",
                                 process->process_id, thread->GetThreadId(), pc_before,
                                 instruction, regs_before[0], regs_before[0] != 0);
                    }
                    if (context.ctrpf_hook_callback_target ==
                            Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS &&
                        pc_before == Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS) {
                        context.ctrpf_hook_current_is_bottom = regs_before[0] != 0;
                        if (regs_before[0] <= 1) {
                            u32& count = regs_before[0] == 0 ? context.ctrpf_top_callback_count
                                                             : context.ctrpf_bottom_callback_count;
                            if (count != UINT32_MAX) {
                                ++count;
                            }
                        }
                    }
                    if (pc_before == Kernel::LEVEL256_CTRPF_OSD_RENDER_ENTRY_ADDRESS &&
                        !context.ctrpf_hook_current_is_bottom &&
                        context.ctrpf_hook_callback_target ==
                            Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS) {
                        if (context.ctrpf_top_render_path_count != UINT32_MAX) {
                            ++context.ctrpf_top_render_path_count;
                        }
                    }
                    jit->Step();
                    const auto reg_after = [&](int index) {
                        return thread->status == Kernel::ThreadStatus::Running
                                   ? GetReg(index)
                                   : thread->context.cpu_registers[index];
                    };
                    const u32 pc_after = reg_after(15);
                    const auto status_after = thread->status;
                    ++context.ctrpf_hook_trace_runs;
                    if (pc_before == Kernel::LEVEL256_CTRPF_HOOK_WRAPPER_ADDRESS &&
                        !context.ctrpf_hook_wrapper_instruction_executed) {
                        context.ctrpf_hook_wrapper_instruction_executed = true;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF HookWrapper instruction executed pid={} "
                                 "thread={} pc_before=0x{:08X} instruction=0x{:08X} "
                                 "pc_after=0x{:08X} step={}",
                                 process->process_id, thread->GetThreadId(), pc_before, instruction,
                                 pc_after, context.ctrpf_hook_trace_runs);
                    }
                    if (pc_before == Kernel::LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS &&
                        !context.ctrpf_hook_callback_instruction_executed) {
                        context.ctrpf_hook_callback_instruction_executed = true;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF callback instruction executed pid={} thread={} "
                                 "pc_before=0x{:08X} instruction=0x{:08X} pc_after=0x{:08X} "
                                 "r0_is_bottom={} step={}",
                                 process->process_id, thread->GetThreadId(), pc_before, instruction,
                                 pc_after, regs_before[0] != 0, context.ctrpf_hook_trace_runs);
                    }
                    if (context.ctrpf_hook_invocation_count <= 1) {
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF hook path pid={} thread={} step={} "
                                 "pc_before=0x{:08X} instruction=0x{:08X} cpsr=0x{:08X} "
                                 "r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} r3=0x{:08X} "
                                 "r12=0x{:08X} sp=0x{:08X} lr=0x{:08X} pc_after=0x{:08X} "
                                 "r0_after=0x{:08X} r1_after=0x{:08X} lr_after=0x{:08X} "
                                 "thread_status_after={}",
                                 process->process_id, thread->GetThreadId(),
                                 context.ctrpf_hook_trace_runs, pc_before, instruction,
                                 cpsr_before, regs_before[0], regs_before[1], regs_before[2],
                                 regs_before[3], regs_before[4], regs_before[5], regs_before[6],
                                 pc_after, reg_after(0), reg_after(1), reg_after(14),
                                 PluginTraceThreadStatusName(status_after));
                    }
                    // The official MITM hook dispatches through `LDR pc, [pc, #-4]`
                    // (0xE51FF004) in addition to wrappers that use BLX LR.
                    const bool is_blx_lr = instruction == 0xE12FFF3E;
                    const bool is_ldr_pc_literal = instruction == 0xE51FF004;
                    if (at_asm_wrapper && (is_blx_lr || is_ldr_pc_literal) &&
                        !context.ctrpf_hook_callback_target_observed) {
                        context.ctrpf_hook_callback_target = pc_after;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 CTRPF AsmWrapper callback dispatch pid={} thread={} "
                                 "asm_pc=0x{:08X} instruction=0x{:08X} dispatch={} "
                                 "target_pc=0x{:08X} target_in_plugin_code={}",
                                 process->process_id, thread->GetThreadId(), pc_before, instruction,
                                 is_blx_lr ? "BLX_LR" : "LDR_PC_LITERAL",
                                 pc_after,
                                 pc_after >= context.plugin_code_begin &&
                                     pc_after < context.plugin_code_end);
                    }
                    if (context.ctrpf_hook_callback_target_observed &&
                        pc_after == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS + 4) {
                        if (context.ctrpf_hook_invocation_count <= 1) {
                            LOG_INFO(Service_PLGLDR,
                                     "Level256 CTRPF hook path returned to original game code "
                                     "pid={} thread={} runs={} pc=0x{:08X} "
                                     "callback_target=0x{:08X}",
                                     process->process_id, thread->GetThreadId(),
                                     context.ctrpf_hook_trace_runs, pc_after,
                                     context.ctrpf_hook_callback_target);
                        }
                        context.ctrpf_hook_trace_started = false;
                    }
                    if (context.ctrpf_hook_trace_runs ==
                        Kernel::LEVEL256_CTRPF_HOOK_TRACE_RUN_LIMIT) {
                        if (context.ctrpf_hook_invocation_count <= 1) {
                            LOG_INFO(Service_PLGLDR,
                                     "Level256 CTRPF hook path trace limit reached pid={} "
                                     "thread={} runs={} pc=0x{:08X} wrapper_seen={} asm_seen={} "
                                     "callback_target_seen={} callback_target=0x{:08X}",
                                     process->process_id, thread->GetThreadId(),
                                     context.ctrpf_hook_trace_runs, pc_after,
                                     context.ctrpf_hook_wrapper_observed,
                                     context.ctrpf_hook_asm_wrapper_observed,
                                     context.ctrpf_hook_callback_target_observed,
                                     context.ctrpf_hook_callback_target);
                        }
                        context.ctrpf_hook_trace_started = false;
                    }
                    return;
                }
            }
        }
    }

    if (plugin_trace_instruction_limit != 0) {
        const auto process = system.Kernel().GetCurrentProcess();
        const auto thread = system.Kernel().GetCurrentThreadManager().GetCurrentThread();
        const bool target_context = process && thread &&
                                    Kernel::IsLevel256OpenProcessTraceContext(
                                        process->process_id, plugin_trace_process_id,
                                        thread->GetThreadId(), plugin_trace_thread_id);
        const auto stop_trace = [&](const char* reason, u32 pc) {
            LOG_INFO(Service_PLGLDR,
                     "Level256 OpenProcess trace stopped reason={} pid={} thread={} "
                     "instructions={} records={} critical_records={} context_records={} "
                     "pc=0x{:08X} r6=0x{:08X} wait_runs={}",
                     reason, plugin_trace_process_id, plugin_trace_thread_id,
                     plugin_trace_instruction_index, plugin_trace_log_count,
                     plugin_trace_critical_log_count, plugin_trace_context_log_count, pc,
                     GetReg(6), plugin_trace_wait_count);
            plugin_trace_instruction_limit = 0;
            plugin_trace_wait_for_target = false;
        };
        const auto wait_for_target_thread = [&](Kernel::Thread* waiting_thread) {
            if (!plugin_trace_waiting_reported &&
                plugin_trace_context_log_count <
                    Kernel::LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT) {
                ++plugin_trace_log_count;
                ++plugin_trace_context_log_count;
                LOG_INFO(Service_PLGLDR,
                         "Level256 OpenProcess trace observed thread wait "
                         "pid={} thread={} status={} instructions={} pc=0x{:08X}",
                         plugin_trace_process_id, plugin_trace_thread_id,
                         PluginTraceThreadStatusName(waiting_thread->status),
                         plugin_trace_instruction_index,
                         waiting_thread->context.GetProgramCounter());
                plugin_trace_waiting_reported = true;
            }
            if (++plugin_trace_wait_count >= 1024) {
                stop_trace("target_thread_not_scheduled",
                           waiting_thread->context.GetProgramCounter());
            }
        };
        if (!target_context) {
            const auto target_thread =
                system.Kernel().GetCurrentThreadManager().GetThreadByID(plugin_trace_thread_id);
            if (!target_thread || target_thread->status == Kernel::ThreadStatus::Dead) {
                stop_trace("thread_exit", GetReg(15));
            } else if (plugin_trace_wait_count < 1024) {
                ++plugin_trace_wait_count;
                jit->Run();
                return;
            } else {
                stop_trace("target_thread_not_scheduled", GetReg(15));
            }
        } else {
            if (plugin_trace_wait_for_target) {
                plugin_trace_wait_for_target = false;
                LOG_INFO(Service_PLGLDR,
                         "Level256 OpenProcess trace entered target thread pid={} thread={} "
                         "entry_pc=0x{:08X} wait_count={}",
                         process->process_id, thread->GetThreadId(), GetReg(15),
                         plugin_trace_wait_count);
            }
            if (thread->status == Kernel::ThreadStatus::Dead) {
                stop_trace("thread_exit", thread->context.GetProgramCounter());
                return;
            }
            if (thread->status != Kernel::ThreadStatus::Running) {
                wait_for_target_thread(thread);
                return;
            }
            plugin_trace_wait_count = 0;
            plugin_trace_waiting_reported = false;
            const u32 pc = GetReg(15);
            if (pc == 0x07023EE8 || pc == 0x07023DA0 || pc == 0x07023DA8 ||
                pc == 0x07023DB8 || pc == 0x07023E10 || pc == 0x07023E2C ||
                pc == 0x07023E78 || pc == 0x07023DE8 || pc == 0x07023DF4 ||
                pc == 0x07023EB0 || pc == 0x0701C4A8 || pc == 0x0701C508 ||
                pc == 0x0701C568 || pc == 0x0701C574 || pc == 0x0701C5D8 ||
                pc == 0x0701C5F4 || pc == 0x0701C88C || pc == 0x07046774 ||
                pc == 0x070467D0) {
                const u32 hook_object = memory.Read32(*process, 0x070F41E4);
                const bool has_shinuchi_osd_candidate =
                    process->codeset->program_id == Kernel::LEVEL256_DIAGNOSTIC_TITLE_ID;
                const u32 osd_candidate_word =
                    has_shinuchi_osd_candidate
                        ? memory.Read32(*process, Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS)
                        : 0xFFFFFFFF;
                LOG_INFO(Service_PLGLDR,
                         "Level256 OSD install path pid={} thread={} pc=0x{:08X} "
                         "r0=0x{:08X} r1=0x{:08X} r2=0x{:08X} lr=0x{:08X} "
                         "text_pages={} hook_object=0x{:08X} hook_state=0x{:08X} "
                         "candidate_word=0x{:08X} trace_index={}",
                         process->process_id, thread->GetThreadId(), pc, GetReg(0), GetReg(1),
                         GetReg(2), GetReg(14), memory.Read32(*process, 0x070F56A8),
                         hook_object,
                         memory.IsValidVirtualAddress(*process, hook_object)
                             ? memory.Read32(*process, hook_object + 4)
                             : 0xFFFFFFFF,
                         osd_candidate_word, plugin_trace_instruction_index);
            }
            const bool in_plugin = pc >= plugin_trace_code_begin && pc < plugin_trace_code_end;
            const bool in_bootloader =
                pc >= plugin_trace_bootloader_begin && pc < plugin_trace_bootloader_end;
            if (!in_plugin && !in_bootloader) {
                stop_trace("left_plugin_and_bootloader", pc);
            } else {
                const bool thumb = (GetCPSR() & (1U << 5)) != 0;
                const u32 instruction = thumb ? memory.Read16(pc) : memory.Read32(pc);
                std::array<u32, 16> before{};
                for (u32 register_index = 0; register_index < before.size(); ++register_index) {
                    before[register_index] = GetReg(static_cast<int>(register_index));
                }
                const u32 cpsr_before = GetCPSR();
                const bool focused_trace = plugin_trace_focus_open_process;
                const bool open_process_svc = pc == 0x070A131C;
                if (focused_trace && Kernel::ShouldLogLevel256OpenProcessTrace(
                                         true, pc, plugin_trace_critical_log_count,
                                         plugin_trace_context_log_count)) {
                    ++plugin_trace_log_count;
                    if (Kernel::IsLevel256OpenProcessCriticalPc(pc)) {
                        ++plugin_trace_critical_log_count;
                    } else {
                        ++plugin_trace_context_log_count;
                    }
                    const bool anchor =
                        std::find(Kernel::LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS.begin(),
                                  Kernel::LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS.end(),
                                  pc) != Kernel::LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS.end();
                    LOG_INFO(Service_PLGLDR,
                             "Level256 OpenProcess ARM trace{} pid={} thread={} index={} "
                             "pc_before=0x{:08X} instruction=0x{:08X} cpsr=0x{:08X} "
                             "r0=0x{:08X} r1=0x{:08X} r5=0x{:08X} sp=0x{:08X} "
                             "lr=0x{:08X} executed={}",
                             anchor ? " target" : " context", process->process_id,
                             thread->GetThreadId(), plugin_trace_instruction_index, pc, instruction,
                             cpsr_before, before[0], before[1], before[5], before[13], before[14],
                             !open_process_svc);
                }

                // Capture the effective address/value from the LDR at 0x07046C70 using
                // the guest register state at instruction entry; do not infer it from
                // the later SVC wrapper's SP.
                if (focused_trace &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT &&
                    pc == 0x07046C70 && !thumb && (instruction & 0x0C100000) == 0x04100000 &&
                    (instruction & (1U << 25)) == 0 && (instruction & (1U << 20)) != 0 &&
                    (instruction & (1U << 24)) != 0) {
                    const auto decoded =
                        Kernel::DecodeLevel256ArmLdrAddress(instruction, pc, before);
                    if (!decoded.valid) {
                        LOG_ERROR(Service_PLGLDR,
                                  "Level256 OpenProcess LDR diagnostic decode failed pid={} "
                                  "thread={} pc=0x{:08X} instruction=0x{:08X} rn={} rd={}",
                                  process->process_id, thread->GetThreadId(), pc, instruction,
                                  decoded.rn, decoded.rd);
                    } else {
                        const s64 effective = decoded.effective_address;
                        const bool address_valid = memory.IsValidVirtualAddress(
                            *process, static_cast<VAddr>(effective));
                        const u32 value = address_valid
                                              ? memory.Read32(*process,
                                                              static_cast<VAddr>(effective))
                                              : 0;
                        const u64 snapshot_base = static_cast<u64>(before[13]) + 0x10;
                        ++plugin_trace_log_count;
                        ++plugin_trace_critical_log_count;
                        LOG_INFO(
                            Service_PLGLDR,
                            "Level256 OpenProcess LDR detail pid={} thread={} "
                            "pc=0x{:08X} base_r{}=0x{:08X} effective_address={} "
                            "value={} snapshot_start={} snapshot_relative_offset={}",
                            process->process_id, thread->GetThreadId(), pc, decoded.rn,
                            decoded.base,
                            effective >= 0 && effective <= UINT32_MAX
                                ? fmt::format("0x{:08X}", static_cast<u32>(effective))
                                : "overflow",
                            address_valid ? fmt::format("0x{:08X}", value) : "invalid",
                            snapshot_base <= UINT32_MAX
                                ? fmt::format("0x{:08X}", static_cast<u32>(snapshot_base))
                                : "overflow",
                            effective >= static_cast<s64>(snapshot_base)
                                ? fmt::format("0x{:X}",
                                              effective - static_cast<s64>(snapshot_base))
                                : "before_snapshot");
                    }
                }
                if (focused_trace && pc == 0x07046A34 &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT) {
                    const u32 address = before[3];
                    const bool address_valid =
                        memory.IsValidVirtualAddress(*process, static_cast<VAddr>(address));
                    const u32 value = address_valid
                                          ? memory.Read8(*process, static_cast<VAddr>(address))
                                          : 0xFFFFFFFF;
                    const bool model_flag_valid =
                        memory.IsValidVirtualAddress(*process, 0x070F5948);
                    const u32 model_flag =
                        model_flag_valid ? memory.Read8(*process, 0x070F5948) : 0xFFFFFFFF;
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 model LDRB before pid={} thread={} pc=0x{:08X} "
                             "instruction=0x{:08X} r3_address=0x{:08X} address_valid={} "
                             "memory_read8={} model_flag_addr=0x070F5948 model_flag_valid={} "
                             "model_flag={} cpsr=0x{:08X}",
                             process->process_id, thread->GetThreadId(), pc, instruction, address,
                             address_valid,
                             value == 0xFFFFFFFF ? "invalid" : fmt::format("0x{:02X}", value),
                             model_flag_valid,
                             model_flag == 0xFFFFFFFF ? "invalid"
                                                      : fmt::format("0x{:02X}", model_flag),
                             cpsr_before);
                }

                jit->Step();
                const auto reg_after = [&](int index) {
                    return thread->status == Kernel::ThreadStatus::Running
                               ? GetReg(index)
                               : thread->context.cpu_registers[index];
                };
                const u32 post_pc = reg_after(15);
                if (focused_trace && open_process_svc) {
                    ++plugin_trace_instruction_index;
                    --plugin_trace_instruction_limit;
                    if (plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT) {
                        ++plugin_trace_log_count;
                        ++plugin_trace_critical_log_count;
                        LOG_INFO(Service_PLGLDR,
                                 "Level256 OpenProcess SVC result pid={} thread={} "
                                 "pc_before=0x{:08X} pc_after=0x{:08X} instruction=0x{:08X} "
                                 "result_r0=0x{:08X} output_r1=0x{:08X} r5=0x{:08X} "
                                 "sp=0x{:08X} lr=0x{:08X}",
                                 process->process_id, thread->GetThreadId(), pc, post_pc,
                                 instruction, reg_after(0), reg_after(1), reg_after(5),
                                 reg_after(13), reg_after(14));
                    }
                    return;
                }
                ++plugin_trace_instruction_index;
                --plugin_trace_instruction_limit;
                if (plugin_trace_instruction_limit == 0) {
                    const u32 limit_pc = thread->status == Kernel::ThreadStatus::Running
                                             ? post_pc
                                             : thread->context.GetProgramCounter();
                    stop_trace("instruction_limit", limit_pc);
                    return;
                }
                if (thread->status != Kernel::ThreadStatus::Running) {
                    if (thread->status == Kernel::ThreadStatus::Dead) {
                        stop_trace("thread_exit", thread->context.GetProgramCounter());
                    } else {
                        wait_for_target_thread(thread);
                    }
                    return;
                }
                const bool should_log_after =
                    !focused_trace ||
                    Kernel::ShouldLogLevel256OpenProcessTrace(
                        true, pc, plugin_trace_critical_log_count, plugin_trace_context_log_count);
                if (should_log_after &&
                    (!focused_trace || Kernel::IsLevel256OpenProcessTracePc(pc))) {
                    if (focused_trace) {
                        ++plugin_trace_log_count;
                        if (Kernel::IsLevel256OpenProcessCriticalPc(pc)) {
                            ++plugin_trace_critical_log_count;
                        } else {
                            ++plugin_trace_context_log_count;
                        }
                    }
                    LOG_INFO(Service_PLGLDR,
                             "Level256 OpenProcess ARM trace after pid={} thread={} "
                             "index={} pc_before=0x{:08X} pc_after=0x{:08X} "
                             "r0=0x{:08X}->0x{:08X} r1=0x{:08X}->0x{:08X} "
                             "r5=0x{:08X}->0x{:08X} cpsr_after=0x{:08X} "
                             "sp=0x{:08X}->0x{:08X} lr=0x{:08X}->0x{:08X}",
                             process->process_id, thread->GetThreadId(),
                             plugin_trace_instruction_index, pc, post_pc, before[0], GetReg(0),
                             before[1], GetReg(1), before[5], GetReg(5), GetCPSR(), before[13],
                             GetReg(13), before[14], GetReg(14));
                }
                if (focused_trace && pc == 0x07046A30 &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT) {
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 model r3 definition pid={} thread={} "
                             "pc_before=0x{:08X} instruction=0x{:08X} pc_after=0x{:08X} "
                             "r0=0x{:08X}->0x{:08X} r1=0x{:08X}->0x{:08X} "
                             "r2=0x{:08X}->0x{:08X} r3=0x{:08X}->0x{:08X}",
                             process->process_id, thread->GetThreadId(), pc, instruction, post_pc,
                             before[0], GetReg(0), before[1], GetReg(1), before[2], GetReg(2),
                             before[3], GetReg(3));
                }

                const auto read_word = [&](u32 offset) -> std::optional<u32> {
                    const u64 address = static_cast<u64>(before[13]) + offset;
                    if (address > UINT32_MAX ||
                        !memory.IsValidVirtualAddress(*process, static_cast<VAddr>(address))) {
                        return std::nullopt;
                    }
                    return memory.Read32(*process, static_cast<VAddr>(address));
                };
                if (focused_trace &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT &&
                    pc == 0x07046C34) {
                    const auto slot_c4_location =
                        Kernel::GetLevel256SnapshotReadLocation(before[13], 0xC4);
                    const auto slot_c8_location =
                        Kernel::GetLevel256SnapshotReadLocation(before[13], 0xC8);
                    const auto slot_c4 = read_word(0xC4);
                    const auto slot_c8 = read_word(0xC8);
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 OpenProcess caller stack pid={} thread={} "
                             "pc=0x{:08X} sp=0x{:08X} snapshot=0x{:08X} "
                             "slot_c4_addr=0x{:08X} slot_c4={} "
                             "slot_c8_addr=0x{:08X} slot_c8={}",
                             process->process_id, thread->GetThreadId(), pc, before[13],
                             static_cast<u32>(slot_c4_location.snapshot_base),
                             slot_c4_location.read_address,
                             slot_c4 ? fmt::format("0x{:08X}", *slot_c4) : "invalid",
                             slot_c8_location.read_address,
                             slot_c8 ? fmt::format("0x{:08X}", *slot_c8) : "invalid");
                }
                if (focused_trace &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT &&
                    pc == 0x07046A34) {
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 model LDRB after pid={} thread={} pc_before=0x{:08X} "
                             "pc_after=0x{:08X} r3_before=0x{:08X} r3_after=0x{:08X} "
                             "cpsr_after=0x{:08X}",
                             process->process_id, thread->GetThreadId(), pc, post_pc, before[3],
                             GetReg(3), GetCPSR());
                }
                if (focused_trace &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT &&
                    pc == 0x07046C70) {
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 PID LDR after pid={} thread={} pc_before=0x{:08X} "
                             "pc_after=0x{:08X} r1_before=0x{:08X} r1_after=0x{:08X} "
                             "sp_before=0x{:08X}",
                             process->process_id, thread->GetThreadId(), pc, post_pc, before[1],
                             GetReg(1), before[13]);
                }
                if (focused_trace && pc == 0x07046A3C &&
                    plugin_trace_critical_log_count <
                        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT) {
                    const bool branch_taken = (cpsr_before & (1U << 30)) != 0;
                    ++plugin_trace_log_count;
                    ++plugin_trace_critical_log_count;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 OpenProcess model branch pid={} thread={} "
                             "pc_before=0x{:08X} instruction=0x{:08X} cpsr=0x{:08X} "
                             "r0=0x{:08X} r1=0x{:08X} r3=0x{:08X} r5=0x{:08X} "
                             "sp=0x{:08X} lr=0x{:08X} "
                             "beq_taken={} pc_after=0x{:08X}",
                             process->process_id, thread->GetThreadId(), pc, instruction,
                             cpsr_before, before[0], before[1], before[3], before[5], before[13],
                             before[14], branch_taken, post_pc);
                }
                const bool post_in_plugin =
                    post_pc >= plugin_trace_code_begin && post_pc < plugin_trace_code_end;
                const bool post_in_bootloader = post_pc >= plugin_trace_bootloader_begin &&
                                                post_pc < plugin_trace_bootloader_end;
                if (!post_in_plugin && !post_in_bootloader) {
                    stop_trace("left_plugin_and_bootloader", post_pc);
                }
                return;
            }
        }
    }

    jit->Run();
}

void ARM_Dynarmic::StartPluginInstructionTrace(u32 process_id, u32 thread_id, u32 code_begin,
                                               u32 code_end, u32 bootloader_begin,
                                               u32 bootloader_end, u32 instruction_limit,
                                               bool focus_open_process, bool wait_for_target) {
    if (instruction_limit == 0 || code_begin >= code_end || bootloader_begin >= bootloader_end) {
        return;
    }
    plugin_trace_process_id = process_id;
    plugin_trace_thread_id = thread_id;
    plugin_trace_code_begin = code_begin;
    plugin_trace_code_end = code_end;
    plugin_trace_bootloader_begin = bootloader_begin;
    plugin_trace_bootloader_end = bootloader_end;
    plugin_trace_instruction_limit = instruction_limit;
    plugin_trace_instruction_index = 0;
    plugin_trace_log_count = 0;
    plugin_trace_critical_log_count = 0;
    plugin_trace_context_log_count = 0;
    plugin_trace_wait_count = 0;
    plugin_trace_focus_open_process = focus_open_process;
    plugin_trace_wait_for_target = wait_for_target;
    plugin_trace_waiting_reported = false;
    LOG_INFO(
        Service_PLGLDR,
        "3GX ARM trace armed pid={} thread={} start_pc=0x{:08X} "
        "code=0x{:08X}-0x{:08X} bootloader=0x{:08X}-0x{:08X} "
        "guest_instruction_limit={} focused_critical_limit={} focused_context_limit={} "
        "focus_open_process={} wait_for_target={}",
        process_id, thread_id, GetReg(15), code_begin, code_end, bootloader_begin, bootloader_end,
        instruction_limit, Kernel::LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT,
        Kernel::LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT, focus_open_process, wait_for_target);
    if (jit && jit->IsExecuting()) {
        jit->HaltExecution();
    }
}

void ARM_Dynarmic::Step() {
    if (break_flag) [[unlikely]] {
        return;
    }

    jit->Step();
}

void ARM_Dynarmic::SetPC(u32 pc) {
    jit->Regs()[15] = pc;
}

u32 ARM_Dynarmic::GetPC() const {
    return jit->Regs()[15];
}

u32 ARM_Dynarmic::GetReg(int index) const {
    return jit->Regs()[index];
}

void ARM_Dynarmic::SetReg(int index, u32 value) {
    jit->Regs()[index] = value;
}

u32 ARM_Dynarmic::GetVFPReg(int index) const {
    return jit->ExtRegs()[index];
}

void ARM_Dynarmic::SetVFPReg(int index, u32 value) {
    jit->ExtRegs()[index] = value;
}

u32 ARM_Dynarmic::GetVFPSystemReg(VFPSystemRegister reg) const {
    switch (reg) {
    case VFP_FPSCR:
        return jit->Fpscr();
    case VFP_FPEXC:
        return fpexc;
    default:
        UNREACHABLE_MSG("Unknown VFP system register: {}", reg);
    }

    return UINT_MAX;
}

void ARM_Dynarmic::SetVFPSystemReg(VFPSystemRegister reg, u32 value) {
    switch (reg) {
    case VFP_FPSCR:
        jit->SetFpscr(value);
        return;
    case VFP_FPEXC:
        fpexc = value;
        return;
    default:
        UNREACHABLE_MSG("Unknown VFP system register: {}", reg);
    }
}

u32 ARM_Dynarmic::GetCPSR() const {
    return jit->Cpsr();
}

void ARM_Dynarmic::SetCPSR(u32 cpsr) {
    jit->SetCpsr(cpsr);
}

u32 ARM_Dynarmic::GetCP15Register(CP15Register reg) const {
    switch (reg) {
    case CP15_THREAD_UPRW:
        return cp15_state.cp15_thread_uprw;
    case CP15_THREAD_URO:
        return cp15_state.cp15_thread_uro;
    default:
        UNREACHABLE_MSG("Unknown CP15 register: {}", reg);
    }

    return 0;
}

void ARM_Dynarmic::SetCP15Register(CP15Register reg, u32 value) {
    switch (reg) {
    case CP15_THREAD_UPRW:
        cp15_state.cp15_thread_uprw = value;
        return;
    case CP15_THREAD_URO:
        cp15_state.cp15_thread_uro = value;
        return;
    default:
        UNREACHABLE_MSG("Unknown CP15 register: {}", reg);
    }
}

void ARM_Dynarmic::SaveContext(ThreadContext& ctx) {
    ctx.cpu_registers = jit->Regs();
    ctx.cpsr = jit->Cpsr();
    ctx.fpu_registers = jit->ExtRegs();
    ctx.fpscr = jit->Fpscr();
    ctx.fpexc = fpexc;
}

void ARM_Dynarmic::LoadContext(const ThreadContext& ctx) {
    jit->Regs() = ctx.cpu_registers;
    jit->SetCpsr(ctx.cpsr);
    jit->ExtRegs() = ctx.fpu_registers;
    jit->SetFpscr(ctx.fpscr);
    fpexc = ctx.fpexc;
}

void ARM_Dynarmic::PrepareReschedule() {
    if (jit->IsExecuting()) {
        jit->HaltExecution();
    }
}

void ARM_Dynarmic::ClearInstructionCache() {
    for (const auto& j : jits) {
        j.second->ClearCache();
    }
}

void ARM_Dynarmic::InvalidateCacheRange(u32 start_address, std::size_t length) {
    jit->InvalidateCacheRange(start_address, length);
}

void ARM_Dynarmic::ClearExclusiveState() {
    jit->ClearExclusiveState();
}

std::shared_ptr<Memory::PageTable> ARM_Dynarmic::GetPageTable() const {
    return current_page_table;
}

void ARM_Dynarmic::SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) {
    current_page_table = page_table;
    ThreadContext ctx{};
    if (jit) {
        SaveContext(ctx);
    }

    auto iter = jits.find(current_page_table);
    if (iter != jits.end()) {
        jit = iter->second.get();
        LoadContext(ctx);
        return;
    }

    auto new_jit = MakeJit();
    jit = new_jit.get();
    LoadContext(ctx);
    jits.emplace(current_page_table, std::move(new_jit));
}

void ARM_Dynarmic::ServeBreak([[maybe_unused]] int signal) {
#ifdef ENABLE_GDBSTUB
    GDBStub::Break(signal);
#endif
}

std::unique_ptr<Dynarmic::A32::Jit> ARM_Dynarmic::MakeJit() {
    Dynarmic::A32::UserConfig config;
    config.callbacks = cb.get();
    if (current_page_table) {
        config.page_table = &current_page_table->GetPointerArray();
    }
    config.coprocessors[15] = std::make_shared<DynarmicCP15>(cp15_state);
    config.define_unpredictable_behaviour = true;

    // Multi-process state
    config.processor_id = GetID();
    config.global_monitor = &exclusive_monitor.monitor;

    return std::make_unique<Dynarmic::A32::Jit>(config);
}

} // namespace Core
