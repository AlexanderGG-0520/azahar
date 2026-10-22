// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <fmt/format.h>
#include "common/archives.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/core_timing.h"
#ifdef ENABLE_GDBSTUB
#include "core/gdbstub/hio.h"
#endif
#include "core/hle/kernel/address_arbiter.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/config_mem.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/ipc.h"
#include "core/hle/kernel/ipc_debugger/recorder.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/luma3ds/compatibility.h"
#include "core/hle/kernel/luma3ds/custom_backdoor.h"
#include "core/hle/kernel/luma3ds/level256_backend.h"
#include "core/hle/kernel/luma3ds/level256_memory_operations.h"
#include "core/hle/kernel/luma3ds/level256_system_info.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/semaphore.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/svc_wrapper.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/timer.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/hle/kernel/wait_object.h"
#include "core/hle/result.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/hle/service/plgldr/plugin_path.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sm/srv.h"

namespace Kernel {

static const char* Level256DiagnosticThreadStatusName(ThreadStatus status) {
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

enum ControlMemoryOperation {
    MEMOP_FREE = 1,
    MEMOP_RESERVE = 2, // This operation seems to be unsupported in the kernel
    MEMOP_COMMIT = 3,
    MEMOP_MAP = 4,
    MEMOP_UNMAP = 5,
    MEMOP_PROTECT = 6,
    MEMOP_OPERATION_MASK = 0xFF,

    MEMOP_REGION_APP = 0x100,
    MEMOP_REGION_SYSTEM = 0x200,
    MEMOP_REGION_BASE = 0x300,
    MEMOP_REGION_MASK = 0xF00,

    MEMOP_LINEAR = 0x10000,
};

struct MemoryInfo {
    u32 base_address;
    u32 size;
    u32 permission;
    u32 state;
};

/// Values accepted by svcKernelSetState, only the known values are listed
/// (the behaviour of other values are known, but their purpose is unclear and irrelevant).
enum class KernelState {
    /**
     * Reboots the console
     */
    KERNEL_STATE_REBOOT = 7,

    // Special Citra only states.
    /**
     * Sets the emulation speed percentage. A value of 0 means unthrottled.
     */
    KERNEL_STATE_CITRA_EMULATION_SPEED = 0x20000 ///
};

struct PageInfo {
    u32 flags;
};

// Values accepted by svcGetHandleInfo.
enum class HandleInfoType {
    /**
     * Returns the time in ticks the KProcess referenced by the handle was created.
     */
    KPROCESS_ELAPSED_TICKS = 0,

    /**
     * Get internal refcount for kernel object.
     */
    REFERENCE_COUNT = 1,

    STUBBED_1 = 2,
    STUBBED_2 = 0x32107,
};

/// Values accepted by svcGetSystemInfo's type parameter.
enum class SystemInfoType {
    /**
     * Reports total used memory for all regions or a specific one, according to the extra
     * parameter. See `SystemInfoMemUsageRegion`.
     */
    REGION_MEMORY_USAGE = 0,
    /**
     * Returns the memory usage for certain allocations done internally by the kernel.
     */
    KERNEL_ALLOCATED_PAGES = 2,
    /**
     * "This returns the total number of processes which were launched directly by the kernel.
     * For the ARM11 NATIVE_FIRM kernel, this is 5, for processes sm, fs, pm, loader, and pxi."
     */
    KERNEL_SPAWNED_PIDS = 26,
    /**
     * Check various Luma3DS config values. This parameter is not available on real systems,
     * but can be used by homebrew applications.
     */
    LUMA_CFW_INFO = 0x10000,
    /**
     * Check if the current system is a new 3DS. This parameter is not available on real systems,
     * but can be used by homebrew applications.
     */
    NEW_3DS_INFO = 0x10001,
    /**
     * Gets citra related information. This parameter is not available on real systems,
     * but can be used by homebrew applications to get some emulator info.
     */
    CITRA_INFORMATION = 0x20000,
};

enum class ProcessInfoType {
    /**
     * Returns the amount of private (code, data, regular heap) and shared memory used by the
     * process + total supervisor-mode stack size + page-rounded size of the external handle table.
     * This is the amount of physical memory the process is using, minus TLS, main thread stack and
     * linear memory.
     */
    PRIVATE_AND_SHARED_USED_MEMORY = 0,

    /**
     * Returns the amount of <related unused field> + total supervisor-mode stack size +
     * page-rounded size of the external handle table.
     */
    SUPERVISOR_AND_HANDLE_USED_MEMORY = 1,

    /**
     * Returns the amount of private (code, data, heap) memory used by the process + total
     * supervisor-mode stack size + page-rounded size of the external handle table.
     */
    PRIVATE_SHARED_SUPERVISOR_HANDLE_USED_MEMORY = 2,

    /**
     * Returns the amount of <related unused field> + total supervisor-mode stack size +
     * page-rounded size of the external handle table.
     */
    SUPERVISOR_AND_HANDLE_USED_MEMORY2 = 3,

    /**
     * Returns the amount of handles in use by the process.
     */
    USED_HANDLE_COUNT = 4,

    /**
     * Returns the highest count of handles that have been open at once by the process.
     */
    HIGHEST_HANDLE_COUNT = 5,

    /**
     * Returns *(u32*)(KProcess+0x234) which is always 0.
     */
    KPROCESS_0X234 = 6,

    /**
     * Returns the number of threads of the process.
     */
    THREAD_COUNT = 7,

    /**
     * Returns the maximum number of threads which can be opened by this process (always 0).
     */
    MAX_THREAD_AMOUNT = 8,

    /**
     * Originally this only returned 0xD8E007ED. Now with v11.3 this returns the memregion for the
     * process: out low u32 = KProcess "Kernel flags from the exheader kernel descriptors" & 0xF00
     * (memory region flag). High out u32 = 0.
     */
    MEMORY_REGION_FLAGS = 19,

    /**
     * Low u32 = (0x20000000 - <LINEAR virtual-memory base for this process>). That is, the output
     * value is the value which can be added to LINEAR memory vaddrs for converting to
     * physical-memory addrs.
     */
    LINEAR_BASE_ADDR_OFFSET = 20,

    /**
     * Returns the VA -> PA conversion offset for the QTM static mem block reserved in the exheader
     * (0x800000), otherwise 0 (+ error 0xE0E01BF4) if it doesn't exist.
     */
    QTM_MEMORY_BLOCK_CONVERSION_OFFSET = 21,

    /**
     * Returns the base VA of the QTM static mem block reserved in the exheader, otherwise 0 (+
     * error 0xE0E01BF4) if it doesn't exist.
     */
    QTM_MEMORY_ADDRESS = 22,

    /**
     * Returns the size of the QTM static mem block reserved in the exheader, otherwise 0 (+ error
     * 0xE0E01BF4) if it doesn't exist.
     */
    QTM_MEMORY_SIZE = 23,

    // Custom values used by Luma3DS and 3GX plugins

    /**
     * Returns the process name.
     */
    LUMA_CUSTOM_PROCESS_NAME = 0x10000,

    /**
     * Returns the process title ID.
     */
    LUMA_CUSTOM_PROCESS_TITLE_ID = 0x10001,

    /**
     * Returns the codeset text size.
     */
    LUMA_CUSTOM_TEXT_SIZE = 0x10002,

    /**
     * Returns the codeset rodata size.
     */
    LUMA_CUSTOM_RODATA_SIZE = 0x10003,

    /**
     * Returns the codeset data size.
     */
    LUMA_CUSTOM_DATA_SIZE = 0x10004,

    /**
     * Returns the codeset text vaddr.
     */
    LUMA_CUSTOM_TEXT_ADDR = 0x10005,

    /**
     * Returns the codeset rodata vaddr.
     */
    LUMA_CUSTOM_RODATA_ADDR = 0x10006,

    /**
     * Returns the codeset data vaddr.
     */
    LUMA_CUSTOM_DATA_ADDR = 0x10007,

};

/**
 * Accepted by svcGetSystemInfo param with REGION_MEMORY_USAGE type. Selects a region to query
 * memory usage of.
 */
enum class SystemInfoMemUsageRegion {
    ALL = 0,
    APPLICATION = 1,
    SYSTEM = 2,
    BASE = 3,
};

enum class SystemInfoLumaCFWInformation {
    REAL_APP_REGION_SIZE = 0x80, // Gets the real APPLICATION region size,
                                 // instead of the one reported by the kernel shared page
                                 // which depends on the memory mode.
    IS_N3DS = 0x201,             // Checks if the system is a N3DS or not.
};

/**
 * Accepted by svcGetSystemInfo param with CITRA_INFORMATION type. Selects which information
 * to fetch from Citra. Some string params don't fit in 7 bytes, so they are split.
 */
enum class SystemInfoEmulatorInformation {
    EMULATOR_ID = 0,       // Always set the output to 1, signaling the app is running on Citra.
    HOST_TICK = 1,         // Tick reference from the host in ns, unaffected by lag or cpu speed.
    EMULATION_SPEED = 2,   // Gets the emulation speed set by the user or by KernelSetState.
    BUILD_NAME = 10,       // (ie: Nightly, Canary).
    BUILD_VERSION = 11,    // Build version.
    BUILD_PLATFORM = 12,   // Build platform, see SystemInfoCitraPlatform.
    BUILD_DATE_PART1 = 20, // Build date first 7 characters.
    BUILD_DATE_PART2 = 21, // Build date next 7 characters.
    BUILD_DATE_PART3 = 22, // Build date next 7 characters.
    BUILD_DATE_PART4 = 23, // Build date last 7 characters.
    BUILD_GIT_BRANCH_PART1 = 30,      // Git branch first 7 characters.
    BUILD_GIT_BRANCH_PART2 = 31,      // Git branch last 7 characters.
    BUILD_GIT_DESCRIPTION_PART1 = 40, // Git description (commit) first 7 characters.
    BUILD_GIT_DESCRIPTION_PART2 = 41, // Git description (commit) last 7 characters.
};

/**
 * Used by the IS_EMULATOR information
 */
enum class EmulatorIDs {
    NONE = 0,
    CITRA_EMULATOR = 1,
    AZAHAR_EMULATOR = 2,
};

/**
 * Current officially supported platforms.
 */
enum class SystemInfoCitraPlatform {
    PLATFORM_UNKNOWN = 0,
    PLATFORM_WINDOWS = 1,
    PLATFORM_LINUX = 2,
    PLATFORM_APPLE = 3,
    PLATFORM_ANDROID = 4,
};

/**
 * Accepted by the custom svcControlProcess.
 */
enum class ControlProcessOP {
    /**
     * List all handles of the process, varg3 can be either 0 to fetch
     * all handles, or token of the type to fetch s32 count =
     * svcControlProcess(handle, PROCESSOP_GET_ALL_HANDLES,
     * (u32)&outBuf, 0) Returns how many handles were found
     */
    PROCESSOP_GET_ALL_HANDLES = 0,

    /**
     * Set the whole memory of the process with rwx access (in the mmu
     * table only) svcControlProcess(handle, PROCESSOP_SET_MMU_TO_RWX,
     * 0, 0)
     */
    PROCESSOP_SET_MMU_TO_RWX,

    /**
     * Get the handle of an event which will be signaled
     * each time the memory layout of this process changes
     * svcControlProcess(handle,
     * PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT,
     * &eventHandleOut, 0)
     */
    PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT,

    /**
     * Set a flag to be signaled when the process will be exited
     * svcControlProcess(handle, PROCESSOP_SIGNAL_ON_EXIT, 0, 0)
     */
    PROCESSOP_SIGNAL_ON_EXIT,

    /**
     * Get the physical address of the VAddr within the process
     * svcControlProcess(handle, PROCESSOP_GET_PA_FROM_VA, (u32)&PAOut,
     * VAddr)
     */
    PROCESSOP_GET_PA_FROM_VA,

    /*
     * Lock / Unlock the process's threads
     * svcControlProcess(handle, PROCESSOP_SCHEDULE_THREADS, lock,
     * threadPredicate) lock: 0 to unlock threads, any other value to
     * lock threads threadPredicate: can be NULL or a funcptr to a
     * predicate (typedef bool (*ThreadPredicate)(KThread *thread);)
     * The predicate must return true to operate on the thread
     */
    PROCESSOP_SCHEDULE_THREADS,

    /*
     * Lock / Unlock the process's threads
     * svcControlProcess(handle, PROCESSOP_SCHEDULE_THREADS, lock,
     * tlsmagicexclude) lock: 0 to unlock threads, any other value to
     * lock threads tlsmagicexclude: do not lock threads with this tls magic
     * value
     */
    PROCESSOP_SCHEDULE_THREADS_WITHOUT_TLS_MAGIC,

    /**
     * Disable any thread creation restrictions, such as priority value
     * or allowed cores
     */
    PROCESSOP_DISABLE_CREATE_THREAD_RESTRICTIONS,
};

/**
 * Accepted by the custom svcMapProcessMemoryEx.
 */
enum class MapMemoryExFlag {
    /**
     * Maps the memory region as PRIVATE instead of SHARED
     */
    MAPEXFLAGS_PRIVATE = (1 << 0),
};

class SVC : public SVCWrapper<SVC> {
public:
    SVC(Core::System& system);
    void CallSVC(u32 immediate);
    bool ShouldLogSVCError(u32 svc_id, u32 raw_result);

private:
    Core::System& system;
    Kernel::KernelSystem& kernel;
    Memory::MemorySystem& memory;

    friend class SVCWrapper<SVC>;

    // ARM interfaces

    u32 GetReg(std::size_t n);
    void SetReg(std::size_t n, u32 value);

    // SVC interfaces

    Result ControlMemory(u32* out_addr, u32 addr0, u32 addr1, u32 size, u32 operation,
                         u32 permissions);
    void ExitProcess();
    Result TerminateProcess(Handle handle);
    Result MapMemoryBlock(Handle handle, u32 addr, u32 permissions, u32 other_permissions);
    Result UnmapMemoryBlock(Handle handle, u32 addr);
    Result ConnectToPort(Handle* out_handle, VAddr port_name_address);
    Result SendSyncRequest(Handle handle);
    Result OpenProcess(Handle* out_handle, u32 process_id);
    Result OpenThread(Handle* out_handle, Handle process_handle, u32 thread_id);
    Result CloseHandle(Handle handle);
    Result WaitSynchronization1(Handle handle, s64 nano_seconds);
    Result WaitSynchronizationN(s32* out, VAddr handles_address, s32 handle_count, bool wait_all,
                                s64 nano_seconds);
    Result ReplyAndReceive(s32* index, VAddr handles_address, s32 handle_count,
                           Handle reply_target);
    Result InvalidateProcessDataCache(Handle process_handle, VAddr address, u32 size);
    Result StoreProcessDataCache(Handle process_handle, VAddr address, u32 size);
    Result FlushProcessDataCache(Handle process_handle, VAddr address, u32 size);
    Result CreateAddressArbiter(Handle* out_handle);
    Result ArbitrateAddress(Handle handle, u32 address, u32 type, u32 value, s64 nanoseconds);
    void Break(u8 break_reason);
    void OutputDebugString(VAddr address, s32 len);
    Result GetResourceLimit(Handle* resource_limit, Handle process_handle);
    Result GetResourceLimitCurrentValues(VAddr values, Handle resource_limit_handle, VAddr names,
                                         u32 name_count);
    Result GetResourceLimitLimitValues(VAddr values, Handle resource_limit_handle, VAddr names,
                                       u32 name_count);
    Result SetResourceLimitLimitValues(Handle res_limit, VAddr names, VAddr resource_list,
                                       u32 name_count);
    Result CreateThread(Handle* out_handle, u32 entry_point, u32 arg, VAddr stack_top, u32 priority,
                        s32 processor_id);
    void ExitThread();
    Result GetThreadPriority(u32* priority, Handle handle);
    Result SetThreadPriority(Handle handle, u32 priority);
    Result CreateMutex(Handle* out_handle, u32 initial_locked);
    Result ReleaseMutex(Handle handle);
    Result GetProcessId(u32* process_id, Handle process_handle);
    Result GetProcessIdOfThread(u32* process_id, Handle thread_handle);
    Result GetThreadId(u32* thread_id, Handle handle);
    Result CreateSemaphore(Handle* out_handle, s32 initial_count, s32 max_count);
    Result ReleaseSemaphore(s32* count, Handle handle, s32 release_count);
    Result KernelSetState(u32 kernel_state, u32 varg1, u32 varg2);
    Result QueryProcessMemory(MemoryInfo* memory_info, PageInfo* page_info, Handle process_handle,
                              u32 addr);
    Result QueryMemory(MemoryInfo* memory_info, PageInfo* page_info, u32 addr);
    Result CreateEvent(Handle* out_handle, u32 reset_type);
    Result DuplicateHandle(Handle* out, Handle handle);
    Result SignalEvent(Handle handle);
    Result ClearEvent(Handle handle);
    Result CreateTimer(Handle* out_handle, u32 reset_type);
    Result ClearTimer(Handle handle);
    Result SetTimer(Handle handle, s64 initial, s64 interval);
    Result CancelTimer(Handle handle);
    void SleepThread(s64 nanoseconds);
    s64 GetSystemTick();
    Result GetHandleInfo(s64* out, Handle handle, u32 type);
    Result CreateMemoryBlock(Handle* out_handle, u32 addr, u32 size, u32 my_permission,
                             u32 other_permission);
    Result CreatePort(Handle* server_port, Handle* client_port, VAddr name_address,
                      u32 max_sessions);
    Result CreateSessionToPort(Handle* out_client_session, Handle client_port_handle);
    Result CreateSession(Handle* server_session, Handle* client_session);
    Result AcceptSession(Handle* out_server_session, Handle server_port_handle);
    Result GetSystemInfo(s64* out, u32 type, s32 param);
    Result GetProcessInfo(s64* out, Handle process_handle, u32 type);
    Result GetThreadInfo(s64* out, Handle thread_handle, u32 type);
    Result GetProcessList(s32* process_count, VAddr out_process_array, s32 out_process_array_count);
    Result InvalidateInstructionCacheRange(u32 addr, u32 size);
    Result InvalidateEntireInstructionCache();
    u32 ConvertVaToPa(u32 addr);
    void FlushDataCacheRange(u32 addr, u32 size);
    void FlushEntireDataCache();
    Result MapProcessMemoryEx(Handle dst_process_handle, u32 dst_address, Handle src_process_handle,
                              u32 src_address, u32 size, MapMemoryExFlag flags,
                              Handle dst_process_handle_backup);
    Result UnmapProcessMemoryEx(Handle process, u32 dst_address, u32 size);
    Result ControlProcess(Handle process_handle, u32 process_OP, u32 varg2, u32 varg3);

    struct FunctionDef {
        using Func = void (SVC::*)();

        u32 id;
        Func func;
        const char* name;
        u32 cycles;
    };

    static const std::array<FunctionDef, 180> SVC_Table;
    static const FunctionDef* GetSVCInfo(u32 func_num);
};

/// Map application or GSP heap memory
Result SVC::ControlMemory(u32* out_addr, u32 addr0, u32 addr1, u32 size, u32 operation,
                          u32 permissions) {
    LOG_DEBUG(Kernel_SVC,
              "called operation=0x{:08X}, addr0=0x{:08X}, addr1=0x{:08X}, "
              "size=0x{:X}, permissions=0x{:08X}",
              operation, addr0, addr1, size, permissions);

    R_UNLESS((addr0 & Memory::CITRA_PAGE_MASK) == 0, ResultMisalignedAddress);
    R_UNLESS((addr1 & Memory::CITRA_PAGE_MASK) == 0, ResultMisalignedAddress);
    R_UNLESS((size & Memory::CITRA_PAGE_MASK) == 0, ResultMisalignedSize);

    const u32 requested_operation = operation;
    const u32 region = operation & MEMOP_REGION_MASK;
    operation &= ~MEMOP_REGION_MASK;

    if (region != 0) {
        LOG_WARNING(Kernel_SVC, "ControlMemory with specified region not supported, region={:X}",
                    region);
    }

    if ((permissions & (u32)MemoryPermission::ReadWrite) != permissions) {
        return ResultInvalidCombination;
    }
    VMAPermission vma_permissions = (VMAPermission)permissions;

    auto& process = *kernel.GetCurrentProcess();
    const auto log_level256_heap_failure = [&](Result result) {
        if (!Settings::values.level256_compatibility_enabled.GetValue() || !process.codeset ||
            !Service::PLGLDR::IsLevel256SupportedTitle(process.codeset->program_id)) {
            return;
        }
        const auto plugin_loader = Service::PLGLDR::GetService(system);
        if (!plugin_loader) {
            return;
        }
        const auto& context = plugin_loader->GetPluginLoaderContext();
        if (!context.plugin_loaded || !context.level256_official_plugin_hash_matches ||
            context.plugin_process_id != process.process_id ||
            GetReg(15) < context.plugin_code_begin || GetReg(15) >= context.plugin_code_end) {
            return;
        }

        const auto vma = process.vm_manager.FindVMA(addr0);
        const bool has_vma = vma != process.vm_manager.vma_map.end();
        const u32 vma_base = has_vma ? vma->second.base : 0;
        const u32 vma_size = has_vma ? vma->second.size : 0;
        const u32 vma_type = has_vma ? static_cast<u32>(vma->second.type) : UINT32_MAX;
        const u32 vma_permissions = has_vma ? static_cast<u32>(vma->second.permissions) : 0;
        const bool overlaps_compat_view =
            plugin_loader && plugin_loader->GetProcessViewManager().OverlapsOwnedMapping(
                                 process, addr0, size);
        const auto current_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
        LOG_WARNING(Kernel_SVC,
                    "Level256 ControlMemory heap allocation failed: pid={} thread={} pc=0x{:08X} "
                    "operation=0x{:08X} addr0=0x{:08X} addr1=0x{:08X} size=0x{:X} "
                    "permissions=0x{:X} result=0x{:08X} vma_found={} vma_base=0x{:08X} "
                    "vma_size=0x{:X} vma_type={} vma_permissions=0x{:X} "
                    "overlaps_compat_view={}",
                    process.process_id, current_thread ? current_thread->GetThreadId() : 0,
                    GetReg(15), requested_operation, addr0, addr1, size, permissions, result.raw,
                    has_vma, vma_base, vma_size, vma_type, vma_permissions, overlaps_compat_view);
    };

    switch (operation & MEMOP_OPERATION_MASK) {
    case MEMOP_FREE: {
        // TODO(Subv): What happens if an application tries to FREE a block of memory that has a
        // SharedMemory pointing to it?
        if (addr0 >= Memory::HEAP_VADDR && addr0 < Memory::HEAP_VADDR_END) {
            R_TRY(process.HeapFree(addr0, size));
        } else if (addr0 >= process.GetLinearHeapBase() && addr0 < process.GetLinearHeapLimit()) {
            R_TRY(process.LinearFree(addr0, size));
        } else {
            return ResultInvalidAddress;
        }
        *out_addr = addr0;
        break;
    }
    case MEMOP_COMMIT: {
        if (operation & MEMOP_LINEAR) {
            return process.LinearAllocate(out_addr, addr0, size, vma_permissions);
        } else {
            const Result result = process.HeapAllocate(out_addr, addr0, size, vma_permissions);
            if (result.IsError()) {
                log_level256_heap_failure(result);
            }
            return result;
        }
        break;
    }
    case MEMOP_MAP: {
        return process.Map(addr0, addr1, size, vma_permissions);
    }
    case MEMOP_UNMAP: {
        return process.Unmap(addr0, addr1, size, vma_permissions);
    }
    case MEMOP_PROTECT: {
        return process.vm_manager.ReprotectRange(addr0, size, vma_permissions);
    }
    default:
        LOG_ERROR(Kernel_SVC, "unknown operation=0x{:08X}", operation);
        return ResultInvalidCombination;
    }

    return ResultSuccess;
}

void SVC::ExitProcess() {
    kernel.TerminateProcess(kernel.GetCurrentProcess());
}

Result SVC::TerminateProcess(Handle handle) {
    std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(handle);
    R_UNLESS(process, ResultInvalidHandle);

    kernel.TerminateProcess(process);
    return ResultSuccess;
}

/// Maps a memory block to specified address
Result SVC::MapMemoryBlock(Handle handle, u32 addr, u32 permissions, u32 other_permissions) {
    LOG_TRACE(Kernel_SVC,
              "called memblock=0x{:08X}, addr=0x{:08X}, mypermissions=0x{:08X}, "
              "otherpermission={}",
              handle, addr, permissions, other_permissions);

    std::shared_ptr<SharedMemory> shared_memory =
        kernel.GetCurrentProcess()->handle_table.Get<SharedMemory>(handle);
    R_UNLESS(shared_memory, ResultInvalidHandle);

    MemoryPermission permissions_type = static_cast<MemoryPermission>(permissions);
    switch (permissions_type) {
    case MemoryPermission::Read:
    case MemoryPermission::Write:
    case MemoryPermission::ReadWrite:
    case MemoryPermission::Execute:
    case MemoryPermission::ReadExecute:
    case MemoryPermission::WriteExecute:
    case MemoryPermission::ReadWriteExecute:
    case MemoryPermission::DontCare:
        return shared_memory->Map(*kernel.GetCurrentProcess(), addr, permissions_type,
                                  static_cast<MemoryPermission>(other_permissions));
    default:
        LOG_ERROR(Kernel_SVC, "unknown permissions=0x{:08X}", permissions);
    }

    return ResultInvalidCombination;
}

Result SVC::UnmapMemoryBlock(Handle handle, u32 addr) {
    LOG_TRACE(Kernel_SVC, "called memblock=0x{:08X}, addr=0x{:08X}", handle, addr);

    // TODO(Subv): Return E0A01BF5 if the address is not in the application's heap

    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();
    std::shared_ptr<SharedMemory> shared_memory =
        current_process->handle_table.Get<SharedMemory>(handle);
    R_UNLESS(shared_memory, ResultInvalidHandle);

    return shared_memory->Unmap(*current_process, addr);
}

/// Connect to an OS service given the port name, returns the handle to the port to out
Result SVC::ConnectToPort(Handle* out_handle, VAddr port_name_address) {
    R_UNLESS(memory.IsValidVirtualAddress(*kernel.GetCurrentProcess(), port_name_address),
             ResultNotFound);

    static constexpr std::size_t PortNameMaxLength = 11;
    // Read 1 char beyond the max allowed port name to detect names that are too long.
    std::string port_name = memory.ReadCString(port_name_address, PortNameMaxLength + 1);
    R_UNLESS(port_name.size() <= PortNameMaxLength, ResultPortNameTooLong);

    LOG_TRACE(Kernel_SVC, "called port_name={}", port_name);

    auto it = kernel.named_ports.find(port_name);
    R_UNLESS(it != kernel.named_ports.end(), ResultNotFound);

    auto client_port = it->second;
    std::shared_ptr<ClientSession> client_session;
    R_TRY(client_port->Connect(std::addressof(client_session)));

    // Return the client session
    return kernel.GetCurrentProcess()->handle_table.Create(out_handle, client_session);
}

/// Makes a blocking IPC call to an OS service.
Result SVC::SendSyncRequest(Handle handle) {
    std::shared_ptr<ClientSession> session =
        kernel.GetCurrentProcess()->handle_table.Get<ClientSession>(handle);
    R_UNLESS(session, ResultInvalidHandle);

    LOG_TRACE(Kernel_SVC, "called handle=0x{:08X}({})", handle, session->GetName());

    system.PrepareReschedule();

    auto thread = SharedFrom(kernel.GetCurrentThreadManager().GetCurrentThread());
    const auto process = kernel.GetCurrentProcess();
    const bool uds_client = session->name == "plg:UDS_Client";
    const bool ndm_client = session->name == "ndm:u_Client";
    const bool soc_client = session->name == "soc:U_Client";
    std::shared_ptr<Service::SM::SRV> srv;
    std::shared_ptr<Service::PLGLDR::PLG_LDR> plugin_loader;
    bool level256_owner = false;
    if (Settings::values.level256_compatibility_enabled.GetValue() && process && process->codeset &&
        Service::PLGLDR::IsLevel256SupportedTitle(process->codeset->program_id) &&
        (uds_client || ndm_client || soc_client)) {
        plugin_loader = Service::PLGLDR::GetService(system);
        if (plugin_loader) {
            const auto& context = plugin_loader->GetPluginLoaderContext();
            level256_owner = IsVerifiedLevel256PluginContext(
                Settings::values.level256_compatibility_enabled.GetValue(), true,
                context.level256_official_plugin_hash_matches, process->process_id,
                context.plugin_process_id, context.plugin_loaded, context.plugin_code_begin,
                context.plugin_code_end, GetReg(15));
        }
        if (level256_owner) {
            srv = system.ServiceManager().GetService<Service::SM::SRV>("srv:");
        }
    }
    const VAddr command_address = thread->GetCommandBufferAddress();
    bool notify_socket_detach = false;

    // Luma's PLGUDS control commands are carried over the guest-registered plg:UDS session.
    // Only process state and commands are handled here; commands that require the guest server
    // continue through the real ClientSession/ServerSession path.
    if (level256_owner && uds_client && srv) {
        const u32 header = memory.Read32(*process, command_address);
        const u16 command_id = static_cast<u16>(header >> 16);
        const u8 normal_params = static_cast<u8>((header >> 6) & 0x3F);
        const u8 translate_params = static_cast<u8>(header & 0x3F);
        const u32 arg1 = memory.Read32(*process, command_address + sizeof(u32));
        const u32 arg2 = memory.Read32(*process, command_address + 2 * sizeof(u32));
        const auto control = srv->ProcessPlgUdsControl(process->process_id, command_id,
                                                       normal_params, translate_params, arg1, arg2);
        if (control.handled && !control.forward_to_server) {
            memory.Write32(*process, command_address,
                           IPC::MakeHeader(control.response_command, 1, 0));
            memory.Write32(*process, command_address + sizeof(u32), control.response_value);
            return ResultSuccess;
        }
    }

    if (level256_owner && srv && srv->IsLevel256Connected(process->process_id)) {
        const u32 header = memory.Read32(*process, command_address);
        if (ndm_client && header == 0x00010042 &&
            memory.Read32(*process, command_address + sizeof(u32)) == 2 &&
            !srv->IsLevel256Paused(process->process_id)) {
            // NDM's local-communication exclusivity is incompatible with the plugin's
            // infrastructure-mode UDS path; this exact command is normalized by Luma too.
            memory.Write32(*process, command_address + sizeof(u32), 1);
        }

        if (uds_client && !srv->IsLevel256Paused(process->process_id)) {
            const u16 command_id = static_cast<u16>(header >> 16);
            const u8 normal_params = static_cast<u8>((header >> 6) & 0x3F);
            const u8 translate_params = static_cast<u8>(header & 0x3F);
            if (const auto translated = Service::SM::TranslateLevel256UdsCommand(
                    command_id, normal_params, translate_params)) {
                const VAddr static_buffer_address = thread->GetTLSAddress() + 0x180;
                if (command_id == 14 || command_id == 31) {
                    memory.Write32(*process, command_address + 7 * sizeof(u32),
                                   memory.Read32(*process, static_buffer_address + sizeof(u32)));
                } else if (command_id == 17) {
                    memory.Write32(*process, command_address + 2 * sizeof(u32),
                                   memory.Read32(*process, static_buffer_address + sizeof(u32)));
                } else if (command_id == 20) {
                    memory.Write32(*process, command_address + 4 * sizeof(u32),
                                   memory.Read32(*process, static_buffer_address + sizeof(u32)));
                }
                memory.Write32(*process, command_address,
                               IPC::MakeHeader(translated->command_id, translated->normal_params,
                                               translated->translate_params));
            }
        }

        if (soc_client && !srv->IsLevel256Paused(process->process_id)) {
            if (srv->Level256SignalOnAttach(process->process_id) && header == 0x00010044) {
                std::shared_ptr<Kernel::ClientSession> signal_session;
                if (system.ServiceManager().ConnectToService(&signal_session, "plg:UDS") ==
                    ResultSuccess) {
                    std::array<u32, 10> backup{};
                    memory.ReadBlock(*process, command_address, backup.data(), sizeof(backup));
                    memory.Write32(*process, command_address, IPC::MakeHeader(102, 1, 0));
                    memory.Write32(*process, command_address + sizeof(u32), 0);
                    const auto signal_result = signal_session->SendSyncRequest(thread);
                    memory.WriteBlock(*process, command_address, backup.data(), sizeof(backup));
                    if (signal_result.IsError()) {
                        LOG_WARNING(Kernel_SVC,
                                    "Level256 socket attach event failed: pid={} result=0x{:08X}",
                                    process->process_id, signal_result.raw);
                    }
                }
            }
            if (header == 0x00190000 && srv->Level256SignalOnAttach(process->process_id)) {
                srv->SetLevel256SignalOnDetach(process->process_id, true);
                notify_socket_detach = true;
            }
        }
    }

    if (kernel.GetIPCRecorder().IsEnabled()) {
        kernel.GetIPCRecorder().RegisterRequest(session, thread);
    }

    const bool is_hle =
        session->parent->server != nullptr && session->parent->server->hle_handler != nullptr;

    if (is_hle) {
        system.perf_stats->BeginIPCProcessing();
    }
    const auto res = session->SendSyncRequest(thread);
    if (is_hle) {
        system.perf_stats->EndIPCProcessing();
    }

    if (notify_socket_detach && process && srv) {
        srv->SetLevel256SignalOnDetach(process->process_id, false);
        std::shared_ptr<Kernel::ClientSession> signal_session;
        if (system.ServiceManager().ConnectToService(&signal_session, "plg:UDS") == ResultSuccess) {
            std::array<u32, 10> backup{};
            memory.ReadBlock(*process, command_address, backup.data(), sizeof(backup));
            memory.Write32(*process, command_address, IPC::MakeHeader(102, 1, 0));
            memory.Write32(*process, command_address + sizeof(u32), 1);
            const auto signal_result = signal_session->SendSyncRequest(thread);
            memory.WriteBlock(*process, command_address, backup.data(), sizeof(backup));
            if (signal_result.IsError()) {
                LOG_WARNING(Kernel_SVC,
                            "Level256 socket detach event failed: pid={} result=0x{:08X}",
                            process->process_id, signal_result.raw);
            }
        }
    }

    return res;
}

Result SVC::OpenProcess(Handle* out_handle, u32 process_id) {
    // A failed OpenProcess must not leak an uninitialized output register through the SVC ABI.
    // In particular, plugin code may otherwise carry that value into a later QueryProcessMemory.
    ClearOpenProcessOutput(*out_handle);
    std::shared_ptr<Process> process = kernel.GetProcessById(process_id);
    if (!process) {
        // Result 0xd9001818 (process not found?)
        return Result(24, ErrorModule::OS, ErrorSummary::WrongArgument, ErrorLevel::Permanent);
    }
    return kernel.GetCurrentProcess()->handle_table.Create(out_handle, process);
}

Result SVC::OpenThread(Handle* out_handle, Handle process_handle, u32 thread_id) {
    // Result 0xd9001819 (thread not found?)
    constexpr static Result ResultThreadNotFound(25, ErrorModule::OS, ErrorSummary::WrongArgument,
                                                 ErrorLevel::Permanent);

    if (process_handle == 0) {
        LOG_ERROR(Kernel_SVC, "Uninplemented svcOpenThread process_handle=0");
        return ResultThreadNotFound;
    }

    std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    for (u32 core_id = 0; core_id < system.GetNumCores(); core_id++) {
        const auto thread_list = kernel.GetThreadManager(core_id).GetThreadList();
        for (auto& thread : thread_list) {
            if (thread->owner_process.lock() == process && thread.get()->thread_id == thread_id) {
                return kernel.GetCurrentProcess()->handle_table.Create(out_handle, thread);
            }
        }
    }
    return ResultThreadNotFound;
}

/// Close a handle
Result SVC::CloseHandle(Handle handle) {
    const auto process = kernel.GetCurrentProcess();
    if (process && process->codeset && IsYoKaiWatch2Title(process->codeset->program_id)) {
        const auto object = process->handle_table.GetGeneric(handle);
        LOG_INFO(Kernel_SVC,
                 "[HOME-HANDOFF] YW2 CloseHandle: pid={} thread={} pc=0x{:08X} handle=0x{:08X} "
                 "valid={} type={} name={}",
                 process->process_id,
                 kernel.GetCurrentThreadManager().GetCurrentThread()
                     ? kernel.GetCurrentThreadManager().GetCurrentThread()->thread_id
                     : 0,
                 GetReg(15), handle, object != nullptr,
                 object ? object->GetTypeName() : "<invalid>",
                 object ? object->GetName() : "<invalid>");
        // Diagnostic only: the guest NDSP sleep callback closes the DSP semaphore
        // while another guest thread may still try to signal it. Do not change
        // handle validity or SVC results here.
        if (object && object->GetHandleType() == HandleType::Event &&
            object->GetName() == "DSP_DSP::semaphore_event") {
            const auto* current_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
            LOG_WARNING(Kernel_SVC,
                        "[DSP-SEM-RACE] Close DSP semaphore: pid={} thread={} pc=0x{:08X} "
                        "handle=0x{:08X} object_id={} ticks={}",
                        process->process_id, current_thread ? current_thread->thread_id : 0,
                        GetReg(15), handle, object->GetObjectId(),
                        system.CoreTiming().GetGlobalTicks());
        }
    }

    // A guest NDSP worker may still be about to signal dspSem after a
    // concurrent sleep callback has closed it. Arm a bounded token only if
    // the exact DSP semaphore was previously signaled by a different thread.
    const auto closing_object = process->handle_table.GetGeneric(handle);
    if (closing_object && closing_object->GetHandleType() == HandleType::Event &&
        closing_object->GetName() == "DSP_DSP::semaphore_event") {
        const auto* current_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
        if (current_thread) {
            constexpr s64 DspSemaphoreGraceTicks = msToCycles(5);
            const s64 now = system.CoreTiming().GetGlobalTicks();
            const bool armed = process->RetireDspSemaphoreSignal(closing_object->GetObjectId(),
                                                                 current_thread->thread_id, now,
                                                                 DspSemaphoreGraceTicks);
            LOG_INFO(Kernel_SVC,
                     "[DSP-SEM-GRACE] Close DSP semaphore: pid={} thread={} "
                     "handle=0x{:08X} object_id={} armed={} ticks={}",
                     process->process_id, current_thread->thread_id, handle,
                     closing_object->GetObjectId(), armed, now);
        }
    }

    LOG_TRACE(Kernel_SVC, "Closing handle 0x{:08X}", handle);
    return process->handle_table.Close(handle);
}

static Result ReceiveIPCRequest(std::shared_ptr<ServerSession> server_session,
                                std::shared_ptr<Thread> thread) {
    return server_session->ReceiveRequest(std::move(thread));
}

class SVC_SyncCallback : public Kernel::WakeupCallback {
public:
    explicit SVC_SyncCallback(bool do_output_) : do_output(do_output_) {}
    void WakeUp(ThreadWakeupReason reason, std::shared_ptr<Thread> thread,
                std::shared_ptr<WaitObject> object) {

        if (reason == ThreadWakeupReason::Timeout) {
            thread->SetWaitSynchronizationResult(ResultTimeout);
            return;
        }

        ASSERT(reason == ThreadWakeupReason::Signal);

        thread->SetWaitSynchronizationResult(ResultSuccess);

        // The wait_all case does not update the output index.
        if (do_output) {
            thread->SetWaitSynchronizationOutput(thread->GetWaitObjectIndex(object.get()));
        }
    }

private:
    bool do_output;

    SVC_SyncCallback() = default;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<Kernel::WakeupCallback>(*this);
        ar & do_output;
    }
    friend class boost::serialization::access;
};

class SVC_IPCCallback : public Kernel::WakeupCallback {
public:
    explicit SVC_IPCCallback(Core::System& system_) : system(system_) {}

