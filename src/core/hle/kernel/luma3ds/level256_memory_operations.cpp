// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/kernel/luma3ds/level256_memory_operations.h"

#include <limits>
#include <new>
#include "common/logging/log.h"
#include "common/memory_ref.h"
#include "common/settings.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/hle/service/plgldr/plgldr.h"

namespace Kernel::Luma3DS {

Level256MemoryOperationResult ControlMemoryUnsafe(
    Memory::MemorySystem& memory, Process& process, Thread& thread,
    const Service::PLGLDR::PLG_LDR::PluginLoaderContext& context,
    Level256ProcessViewManager& process_view_manager, const SvcRequest& request) {
    const u32 immediate = request.svc_id;
    const u32 in_pc = request.entry_pc;
    const u32 in_r0 = request.arguments[0];
    const u32 in_r1 = request.arguments[1];
    const u32 in_r2 = request.arguments[2];
    const u32 in_r3 = request.arguments[3];
    const u32 in_r4 = request.arguments[4];
    if (immediate == LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID && process.codeset) {
        const u32 output_address = in_r0;
        const u32 address = in_r1;
        const u32 size = in_r2 & ~Memory::CITRA_PAGE_MASK;
        const u32 operation = in_r3;
        const u32 permissions = in_r4;
        const bool exact_request = ShouldHandleLevel256ControlMemoryUnsafe(
            Settings::values.level256_compatibility_enabled.GetValue(),
            context.level256_official_plugin_hash_matches, process.codeset->program_id,
            process.process_id, context.plugin_process_id, context.plugin_loaded,
            context.plugin_code_begin, context.plugin_code_end, in_pc, immediate, output_address,
            address, in_r2, operation, permissions);
        if (exact_request) {
            Result result = ResultInvalidAddress;
            const u64 output_end = static_cast<u64>(output_address) + sizeof(u32);
            const auto output_vma = process.vm_manager.FindVMA(output_address);
            const bool output_valid =
                output_end <= static_cast<u64>(UINT32_MAX) + 1 &&
                memory.IsValidVirtualAddress(process, output_address) &&
                output_vma != process.vm_manager.vma_map.end() &&
                output_vma->second.type == VMAType::BackingMemory &&
                output_end <= static_cast<u64>(output_vma->second.base) + output_vma->second.size &&
                (static_cast<u8>(output_vma->second.permissions) &
                 static_cast<u8>(VMAPermission::Write)) != 0;
            const u64 target_end = static_cast<u64>(address) + size;
            const auto target_vma = process.vm_manager.FindVMA(address);
            const bool target_is_free =
                target_end <= static_cast<u64>(UINT32_MAX) + 1 &&
                target_vma != process.vm_manager.vma_map.end() &&
                target_vma->second.type == VMAType::Free && target_vma->second.base <= address &&
                target_end <= static_cast<u64>(target_vma->second.base) + target_vma->second.size;
            const u64 view_begin = process_view_manager.GetAddress(process);
            const u64 view_end = view_begin + LEVEL256_PROCESS_VIEW_SIZE;
            const bool overlaps_view =
                view_begin != 0 && address < view_end && view_begin < target_end;
            const bool overlaps_plugin =
                address < context.plugin_code_end && context.plugin_code_begin < target_end;
            if (!output_valid) {
                result = ResultInvalidPointer;
            } else if (!target_is_free || overlaps_view || overlaps_plugin) {
                result = target_vma != process.vm_manager.vma_map.end() &&
                                 target_vma->second.type != VMAType::Free
                             ? ResultInvalidAddressState
                             : ResultInvalidAddress;
            } else {
                try {
                    auto backing = std::make_shared<BufferMem>(size);
                    const auto mapping = process.vm_manager.MapBackingMemory(
                        address, MemoryRef{backing}, size, MemoryState::Private);
                    if (mapping.Succeeded()) {
                        memory.Write32(process, output_address, address);
                        result = ResultSuccess;
                    } else {
                        result = mapping.Code();
                    }
                } catch (const std::bad_alloc&) {
                    result = ResultOutOfMemory;
                }
            }

            LOG_INFO(Kernel_SVC,
                     "Level256 ControlMemoryUnsafe exact COMMIT: pid={} thread={} "
                     "pc=0x{:08X} target=0x{:08X}-0x{:08X} size=0x{:X} "
                     "region=SYSTEM permissions=RW state=Private output=0x{:08X} "
                     "result=0x{:08X} output_valid={} target_free={} overlaps_view={} "
                     "overlaps_plugin={}",
                     process.process_id, thread.GetThreadId(), in_pc, address,
                     static_cast<u32>(target_end), size, output_address, result.raw, output_valid,
                     target_is_free, overlaps_view, overlaps_plugin);
            return {.handled = true, .result = result};
        }
    }

    return {.handled = false, .result = ResultNotImplemented};
}

} // namespace Kernel::Luma3DS
