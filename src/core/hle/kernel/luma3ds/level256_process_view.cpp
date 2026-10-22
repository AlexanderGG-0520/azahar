// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/kernel/luma3ds/level256_process_view.h"

#include "common/memory_ref.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/vm_manager.h"

namespace Kernel::Luma3DS {

bool Level256ProcessViewRecord::CanClaim(const Process* process, u32 process_id) const {
    return Empty() || Matches(process, process_id);
}

bool Level256ProcessViewRecord::Overlaps(u32 address, u32 size) const {
    if (Empty() || size == 0) {
        return false;
    }
    const u64 range_end = static_cast<u64>(address) + size;
    const u64 view_end = static_cast<u64>(guest_address) + LEVEL256_PROCESS_VIEW_SIZE;
    return range_end <= static_cast<u64>(UINT32_MAX) + 1 && address < view_end &&
           guest_address < range_end;
}

void Level256ProcessViewRecord::Claim(const Process* process, u32 process_id, VAddr address) {
    owner = process;
    owner_pid = process_id;
    guest_address = address;
    release_pending = false;
}

void Level256ProcessViewRecord::ClearFor(const Process& process) {
    ClearFor(&process, process.process_id);
}

Result Level256ProcessViewManager::MapAt(Process& process, VAddr address,
                                         std::shared_ptr<BufferMem> backing_memory) {
    if (!backing_memory || backing_memory->GetSize() != LEVEL256_PROCESS_VIEW_SIZE ||
        !record.CanClaim(&process, process.process_id) || record.release_pending) {
        return ResultInvalidAddressState;
    }
    if (!record.Empty()) {
        const auto vma = process.vm_manager.FindVMA(record.guest_address);
        const bool intact =
            record.Matches(&process, process.process_id) && record.guest_address == address &&
            vma != process.vm_manager.vma_map.end() && vma->second.type == VMAType::BackingMemory &&
            backing_memory && vma->second.backing_memory.GetPtr() == backing->GetPtr();
        return intact ? ResultSuccess : ResultInvalidAddressState;
    }

    const auto result = process.vm_manager.MapBackingMemory(
        address, MemoryRef{backing_memory}, LEVEL256_PROCESS_VIEW_SIZE, MemoryState::Private);
    if (!result.Succeeded()) {
        return result.Code();
    }
    record.Claim(&process, process.process_id, address);
    backing = std::move(backing_memory);
    return ResultSuccess;
}

bool Level256ProcessViewManager::Initialize(
    Process& process, Memory::MemorySystem& memory, VAddr address, u32 priority_limit,
    u32 thread_limit, Service::PLGLDR::Level256::KProcessViewV1::CodeSetFields code_set,
    bool is_n3ds) {
    if (!IsOwnedMapping(process, address) || !backing ||
        !memory.IsValidVirtualAddress(process, address) ||
        !memory.IsValidVirtualAddress(process, address + LEVEL256_PROCESS_VIEW_SIZE - 1)) {
        return false;
    }
    const auto view = Service::PLGLDR::Level256::KProcessViewV1::Build(
        address, process.process_id, priority_limit, thread_limit, code_set, is_n3ds);
    memory.WriteBlock(process, address, view.data(), sizeof(view));
    return true;
}

bool Level256ProcessViewManager::ReadWord(Process& process, Memory::MemorySystem& memory,
                                          VAddr address, u32 offset, u32& value) const {
    if (!IsOwnedMapping(process, address) || !backing ||
        offset > LEVEL256_PROCESS_VIEW_SIZE - sizeof(u32) ||
        static_cast<u64>(address) + offset > UINT32_MAX ||
        !memory.IsValidVirtualAddress(process, address + offset)) {
        return false;
    }
    value = memory.Read32(process, address + offset);
    return true;
}

bool Level256ProcessViewManager::WriteWord(Process& process, Memory::MemorySystem& memory,
                                           VAddr address, u32 offset, u32 value) const {
    if (!IsOwnedMapping(process, address) || !backing ||
        offset > LEVEL256_PROCESS_VIEW_SIZE - sizeof(u32) ||
        static_cast<u64>(address) + offset > UINT32_MAX ||
        !memory.IsValidVirtualAddress(process, address + offset)) {
        return false;
    }
    memory.Write32(process, address + offset, value);
    return true;
}

bool Level256ProcessViewManager::CopySnapshot(Process& process, Memory::MemorySystem& memory,
                                              VAddr destination, VAddr source, u32 size) const {
    if (size == 0 || size > LEVEL256_PROCESS_VIEW_COPY_SIZE) {
        return false;
    }
    const auto destination_vma = process.vm_manager.FindVMA(destination);
    const auto source_vma = process.vm_manager.FindVMA(source);
    const u64 destination_end = static_cast<u64>(destination) + size;
    const u64 source_end = static_cast<u64>(source) + size;
    const bool destination_valid =
        destination_end <= static_cast<u64>(UINT32_MAX) + 1 &&
        destination_vma != process.vm_manager.vma_map.end() &&
        destination_vma->second.type == VMAType::BackingMemory &&
        destination_vma->second.base <= destination &&
        destination_end <=
            static_cast<u64>(destination_vma->second.base) + destination_vma->second.size &&
        (static_cast<u8>(destination_vma->second.permissions) &
         static_cast<u8>(VMAPermission::Write)) != 0;
    const bool source_valid =
        source_end <= static_cast<u64>(UINT32_MAX) + 1 &&
        source_vma != process.vm_manager.vma_map.end() &&
        source_vma->second.type == VMAType::BackingMemory && source_vma->second.base <= source &&
        source_end <= static_cast<u64>(source_vma->second.base) + source_vma->second.size;
    if (!destination_valid || !source_valid) {
        return false;
    }
    std::array<u8, LEVEL256_PROCESS_VIEW_COPY_SIZE> snapshot{};
    memory.ReadBlock(process, source, snapshot.data(), size);
    memory.WriteBlock(process, destination, snapshot.data(), size);
    return true;
}

Result Level256ProcessViewManager::Release(Process& process) {
    if (record.Empty() || !record.Matches(&process, process.process_id)) {
        return ResultSuccess;
    }
    const auto vma = process.vm_manager.FindVMA(record.guest_address);
    if (vma == process.vm_manager.vma_map.end() || vma->second.type != VMAType::BackingMemory ||
        !backing || vma->second.backing_memory.GetPtr() != backing->GetPtr() ||
        vma->second.base > record.guest_address ||
        static_cast<u64>(record.guest_address) + LEVEL256_PROCESS_VIEW_SIZE >
            static_cast<u64>(vma->second.base) + vma->second.size) {
        record.MarkReleasePending();
        return ResultInvalidAddressState;
    }
    const auto result =
        process.vm_manager.UnmapRange(record.guest_address, LEVEL256_PROCESS_VIEW_SIZE);
    if (result.IsError()) {
        record.MarkReleasePending();
        return result;
    }
    record.ClearFor(process);
    backing.reset();
    return ResultSuccess;
}

void Level256ProcessViewManager::OnProcessDestroyed(Process& process) {
    if (!record.Matches(&process, process.process_id)) {
        return;
    }
    // Process::FreeAllMemory owns VM reclamation after this callback. Do not attempt to unmap
    // during destruction; release only the manager's retained backing reference.
    record.ClearFor(process);
    backing.reset();
}

void Level256ProcessViewManager::ResetAfterSavestate() {
    // VMManager serialization preserves the backing mapping. Drop only the runtime owner record;
    // compatibility trust is cleared during plugin context deserialization.
    record = {};
    backing.reset();
}

bool Level256ProcessViewManager::ResetForNewPlugin(Process& process) {
    if (record.Empty()) {
        return true;
    }
    if (record.Matches(&process, process.process_id)) {
        return Release(process).IsSuccess() && record.Empty();
    }
    Process* old_owner = const_cast<Process*>(record.owner);
    if (!old_owner) {
        return false;
    }
    return Release(*old_owner).IsSuccess() && record.Empty();
}

VAddr Level256ProcessViewManager::GetAddress(const Process& process) const {
    return HasIntactMapping(process, record.guest_address) ? record.guest_address : 0;
}

bool Level256ProcessViewManager::IsOwnedMapping(const Process& process, VAddr address) const {
    return HasIntactMapping(process, address);
}

bool Level256ProcessViewManager::OverlapsOwnedMapping(const Process& process, VAddr address,
                                                       u32 size) const {
    return record.Matches(&process, process.process_id) && record.Overlaps(address, size);
}

bool Level256ProcessViewManager::HasIntactMapping(const Process& process, VAddr address) const {
    if (address == 0 || record.release_pending || !record.Matches(&process, process.process_id) ||
        record.guest_address != address || !backing) {
        return false;
    }
    const auto vma = process.vm_manager.FindVMA(address);
    return vma != process.vm_manager.vma_map.end() && vma->second.type == VMAType::BackingMemory &&
           vma->second.base <= address &&
           static_cast<u64>(address) + LEVEL256_PROCESS_VIEW_SIZE <=
               static_cast<u64>(vma->second.base) + vma->second.size &&
           vma->second.backing_memory.GetPtr() == backing->GetPtr();
}

bool Level256ProcessViewManager::IsReleasePending(const Process& process) const {
    return record.Matches(&process, process.process_id) && record.release_pending;
}

bool Level256ProcessViewManager::HasOwner() const {
    return !record.Empty();
}

u32 Level256ProcessViewManager::GetOwnerProcessId() const {
    return record.owner_pid;
}

VAddr Level256ProcessViewManager::GetOwnedAddress() const {
    return record.guest_address;
}

} // namespace Kernel::Luma3DS