    void WakeUp(ThreadWakeupReason reason, std::shared_ptr<Thread> thread,
                std::shared_ptr<WaitObject> object) {

        ASSERT(thread->status == ThreadStatus::WaitSynchAny);
        ASSERT(reason == ThreadWakeupReason::Signal);

        Result result = ResultSuccess;

        if (object->GetHandleType() == HandleType::ServerSession) {
            auto server_session = DynamicObjectCast<ServerSession>(object);
            result = ReceiveIPCRequest(server_session, thread);
        }

        thread->SetWaitSynchronizationResult(result);
        thread->SetWaitSynchronizationOutput(thread->GetWaitObjectIndex(object.get()));
    }

private:
    Core::System& system;

    SVC_IPCCallback() : system(Core::Global<Core::System>()) {}

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<Kernel::WakeupCallback>(*this);
    }
    friend class boost::serialization::access;
};

/// Wait for a handle to synchronize, timeout after the specified nanoseconds
Result SVC::WaitSynchronization1(Handle handle, s64 nano_seconds) {
    const auto process = kernel.GetCurrentProcess();

    Thread* thread = kernel.GetCurrentThreadManager().GetCurrentThread();

    // A guest DSP audio worker may race with the teardown callback: it has
    // previously waited on the registered audio IRQ, but the callback has now
    // closed and zeroed its handle. Allow only that same thread's next finite
    // zero-handle wait to complete as a missed IRQ timeout.
    //
    // This is an explicit, one-shot compatibility workaround, NOT real 3DS
    // kernel behavior. All other invalid handles keep ResultInvalidHandle.
    if (handle == 0 && nano_seconds > 0 && process && thread &&
        thread->owner_process.lock() == process &&
        thread->ConsumeDspAudioIrqGrace(system.CoreTiming().GetGlobalTicks())) {
        LOG_WARNING(Kernel_SVC,
                    "[DSP-IRQ-GRACE] Consumed retired audio IRQ wait: pid={} thread={} "
                    "timeout={} -> ResultTimeout",
                    process->process_id, thread->thread_id, nano_seconds);
        return ResultTimeout;
    }

    auto object = process->handle_table.Get<WaitObject>(handle);
    if (thread) {
        if (object && object->GetHandleType() == HandleType::Event &&
            std::static_pointer_cast<Event>(object)->IsDspAudioIrqRegistered()) {
            thread->RememberDspAudioIrqWait(object->GetObjectId());
        } else {
            // A different wait breaks the association with the DSP audio IRQ,
            // preventing a stale retirement token from affecting other waits.
            thread->ClearDspAudioIrqWait();
        }
    }

    if (process && process->codeset && IsYoKaiWatch2Title(process->codeset->program_id)) {
        if (object) {
            LOG_INFO(Kernel_SVC,
                     "[HOME-HANDOFF] YW2 WaitSynchronization1: pid={} thread={} pc=0x{:08X} "
                     "handle=0x{:08X} type={} name={} timeout={}",
                     process->process_id, thread ? thread->thread_id : 0, GetReg(15), handle,
                     object->GetTypeName(), object->GetName(), nano_seconds);
        } else {
            LOG_CRITICAL(Kernel_SVC,
                         "[HOME-HANDOFF] YW2 WaitSynchronization1 INVALID: pid={} thread={} "
                         "pc=0x{:08X} handle=0x{:08X} timeout={}",
                         process->process_id, thread ? thread->thread_id : 0, GetReg(15), handle,
                         nano_seconds);
        }
    }

    R_UNLESS(object, ResultInvalidHandle);

    LOG_TRACE(Kernel_SVC, "called handle=0x{:08X}({}:{}), nanoseconds={}", handle,
              object->GetTypeName(), object->GetName(), nano_seconds);

    const bool actual_should_wait = object->ShouldWait(thread);
    bool apply_yw2_worker_ordering_workaround = false;
    if (!actual_should_wait && nano_seconds == 0 && object->GetHandleType() == HandleType::Thread) {
        if (process && process->codeset && IsYoKaiWatch2Title(process->codeset->program_id)) {
            const auto target_thread = std::static_pointer_cast<Thread>(object);
            const auto target_process = target_thread->owner_process.lock();
            if (target_process == process && target_thread->status == ThreadStatus::Dead) {
                const u16 command_id =
                    kernel.TryUseYW2UDSWorkerOrderingWorkaround(process, target_thread);
                apply_yw2_worker_ordering_workaround = command_id != 0;
                if (command_id != 0) {
                    LOG_DEBUG(Service_NWM,
                              "YW2 worker polling workaround: title_id=0x{:016X} process_id={} "
                              "role={} command_id=0x{:04X} worker_thread_id={} timeout=0 "
                              "worker_status=Dead first_poll_consumed=true matched=true",
                              process->codeset->program_id, process->process_id,
                              command_id == 0x001D ? "host" : "client", command_id,
                              target_thread->GetThreadId());
                }
            }
        }
    }

    const bool effective_should_wait = actual_should_wait || apply_yw2_worker_ordering_workaround;
    if (effective_should_wait) {
        R_UNLESS(nano_seconds != 0, ResultTimeout);

        thread->wait_objects = {object};
        object->AddWaitingThread(SharedFrom(thread));
        thread->status = ThreadStatus::WaitSynchAny;

        // Create an event to wake the thread up after the specified nanosecond delay has passed
        thread->WakeAfterDelay(nano_seconds);

        thread->wakeup_callback = std::make_shared<SVC_SyncCallback>(false);

        system.PrepareReschedule();

        // Note: The output of this SVC will be set to ResultSuccess if the thread
        // resumes due to a signal in its wait objects.
        // Otherwise we retain the default value of timeout.
        return ResultTimeout;
    }

    object->Acquire(thread);
    return ResultSuccess;
}

