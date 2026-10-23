// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/kernel/luma3ds/level256_backend.h"

#include <array>
#include <limits>
#include <optional>
#include "common/logging/log.h"
#include "common/memory_ref.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/hle/service/plgldr/level256_kprocess_view.h"
#include "core/hle/service/plgldr/plgldr.h"

namespace Kernel::Luma3DS {

Level256BackdoorResult HandleLevel256CustomBackdoor(
    Core::System& system, Memory::MemorySystem& memory, std::shared_ptr<Process> process,
    Thread* thread, const Service::PLGLDR::PLG_LDR::PluginLoaderContext& context,
    Level256ProcessViewManager& process_view_manager, const CompatibilityContext& luma_context,
    const PluginAuthorization& authorization, const SvcRequest& request) {
    const u32 immediate = request.svc_id;
    const u32 in_pc = request.entry_pc;
    const u32 in_r0 = request.arguments[0];
    const u32 in_r1 = request.arguments[1];
    const u32 in_r2 = request.arguments[2];
    const u32 in_r3 = request.arguments[3];
    const bool level256_plugin_context =
        process && CanDispatchExtension(luma_context, authorization, process->process_id, in_pc);
    bool level256_backdoor_handled = false;
    u32 level256_backdoor_result = 0;
    if (immediate == Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID && level256_plugin_context &&
        process && thread &&
        IsValidCustomBackdoorCallback(luma_context, authorization, process->process_id, in_r0)) {
        const bool compatibility_enabled =
            Settings::values.level256_compatibility_enabled.GetValue();
        const bool trusted_plugin = context.level256_hle_compatibility_verified;
        const u32 callback = in_r0;
        const u32 arg0 = in_r1;
        const u32 arg1 = in_r2;
        const u32 arg2 = in_r3;
        const auto& lctx = context;
        const auto callback_header_matches = [&](u32 target_callback) {
            return compatibility_enabled && trusted_plugin && process->codeset &&
                   Service::PLGLDR::IsLevel256SupportedTitle(process->codeset->program_id) &&
                   process->process_id == lctx.plugin_process_id && lctx.plugin_loaded &&
                   lctx.plugin_code_begin == LEVEL256_DIAGNOSTIC_CODE_BEGIN &&
                   lctx.plugin_code_end == LEVEL256_DIAGNOSTIC_CODE_END &&
                   in_pc == LEVEL256_CUSTOM_BACKDOOR_SVC_PC &&
                   immediate == LEVEL256_CUSTOM_BACKDOOR_SVC_ID && callback == target_callback;
        };
        const auto get_plugin_model = [&]() -> std::optional<bool> {
            if (!memory.IsValidVirtualAddress(*process, LEVEL256_PROCESS_VIEW_MODEL_FLAG_ADDRESS)) {
                return std::nullopt;
            }
            const u8 model_flag = memory.Read8(*process, LEVEL256_PROCESS_VIEW_MODEL_FLAG_ADDRESS);
            if (model_flag > 1) {
                return std::nullopt;
            }
            return model_flag != 0;
        };
        const auto get_owned_view_address = [&]() -> VAddr {
            return process_view_manager.GetAddress(*process);
        };

        if (callback == LEVEL256_BACKDOOR_PRIORITY_LIMIT_CALLBACK &&
            callback_header_matches(callback) && process->resource_limit) {
            // This exact guest routine stores r1 at KResourceLimit::maxPriority (+0x08)
            // and returns the previous value in r0. Keep the guest view and the real
            // ResourceLimit priority cap in sync.
            constexpr std::array<u32, 16> current_resource_callback_code{
                0xE7903002, 0xE1A0C421, 0xE5D30009, 0xE5D32008, 0xE5C3C009, 0xE1822400,
                0xE5D3000A, 0xE1A0C821, 0xE1822800, 0xE5D3000B, 0xE5C31008, 0xE1A01C21,
                0xE5C3C00A, 0xE5C3100B, 0xE1820C00, 0xE12FFF1E,
            };
            const auto is_n3ds = get_plugin_model();
            const u32 resource_limit_offset =
                is_n3ds.value_or(true) ? LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET
                                       : LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET;
            const u32 process_id_offset = is_n3ds.value_or(true)
                                              ? LEVEL256_PROCESS_VIEW_N3DS_PROCESS_ID_OFFSET
                                              : LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET;
            bool callback_code_matches = true;
            for (std::size_t i = 0; i < current_resource_callback_code.size(); ++i) {
                const VAddr address = callback + static_cast<VAddr>(i * sizeof(u32));
                if (!memory.IsValidVirtualAddress(*process, address) ||
                    memory.Read32(*process, address) != current_resource_callback_code[i]) {
                    callback_code_matches = false;
                    break;
                }
            }

            const VAddr view_address = arg0;
            const u64 view_end = static_cast<u64>(view_address) + LEVEL256_PROCESS_VIEW_SIZE;
            const auto view_vma = process->vm_manager.FindVMA(view_address);
            u32 guest_resource_view = 0;
            u32 guest_process_id = 0;
            const bool resource_view_read = process_view_manager.ReadWord(
                *process, memory, view_address, resource_limit_offset, guest_resource_view);
            const bool process_id_read = process_view_manager.ReadWord(
                *process, memory, view_address, process_id_offset, guest_process_id);
            const bool view_memory_valid =
                is_n3ds.has_value() && view_address != 0 && arg2 == resource_limit_offset &&
                view_end <= static_cast<u64>(UINT32_MAX) + 1 &&
                view_vma != process->vm_manager.vma_map.end() &&
                view_vma->second.type == VMAType::BackingMemory &&
                view_vma->second.base <= view_address &&
                view_end <= static_cast<u64>(view_vma->second.base) + view_vma->second.size &&
                resource_view_read && process_id_read &&
                guest_resource_view ==
                    view_address + LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET &&
                guest_process_id == process->process_id;
            const VAddr priority_limit_address = view_address +
                                                 LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET +
                                                 LEVEL256_PROCESS_VIEW_PRIORITY_LIMIT_OFFSET;
            const u32 old_priority_limit = static_cast<u32>(
                process->resource_limit->GetLimitValue(ResourceLimitType::Priority));
            u32 old_guest_limit = 0xFFFFFFFF;
            if (view_memory_valid) {
                process_view_manager.ReadWord(*process, memory, view_address,
                                              LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET +
                                                  LEVEL256_PROCESS_VIEW_PRIORITY_LIMIT_OFFSET,
                                              old_guest_limit);
            }
            u32 updated_priority_limit = old_priority_limit;
            u32 callback_return = 0;

            if (ShouldApplyLevel256PriorityLimitCallback(
                    callback_code_matches, view_memory_valid, old_guest_limit, old_priority_limit,
                    arg1, arg2, resource_limit_offset, updated_priority_limit, callback_return)) {
                process->resource_limit->SetLimitValue(ResourceLimitType::Priority,
                                                       static_cast<s32>(updated_priority_limit));
                process_view_manager.WriteWord(*process, memory, view_address,
                                               LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET +
                                                   LEVEL256_PROCESS_VIEW_PRIORITY_LIMIT_OFFSET,
                                               updated_priority_limit);
                level256_backdoor_result = callback_return;
                level256_backdoor_handled = true;
                LOG_INFO(Kernel_SVC,
                         "Level256 CustomBackdoor max-priority update: pid={} thread={} "
                         "callback=0x{:08X} view=0x{:08X} field=0x{:08X} "
                         "value=0x{:08X}->0x{:08X} result=0x{:08X}",
                         process->process_id, thread->GetThreadId(), callback, view_address,
                         priority_limit_address, old_guest_limit, updated_priority_limit,
                         level256_backdoor_result);
            } else {
                LOG_WARNING(Kernel_SVC,
                            "Level256 CustomBackdoor max-priority callback rejected: "
                            "pid={} thread={} callback_code_match={} view_valid={} "
                            "old_guest=0x{:08X} old_priority_limit={} arg1=0x{:08X} "
                            "arg2=0x{:08X} expected_offset=0x{:X}",
                            process->process_id, thread->GetThreadId(), callback_code_matches,
                            view_memory_valid, old_guest_limit, old_priority_limit, arg1, arg2,
                            resource_limit_offset);
            }
        } else if (callback == LEVEL256_BACKDOOR_CURRENT_PROCESS_CALLBACK) {
            const bool callback_matches = ShouldHandleLevel256CurrentProcessCallback(
                compatibility_enabled, trusted_plugin, process->codeset->program_id,
                process->process_id, lctx.plugin_process_id, lctx.plugin_loaded,
                lctx.plugin_code_begin, lctx.plugin_code_end, in_pc, immediate, callback, arg0,
                arg1, arg2);
            if (!callback_matches) {
                LOG_WARNING(Kernel_SVC,
                            "Level256 CustomBackdoor callback rejected: reason=conditions "
                            "pid={} thread={} pc=0x{:08X} callback=0x{:08X} "
                            "args=0x{:08X},0x{:08X},0x{:08X}",
                            process->process_id, thread->GetThreadId(), in_pc, callback, arg0, arg1,
                            arg2);
            } else if (!process->resource_limit) {
                LOG_WARNING(Kernel_SVC,
                            "Level256 CustomBackdoor callback rejected: "
                            "reason=missing_resource_limit pid={} thread={}",
                            process->process_id, thread->GetThreadId());
            } else {
                const auto is_n3ds = get_plugin_model();
                VAddr view_address = get_owned_view_address();
                if (!is_n3ds) {
                    LOG_WARNING(Kernel_SVC,
                                "Level256 CustomBackdoor callback rejected: "
                                "reason=unknown_kernel_model_flag pid={} thread={} "
                                "address=0x{:08X}",
                                process->process_id, thread->GetThreadId(),
                                LEVEL256_PROCESS_VIEW_MODEL_FLAG_ADDRESS);
                } else if (view_address == 0) {
                    VAddr candidate_address = 0;
                    for (const auto& entry : process->vm_manager.vma_map) {
                        const auto& vma = entry.second;
                        if (vma.type != VMAType::Free) {
                            continue;
                        }
                        candidate_address = FindLevel256GuestRangeCandidateFromHigh(
                            vma.base, vma.size, lctx.plugin_code_end, Memory::HEAP_VADDR,
                            LEVEL256_PROCESS_VIEW_SIZE, LEVEL256_PROCESS_VIEW_PAGE_ALIGNMENT);
                        if (candidate_address != 0) {
                            break;
                        }
                    }
                    if (candidate_address == 0) {
                        LOG_WARNING(Kernel_SVC,
                                    "Level256 CustomBackdoor callback rejected: "
                                    "reason=no_free_plugin_gap_vma pid={} thread={} "
                                    "range=0x{:08X}-0x{:08X} size=0x{:X}",
                                    process->process_id, thread->GetThreadId(),
                                    lctx.plugin_code_end, Memory::HEAP_VADDR,
                                    LEVEL256_PROCESS_VIEW_SIZE);
                    } else {
                        auto view_backing = std::make_shared<BufferMem>(LEVEL256_PROCESS_VIEW_SIZE);
                        const auto mapping_result =
                            process_view_manager.MapAt(*process, candidate_address, view_backing);
                        if (mapping_result.IsSuccess()) {
                            view_address = candidate_address;
                        } else {
                            LOG_WARNING(Kernel_SVC,
                                        "Level256 CustomBackdoor callback rejected: "
                                        "reason=view_mapping_failed pid={} thread={} "
                                        "candidate=0x{:08X} size=0x{:X} result=0x{:08X}",
                                        process->process_id, thread->GetThreadId(),
                                        candidate_address, LEVEL256_PROCESS_VIEW_SIZE,
                                        mapping_result.raw);
                            view_address = 0;
                        }
                    }
                }

                if (view_address != 0) {
                    const auto& code_set = *process->codeset;
                    const Service::PLGLDR::Level256::KProcessViewV1::CodeSetFields code_info{
                        code_set.CodeSegment().addr,   code_set.CodeSegment().size,
                        code_set.RODataSegment().addr, code_set.RODataSegment().size,
                        code_set.DataSegment().addr,   code_set.DataSegment().size,
                        code_set.program_id,
                    };
                    const bool initialized = process_view_manager.Initialize(
                        *process, memory, view_address,
                        static_cast<u32>(
                            process->resource_limit->GetLimitValue(ResourceLimitType::Priority)),
                        static_cast<u32>(
                            process->resource_limit->GetLimitValue(ResourceLimitType::Thread)),
                        code_info, *is_n3ds);
                    if (initialized) {
                        level256_backdoor_result = view_address;
                        level256_backdoor_handled = true;
                    }
                    LOG_INFO(Kernel_SVC,
                             "Level256 CustomBackdoor current-process view: pid={} thread={} "
                             "pc=0x{:08X} guest_view=0x{:08X} is_n3ds={} initialized={} "
                             "resource_limit_offset=0x{:X} process_id_offset=0x{:X}",
                             process->process_id, thread->GetThreadId(), in_pc, view_address,
                             *is_n3ds, initialized,
                             *is_n3ds ? LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET
                                      : LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET,
                             *is_n3ds ? LEVEL256_PROCESS_VIEW_N3DS_PROCESS_ID_OFFSET
                                      : LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET);
                }
            }
        } else if ((callback == LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK ||
                    callback == LEVEL256_BACKDOOR_VIEW_FIELD_CALLBACK) &&
                   callback_header_matches(callback) && arg0 == get_owned_view_address() &&
                   process->resource_limit) {
            constexpr std::array<u32, 4> flag_callback_code{0xE59030B0, 0xE3833A02, 0xE58030B0,
                                                            0xE12FFF1E};
            constexpr std::array<u32, 6> field_callback_code{0xE790C002, 0xE3CC3C0F, 0xE1833001,
                                                             0xE7803002, 0xE20C0C0F, 0xE12FFF1E};
            const u32 expected_callback_code = callback == LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK
                                                   ? LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK
                                                   : LEVEL256_BACKDOOR_VIEW_FIELD_CALLBACK;
            const auto callback_code_matches = [&](const auto& expected_code) {
                for (std::size_t i = 0; i < expected_code.size(); ++i) {
                    const VAddr instruction_address =
                        expected_callback_code + static_cast<VAddr>(i * sizeof(u32));
                    if (!memory.IsValidVirtualAddress(*process, instruction_address) ||
                        memory.Read32(*process, instruction_address) != expected_code[i]) {
                        return false;
                    }
                }
                return true;
            };
            const bool callback_instructions_match =
                callback == LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK
                    ? callback_code_matches(flag_callback_code)
                    : callback_code_matches(field_callback_code);
            const auto is_n3ds = get_plugin_model();
            const u32 resource_limit_offset =
                is_n3ds.value_or(true) ? LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET
                                       : LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET;
            const u32 process_id_offset = is_n3ds.value_or(true)
                                              ? LEVEL256_PROCESS_VIEW_N3DS_PROCESS_ID_OFFSET
                                              : LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET;

            const u64 view_end = static_cast<u64>(arg0) + LEVEL256_PROCESS_VIEW_SIZE;
            const auto view_vma = process->vm_manager.FindVMA(arg0);
            u32 guest_resource_view = 0;
            u32 guest_process_id = 0;
            u32 guest_thread_limit = 0;
            u32 old_value = 0xFFFFFFFF;
            const bool view_memory_valid =
                is_n3ds.has_value() && process_view_manager.IsOwnedMapping(*process, arg0) &&
                view_vma != process->vm_manager.vma_map.end() &&
                view_vma->second.type == VMAType::BackingMemory && view_vma->second.base <= arg0 &&
                view_end <= static_cast<u64>(view_vma->second.base) + view_vma->second.size &&
                process_view_manager.ReadWord(*process, memory, arg0, resource_limit_offset,
                                              guest_resource_view) &&
                process_view_manager.ReadWord(*process, memory, arg0, process_id_offset,
                                              guest_process_id) &&
                process_view_manager.ReadWord(*process, memory, arg0,
                                              LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET +
                                                  LEVEL256_PROCESS_VIEW_THREAD_LIMIT_OFFSET,
                                              guest_thread_limit) &&
                process_view_manager.ReadWord(*process, memory, arg0,
                                              LEVEL256_BACKDOOR_VIEW_FLAG_OFFSET, old_value) &&
                guest_resource_view == arg0 + LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET &&
                guest_process_id == process->process_id &&
                guest_thread_limit == static_cast<u32>(process->resource_limit->GetLimitValue(
                                          ResourceLimitType::Thread));
            const VAddr field_address = arg0 + LEVEL256_BACKDOOR_VIEW_FLAG_OFFSET;

            if (callback_instructions_match && view_memory_valid &&
                ShouldHandleLevel256ViewFlagCallback(callback, arg1, arg2, old_value)) {
                const u32 new_value = ApplyLevel256ViewFlagCallback(callback, old_value, arg1);
                process_view_manager.WriteWord(*process, memory, arg0,
                                               LEVEL256_BACKDOOR_VIEW_FLAG_OFFSET, new_value);
                level256_backdoor_result = callback == LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK
                                               ? arg0
                                               : GetLevel256FieldUpdateResult(old_value);
                level256_backdoor_handled = true;
                LOG_INFO(Kernel_SVC,
                         "Level256 CustomBackdoor guest-view flag update: pid={} thread={} "
                         "callback=0x{:08X} view=0x{:08X} field=0x{:08X} "
                         "value=0x{:08X}->0x{:08X} result=0x{:08X}",
                         process->process_id, thread->GetThreadId(), callback, arg0, field_address,
                         old_value, new_value, level256_backdoor_result);
            } else {
                LOG_WARNING(Kernel_SVC,
                            "Level256 CustomBackdoor guest-view callback rejected: "
                            "pid={} thread={} callback=0x{:08X} view=0x{:08X} "
                            "code_match={} view_valid={} old=0x{:08X} "
                            "arg1=0x{:08X} arg2=0x{:08X}",
                            process->process_id, thread->GetThreadId(), callback, arg0,
                            callback_instructions_match, view_memory_valid, old_value, arg1, arg2);
            }
        } else if (callback_header_matches(LEVEL256_BACKDOOR_RESOURCE_LIMIT_CALLBACK) &&
                   arg0 == get_owned_view_address() && process->resource_limit) {
            const auto is_n3ds = get_plugin_model();
            const u32 resource_limit_offset =
                is_n3ds.value_or(true) ? LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET
                                       : LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET;
            const bool callback_matches =
                is_n3ds.has_value() &&
                ShouldHandleLevel256ResourceLimitCallback(
                    compatibility_enabled, trusted_plugin, process->codeset->program_id,
                    process->process_id, lctx.plugin_process_id, lctx.plugin_loaded,
                    lctx.plugin_code_begin, lctx.plugin_code_end, in_pc, immediate, callback,
                    get_owned_view_address(), arg0, arg1, arg2, resource_limit_offset) &&
                process_view_manager.IsOwnedMapping(*process, arg0);
            u32 resource_view = 0;
            const bool resource_view_read =
                callback_matches &&
                process_view_manager.ReadWord(*process, memory, arg0, arg1, resource_view);
            const s32 old_limit = process->resource_limit->GetLimitValue(ResourceLimitType::Thread);
            u32 guest_thread_limit = 0;
            const bool thread_limit_read =
                resource_view_read &&
                process_view_manager.ReadWord(*process, memory, arg0,
                                              arg1 + LEVEL256_PROCESS_VIEW_THREAD_LIMIT_OFFSET,
                                              guest_thread_limit);
            if (callback_matches && resource_view_read && thread_limit_read &&
                resource_view == arg0 + LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET &&
                old_limit <= std::numeric_limits<s32>::max() - 4 &&
                guest_thread_limit == static_cast<u32>(old_limit)) {
                const s32 new_limit = old_limit + 4;
                process->resource_limit->SetLimitValue(ResourceLimitType::Thread, new_limit);
                process_view_manager.WriteWord(*process, memory, arg0,
                                               arg1 + LEVEL256_PROCESS_VIEW_THREAD_LIMIT_OFFSET,
                                               static_cast<u32>(new_limit));
                level256_backdoor_result = static_cast<u32>(new_limit) >> 16;
                level256_backdoor_handled = true;
                LOG_INFO(Kernel_SVC,
                         "Level256 CustomBackdoor thread limit update: pid={} thread={} "
                         "limit={}->{} result=0x{:08X}",
                         process->process_id, thread->GetThreadId(), old_limit, new_limit,
                         level256_backdoor_result);
            } else {
                LOG_WARNING(Kernel_SVC,
                            "Level256 CustomBackdoor resource-limit callback rejected: "
                            "pid={} callback=0x{:08X} is_n3ds_known={} is_n3ds={} "
                            "arg1=0x{:08X} expected_offset=0x{:X} view=0x{:08X}",
                            process->process_id, callback, is_n3ds.has_value(),
                            is_n3ds.value_or(false), arg1, resource_limit_offset, arg0);
            }
        } else if (callback_header_matches(LEVEL256_BACKDOOR_COPY_CALLBACK) &&
                   ShouldHandleLevel256ProcessSnapshotCopy(
                       compatibility_enabled, trusted_plugin, process->codeset->program_id,
                       process->process_id, lctx.plugin_process_id, lctx.plugin_loaded,
                       lctx.plugin_code_begin, lctx.plugin_code_end, in_pc, immediate, callback,
                       arg0, arg1, get_owned_view_address(), arg2)) {
            if (process_view_manager.CopySnapshot(*process, memory, arg0, arg1, arg2)) {
                level256_backdoor_result = arg0;
                level256_backdoor_handled = true;
                LOG_INFO(Kernel_SVC,
                         "Level256 CustomBackdoor process snapshot copied: pid={} "
                         "source=0x{:08X} destination=0x{:08X} size=0x{:X}",
                         process->process_id, arg1, arg0, arg2);
            }
        }

        const VAddr patch_address = Kernel::LEVEL256_CUSTOM_BACKDOOR_PATCH_ADDRESS;
        const bool patch_is_inside_verified_plugin_ram = IsPluginRamPatchRange(
            patch_address, sizeof(u32), context.plugin_code_begin, context.plugin_code_end);
        const auto vma = process->vm_manager.FindVMA(patch_address);
        const bool writable_code =
            patch_is_inside_verified_plugin_ram &&
            memory.IsValidVirtualAddress(*process, patch_address) &&
            vma != process->vm_manager.vma_map.end() && vma->second.base <= patch_address &&
            vma->second.type == VMAType::BackingMemory &&
            static_cast<u64>(patch_address) + sizeof(u32) <=
                static_cast<u64>(vma->second.base) + vma->second.size &&
            (static_cast<u8>(vma->second.permissions) & static_cast<u8>(VMAPermission::Write)) !=
                0 &&
            (static_cast<u8>(vma->second.permissions) & static_cast<u8>(VMAPermission::Execute)) !=
                0;
        const u32 current_instruction =
            writable_code ? memory.Read32(*process, patch_address) : 0xFFFFFFFF;
        u32 patched_instruction = current_instruction;
        u32 callback_return = 0;
        if (!level256_backdoor_handled && writable_code && patch_is_inside_verified_plugin_ram &&
            process->codeset &&
            Kernel::ApplyLevel256BackdoorPatch(
                Kernel::ShouldApplyLevel256BackdoorPatch(
                    Settings::values.level256_compatibility_enabled.GetValue(),
                    context.level256_hle_compatibility_verified, process->codeset->program_id,
                    process->process_id, context.plugin_process_id, context.plugin_loaded,
                    context.plugin_code_begin, context.plugin_code_end, in_pc, immediate, in_r0,
                    in_r1, in_r2, in_r3, current_instruction),
                patched_instruction, callback_return)) {
            memory.WriteBlock(*process, patch_address, &patched_instruction,
                              sizeof(patched_instruction));
            system.InvalidateCacheRange(patch_address, sizeof(patched_instruction));
            level256_backdoor_result = callback_return;
            level256_backdoor_handled = true;
            LOG_INFO(Kernel_SVC,
                     "Level256 CustomBackdoor HLE patch: pid={} thread={} pc=0x{:08X} "
                     "callback=0x{:08X} address=0x{:08X} instruction=0x{:08X}->0x{:08X}",
                     process->process_id, thread->GetThreadId(), in_pc, in_r0, patch_address,
                     current_instruction, patched_instruction);
        }
    }

    return {.handled = level256_backdoor_handled, .result = level256_backdoor_result};
}

} // namespace Kernel::Luma3DS