/// Wait for the given handles to synchronize, timeout after the specified nanoseconds
Result SVC::WaitSynchronizationN(s32* out, VAddr handles_address, s32 handle_count, bool wait_all,
                                 s64 nano_seconds) {
    Thread* thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    R_UNLESS(memory.IsValidVirtualAddress(*kernel.GetCurrentProcess(), handles_address),
             ResultInvalidPointer);

    // NOTE: on real hardware, there is no nullptr check for 'out' (tested with firmware 4.4). If
    // this happens, the running application will crash.
    ASSERT_MSG(out != nullptr, "invalid output pointer specified!");

    // Check if 'handle_count' is invalid
    R_UNLESS(handle_count >= 0, ResultOutOfRange);

    using ObjectPtr = std::shared_ptr<WaitObject>;
    std::vector<ObjectPtr> objects(handle_count);

    for (int i = 0; i < handle_count; ++i) {
        Handle handle = memory.Read32(handles_address + i * sizeof(Handle));
        auto object = kernel.GetCurrentProcess()->handle_table.Get<WaitObject>(handle);
        R_UNLESS(object, ResultInvalidHandle);
        objects[i] = object;
    }

    if (wait_all) {
        bool all_available =
            std::all_of(objects.begin(), objects.end(),
                        [thread](const ObjectPtr& object) { return !object->ShouldWait(thread); });
        if (all_available) {
            // We can acquire all objects right now, do so.
            for (auto& object : objects)
                object->Acquire(thread);
            // Note: In this case, the `out` parameter is not set,
            // and retains whatever value it had before.
            return ResultSuccess;
        }

        // Not all objects were available right now, prepare to suspend the thread.

        // If a timeout value of 0 was provided, just return the Timeout error code instead of
        // suspending the thread.
        R_UNLESS(nano_seconds != 0, ResultTimeout);

        // Put the thread to sleep
        thread->status = ThreadStatus::WaitSynchAll;

        // Add the thread to each of the objects' waiting threads.
        for (auto& object : objects) {
            object->AddWaitingThread(SharedFrom(thread));
        }

        thread->wait_objects = std::move(objects);

        // Create an event to wake the thread up after the specified nanosecond delay has passed
        thread->WakeAfterDelay(nano_seconds);

        thread->wakeup_callback = std::make_shared<SVC_SyncCallback>(false);

        system.PrepareReschedule();

        // This value gets set to -1 by default in this case, it is not modified after this.
        *out = -1;
        // Note: The output of this SVC will be set to ResultSuccess if the thread resumes due to
        // a signal in one of its wait objects.
        return ResultTimeout;
    } else {
        // Find the first object that is acquirable in the provided list of objects
        auto itr = std::find_if(objects.begin(), objects.end(), [thread](const ObjectPtr& object) {
            return !object->ShouldWait(thread);
        });

        if (itr != objects.end()) {
            // We found a ready object, acquire it and set the result value
            WaitObject* object = itr->get();
            object->Acquire(thread);
            *out = static_cast<s32>(std::distance(objects.begin(), itr));
            return ResultSuccess;
        }

        // No objects were ready to be acquired, prepare to suspend the thread.

        // If a timeout value of 0 was provided, just return the Timeout error code instead of
        // suspending the thread.
        R_UNLESS(nano_seconds != 0, ResultTimeout);

        // Put the thread to sleep
        thread->status = ThreadStatus::WaitSynchAny;

        // Add the thread to each of the objects' waiting threads.
        for (std::size_t i = 0; i < objects.size(); ++i) {
            WaitObject* object = objects[i].get();
            object->AddWaitingThread(SharedFrom(thread));
        }

        thread->wait_objects = std::move(objects);

        // Note: If no handles and no timeout were given, then the thread will deadlock, this is
        // consistent with hardware behavior.

        // Create an event to wake the thread up after the specified nanosecond delay has passed
        thread->WakeAfterDelay(nano_seconds);

        thread->wakeup_callback = std::make_shared<SVC_SyncCallback>(true);

        system.PrepareReschedule();

        // Note: The output of this SVC will be set to ResultSuccess if the thread resumes due to a
        // signal in one of its wait objects.
        // Otherwise we retain the default value of timeout, and -1 in the out parameter
        *out = -1;
        return ResultTimeout;
    }
}

/// In a single operation, sends a IPC reply and waits for a new request.
Result SVC::ReplyAndReceive(s32* index, VAddr handles_address, s32 handle_count,
                            Handle reply_target) {
    R_UNLESS(memory.IsValidVirtualAddress(*kernel.GetCurrentProcess(), handles_address),
             ResultInvalidPointer);

    // Check if 'handle_count' is invalid
    R_UNLESS(handle_count >= 0, ResultOutOfRange);

    using ObjectPtr = std::shared_ptr<WaitObject>;
    std::vector<ObjectPtr> objects(handle_count);

    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();

    for (int i = 0; i < handle_count; ++i) {
        Handle handle = memory.Read32(handles_address + i * sizeof(Handle));
        auto object = current_process->handle_table.Get<WaitObject>(handle);
        R_UNLESS(object, ResultInvalidHandle);
        objects[i] = object;
    }

    // We are also sending a command reply.
    // Do not send a reply if the command id in the command buffer is 0xFFFF.
    Thread* thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    u32 cmd_buff_header = memory.Read32(thread->GetCommandBufferAddress());
    IPC::Header header{cmd_buff_header};
    if (reply_target != 0 && header.command_id != 0xFFFF) {
        auto session = current_process->handle_table.Get<ServerSession>(reply_target);
        R_UNLESS(session, ResultInvalidHandle);

        const auto reply_result = session->ReplyRequest(SharedFrom(thread));
        if (reply_result.IsError()) {
            *index = -1;
            return reply_result;
        }
    }

    if (handle_count == 0) {
        *index = 0;
        R_SUCCEED_IF(reply_target != 0 && header.command_id != 0xFFFF);
        // The kernel uses this value as a placeholder for the real error, and returns it when we
        // pass no handles and do not perform any reply.
        return Result(0xE7E3FFFF);
    }

    // Find the first object that is acquirable in the provided list of objects
    auto itr = std::find_if(objects.begin(), objects.end(), [thread](const ObjectPtr& object) {
        return !object->ShouldWait(thread);
    });

    if (itr != objects.end()) {
        // We found a ready object, acquire it and set the result value
        WaitObject* object = itr->get();
        object->Acquire(thread);
        *index = static_cast<s32>(std::distance(objects.begin(), itr));

        R_SUCCEED_IF(object->GetHandleType() != HandleType::ServerSession);

        auto server_session = static_cast<ServerSession*>(object);
        const auto receive_result = server_session->ReceiveRequest(SharedFrom(thread));
        if (receive_result.IsError()) {
            *index = -1;
        }
        return receive_result;
    }

    // No objects were ready to be acquired, prepare to suspend the thread.

    // Put the thread to sleep
    thread->status = ThreadStatus::WaitSynchAny;

    // Add the thread to each of the objects' waiting threads.
    for (std::size_t i = 0; i < objects.size(); ++i) {
        WaitObject* object = objects[i].get();
        object->AddWaitingThread(SharedFrom(thread));
    }

    thread->wait_objects = std::move(objects);

    thread->wakeup_callback = std::make_shared<SVC_IPCCallback>(system);

    system.PrepareReschedule();

    // Note: The output of this SVC will be set to ResultSuccess if the thread resumes due to a
    // signal in one of its wait objects, or to 0xC8A01836 if there was a translation error.
    // By default the index is set to -1.
    *index = -1;
    return ResultSuccess;
}

/// Invalidates the specified cache range (stubbed as we do not emulate cache).
Result SVC::InvalidateProcessDataCache(Handle process_handle, VAddr address, u32 size) {
    const std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    LOG_DEBUG(Kernel_SVC, "called address=0x{:08X}, size=0x{:08X}", address, size);

    return ResultSuccess;
}

/// Stores the specified cache range (stubbed as we do not emulate cache).
Result SVC::StoreProcessDataCache(Handle process_handle, VAddr address, u32 size) {
    const std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    LOG_DEBUG(Kernel_SVC, "called address=0x{:08X}, size=0x{:08X}", address, size);

    return ResultSuccess;
}

/// Luma's custom SVC 0x91 cleans guest data-cache lines. Azahar does not model a separate guest
/// data cache: guest writes are immediately visible in emulated memory, so this operation is a
/// no-op. This must not invalidate the Dynarmic instruction cache (that is SVC 0x93).
void SVC::FlushDataCacheRange(u32, u32) {}

/// Flushes the specified cache range (stubbed as we do not emulate cache).
Result SVC::FlushProcessDataCache(Handle process_handle, VAddr address, u32 size) {
    const std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    constexpr u64 osd_candidate_end =
        static_cast<u64>(LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS) + sizeof(u32);
    const u64 flush_end = static_cast<u64>(address) + size;
    if (process->codeset && process->codeset->program_id == LEVEL256_DIAGNOSTIC_TITLE_ID &&
        address <= LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS && flush_end >= osd_candidate_end) {
        if (const auto plugin_loader = Service::PLGLDR::GetService(system)) {
            auto& context = plugin_loader->GetPluginLoaderContext();
            if (context.ctrpf_osd_candidate_flush_observed_pids.insert(process->process_id)
                    .second) {
                const bool instruction_valid = memory.IsValidVirtualAddress(
                    *process, LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS);
                const u32 instruction =
                    instruction_valid
                        ? memory.Read32(*process, LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS)
                        : 0xFFFFFFFF;
                const auto caller = kernel.GetCurrentProcess();
                const u32 original_instruction = context.ctrpf_osd_candidate_initial_words[0];
                const bool original_valid =
                    context.ctrpf_osd_scan.selected_candidate ==
                    LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS;
                if (instruction_valid &&
                    instruction == LEVEL256_CTRPF_OSD_HOOK_BRANCH &&
                    !context.ctrpf_hook_target_instruction_executed &&
                    !context.ctrpf_hook_trace_started) {
                    context.ctrpf_hook_execution_trace_armed = true;
                    context.ctrpf_hook_trace_process_id = process->process_id;
                    context.ctrpf_hook_target_instruction_executed = false;
                    context.ctrpf_hook_wrapper_instruction_executed = false;
                    context.ctrpf_hook_callback_instruction_executed = false;
                    context.ctrpf_hook_trace_thread_id = UINT32_MAX;
                    context.ctrpf_hook_trace_runs = 0;
                    context.ctrpf_hook_invocation_count = 0;
                    context.ctrpf_hook_asm_wrapper_executed = false;
                    context.ctrpf_hook_callback_target_observed = false;
                    context.ctrpf_hook_callback_target = 0;
                    LOG_INFO(Service_PLGLDR,
                             "Level256 OSD hook execution trace armed after cache flush "
                             "pid={} caller_pc=0x{:08X} candidate=0x{:08X} "
                             "instruction=0x{:08X}",
                             process->process_id, GetReg(15), LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS,
                             instruction);
                }
                LOG_INFO(Service_PLGLDR,
                         "Level256 FlushProcessDataCache OSD candidate target_pid={} "
                         "caller_pid={} title_id={:016X} pc=0x{:08X} "
                         "range=[0x{:08X},0x{:016X}) candidate=0x{:08X} "
                         "original_valid={} original=0x{:08X} readable={} "
                         "instruction=0x{:08X} changed={}",
                         process->process_id, caller ? caller->process_id : 0,
                         process->codeset->program_id, GetReg(15), address, flush_end,
                         LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS, original_valid,
                         original_instruction, instruction_valid, instruction,
                         original_valid && instruction_valid &&
                             instruction != original_instruction);
            }
        }
    }

    LOG_DEBUG(Kernel_SVC, "called address=0x{:08X}, size=0x{:08X}", address, size);

    return ResultSuccess;
}

/// Create an address arbiter (to allocate access to shared resources)
Result SVC::CreateAddressArbiter(Handle* out_handle) {
    // Update address arbiter count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::AddressArbiter, 1)) {
        return Result(ErrCodes::OutOfAddressArbiters, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create address arbiter.
    const auto arbiter = kernel.CreateAddressArbiter();
    arbiter->resource_limit = resource_limit;
    return current_process->handle_table.Create(out_handle, std::move(arbiter));
}

/// Arbitrate address
Result SVC::ArbitrateAddress(Handle handle, u32 address, u32 type, u32 value, s64 nanoseconds) {
    LOG_TRACE(Kernel_SVC, "called handle=0x{:08X}, address=0x{:08X}, type=0x{:08X}, value=0x{:08X}",
              handle, address, type, value);

    std::shared_ptr<AddressArbiter> arbiter =
        kernel.GetCurrentProcess()->handle_table.Get<AddressArbiter>(handle);
    R_UNLESS(arbiter, ResultInvalidHandle);

    auto res =
        arbiter->ArbitrateAddress(SharedFrom(kernel.GetCurrentThreadManager().GetCurrentThread()),
                                  static_cast<ArbitrationType>(type), address, value, nanoseconds);

    // TODO(Subv): Identify in which specific cases this call should cause a reschedule.
    system.PrepareReschedule();
    return res;
}

void SVC::Break(u8 break_reason) {
    LOG_CRITICAL(Debug_Emulated, "Emulated program broke execution!");
    std::string reason_str;
    switch (break_reason) {
    case 0:
        reason_str = "PANIC";
        break;
    case 1:
        reason_str = "ASSERT";
        break;
    case 2:
        reason_str = "USER";
        break;
    default:
        reason_str = "UNKNOWN";
        break;
    }
    LOG_CRITICAL(Debug_Emulated, "Break reason: {}", reason_str);
    system.SetStatus(Core::System::ResultStatus::ErrorUnknown);
}

/// Used to output a message on a debug hardware unit, or for the GDB file I/O
/// (HIO) protocol - does nothing on a retail unit.
void SVC::OutputDebugString(VAddr address, s32 len) {
    if (!memory.IsValidVirtualAddress(*kernel.GetCurrentProcess(), address)) {
        LOG_WARNING(Kernel_SVC, "OutputDebugString called with invalid address {:X}", address);
        return;
    }

    if (len == 0) {
#ifdef ENABLE_GDBSTUB
        GDBStub::SetHioRequest(system, kernel.GetCurrentProcess().get(), address);
#endif
        return;
    }

    if (len < 0) {
        LOG_WARNING(Kernel_SVC, "OutputDebugString called with invalid length {}", len);
        return;
    }

    std::string string(len, ' ');
    memory.ReadBlock(*kernel.GetCurrentProcess(), address, string.data(), len);
    LOG_DEBUG(Debug_Emulated, "{}", string);
}

/// Get resource limit
Result SVC::GetResourceLimit(Handle* resource_limit, Handle process_handle) {
    LOG_TRACE(Kernel_SVC, "called process=0x{:08X}", process_handle);

    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();
    std::shared_ptr<Process> process = current_process->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    return current_process->handle_table.Create(resource_limit, process->resource_limit);
}

/// Get resource limit current values
Result SVC::GetResourceLimitCurrentValues(VAddr values, Handle resource_limit_handle, VAddr names,
                                          u32 name_count) {
    LOG_TRACE(Kernel_SVC, "called resource_limit={:08X}, names={:08X}, name_count={}",
              resource_limit_handle, names, name_count);

    const auto resource_limit =
        kernel.GetCurrentProcess()->handle_table.Get<ResourceLimit>(resource_limit_handle);
    R_UNLESS(resource_limit, ResultInvalidHandle);

    for (u32 i = 0; i < name_count; ++i) {
        const u32 name = memory.Read32(names + i * sizeof(u32));
        const s64 value = resource_limit->GetCurrentValue(static_cast<ResourceLimitType>(name));
        memory.Write64(values + i * sizeof(u64), value);
    }

    return ResultSuccess;
}

/// Get resource limit max values
Result SVC::GetResourceLimitLimitValues(VAddr values, Handle resource_limit_handle, VAddr names,
                                        u32 name_count) {
    LOG_TRACE(Kernel_SVC, "called resource_limit={:08X}, names={:08X}, name_count={}",
              resource_limit_handle, names, name_count);

    const auto resource_limit =
        kernel.GetCurrentProcess()->handle_table.Get<ResourceLimit>(resource_limit_handle);
    R_UNLESS(resource_limit, ResultInvalidHandle);

    for (u32 i = 0; i < name_count; ++i) {
        const auto name = static_cast<ResourceLimitType>(memory.Read32(names + i * sizeof(u32)));
        R_UNLESS(name < ResourceLimitType::Max, ResultInvalidEnumValue);
        const s64 value = resource_limit->GetLimitValue(name);
        memory.Write64(values + i * sizeof(u64), value);
    }

    return ResultSuccess;
}

Result SVC::SetResourceLimitLimitValues(Handle res_limit, VAddr names, VAddr resource_list,
                                        u32 name_count) {
    LOG_TRACE(Kernel_SVC, "called resource_limit={:08X}, names={:08X}, name_count={}", res_limit,
              names, name_count);

    const auto resource_limit =
        kernel.GetCurrentProcess()->handle_table.Get<ResourceLimit>(res_limit);
    R_UNLESS(resource_limit, ResultInvalidHandle);

    for (u32 i = 0; i < name_count; ++i) {
        const auto name = static_cast<ResourceLimitType>(memory.Read32(names + i * sizeof(u32)));
        R_UNLESS(name < ResourceLimitType::Max, ResultInvalidEnumValue);

        const s64 value = memory.Read64(resource_list + i * sizeof(u64));
        const s32 value_high = value >> 32;
        R_UNLESS(value_high >= 0, ResultOutOfRangeKernel);

        if (name == ResourceLimitType::Commit && value_high != 0) {
            auto& config_mem = kernel.GetConfigMemHandler().GetConfigMem();
            config_mem.app_mem_alloc = value_high;
        }
        resource_limit->SetLimitValue(name, static_cast<s32>(value));
    }

    return ResultSuccess;
}

/// Creates a new thread
Result SVC::CreateThread(Handle* out_handle, u32 entry_point, u32 arg, VAddr stack_top,
                         u32 priority, s32 processor_id) {
    R_UNLESS(priority <= ThreadPrioLowest, ResultOutOfRange);

    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    const u32 max_priority = resource_limit->GetLimitValue(ResourceLimitType::Priority);
    const bool priority_allowed =
        CanCreateThreadWithPriority(max_priority, priority, current_process->no_thread_restrictions);
    if (!priority_allowed && current_process->codeset &&
        Service::PLGLDR::IsLevel256SupportedTitle(current_process->codeset->program_id)) {
        const auto current_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
        LOG_WARNING(Kernel_SVC,
                    "Level256 CreateThread priority denied: pid={} thread={} pc=0x{:08X} "
                    "svc=0x08 result=0x{:08X} entry=0x{:08X} requested_priority={} "
                    "priority_limit={} no_thread_restrictions={}",
                    current_process->process_id,
                    current_thread ? current_thread->GetThreadId() : 0, GetReg(15),
                    ResultNotAuthorized.raw, entry_point, priority, max_priority,
                    current_process->no_thread_restrictions);
    }
    R_UNLESS(priority_allowed, ResultNotAuthorized);

    if (processor_id == ThreadProcessorIdDefault) {
        // Set the target CPU to the one specified in the process' exheader.
        processor_id = current_process->ideal_processor;
        ASSERT(processor_id != ThreadProcessorIdDefault);
    }

    switch (processor_id) {
    case ThreadProcessorId0:
        break;
    case ThreadProcessorIdAll:
        LOG_INFO(Kernel_SVC,
                 "Newly created thread is allowed to be run in any Core, for now run in core 0.");
        processor_id = ThreadProcessorId0;
        break;
    case ThreadProcessorId1:
    case ThreadProcessorId2:
    case ThreadProcessorId3:
        // TODO: Check and log for: When processorid==0x2 and the process is not a BASE mem-region
        // process, exheader kernel-flags bitmask 0x2000 must be set (otherwise error 0xD9001BEA is
        // returned). When processorid==0x3 and the process is not a BASE mem-region process, error
        // 0xD9001BEA is returned. These are the only restriction checks done by the kernel for
        // processorid. If this is implemented, make sure to check process->no_thread_restrictions.
        break;
    default:
        return ResultOutOfRange;
        break;
    }

    // Update thread count in resource limit.
    if (!resource_limit->Reserve(ResourceLimitType::Thread, 1)) {
        return Result(ErrCodes::OutOfThreads, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create thread.
    const std::string name = fmt::format("{}-{:08X}", current_process->codeset->name, entry_point);
    CASCADE_RESULT(std::shared_ptr<Thread> thread,
                   kernel.CreateThread(name, entry_point, priority, arg, processor_id, stack_top,
                                       current_process));

    thread->context.fpscr =
        FPSCR_DEFAULT_NAN | FPSCR_FLUSH_TO_ZERO | FPSCR_ROUND_TOZERO; // 0x03C00000

    system.PrepareReschedule();

    LOG_TRACE(Kernel_SVC,
              "called entrypoint=0x{:08X} ({}), arg=0x{:08X}, stacktop=0x{:08X}, "
              "threadpriority=0x{:08X}, processorid=0x{:08X}",
              entry_point, name, arg, stack_top, priority, processor_id);

    const Result result = current_process->handle_table.Create(out_handle, std::move(thread));
    if (result.IsSuccess() && entry_point == LEVEL256_WORKER_THREAD_ENTRY &&
        current_process->codeset) {
        const auto plugin_loader = Service::PLGLDR::GetService(system);
        if (plugin_loader) {
            auto& context = plugin_loader->GetPluginLoaderContext();
            if (ShouldStartLevel256OpenProcessTrace(
                    Settings::values.level256_compatibility_enabled.GetValue(),
                    context.level256_official_plugin_hash_matches,
                    current_process->codeset->program_id, current_process->process_id,
                    context.plugin_process_id, context.plugin_loaded, context.plugin_code_begin,
                    context.plugin_code_end, entry_point,
                    context.level256_open_process_trace_started)) {
                const auto created_thread = current_process->handle_table.Get<Thread>(*out_handle);
                if (created_thread && created_thread->processor_id >= 0 &&
                    static_cast<u32>(created_thread->processor_id) < system.GetNumCores()) {
                    context.level256_open_process_trace_started = true;
                    // In Level256 mode, the focused worker trace replaces the earlier
                    // GetSystemInfo instruction probe for this process.
                    system.GetCore(static_cast<u32>(created_thread->processor_id))
                        .StartPluginInstructionTrace(
                            current_process->process_id, created_thread->GetThreadId(),
                            context.plugin_code_begin, context.plugin_code_end,
                            context.plugin_bootloader_begin, context.plugin_bootloader_end,
                            LEVEL256_OPEN_PROCESS_TRACE_LIMIT, true, true);
                    LOG_INFO(Kernel_SVC,
                             "Level256 OpenProcess ARM trace armed: pid={} thread={} "
                             "entry=0x{:08X} core={} instruction_limit={}",
                             current_process->process_id, created_thread->GetThreadId(),
                             entry_point, created_thread->processor_id,
                             LEVEL256_OPEN_PROCESS_TRACE_LIMIT);
                }
            }
        }
    }
    return result;
}

/// Called when a thread exits
void SVC::ExitThread() {
    LOG_TRACE(Kernel_SVC, "called, pc=0x{:08X}", system.GetRunningCore().GetPC());

    kernel.GetCurrentThreadManager().ExitCurrentThread();
    system.PrepareReschedule();
}

/// Gets the priority for the specified thread
Result SVC::GetThreadPriority(u32* priority, Handle handle) {
    const std::shared_ptr<Thread> thread =
        kernel.GetCurrentProcess()->handle_table.Get<Thread>(handle);
    R_UNLESS(thread, ResultInvalidHandle);

    *priority = thread->GetPriority();
    return ResultSuccess;
}

/// Sets the priority for the specified thread
Result SVC::SetThreadPriority(Handle handle, u32 priority) {
    R_UNLESS(priority <= ThreadPrioLowest, ResultOutOfRange);

    const auto thread = kernel.GetCurrentProcess()->handle_table.Get<Thread>(handle);
    R_UNLESS(thread, ResultInvalidHandle);

    // Note: The kernel uses the current process's resource limit instead of
    // the one from the thread owner's resource limit.
    const auto& resource_limit = kernel.GetCurrentProcess()->resource_limit;
    const u32 max_priority = resource_limit->GetLimitValue(ResourceLimitType::Priority);
    R_UNLESS(max_priority <= priority, ResultNotAuthorized);

    thread->SetPriority(priority);
    thread->UpdatePriority();

    // Update the mutexes that this thread is waiting for
    for (auto& mutex : thread->pending_mutexes) {
        mutex->UpdatePriority();
    }

    system.PrepareReschedule();
    return ResultSuccess;
}

/// Create a mutex
Result SVC::CreateMutex(Handle* out_handle, u32 initial_locked) {
    // Update mutex count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::Mutex, 1)) {
        return Result(ErrCodes::OutOfMutexes, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create mutex.
    const auto mutex = kernel.CreateMutex(initial_locked != 0);
    mutex->name = fmt::format("mutex-{:08x}", system.GetRunningCore().GetReg(14));
    mutex->resource_limit = resource_limit;
    return current_process->handle_table.Create(out_handle, std::move(mutex));
}

/// Release a mutex
Result SVC::ReleaseMutex(Handle handle) {
    LOG_TRACE(Kernel_SVC, "called handle=0x{:08X}", handle);

    std::shared_ptr<Mutex> mutex = kernel.GetCurrentProcess()->handle_table.Get<Mutex>(handle);
    R_UNLESS(mutex, ResultInvalidHandle);

    return mutex->Release(kernel.GetCurrentThreadManager().GetCurrentThread());
}

/// Get the ID of the specified process
Result SVC::GetProcessId(u32* process_id, Handle process_handle) {
    LOG_TRACE(Kernel_SVC, "called process=0x{:08X}", process_handle);

    const std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    *process_id = process->process_id;
    return ResultSuccess;
}

/// Get the ID of the process that owns the specified thread
Result SVC::GetProcessIdOfThread(u32* process_id, Handle thread_handle) {
    LOG_TRACE(Kernel_SVC, "called thread=0x{:08X}", thread_handle);

    const std::shared_ptr<Thread> thread =
        kernel.GetCurrentProcess()->handle_table.Get<Thread>(thread_handle);
    R_UNLESS(thread, ResultInvalidHandle);

    const std::shared_ptr<Process> process = thread->owner_process.lock();
    ASSERT_MSG(process != nullptr, "Invalid parent process for thread={:#010X}", thread_handle);

    *process_id = process->process_id;
    return ResultSuccess;
}

/// Get the ID for the specified thread.
Result SVC::GetThreadId(u32* thread_id, Handle handle) {
    LOG_TRACE(Kernel_SVC, "called thread=0x{:08X}", handle);

    const std::shared_ptr<Thread> thread =
        kernel.GetCurrentProcess()->handle_table.Get<Thread>(handle);
    R_UNLESS(thread, ResultInvalidHandle);

    *thread_id = thread->GetThreadId();
    return ResultSuccess;
}

/// Creates a semaphore
Result SVC::CreateSemaphore(Handle* out_handle, s32 initial_count, s32 max_count) {
    // Update semaphore count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::Semaphore, 1)) {
        return Result(ErrCodes::OutOfSemaphores, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create semaphore
    CASCADE_RESULT(std::shared_ptr<Semaphore> semaphore,
                   kernel.CreateSemaphore(initial_count, max_count));
    semaphore->name = fmt::format("semaphore-{:08x}", system.GetRunningCore().GetReg(14));
    semaphore->resource_limit = resource_limit;
    return current_process->handle_table.Create(out_handle, std::move(semaphore));
}

/// Releases a certain number of slots in a semaphore
Result SVC::ReleaseSemaphore(s32* count, Handle handle, s32 release_count) {
    LOG_TRACE(Kernel_SVC, "called release_count={}, handle=0x{:08X}", release_count, handle);

    std::shared_ptr<Semaphore> semaphore =
        kernel.GetCurrentProcess()->handle_table.Get<Semaphore>(handle);
    R_UNLESS(semaphore, ResultInvalidHandle);

    return semaphore->Release(count, release_count);
}

/// Sets the kernel state
Result SVC::KernelSetState(u32 kernel_state, u32 varg1, u32 varg2) {
    switch (static_cast<KernelState>(kernel_state)) {
    // This triggers a hardware reboot on real console, since this doesn't make sense
    // on emulator, we shutdown instead.
    case KernelState::KERNEL_STATE_REBOOT:
        system.RequestShutdown();
        break;

    // Citra specific states.
    case KernelState::KERNEL_STATE_CITRA_EMULATION_SPEED: {
        u16 new_value = static_cast<u16>(varg1);
        if (new_value == 0xFFFF) {
            Settings::is_temporary_frame_limit = false;
            Settings::temporary_frame_limit = 0;
        } else {
            Settings::is_temporary_frame_limit = true;
            Settings::temporary_frame_limit = static_cast<double>(new_value);
        }
    } break;
    default:
        LOG_ERROR(Kernel_SVC, "Unknown KernelSetState state={} varg1={} varg2={}", kernel_state,
                  varg1, varg2);
    }
    return ResultSuccess;
}

/// Query process memory
Result SVC::QueryProcessMemory(MemoryInfo* memory_info, PageInfo* page_info, Handle process_handle,
                               u32 addr) {
    std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    auto vma = process->vm_manager.FindVMA(addr);
    R_UNLESS(vma != process->vm_manager.vma_map.end(), ResultInvalidAddress);

    auto permissions = vma->second.permissions;
    auto state = vma->second.meminfo_state;

    // Query(Process)Memory merges vma with neighbours when they share the same state and
    // permissions, regardless of their physical mapping.

    auto mismatch = [permissions, state](const std::pair<VAddr, Kernel::VirtualMemoryArea>& v) {
        return v.second.permissions != permissions || v.second.meminfo_state != state;
    };

    std::reverse_iterator rvma(vma);

    auto lower = std::find_if(rvma, process->vm_manager.vma_map.crend(), mismatch);
    --lower;
    auto upper = std::find_if(vma, process->vm_manager.vma_map.cend(), mismatch);
    --upper;

    memory_info->base_address = lower->second.base;
    memory_info->permission = static_cast<u32>(permissions);
    memory_info->size = upper->second.base + upper->second.size - lower->second.base;
    memory_info->state = static_cast<u32>(state);

    page_info->flags = 0;
    LOG_TRACE(Kernel_SVC, "called process=0x{:08X} addr=0x{:08X}", process_handle, addr);
    return ResultSuccess;
}

/// Query memory
Result SVC::QueryMemory(MemoryInfo* memory_info, PageInfo* page_info, u32 addr) {
    return QueryProcessMemory(memory_info, page_info, CurrentProcess, addr);
}

/// Create an event
Result SVC::CreateEvent(Handle* out_handle, u32 reset_type) {
    // Update event count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::Event, 1)) {
        return Result(ErrCodes::OutOfEvents, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create event.
    const auto name = fmt::format("event-{:08x}", system.GetRunningCore().GetReg(14));
    const auto event = kernel.CreateEvent(static_cast<ResetType>(reset_type), name);
    event->resource_limit = resource_limit;
    return current_process->handle_table.Create(out_handle, std::move(event));
}

/// Duplicates a kernel handle
Result SVC::DuplicateHandle(Handle* out, Handle handle) {
    return kernel.GetCurrentProcess()->handle_table.Duplicate(out, handle);
}

/// Signals an event
Result SVC::SignalEvent(Handle handle) {
    LOG_TRACE(Kernel_SVC, "called event=0x{:08X}", handle);

    const auto process = kernel.GetCurrentProcess();
    const auto* current_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    std::shared_ptr<Event> evt = process->handle_table.Get<Event>(handle);

    if (evt && current_thread && evt->GetName() == "DSP_DSP::semaphore_event") {
        process->RememberDspSemaphoreSignal(evt->GetObjectId(), current_thread->thread_id);
    }

    // Compatibility-only: permit exactly one missed signal on the *retired*
    // DSP semaphore from its prior signaler, within five ms of another
    // thread closing it. Unrelated invalid handles still return an error.
    if (!evt && handle == 0 && current_thread &&
        process->ConsumeDspSemaphoreSignalGrace(current_thread->thread_id,
                                                system.CoreTiming().GetGlobalTicks())) {
        LOG_WARNING(Kernel_SVC,
                    "[DSP-SEM-GRACE] Consumed retired semaphore signal: pid={} "
                    "thread={} pc=0x{:08X} -> ResultSuccess",
                    process->process_id, current_thread->thread_id, GetReg(15));
        return ResultSuccess;
    }
    R_UNLESS(evt, ResultInvalidHandle);

    evt->Signal();
    return ResultSuccess;
}

/// Clears an event
Result SVC::ClearEvent(Handle handle) {
    LOG_TRACE(Kernel_SVC, "called event=0x{:08X}", handle);

    std::shared_ptr<Event> evt = kernel.GetCurrentProcess()->handle_table.Get<Event>(handle);
    R_UNLESS(evt, ResultInvalidHandle);

    evt->Clear();
    return ResultSuccess;
}

/// Creates a timer
Result SVC::CreateTimer(Handle* out_handle, u32 reset_type) {
    // Update timer count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::Timer, 1)) {
        return Result(ErrCodes::OutOfTimers, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // Create timer.
    const auto name = fmt::format("timer-{:08x}", system.GetRunningCore().GetReg(14));
    const auto timer = kernel.CreateTimer(static_cast<ResetType>(reset_type), name);
    timer->resource_limit = resource_limit;
    return current_process->handle_table.Create(out_handle, std::move(timer));
}

/// Clears a timer
Result SVC::ClearTimer(Handle handle) {
    LOG_TRACE(Kernel_SVC, "called timer=0x{:08X}", handle);

    std::shared_ptr<Timer> timer = kernel.GetCurrentProcess()->handle_table.Get<Timer>(handle);
    R_UNLESS(timer, ResultInvalidHandle);

    timer->Clear();
    return ResultSuccess;
}

/// Starts a timer
Result SVC::SetTimer(Handle handle, s64 initial, s64 interval) {
    LOG_TRACE(Kernel_SVC, "called timer=0x{:08X}", handle);

    R_UNLESS(initial >= 0 && interval >= 0, ResultOutOfRangeKernel);

    std::shared_ptr<Timer> timer = kernel.GetCurrentProcess()->handle_table.Get<Timer>(handle);
    R_UNLESS(timer, ResultInvalidHandle);

    timer->Set(initial, interval);

    return ResultSuccess;
}

/// Cancels a timer
Result SVC::CancelTimer(Handle handle) {
    LOG_TRACE(Kernel_SVC, "called timer=0x{:08X}", handle);

    std::shared_ptr<Timer> timer = kernel.GetCurrentProcess()->handle_table.Get<Timer>(handle);
    R_UNLESS(timer, ResultInvalidHandle);

    timer->Cancel();
    return ResultSuccess;
}

/// Sleep the current thread
void SVC::SleepThread(s64 nanoseconds) {
    LOG_TRACE(Kernel_SVC, "called nanoseconds={}", nanoseconds);

    ThreadManager& thread_manager = kernel.GetCurrentThreadManager();

    // Don't attempt to yield execution if there are no available threads to run,
    // this way we avoid a useless reschedule to the idle thread.
    if (nanoseconds == 0 && !thread_manager.HaveReadyThreads()) {
        return;
    }

    // Sleep current thread and check for next thread to schedule
    thread_manager.WaitCurrentThread_Sleep();

    // Create an event to wake the thread up after the specified nanosecond delay has passed
    thread_manager.GetCurrentThread()->WakeAfterDelay(nanoseconds);

    system.PrepareReschedule();
}

/// This returns the total CPU ticks elapsed since the CPU was powered-on
s64 SVC::GetSystemTick() {
    // TODO: Use globalTicks here?
    return system.GetRunningCore().GetTimer().GetTicks();
}

// Returns information of the specified handle
Result SVC::GetHandleInfo(s64* out, Handle handle, u32 type) {
    std::shared_ptr<Object> object = kernel.GetCurrentProcess()->handle_table.GetGeneric(handle);
    R_UNLESS(object, ResultInvalidHandle);

    // Not initialized in real kernel, but we don't want to leak memory.
    s64 value = 0;
    std::shared_ptr<Process> process;

    switch (static_cast<HandleInfoType>(type)) {
    case HandleInfoType::KPROCESS_ELAPSED_TICKS:
        process = DynamicObjectCast<Process>(object);
        if (process) {
            value = process->creation_time_ticks;
        }
        break;
    case HandleInfoType::REFERENCE_COUNT:
        // This is the closest approximation we can get without a full KObject impl.
        value = object.use_count() - 1;
        break;

    // These values are stubbed in real kernel, they do nothing.
    case HandleInfoType::STUBBED_1:
    case HandleInfoType::STUBBED_2:
        break;
    default:
        return ResultInvalidEnumValue;
    }
    *out = value;
    return ResultSuccess;
}

/// Creates a memory block at the specified address with the specified permissions and size
Result SVC::CreateMemoryBlock(Handle* out_handle, u32 addr, u32 size, u32 my_permission,
                              u32 other_permission) {
    R_UNLESS(size % Memory::CITRA_PAGE_SIZE == 0, ResultMisalignedSize);

    std::shared_ptr<SharedMemory> shared_memory = nullptr;

    auto VerifyPermissions = [](MemoryPermission permission) {
        // SharedMemory blocks can not be created with execute permissions
        switch (permission) {
        case MemoryPermission::None:
        case MemoryPermission::Read:
        case MemoryPermission::Write:
        case MemoryPermission::ReadWrite:
        case MemoryPermission::DontCare:
            return true;
        default:
            return false;
        }
    };

    R_UNLESS(VerifyPermissions(static_cast<MemoryPermission>(my_permission)),
             ResultInvalidCombination);
    R_UNLESS(VerifyPermissions(static_cast<MemoryPermission>(other_permission)),
             ResultInvalidCombination);

    // TODO(Subv): Processes with memory type APPLICATION are not allowed
    // to create memory blocks with addr = 0, any attempts to do so
    // should return error 0xD92007EA.
    if ((addr < Memory::PROCESS_IMAGE_VADDR || addr + size > Memory::SHARED_MEMORY_VADDR_END) &&
        addr != 0) {
        return ResultInvalidAddress;
    }

    // Update shared memory count in resource limit.
    const auto current_process = kernel.GetCurrentProcess();
    const auto& resource_limit = current_process->resource_limit;
    if (!resource_limit->Reserve(ResourceLimitType::SharedMemory, 1)) {
        return Result(ErrCodes::OutOfSharedMems, ErrorModule::OS, ErrorSummary::OutOfResource,
                      ErrorLevel::Status);
    }

    // When trying to create a memory block with address = 0,
    // if the process has the Shared Device Memory flag in the exheader,
    // then we have to allocate from the same region as the caller process instead of the BASE
    // region.
    MemoryRegion region = MemoryRegion::BASE;
    if (addr == 0 && current_process->flags.shared_device_mem) {
        region = current_process->flags.memory_region;
    }

    CASCADE_RESULT(shared_memory,
                   kernel.CreateSharedMemory(
                       current_process, size, static_cast<MemoryPermission>(my_permission),
                       static_cast<MemoryPermission>(other_permission), addr, region));
    return current_process->handle_table.Create(out_handle, std::move(shared_memory));
}

Result SVC::CreatePort(Handle* server_port, Handle* client_port, VAddr name_address,
                       u32 max_sessions) {
    // TODO(Subv): Implement named ports.
    ASSERT_MSG(name_address == 0, "Named ports are currently unimplemented");
    LOG_TRACE(Kernel_SVC, "called max_sessions={}", max_sessions);

    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();

    auto [server, client] = kernel.CreatePortPair(max_sessions);
    R_TRY(current_process->handle_table.Create(client_port, std::move(client)));
    // Note: The 3DS kernel also leaks the client port handle if the server port handle fails to be
    // created.
    return current_process->handle_table.Create(server_port, std::move(server));
}

Result SVC::CreateSessionToPort(Handle* out_client_session, Handle client_port_handle) {
    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();
    std::shared_ptr<ClientPort> client_port =
        current_process->handle_table.Get<ClientPort>(client_port_handle);
    R_UNLESS(client_port, ResultInvalidHandle);

    std::shared_ptr<ClientSession> session;
    R_TRY(client_port->Connect(std::addressof(session)));

    return current_process->handle_table.Create(out_client_session, std::move(session));
}

Result SVC::CreateSession(Handle* server_session, Handle* client_session) {
    auto [server, client] = kernel.CreateSessionPair();
    LOG_TRACE(Kernel_SVC, "called");

    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();
    R_TRY(current_process->handle_table.Create(server_session, std::move(server)));

    return current_process->handle_table.Create(client_session, std::move(client));
}

Result SVC::AcceptSession(Handle* out_server_session, Handle server_port_handle) {
    std::shared_ptr<Process> current_process = kernel.GetCurrentProcess();
    std::shared_ptr<ServerPort> server_port =
        current_process->handle_table.Get<ServerPort>(server_port_handle);
    R_UNLESS(server_port, ResultInvalidHandle);

    std::shared_ptr<ServerSession> session;
    R_TRY(server_port->Accept(std::addressof(session)));

    return current_process->handle_table.Create(out_server_session, std::move(session));
}

static void CopyStringPart(char* out, const char* in, std::size_t offset, std::size_t max_length) {
    std::size_t str_size = strlen(in);
    if (offset < str_size) {
        strncpy(out, in + offset, max_length - 1);
        out[max_length - 1] = '\0';
    } else {
        out[0] = '\0';
    }
}

Result SVC::GetSystemInfo(s64* out, u32 type, s32 param) {
    LOG_TRACE(Kernel_SVC, "called type={} param={}", type, param);

    switch ((SystemInfoType)type) {
    case SystemInfoType::REGION_MEMORY_USAGE:
        switch ((SystemInfoMemUsageRegion)param) {
        case SystemInfoMemUsageRegion::ALL:
            *out = kernel.GetMemoryRegion(MemoryRegion::APPLICATION)->used +
                   kernel.GetMemoryRegion(MemoryRegion::SYSTEM)->used +
                   kernel.GetMemoryRegion(MemoryRegion::BASE)->used;
            break;
        case SystemInfoMemUsageRegion::APPLICATION:
            *out = kernel.GetMemoryRegion(MemoryRegion::APPLICATION)->used;
            break;
        case SystemInfoMemUsageRegion::SYSTEM:
            *out = kernel.GetMemoryRegion(MemoryRegion::SYSTEM)->used;
            break;
        case SystemInfoMemUsageRegion::BASE:
            *out = kernel.GetMemoryRegion(MemoryRegion::BASE)->used;
            break;
        default:
            LOG_ERROR(Kernel_SVC, "unknown GetSystemInfo type=0 region: param={}", param);
            *out = 0;
            break;
        }
        break;
    case SystemInfoType::KERNEL_ALLOCATED_PAGES:
        LOG_ERROR(Kernel_SVC, "unimplemented GetSystemInfo type=2 param={}", param);
        *out = 0;
        break;
    case SystemInfoType::KERNEL_SPAWNED_PIDS:
        *out = 5;
        break;
    case SystemInfoType::LUMA_CFW_INFO:
        switch ((SystemInfoLumaCFWInformation)param) {
        case SystemInfoLumaCFWInformation::REAL_APP_REGION_SIZE:
            *out = kernel.GetMemoryRegion(MemoryRegion::APPLICATION)->size;
            break;
        case SystemInfoLumaCFWInformation::IS_N3DS:
            *out = Settings::values.is_new_3ds ? 1 : 0;
            break;
        default:
            LOG_ERROR(Kernel_SVC, "unknown GetSystemInfo type=0x10000 region: param={}", param);
            *out = 0;
            break;
        }
        break;
    case SystemInfoType::NEW_3DS_INFO:
        // The actual subtypes are not implemented, homebrew just check
        // this doesn't return an error in n3ds to know the system type
        LOG_ERROR(Kernel_SVC, "unimplemented GetSystemInfo type=65537 param={}", param);
        *out = 0;
        return (system.GetNumCores() == 4) ? ResultSuccess : ResultInvalidEnumValue;
    case SystemInfoType::CITRA_INFORMATION:
        switch ((SystemInfoEmulatorInformation)param) {
        case SystemInfoEmulatorInformation::EMULATOR_ID:
            *out = static_cast<s64>(EmulatorIDs::AZAHAR_EMULATOR);
            break;
        case SystemInfoEmulatorInformation::HOST_TICK:
            *out = static_cast<s64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
            break;
        case SystemInfoEmulatorInformation::EMULATION_SPEED:
            *out = static_cast<s64>(Settings::values.frame_limit.GetValue());
            break;
        case SystemInfoEmulatorInformation::BUILD_NAME:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_name, 0, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_VERSION:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_version, 0, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_PLATFORM: {
#if defined(_WIN32)
            *out = static_cast<s64>(SystemInfoCitraPlatform::PLATFORM_WINDOWS);
#elif defined(ANDROID)
            *out = static_cast<s64>(SystemInfoCitraPlatform::PLATFORM_ANDROID);
#elif defined(__linux__)
            *out = static_cast<s64>(SystemInfoCitraPlatform::PLATFORM_LINUX);
#elif defined(__APPLE__)
            *out = static_cast<s64>(SystemInfoCitraPlatform::PLATFORM_APPLE);
#else
            *out = static_cast<s64>(SystemInfoCitraPlatform::PLATFORM_UNKNOWN);
#endif
            break;
        }
        case SystemInfoEmulatorInformation::BUILD_DATE_PART1:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_date,
                           (sizeof(s64) - 1) * 0, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_DATE_PART2:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_date,
                           (sizeof(s64) - 1) * 1, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_DATE_PART3:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_date,
                           (sizeof(s64) - 1) * 2, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_DATE_PART4:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_build_date,
                           (sizeof(s64) - 1) * 3, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_GIT_BRANCH_PART1:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_scm_branch,
                           (sizeof(s64) - 1) * 0, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_GIT_BRANCH_PART2:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_scm_branch,
                           (sizeof(s64) - 1) * 1, sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_GIT_DESCRIPTION_PART1:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_scm_desc, (sizeof(s64) - 1) * 0,
                           sizeof(s64));
            break;
        case SystemInfoEmulatorInformation::BUILD_GIT_DESCRIPTION_PART2:
            CopyStringPart(reinterpret_cast<char*>(out), Common::g_scm_desc, (sizeof(s64) - 1) * 1,
                           sizeof(s64));
            break;
        default:
            LOG_ERROR(Kernel_SVC, "unknown GetSystemInfo citra info param={}", param);
            *out = 0;
            break;
        }
        break;
    default:
        LOG_ERROR(Kernel_SVC, "unknown GetSystemInfo type={} param={}", type, param);
        *out = 0;
        break;
    }

    // This function never returns an error, even if invalid parameters were passed.
    return ResultSuccess;
}

Result SVC::GetProcessInfo(s64* out, Handle process_handle, u32 type) {
    LOG_TRACE(Kernel_SVC, "called process=0x{:08X} type={}", process_handle, type);

    std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    switch (static_cast<ProcessInfoType>(type)) {
    case ProcessInfoType::PRIVATE_AND_SHARED_USED_MEMORY:
    case ProcessInfoType::PRIVATE_SHARED_SUPERVISOR_HANDLE_USED_MEMORY:
        // TODO(yuriks): Type 0 returns a slightly higher number than type 2, but I'm not sure
        // what's the difference between them.
        *out = process->memory_used;
        if (*out % Memory::CITRA_PAGE_SIZE != 0) {
            LOG_ERROR(Kernel_SVC, "called, memory size not page-aligned");
            return ResultMisalignedSize;
        }
        break;
    case ProcessInfoType::SUPERVISOR_AND_HANDLE_USED_MEMORY:
    case ProcessInfoType::SUPERVISOR_AND_HANDLE_USED_MEMORY2:
    case ProcessInfoType::USED_HANDLE_COUNT:
    case ProcessInfoType::HIGHEST_HANDLE_COUNT:
    case ProcessInfoType::KPROCESS_0X234:
    case ProcessInfoType::THREAD_COUNT:
    case ProcessInfoType::MAX_THREAD_AMOUNT:
        // These are valid, but not implemented yet
        LOG_ERROR(Kernel_SVC, "unimplemented GetProcessInfo type={}", type);
        break;
    case ProcessInfoType::LINEAR_BASE_ADDR_OFFSET:
        *out = Memory::FCRAM_PADDR - process->GetLinearHeapAreaAddress();
        break;
    case ProcessInfoType::QTM_MEMORY_BLOCK_CONVERSION_OFFSET:
    case ProcessInfoType::QTM_MEMORY_ADDRESS:
    case ProcessInfoType::QTM_MEMORY_SIZE:
        // These return a different error value than higher invalid values
        LOG_ERROR(Kernel_SVC, "unknown GetProcessInfo type={}", type);
        return ResultNotImplemented;
    // Here start the custom ones, taken from Luma3DS for 3GX support
    case ProcessInfoType::LUMA_CUSTOM_PROCESS_NAME:
        // Get process name
        std::strncpy(reinterpret_cast<char*>(out), process->codeset->GetName().c_str(), 8);
        break;
    case ProcessInfoType::LUMA_CUSTOM_PROCESS_TITLE_ID:
        // Get process TID
        *out = process->codeset->program_id;
        break;
    case ProcessInfoType::LUMA_CUSTOM_TEXT_SIZE:
        *out = process->codeset->CodeSegment().size;
        break;
    case ProcessInfoType::LUMA_CUSTOM_RODATA_SIZE:
        *out = process->codeset->RODataSegment().size;
        break;
    case ProcessInfoType::LUMA_CUSTOM_DATA_SIZE:
        *out = process->codeset->DataSegment().size;
        break;
    case ProcessInfoType::LUMA_CUSTOM_TEXT_ADDR:
        *out = process->codeset->CodeSegment().addr;
        break;
    case ProcessInfoType::LUMA_CUSTOM_RODATA_ADDR:
        *out = process->codeset->RODataSegment().addr;
        break;
    case ProcessInfoType::LUMA_CUSTOM_DATA_ADDR:
        *out = process->codeset->DataSegment().addr;
        break;

    default:
        LOG_ERROR(Kernel_SVC, "unknown GetProcessInfo type={}", type);
        return ResultInvalidEnumValue;
    }

    return ResultSuccess;
}

Result SVC::GetThreadInfo(s64* out, Handle thread_handle, u32 type) {
    LOG_TRACE(Kernel_SVC, "called thread=0x{:08X} type={}", thread_handle, type);

    std::shared_ptr<Thread> thread =
        kernel.GetCurrentProcess()->handle_table.Get<Thread>(thread_handle);
    R_UNLESS(thread, ResultInvalidHandle);

    switch (type) {
    case 0x10000:
        *out = static_cast<s64>(thread->GetTLSAddress());
        break;
    default:
        LOG_ERROR(Kernel_SVC, "unknown GetThreadInfo type={}", type);
        return ResultInvalidEnumValue;
    }

    return ResultSuccess;
}

Result SVC::GetProcessList(s32* process_count, VAddr out_process_array,
                           s32 out_process_array_count) {
    R_UNLESS(memory.IsValidVirtualAddress(*kernel.GetCurrentProcess(), out_process_array),
             ResultInvalidPointer);

    s32 written = 0;
    for (const auto& process : kernel.GetProcessList()) {
        if (written >= out_process_array_count) {
            break;
        }
        if (process) {
            memory.Write32(out_process_array + written++ * sizeof(u32), process->process_id);
        }
    }
    *process_count = written;
    return ResultSuccess;
}

Result SVC::InvalidateInstructionCacheRange(u32 addr, u32 size) {
    for (size_t i = 0; i < system.GetNumCores(); i++) {
        system.GetCore(i).InvalidateCacheRange(addr, size);
    }
    return ResultSuccess;
}

void SVC::FlushEntireDataCache() {
    // Guest data reads and writes are coherent in Azahar's unified memory model. Keep this as a
    // distinct Luma extension entry rather than conflating it with Dynarmic I-cache invalidation.
}

Result SVC::InvalidateEntireInstructionCache() {
    for (size_t i = 0; i < system.GetNumCores(); i++) {
        system.GetCore(i).ClearInstructionCache();
    }
    return ResultSuccess;
}

u32 SVC::ConvertVaToPa(u32 addr) {
    const auto process = kernel.GetCurrentProcess();
    auto vma = process->vm_manager.FindVMA(addr);
    if (vma == process->vm_manager.vma_map.end() || vma->second.type != VMAType::BackingMemory) {
        return 0;
    }

    const auto* backing_start = vma->second.backing_memory.GetPtr();
    const auto* host_pointer = backing_start + (addr - vma->second.base);
    const auto fcram_offset = kernel.memory.TryGetFCRAMOffset(host_pointer);
    if (fcram_offset) {
        return *fcram_offset + Memory::FCRAM_PADDR;
    }

    // Identify VRAM by the VMA's actual backing allocation. The virtual address alone is not
    // sufficient because another allocation can be mapped into the same guest range.
    const auto vram_offset = kernel.memory.TryGetVRAMOffset(host_pointer);
    if (vram_offset) {
        return *vram_offset + Memory::VRAM_PADDR;
    }

    {
        const u64 title_id = process->codeset ? process->codeset->program_id : 0;
        const auto thread = kernel.GetCurrentThreadManager().GetCurrentThread();
        const u32 pc = GetReg(15);
        bool level256_target = false;
        if (Settings::values.level256_compatibility_enabled.GetValue() &&
            title_id == LEVEL256_DIAGNOSTIC_TITLE_ID && process->process_id == 16 && thread &&
            thread->GetThreadId() == 31 && pc == 0x070991F8) {
            const auto plugin_loader = Service::PLGLDR::GetService(system);
            if (plugin_loader) {
                const auto& context = plugin_loader->GetPluginLoaderContext();
                level256_target = context.plugin_loaded &&
                                  context.level256_official_plugin_hash_matches &&
                                  context.plugin_process_id == process->process_id &&
                                  pc >= context.plugin_code_begin && pc < context.plugin_code_end;
            }
        }

        if (level256_target) {
            const auto fcram_begin = reinterpret_cast<std::uintptr_t>(
                kernel.memory.GetFCRAMPointer(0));
            const auto fcram_end = fcram_begin + Memory::FCRAM_N3DS_SIZE;
            LOG_WARNING(Kernel_SVC,
                        "Level256 ConvertVaToPa rejected non-FCRAM backing: pid={} title=0x{:016X} "
                        "thread={} pc=0x{:08X} va=0x{:08X} vma_base=0x{:08X} "
                        "vma_size=0x{:X} vma_type={} vma_state={} backing_host=0x{:X} "
                        "computed_host=0x{:X} fcram_begin=0x{:X} fcram_end=0x{:X} "
                        "host_in_fcram=false result=0",
                        process->process_id, title_id, thread->GetThreadId(), pc, addr,
                        vma->second.base, vma->second.size,
                        static_cast<u32>(vma->second.type),
                        static_cast<u32>(vma->second.meminfo_state),
                        reinterpret_cast<std::uintptr_t>(backing_start),
                        reinterpret_cast<std::uintptr_t>(host_pointer), fcram_begin, fcram_end);
        }
        // svcConvertVAToPA returns NULL when no physical address can be obtained.
        return 0;
    }
}

Result SVC::MapProcessMemoryEx(Handle dst_process_handle, u32 dst_address,
                               Handle src_process_handle, u32 src_address, u32 size,
                               MapMemoryExFlag flags, Handle dst_process_handle_backup) {

    // Determine if this is the second version of the svc by checking the value at R0.
    constexpr u32 SVC_VERSION2_MAGIC = 0xFFFFFFF2;
    if (static_cast<u32>(dst_process_handle) == SVC_VERSION2_MAGIC) {
        // Version 2, actual handle is provided in 6th argument
        dst_process_handle = dst_process_handle_backup;
    } else {
        // Version 1, the flags argument is not used
        flags = static_cast<MapMemoryExFlag>(0);
    }

    const bool map_as_private =
        (static_cast<u32>(flags) & static_cast<u32>(MapMemoryExFlag::MAPEXFLAGS_PRIVATE)) != 0;

    std::shared_ptr<Process> dst_process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(dst_process_handle);
    std::shared_ptr<Process> src_process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(src_process_handle);

    R_UNLESS(dst_process && src_process, ResultInvalidHandle);

    if (size & 0xFFF) {
        size = (size & ~0xFFF) + Memory::CITRA_PAGE_SIZE;
    }

    // TODO(PabloMK7) Fix-up this svc.

    // Only linear memory supported
    auto vma = src_process->vm_manager.FindVMA(src_address);
    R_UNLESS(vma != src_process->vm_manager.vma_map.end() &&
                 vma->second.type == VMAType::BackingMemory,
             ResultInvalidAddress);

    const u32 offset = src_address - vma->second.base;
    R_UNLESS(offset + size <= vma->second.size, ResultInvalidAddress);

    auto vma_res = dst_process->vm_manager.MapBackingMemory(
        dst_address,
        memory.GetFCRAMRef(vma->second.backing_memory.GetPtr() + offset -
                           kernel.memory.GetFCRAMPointer(0)),
        size, map_as_private ? MemoryState::Private : MemoryState::Shared);

    if (!vma_res.Succeeded()) {
        return ResultInvalidAddressState;
    }

    dst_process->vm_manager.Reprotect(vma_res.Unwrap(), Kernel::VMAPermission::ReadWriteExecute);
    return ResultSuccess;
}

Result SVC::UnmapProcessMemoryEx(Handle process, u32 dst_address, u32 size) {
    std::shared_ptr<Process> dst_process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process);
    R_UNLESS(dst_process, ResultInvalidHandle);

    if (size & 0xFFF) {
        size = (size & ~0xFFF) + Memory::CITRA_PAGE_SIZE;
    }

    // Only linear memory supported
    auto vma = dst_process->vm_manager.FindVMA(dst_address);
    R_UNLESS(vma != dst_process->vm_manager.vma_map.end() &&
                 vma->second.type == VMAType::BackingMemory,
             ResultInvalidAddress);

    dst_process->vm_manager.UnmapRange(dst_address, size);
    return ResultSuccess;
}

Result SVC::ControlProcess(Handle process_handle, u32 process_OP, u32 varg2, u32 varg3) {
    std::shared_ptr<Process> process =
        kernel.GetCurrentProcess()->handle_table.Get<Process>(process_handle);
    R_UNLESS(process, ResultInvalidHandle);

    switch (static_cast<ControlProcessOP>(process_OP)) {
    case ControlProcessOP::PROCESSOP_SET_MMU_TO_RWX: {
        for (auto it = process->vm_manager.vma_map.cbegin();
             it != process->vm_manager.vma_map.cend(); it++) {
            if (it->second.meminfo_state != MemoryState::Free)
                process->vm_manager.Reprotect(it, Kernel::VMAPermission::ReadWriteExecute);
        }
        return ResultSuccess;
    }
    case ControlProcessOP::PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT: {
        auto plgldr = Service::PLGLDR::GetService(system);
        R_UNLESS(plgldr, ResultNotFound);

        ResultVal<Handle> out = plgldr->GetMemoryChangedHandle(kernel);
        if (out.Failed()) {
            return out.Code();
        }

        memory.Write32(varg2, out.Unwrap());
        return ResultSuccess;
    }
    case ControlProcessOP::PROCESSOP_SCHEDULE_THREADS_WITHOUT_TLS_MAGIC: {
        for (u32 core_id = 0; core_id < system.GetNumCores(); core_id++) {
            const auto thread_list = kernel.GetThreadManager(core_id).GetThreadList();
            for (auto& thread : thread_list) {
                if (thread->owner_process.lock() != process) {
                    continue;
                }
                if (memory.Read32(thread.get()->GetTLSAddress()) == varg3) {
                    continue;
                }
                if (thread.get()->thread_id ==
                    kernel.GetCurrentThreadManager().GetCurrentThread()->thread_id) {
                    continue;
                }
                if (varg2) {
                    thread->SetUnscheduleMode(Kernel::UnscheduleMode::SVC);
                } else {
                    thread->ClearUnscheduleMode(Kernel::UnscheduleMode::SVC);
                }
            }
        }
        return ResultSuccess;
    }
    case ControlProcessOP::PROCESSOP_DISABLE_CREATE_THREAD_RESTRICTIONS: {
        process->no_thread_restrictions = varg2 == 1;
        return ResultSuccess;
    }
    case ControlProcessOP::PROCESSOP_GET_ALL_HANDLES:
    case ControlProcessOP::PROCESSOP_GET_PA_FROM_VA:
    case ControlProcessOP::PROCESSOP_SIGNAL_ON_EXIT:
    case ControlProcessOP::PROCESSOP_SCHEDULE_THREADS:
    default:
        LOG_ERROR(Kernel_SVC, "Unknown ControlProcessOp type={}", process_OP);
        return ResultNotImplemented;
    }
}

// Array of SVC handlers, and the cycles it takes to process them.
// The cycles have been obtained from real hardware using a
// custom svc profiler and doing an average.
// TODO(PabloMK7):
//    1. Use a better profiling technique. The profiler used
//       did not handle context switching well, so cycle
//       counts are not very accurate. Manual adjustments
//       had to be done which may not fully reflect how
//       real hardware works.
//    2. Figure out timings for barely used SVCs. The profile
//       method involved running several commercial applications
//       which do not use all SVCs. A custom homebrew app will
//       need to be used. For now, 1000 cycles are assumed for
//       SVCs that couldn't be profiled.
const std::array<SVC::FunctionDef, 180> SVC::SVC_Table{{
    {0x00, nullptr, "Unknown", 0},
    {0x01, &SVC::Wrap<&SVC::ControlMemory, 0x01>, "ControlMemory", 1000},
    {0x02, &SVC::Wrap<&SVC::QueryMemory, 0x02>, "QueryMemory", 1000},
    {0x03, &SVC::ExitProcess, "ExitProcess", 1000},
    {0x04, nullptr, "GetProcessAffinityMask", 1000},
    {0x05, nullptr, "SetProcessAffinityMask", 1000},
    {0x06, nullptr, "GetProcessIdealProcessor", 1000},
    {0x07, nullptr, "SetProcessIdealProcessor", 1000},
    {0x08, &SVC::Wrap<&SVC::CreateThread, 0x08>, "CreateThread", 5214},
    {0x09, &SVC::ExitThread, "ExitThread", 1000},
    {0x0A, &SVC::Wrap<&SVC::SleepThread, 0x0A>, "SleepThread", 946},
    {0x0B, &SVC::Wrap<&SVC::GetThreadPriority, 0x0B>, "GetThreadPriority", 616},
    {0x0C, &SVC::Wrap<&SVC::SetThreadPriority, 0x0C>, "SetThreadPriority", 1812},
    {0x0D, nullptr, "GetThreadAffinityMask", 1000},
    {0x0E, nullptr, "SetThreadAffinityMask", 1000},
    {0x0F, nullptr, "GetThreadIdealProcessor", 1000},
    {0x10, nullptr, "SetThreadIdealProcessor", 1000},
    {0x11, nullptr, "GetCurrentProcessorNumber", 1000},
    {0x12, nullptr, "Run", 1000},
    {0x13, &SVC::Wrap<&SVC::CreateMutex, 0x13>, "CreateMutex", 1000},
    {0x14, &SVC::Wrap<&SVC::ReleaseMutex, 0x14>, "ReleaseMutex", 1324},
    {0x15, &SVC::Wrap<&SVC::CreateSemaphore, 0x15>, "CreateSemaphore", 1000},
    {0x16, &SVC::Wrap<&SVC::ReleaseSemaphore, 0x16>, "ReleaseSemaphore", 2713},
    {0x17, &SVC::Wrap<&SVC::CreateEvent, 0x17>, "CreateEvent", 4329},
    {0x18, &SVC::Wrap<&SVC::SignalEvent, 0x18>, "SignalEvent", 3285},
    {0x19, &SVC::Wrap<&SVC::ClearEvent, 0x19>, "ClearEvent", 1389},
    {0x1A, &SVC::Wrap<&SVC::CreateTimer, 0x1A>, "CreateTimer", 1000},
    {0x1B, &SVC::Wrap<&SVC::SetTimer, 0x1B>, "SetTimer", 5163},
    {0x1C, &SVC::Wrap<&SVC::CancelTimer, 0x1C>, "CancelTimer", 1000},
    {0x1D, &SVC::Wrap<&SVC::ClearTimer, 0x1D>, "ClearTimer", 1000},
    {0x1E, &SVC::Wrap<&SVC::CreateMemoryBlock, 0x1E>, "CreateMemoryBlock", 1000},
    {0x1F, &SVC::Wrap<&SVC::MapMemoryBlock, 0x1F>, "MapMemoryBlock", 1000},
    {0x20, &SVC::Wrap<&SVC::UnmapMemoryBlock, 0x20>, "UnmapMemoryBlock", 1000},
    {0x21, &SVC::Wrap<&SVC::CreateAddressArbiter, 0x21>, "CreateAddressArbiter", 1000},
    {0x22, &SVC::Wrap<&SVC::ArbitrateAddress, 0x22>, "ArbitrateAddress", 5664},
    {0x23, &SVC::Wrap<&SVC::CloseHandle, 0x23>, "CloseHandle", 2937},
    {0x24, &SVC::Wrap<&SVC::WaitSynchronization1, 0x24>, "WaitSynchronization1", 4005},
    {0x25, &SVC::Wrap<&SVC::WaitSynchronizationN, 0x25>, "WaitSynchronizationN", 6918},
    {0x26, nullptr, "SignalAndWait", 1000},
    {0x27, &SVC::Wrap<&SVC::DuplicateHandle, 0x27>, "DuplicateHandle", 1000},
    {0x28, &SVC::Wrap<&SVC::GetSystemTick, 0x28>, "GetSystemTick", 340},
    {0x29, &SVC::Wrap<&SVC::GetHandleInfo, 0x29>, "GetHandleInfo", 1000},
    {0x2A, &SVC::Wrap<&SVC::GetSystemInfo, 0x2A>, "GetSystemInfo", 1000},
    {0x2B, &SVC::Wrap<&SVC::GetProcessInfo, 0x2B>, "GetProcessInfo", 1510},
    {0x2C, &SVC::Wrap<&SVC::GetThreadInfo, 0x2C>, "GetThreadInfo", 1000},
    {0x2D, &SVC::Wrap<&SVC::ConnectToPort, 0x2D>, "ConnectToPort", 1000},
    {0x2E, nullptr, "SendSyncRequest1", 1000},
    {0x2F, nullptr, "SendSyncRequest2", 1000},
    {0x30, nullptr, "SendSyncRequest3", 1000},
    {0x31, nullptr, "SendSyncRequest4", 1000},
    {0x32, &SVC::Wrap<&SVC::SendSyncRequest, 0x32>, "SendSyncRequest", 5825},
    {0x33, &SVC::Wrap<&SVC::OpenProcess, 0x33>, "OpenProcess", 1000},
    {0x34, &SVC::Wrap<&SVC::OpenThread, 0x34>, "OpenThread", 1000},
    {0x35, &SVC::Wrap<&SVC::GetProcessId, 0x35>, "GetProcessId", 1000},
    {0x36, &SVC::Wrap<&SVC::GetProcessIdOfThread, 0x36>, "GetProcessIdOfThread", 1000},
    {0x37, &SVC::Wrap<&SVC::GetThreadId, 0x37>, "GetThreadId", 677},
    {0x38, &SVC::Wrap<&SVC::GetResourceLimit, 0x38>, "GetResourceLimit", 1000},
    {0x39, &SVC::Wrap<&SVC::GetResourceLimitLimitValues, 0x39>, "GetResourceLimitLimitValues",
     1000},
    {0x3A, &SVC::Wrap<&SVC::GetResourceLimitCurrentValues, 0x3A>, "GetResourceLimitCurrentValues",
     1000},
    {0x3B, nullptr, "GetThreadContext", 1000},
    {0x3C, &SVC::Wrap<&SVC::Break, 0x3C>, "Break", 1000},
    {0x3D, &SVC::Wrap<&SVC::OutputDebugString, 0x3D>, "OutputDebugString", 1000},
    {0x3E, nullptr, "ControlPerformanceCounter", 1000},
    {0x3F, nullptr, "Unknown", 1000},
    {0x40, nullptr, "Unknown", 1000},
    {0x41, nullptr, "Unknown", 1000},
    {0x42, nullptr, "Unknown", 1000},
    {0x43, nullptr, "Unknown", 1000},
    {0x44, nullptr, "Unknown", 1000},
    {0x45, nullptr, "Unknown", 1000},
    {0x46, nullptr, "Unknown", 1000},
    {0x47, &SVC::Wrap<&SVC::CreatePort, 0x47>, "CreatePort", 1000},
    {0x48, &SVC::Wrap<&SVC::CreateSessionToPort, 0x48>, "CreateSessionToPort", 5122},
    {0x49, &SVC::Wrap<&SVC::CreateSession, 0x49>, "CreateSession", 3492},
    {0x4A, &SVC::Wrap<&SVC::AcceptSession, 0x4A>, "AcceptSession", 1842},
    {0x4B, nullptr, "ReplyAndReceive1", 1000},
    {0x4C, nullptr, "ReplyAndReceive2", 1000},
    {0x4D, nullptr, "ReplyAndReceive3", 1000},
    {0x4E, nullptr, "ReplyAndReceive4", 1000},
    {0x4F, &SVC::Wrap<&SVC::ReplyAndReceive, 0x4F>, "ReplyAndReceive", 8762},
    {0x50, nullptr, "BindInterrupt", 1000},
    {0x51, nullptr, "UnbindInterrupt", 1000},
    {0x52, &SVC::Wrap<&SVC::InvalidateProcessDataCache, 0x52>, "InvalidateProcessDataCache", 9609},
    {0x53, &SVC::Wrap<&SVC::StoreProcessDataCache, 0x53>, "StoreProcessDataCache", 7174},
    {0x54, &SVC::Wrap<&SVC::FlushProcessDataCache, 0x54>, "FlushProcessDataCache", 9084},
    {0x55, nullptr, "StartInterProcessDma", 9146},
    {0x56, nullptr, "StopDma", 1163},
    {0x57, nullptr, "GetDmaState", 2222},
    {0x58, nullptr, "RestartDma", 8096},
    {0x59, nullptr, "SetGpuProt", 356},
    {0x5A, nullptr, "SetWifiEnabled", 1000},
    {0x5B, nullptr, "Unknown", 1000},
    {0x5C, nullptr, "Unknown", 1000},
    {0x5D, nullptr, "Unknown", 1000},
    {0x5E, nullptr, "Unknown", 1000},
    {0x5F, nullptr, "Unknown", 1000},
    {0x60, nullptr, "DebugActiveProcess", 1000},
    {0x61, nullptr, "BreakDebugProcess", 1000},
    {0x62, nullptr, "TerminateDebugProcess", 1000},
    {0x63, nullptr, "GetProcessDebugEvent", 1000},
    {0x64, nullptr, "ContinueDebugEvent", 1000},
    {0x65, &SVC::Wrap<&SVC::GetProcessList, 0x65>, "GetProcessList", 1000},
    {0x66, nullptr, "GetThreadList", 1000},
    {0x67, nullptr, "GetDebugThreadContext", 1000},
    {0x68, nullptr, "SetDebugThreadContext", 1000},
    {0x69, nullptr, "QueryDebugProcessMemory", 1000},
    {0x6A, nullptr, "ReadProcessMemory", 1000},
    {0x6B, nullptr, "WriteProcessMemory", 1000},
    {0x6C, nullptr, "SetHardwareBreakPoint", 1000},
    {0x6D, nullptr, "GetDebugThreadParam", 1000},
    {0x6E, nullptr, "Unknown", 1000},
    {0x6F, nullptr, "Unknown", 1000},
    {0x70, nullptr, "ControlProcessMemory", 1000},
    {0x71, nullptr, "MapProcessMemory", 1000},
    {0x72, nullptr, "UnmapProcessMemory", 1000},
    {0x73, nullptr, "CreateCodeSet", 1000},
    {0x74, nullptr, "RandomStub", 1000},
    {0x75, nullptr, "CreateProcess", 1000},
    {0x76, &SVC::Wrap<&SVC::TerminateProcess, 0x76>, "TerminateProcess", 1000},
    {0x77, nullptr, "SetProcessResourceLimits", 1000},
    {0x78, nullptr, "CreateResourceLimit", 1000},
    {0x79, &SVC::Wrap<&SVC::SetResourceLimitLimitValues, 0x79>, "SetResourceLimitLimitValues",
     1000},
    {0x7A, nullptr, "AddCodeSegment", 1000},
    {0x7B, nullptr, "Backdoor", 1000},
    {0x7C, &SVC::Wrap<&SVC::KernelSetState, 0x7C>, "KernelSetState", 1000},
    {0x7D, &SVC::Wrap<&SVC::QueryProcessMemory, 0x7D>, "QueryProcessMemory", 1000},
    // Custom SVCs
    {0x7E, nullptr, "Unused", 1000},
    {0x7F, nullptr, "Unused", 1000},
    {0x80, nullptr, "CustomBackdoor", 1000},
    {0x81, nullptr, "Unused", 1000},
    {0x82, nullptr, "Unused", 1000},
    {0x83, nullptr, "Unused", 1000},
    {0x84, nullptr, "Unused", 1000},
    {0x85, nullptr, "Unused", 1000},
    {0x86, nullptr, "Unused", 1000},
    {0x87, nullptr, "Unused", 1000},
    {0x88, nullptr, "Unused", 1000},
    {0x89, nullptr, "Unused", 1000},
    {0x8A, nullptr, "Unused", 1000},
    {0x8B, nullptr, "Unused", 1000},
    {0x8C, nullptr, "Unused", 1000},
    {0x8D, nullptr, "Unused", 1000},
    {0x8E, nullptr, "Unused", 1000},
    {0x8F, nullptr, "Unused", 1000},
    {0x90, &SVC::Wrap<&SVC::ConvertVaToPa, 0x90>, "ConvertVaToPa", 1000},
    {0x91, &SVC::Wrap<&SVC::FlushDataCacheRange, 0x91>, "FlushDataCacheRange", 1000},
    {0x92, &SVC::Wrap<&SVC::FlushEntireDataCache, 0x92>, "FlushEntireDataCache", 1000},
    {0x93, &SVC::Wrap<&SVC::InvalidateInstructionCacheRange, 0x93>,
     "InvalidateInstructionCacheRange", 1000},
    {0x94, &SVC::Wrap<&SVC::InvalidateEntireInstructionCache, 0x94>,
     "InvalidateEntireInstructionCache", 1000},
    {0x95, nullptr, "Unused", 1000},
    {0x96, nullptr, "Unused", 1000},
    {0x97, nullptr, "Unused", 1000},
    {0x98, nullptr, "Unused", 1000},
    {0x99, nullptr, "Unused", 1000},
    {0x9A, nullptr, "Unused", 1000},
    {0x9B, nullptr, "Unused", 1000},
    {0x9C, nullptr, "Unused", 1000},
    {0x9D, nullptr, "Unused", 1000},
    {0x9E, nullptr, "Unused", 1000},
    {0x9F, nullptr, "Unused", 1000},
    {0xA0, &SVC::Wrap<&SVC::MapProcessMemoryEx, 0xA0>, "MapProcessMemoryEx", 1000},
    {0xA1, &SVC::Wrap<&SVC::UnmapProcessMemoryEx, 0xA1>, "UnmapProcessMemoryEx", 1000},
    {0xA2, nullptr, "ControlMemoryEx", 1000},
    {0xA3, nullptr, "ControlMemoryUnsafe", 1000},
    {0xA4, nullptr, "Unused", 1000},
    {0xA5, nullptr, "Unused", 1000},
    {0xA6, nullptr, "Unused", 1000},
    {0xA7, nullptr, "Unused", 1000},
    {0xA8, nullptr, "Unused", 1000},
    {0xA9, nullptr, "Unused", 1000},
    {0xAA, nullptr, "Unused", 1000},
    {0xAB, nullptr, "Unused", 1000},
    {0xAC, nullptr, "Unused", 1000},
    {0xAD, nullptr, "Unused", 1000},
    {0xAE, nullptr, "Unused", 1000},
    {0xAF, nullptr, "Unused", 1000},
    {0xB0, nullptr, "ControlService", 1000},
    {0xB1, nullptr, "CopyHandle", 1000},
    {0xB2, nullptr, "TranslateHandle", 1000},
    {0xB3, &SVC::Wrap<&SVC::ControlProcess, 0xB3>, "ControlProcess", 1000},
}};

const SVC::FunctionDef* SVC::GetSVCInfo(u32 func_num) {
    if (func_num >= SVC_Table.size()) {
        LOG_ERROR(Kernel_SVC, "unknown svc=0x{:02X}", func_num);
        return nullptr;
    }
    return &SVC_Table[func_num];
}

MICROPROFILE_DEFINE(Kernel_SVC, "Kernel", "SVC", MP_RGB(70, 200, 70));

void SVC::CallSVC(u32 immediate) {
    MICROPROFILE_SCOPE(Kernel_SVC);
    system.perf_stats->BeginSVCProcessing();

    // Lock the kernel mutex when we enter the kernel HLE.
    std::scoped_lock lock{kernel.GetHLELock()};

    DEBUG_ASSERT_MSG(kernel.GetCurrentProcess()->status == ProcessStatus::Running,
                     "Running threads from exiting processes is unimplemented");

    const FunctionDef* info = GetSVCInfo(immediate);
    if (!info) {
        system.perf_stats->EndSVCProcessing();
        return;
    }

    const auto process = kernel.GetCurrentProcess();
    const u32 in_r0 = GetReg(0);
    const u32 in_r1 = GetReg(1);
    const u32 in_r2 = GetReg(2);
    const u32 in_r3 = GetReg(3);
    const u32 in_pc = GetReg(15);

    const auto thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    std::shared_ptr<Service::PLGLDR::PLG_LDR> plugin_loader;
    bool plugin_svc = false;
    bool level256_plugin_context = false;
    bool level256_official_plugin_context = false;
    Luma3DS::CompatibilityContext luma_compatibility_context{};
    Luma3DS::PluginAuthorization luma_plugin_authorization{};
    std::string service_name;
    // The mapped 3GX bootloader and executable occupy these reserved windows. Avoid a service
    // lookup on the normal SVC hot path for PCs outside them.
    if (process && thread && in_pc >= 0x06FFF000 && in_pc < 0x08000000) {
        if (immediate == 0x32) {
            const auto session = process->handle_table.Get<ClientSession>(in_r0);
            if (session) {
                service_name = session->name;
            }
        }
        plugin_loader = Service::PLGLDR::GetService(system);
        if (plugin_loader) {
            const auto& plugin_context = plugin_loader->GetPluginLoaderContext();
            luma_compatibility_context = plugin_loader->GetLumaCompatibilityContext(
                Settings::values.level256_compatibility_enabled.GetValue());
            luma_plugin_authorization = plugin_loader->GetLumaExtensionAuthorization(
                process->codeset ? process->codeset->program_id : 0);
            level256_plugin_context = Luma3DS::CanDispatchExtension(
                luma_compatibility_context, luma_plugin_authorization, process->process_id, in_pc);
            const bool supported_title =
                process->codeset &&
                Service::PLGLDR::IsLevel256SupportedTitle(process->codeset->program_id);
            level256_official_plugin_context = IsVerifiedLevel256PluginContext(
                true, supported_title, plugin_context.level256_official_plugin_hash_matches,
                process->process_id, plugin_context.plugin_process_id, plugin_context.plugin_loaded,
                plugin_context.plugin_code_begin, plugin_context.plugin_code_end, in_pc);
            plugin_svc = plugin_loader->TracePluginSVC(
                *process, *thread, in_pc, immediate, info->name,
                service_name.empty() ? nullptr : service_name.c_str());
        }
    }

    // ARM11 kernel-extension SVCs are not part of the ordinary 3DS SVC ABI. Expose them only to
    // the active, hash-verified plugin mapping when the compatibility mode is enabled. The
    // ordinary SVC table and its behavior remain unchanged for games and unverified code.
    Luma3DS::SvcRequest luma_svc_request{.svc_id = immediate,
                                          .process_id = process ? process->process_id : 0,
                                          .entry_pc = in_pc,
                                          .arguments = {GetReg(0), GetReg(1), GetReg(2), GetReg(3),
                                                        GetReg(4), GetReg(5), GetReg(6), GetReg(7)}};
    const auto luma_dispatch = Luma3DS::DispatchExtensionSvc(
        luma_svc_request, luma_compatibility_context, luma_plugin_authorization);
    if (luma_dispatch.kind == Luma3DS::SvcDispatchKind::Rejected) {
        SetReg(0, ResultNotImplemented.raw);
        system.perf_stats->EndSVCProcessing();
        return;
    }

    LOG_TRACE(Kernel_SVC, "calling {}", info->name);
    std::shared_ptr<AddressArbiter> level256_arbiter;
    std::vector<std::pair<u32, ThreadStatus>> level256_waiters_before;
    bool trace_level256_arbiter = false;
    bool trace_level256_hook_arbiter = false;
    bool level256_arbiter_address_valid = false;
    u32 level256_arbiter_memory_before = 0xFFFFFFFF;
    ThreadStatus level256_arbiter_thread_status_before = ThreadStatus::Dead;
    if (immediate == 0x22 && level256_official_plugin_context && plugin_loader && process &&
        thread) {
        auto& context = plugin_loader->GetPluginLoaderContext();
        auto current_arbiter = process->handle_table.Get<AddressArbiter>(in_r0);
        if (!context.ctrpf_arbiter_watch_started) {
            context.ctrpf_arbiter_watch_started = true;
            context.ctrpf_arbiter_watch_handle = in_r0;
            context.ctrpf_arbiter_watch_address = in_r1;
            context.ctrpf_arbiter_watch_object =
                reinterpret_cast<std::uintptr_t>(current_arbiter.get());
        }
        const bool same_arbiter = context.ctrpf_arbiter_watch_object != 0
                                      ? reinterpret_cast<std::uintptr_t>(current_arbiter.get()) ==
                                            context.ctrpf_arbiter_watch_object
                                      : in_r0 == context.ctrpf_arbiter_watch_handle;
        trace_level256_hook_arbiter = context.ctrpf_hook_callback_target_observed &&
                                      context.ctrpf_hook_arbiter_trace_count <
                                          Kernel::LEVEL256_CTRPF_ARBITER_DIAGNOSTIC_LIMIT;
        trace_level256_arbiter =
            trace_level256_hook_arbiter ||
            (same_arbiter && in_r1 == context.ctrpf_arbiter_watch_address &&
             ShouldLogLevel256ArbiterDiagnostic(true, context.ctrpf_arbiter_trace_count));
        if (trace_level256_arbiter) {
            level256_arbiter_address_valid = memory.IsValidVirtualAddress(*process, in_r1);
            if (level256_arbiter_address_valid) {
                level256_arbiter_memory_before = memory.Read32(*process, in_r1);
            }
            level256_arbiter_thread_status_before = thread->status;
            level256_arbiter = current_arbiter;
            if (level256_arbiter) {
                level256_waiters_before = level256_arbiter->GetWaitersForAddress(in_r1);
            }
        }
    }
    if (info->func) {
        system.GetRunningCore().GetTimer().AddTicks(info->cycles);
        (this->*(info->func))();

        if (level256_plugin_context && immediate == 0x08 && process && thread && plugin_loader) {
            auto& context = plugin_loader->GetPluginLoaderContext();
            if (ConsumeLevel256ThreadCreateLogSlot(true,
                                                   context.level256_thread_create_log_count)) {
                const Handle returned_handle = GetReg(1);
                const auto created_thread = GetReg(0) == ResultSuccess.raw
                                                ? process->handle_table.Get<Thread>(returned_handle)
                                                : nullptr;
                LOG_INFO(Kernel_SVC,
                         "Level256 worker CreateThread result: title={:016X} pid={} "
                         "caller_thread={} caller_pc=0x{:08X} requested_entry_pc=0x{:08X} "
                         "result=0x{:08X} handle=0x{:08X} created_thread={} "
                         "created_pc=0x{:08X} created_status={}",
                         process->codeset ? process->codeset->program_id : 0, process->process_id,
                         thread->GetThreadId(), in_pc, in_r1, GetReg(0), returned_handle,
                         created_thread ? created_thread->GetThreadId() : UINT32_MAX,
                         created_thread ? created_thread->context.GetProgramCounter() : 0,
                         created_thread
                             ? Level256DiagnosticThreadStatusName(created_thread->status)
                             : "NotCreated");
            }
        }

        if (trace_level256_arbiter && plugin_loader && process && thread) {
            auto& context = plugin_loader->GetPluginLoaderContext();
            ++context.ctrpf_arbiter_trace_count;
            if (trace_level256_hook_arbiter) {
                ++context.ctrpf_hook_arbiter_trace_count;
            }
            const u32 memory_after =
                level256_arbiter_address_valid ? memory.Read32(*process, in_r1) : 0xFFFFFFFF;
            const auto waiters_after = level256_arbiter
                                           ? level256_arbiter->GetWaitersForAddress(in_r1)
                                           : std::vector<std::pair<u32, ThreadStatus>>{};
            std::string resumed_threads;
            for (const auto& [waiter_id, old_status] : level256_waiters_before) {
                const bool still_waiting = std::any_of(
                    waiters_after.begin(), waiters_after.end(),
                    [waiter_id](const auto& waiter) { return waiter.first == waiter_id; });
                if (still_waiting) {
                    continue;
                }
                if (!resumed_threads.empty()) {
                    resumed_threads += ",";
                }
                const auto resumed = kernel.GetCurrentThreadManager().GetThreadByID(waiter_id);
                if (resumed) {
                    ++context.ctrpf_guest_arbiter_wakeup_count;
                }
                resumed_threads += fmt::format(
                    "{}:{}->{}:pc=0x{:08X}", waiter_id,
                    Level256DiagnosticThreadStatusName(old_status),
                    resumed ? Level256DiagnosticThreadStatusName(resumed->status) : "Gone",
                    resumed ? resumed->context.GetProgramCounter() : 0);
            }
            if (thread->status == ThreadStatus::WaitArb) {
                ++context.ctrpf_guest_arbiter_wait_count;
            }
            if (in_r2 == static_cast<u32>(ArbitrationType::Signal)) {
                ++context.ctrpf_guest_arbiter_signal_count;
            }

            if (!context.ctrpf_osd_post_init_compared) {
                context.ctrpf_osd_post_init_compared = true;
                const std::array<u32, 3> candidates = {
                    context.ctrpf_osd_scan.candidate1,
                    context.ctrpf_osd_scan.candidate2,
                    context.ctrpf_osd_scan.candidate3,
                };
                for (std::size_t i = 0; i < candidates.size(); ++i) {
                    if (candidates[i] == LEVEL256_CTRPF_OSD_NOT_FOUND ||
                        !memory.IsValidVirtualAddress(*process, candidates[i])) {
                        continue;
                    }
                    const u32 after = memory.Read32(*process, candidates[i]);
                    LOG_INFO(Service_PLGLDR,
                             "Level256 CTRPF OSD candidate post-arbiter pid={} thread={} "
                             "candidate=0x{:08X} before=0x{:08X} after=0x{:08X} changed={}",
                             process->process_id, thread->GetThreadId(), candidates[i],
                             context.ctrpf_osd_candidate_initial_words[i], after,
                             after != context.ctrpf_osd_candidate_initial_words[i]);
                }
            }

            LOG_INFO(Service_PLGLDR,
                     "Level256 CTRPF ArbitrateAddress phase={} pid={} thread={} pc=0x{:08X} "
                     "handle=0x{:08X} address=0x{:08X} type={} value={} "
                     "memory_valid={} memory_before=0x{:08X} memory_after=0x{:08X} "
                     "result=0x{:08X} wait_reason={} return_pc=0x{:08X} "
                     "thread_status_before={} thread_status_after={} waiters_before={} "
                     "waiters_after={} resumed_threads={}",
                     trace_level256_hook_arbiter ? "after_hook_callback" : "initial_watch",
                     process->process_id, thread->GetThreadId(), in_pc, in_r0, in_r1, in_r2,
                     static_cast<s32>(in_r3), level256_arbiter_address_valid,
                     level256_arbiter_memory_before, memory_after, GetReg(0),
                     thread->status == ThreadStatus::WaitArb ? "ArbitrateAddress" : "none",
                     thread->context.GetProgramCounter(),
                     Level256DiagnosticThreadStatusName(level256_arbiter_thread_status_before),
                     Level256DiagnosticThreadStatusName(thread->status),
                     level256_waiters_before.size(), waiters_after.size(), resumed_threads);
        }

        if (level256_plugin_context && immediate == 0x33 && in_pc == 0x070A1320) {
            // OpenProcess's pointer output occupies r0; the process ID remains in r1 per the
            // ARM SVC ABI. The returned handle is in r1 after dispatch.
            const auto target = kernel.GetProcessById(in_r1);
            const VAddr stack_pointer = GetReg(13);
            const auto read_stack_word = [&](u32 offset) {
                const u64 address = static_cast<u64>(stack_pointer) + offset;
                if (address > 0xFFFFFFFFULL ||
                    !memory.IsValidVirtualAddress(*process, static_cast<VAddr>(address))) {
                    return 0xFFFFFFFF;
                }
                return memory.Read32(*process, static_cast<VAddr>(address));
            };
            // OpenProcess's caller has two paths: r5 == 0 loads its PID from caller [sp+0xC4],
            // otherwise it loads caller [sp+0xC8]. The wrapper has pushed its output pointer, so
            // the corresponding SVC-entry offsets are +0xC8 and +0xCC.
            const u32 stack_c4 = read_stack_word(0xC8);
            const u32 stack_c8 = read_stack_word(0xCC);
            LOG_WARNING(Kernel_SVC,
                        "Level256 OpenProcess: caller_pid={} thread={} pc=0x{:08X} "
                        "target_pid={} target_present={} target_title={:016X} "
                        "output_address=0x{:08X} result=0x{:08X} output_handle=0x{:08X} "
                        "lr=0x{:08X} r5=0x{:08X} sp=0x{:08X} stack_c4=0x{:08X} "
                        "stack_c8=0x{:08X} selected_stack_pid=0x{:08X}",
                        process->process_id, thread ? thread->GetThreadId() : 0, in_pc, in_r1,
                        static_cast<bool>(target),
                        target && target->codeset ? target->codeset->program_id : 0, in_r0,
                        GetReg(0), GetReg(1), GetReg(14), GetReg(5), stack_pointer, stack_c4,
                        stack_c8, GetReg(5) == 0 ? stack_c4 : stack_c8);
        }

        if (level256_plugin_context && immediate == LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID &&
            in_pc == LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC &&
            GetReg(0) == LEVEL256_QUERY_PROCESS_MEMORY_FAILURE &&
            plugin_loader->GetPluginLoaderContext().level256_official_plugin_hash_matches) {
            auto& plugin_context = plugin_loader->GetPluginLoaderContext();
            auto& failure_count = plugin_context.plugin_query_process_memory_failures;
            if (RecordLevel256QueryProcessMemoryFailure(failure_count)) {
                LOG_WARNING(Kernel_SVC,
                            "Level256 QueryProcessMemory failure: caller_pid={} thread={} "
                            "pc=0x{:08X} process_handle=0x{:08X} address=0x{:08X} "
                            "result=0x{:08X} aggregated_failures={}",
                            process->process_id, thread ? thread->GetThreadId() : 0, in_pc, in_r2,
                            in_r3, GetReg(0), failure_count);
            }
        }

        // Reproduce Luma3DS-Level256's documented type 0x30000 verification response only for
        // the enabled mode, verified plugin hash, owning process, and mapped plugin code.
        if (immediate == LEVEL256_DIAGNOSTIC_SVC_ID && plugin_loader && process && thread &&
            process->codeset) {
            Luma3DS::SystemInfoResponse level256_response{};
            const auto level256_policy = plugin_loader->GetLevel256VerificationPolicy(
                Settings::values.level256_compatibility_enabled.GetValue(),
                process->codeset->program_id);
            const bool response_available = Luma3DS::TryGetSystemInfoCompatibilityResponse(
                luma_compatibility_context, luma_plugin_authorization, level256_policy,
                process->process_id, in_pc, immediate, in_r1, static_cast<s32>(in_r2),
                level256_response);
            if (response_available) {
                u32 result = GetReg(0);
                u64 output = static_cast<u64>(GetReg(1)) | (static_cast<u64>(GetReg(2)) << 32);
                const u64 original_value = output;
                const u32 original_result = result;
                result = level256_response.result;
                output = level256_response.value;
                LOG_WARNING(Kernel_SVC,
                            "Level256 GetSystemInfo compatibility response: pid={} pc=0x{:08X} "
                            "type=0x{:X} param={} "
                            "original=0x{:016X} replacement=0x{:016X} "
                            "result=0x{:08X}->0x{:08X}",
                            process->process_id, in_pc, in_r1, static_cast<s32>(in_r2),
                            original_value, output, original_result, result);
                SetReg(0, result);
                SetReg(1, static_cast<u32>(output));
                SetReg(2, static_cast<u32>(output >> 32));
            }
        }

        if (plugin_svc && plugin_loader) {
            const u64 output_value =
                static_cast<u64>(GetReg(1)) | (static_cast<u64>(GetReg(2)) << 32);
            plugin_loader->TracePluginSVCResult(*process, *thread, immediate, info->name, in_pc,
                                                GetReg(15), in_r1, static_cast<s32>(in_r2),
                                                GetReg(0), output_value);

            // Follow the observed Level256/Citra-information query only. The trace is bounded,
            // restricted to this plugin owner/thread/code range, and leaves guest state untouched.
            constexpr u64 level256_probe_title_id = 0x0004000000155100ULL;
            constexpr u32 citra_information_type = 0x20000;
            constexpr s32 emulator_id_param = 0;
            constexpr u64 azahar_emulator_id = 2;
            constexpr u32 instruction_trace_limit = 256;
            auto& plugin_context = plugin_loader->GetPluginLoaderContext();
            if (immediate == 0x2A && process->codeset &&
                !Settings::values.level256_compatibility_enabled.GetValue() &&
                process->codeset->program_id == level256_probe_title_id &&
                in_r1 == citra_information_type && static_cast<s32>(in_r2) == emulator_id_param &&
                ((GetReg(0) == ResultSuccess.raw && output_value == azahar_emulator_id) ||
                 (GetReg(0) == LEVEL256_LUMA_MARKER_RESULT && output_value == 0)) &&
                plugin_context.plugin_trace_thread_id == thread->GetThreadId() &&
                !plugin_context.plugin_instruction_trace_started) {
                plugin_context.plugin_instruction_trace_started = true;
                system.GetRunningCore().StartPluginInstructionTrace(
                    process->process_id, thread->GetThreadId(), plugin_context.plugin_code_begin,
                    plugin_context.plugin_code_end, plugin_context.plugin_bootloader_begin,
                    plugin_context.plugin_bootloader_end, instruction_trace_limit);
            }
        }

        const u32 out_r0 = GetReg(0);
        if (out_r0 == ResultInvalidHandle.raw && !(level256_plugin_context && immediate == 0x7D)) {
            LOG_CRITICAL(
                Kernel_SVC,
                "[HOME-HANDOFF] INVALID HANDLE SVC: svc=0x{:02X} name={} pid={} title={:016X} "
                "thread={} pc=0x{:08X} in_r0=0x{:08X} in_r1=0x{:08X} "
                "in_r2=0x{:08X} in_r3=0x{:08X} out_r0=0x{:08X}",
                info->id, info->name, process ? process->process_id : 0,
                process && process->codeset ? process->codeset->program_id : 0,
                thread ? thread->thread_id : 0, in_pc, in_r0, in_r1, in_r2, in_r3, out_r0);
        }
    } else {
        if (immediate == Kernel::LEVEL256_CUSTOM_BACKDOOR_SVC_ID && process && thread &&
            plugin_loader && luma_dispatch.kind == Luma3DS::SvcDispatchKind::DelegateToRegisteredHandler) {
            const auto common_status = Luma3DS::ValidateCustomBackdoorRequest(
                luma_svc_request, luma_compatibility_context, luma_plugin_authorization);
            if (common_status == Luma3DS::CustomBackdoorStatus::Unsupported) {
                const auto backdoor = Luma3DS::HandleLevel256CustomBackdoor(
                    system, memory, process, thread, plugin_loader->GetPluginLoaderContext(),
                    plugin_loader->GetProcessViewManager(), luma_compatibility_context,
                    luma_plugin_authorization, luma_svc_request);
                if (backdoor.handled) {
                    SetReg(0, backdoor.result);
                    system.perf_stats->EndSVCProcessing();
                    return;
                }
            }
        }

        if (level256_plugin_context && immediate == 0xA3 && process && thread) {
            const u32 r4 = GetReg(4);
            const u32 r5 = GetReg(5);
            const u32 r6 = GetReg(6);
            const u32 r7 = GetReg(7);
            const char* operation_name = "unsupported";
            u32 expected_memory_state = 0;
            switch (in_r3 & MEMOP_OPERATION_MASK) {
            case MEMOP_FREE:
                operation_name = "FREE";
                break;
            case MEMOP_COMMIT:
                operation_name = "COMMIT";
                expected_memory_state = (in_r3 & MEMOP_LINEAR) ? 0x3907 : 0xBB05;
                break;
            default:
                break;
            }
            LOG_WARNING(Kernel_SVC,
                        "Level256 ControlMemoryUnsafe ABI: pid={} thread={} pc=0x{:08X} "
                        "r0_out=0x{:08X} r1_addr=0x{:08X} r2_size=0x{:08X} "
                        "r3_operation=0x{:08X} op_name={} region=0x{:X} linear={} "
                        "r4_permissions=0x{:08X} r5=0x{:08X} r6=0x{:08X} r7=0x{:08X} "
                        "effective_size=0x{:08X} expected_luma_state=0x{:08X}",
                        process->process_id, thread->GetThreadId(), in_pc, in_r0, in_r1, in_r2,
                        in_r3, operation_name, in_r3 & MEMOP_REGION_MASK,
                        (in_r3 & MEMOP_LINEAR) != 0, r4, r5, r6, r7,
                        in_r2 & ~Memory::CITRA_PAGE_MASK, expected_memory_state);
        }

        if (immediate == LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID && process && thread &&
            plugin_loader && level256_plugin_context) {
            const auto operation = Luma3DS::ControlMemoryUnsafe(
                memory, *process, *thread, plugin_loader->GetPluginLoaderContext(),
                plugin_loader->GetProcessViewManager(), luma_svc_request);
            if (operation.handled) {
                SetReg(0, operation.result.raw);
                system.perf_stats->EndSVCProcessing();
                return;
            }
        }

        LOG_ERROR(Kernel_SVC,
                  "unimplemented SVC id=0x{:02X} name={} pid={} title_id={:016X} thread={} "
                  "pc=0x{:08X}",
                  info->id, info->name, process ? process->process_id : 0,
                  process && process->codeset ? process->codeset->program_id : 0,
                  thread ? thread->thread_id : 0, in_pc);
        if (Luma3DS::IsExtensionSVC(immediate)) {
            // A recognized extension with no safe HLE implementation must fail explicitly;
            // returning the callback pointer or an old guest register value would look successful.
            SetReg(0, ResultNotImplemented.raw);
        }
    }
    system.perf_stats->EndSVCProcessing();
}

bool SVC::ShouldLogSVCError(u32 svc_id, u32 raw_result) {
    const auto process = kernel.GetCurrentProcess();
    const auto thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    if (!process || !thread || !process->codeset ||
        !Settings::values.level256_compatibility_enabled.GetValue() ||
        !Service::PLGLDR::IsLevel256SupportedTitle(process->codeset->program_id) ||
        svc_id != LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID ||
        raw_result != LEVEL256_QUERY_PROCESS_MEMORY_FAILURE) {
        return true;
    }

    const auto plugin_loader = Service::PLGLDR::GetService(system);
    if (!plugin_loader) {
        return true;
    }
    const auto& context = plugin_loader->GetPluginLoaderContext();
    const u32 pc = GetReg(15);
    const bool suppress = ShouldSuppressDuplicateLevel256SVCError(
        true, context.level256_official_plugin_hash_matches, process->codeset->program_id,
        process->process_id, context.plugin_process_id, context.plugin_loaded,
        context.plugin_code_begin, context.plugin_code_end, pc, svc_id, raw_result);
    return !suppress;
}

SVC::SVC(Core::System& system) : system(system), kernel(system.Kernel()), memory(system.Memory()) {}

u32 SVC::GetReg(std::size_t n) {
    return system.GetRunningCore().GetReg(static_cast<int>(n));
}

void SVC::SetReg(std::size_t n, u32 value) {
    system.GetRunningCore().SetReg(static_cast<int>(n), value);
}

SVCContext::SVCContext(Core::System& system) : impl(std::make_unique<SVC>(system)) {}
SVCContext::~SVCContext() = default;

void SVCContext::CallSVC(u32 immediate) {
    impl->CallSVC(immediate);
}

} // namespace Kernel

SERIALIZE_EXPORT_IMPL(Kernel::SVC_SyncCallback)
SERIALIZE_EXPORT_IMPL(Kernel::SVC_IPCCallback)
