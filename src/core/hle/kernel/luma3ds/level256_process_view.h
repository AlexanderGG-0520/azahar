// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/hle/service/plgldr/level256_kprocess_view.h"

class BufferMem;
namespace Kernel {
class Process;
}
namespace Memory {
class MemorySystem;
}
namespace Kernel::Luma3DS {

/// One owned Level256 ABI view mapping. A failed live-process release keeps this record and its
/// backing memory until retry succeeds or Process destruction is confirmed.
struct Level256ProcessViewRecord {
    const Process* owner{};
    u32 owner_pid{};
    VAddr guest_address{};
    bool release_pending{};

    bool Empty() const {
        return owner == nullptr;
    }
    bool Matches(const Process* process, u32 process_id) const {
        return owner == process && owner_pid == process_id;
    }
    bool CanClaim(const Process* process, u32 process_id) const;
    bool Overlaps(u32 address, u32 size) const;
    void Claim(const Process* process, u32 process_id, VAddr address);
    void MarkReleasePending() {
        release_pending = true;
    }
    void ClearFor(const Process* process, u32 process_id) {
        if (Matches(process, process_id)) {
            *this = {};
        }
    }
    void ClearFor(const Process& process);
};

class Level256ProcessViewManager {
public:
    Result MapAt(Process& process, VAddr address, std::shared_ptr<BufferMem> backing);
    bool Initialize(Process& process, Memory::MemorySystem& memory, VAddr address,
                    u32 priority_limit, u32 thread_limit,
                    Service::PLGLDR::Level256::KProcessViewV1::CodeSetFields code_set,
                    bool is_n3ds);
    bool ReadWord(Process& process, Memory::MemorySystem& memory, VAddr address, u32 offset,
                  u32& value) const;
    bool WriteWord(Process& process, Memory::MemorySystem& memory, VAddr address, u32 offset,
                   u32 value) const;
    bool CopySnapshot(Process& process, Memory::MemorySystem& memory, VAddr destination,
                      VAddr source, u32 size) const;
    Result Release(Process& process);
    void OnProcessDestroyed(Process& process);
    void ResetAfterSavestate();
    bool ResetForNewPlugin(Process& process);

    VAddr GetAddress(const Process& process) const;
    bool IsOwnedMapping(const Process& process, VAddr address) const;
    bool OverlapsOwnedMapping(const Process& process, VAddr address, u32 size) const;
    bool IsReleasePending(const Process& process) const;
    bool HasOwner() const;
    u32 GetOwnerProcessId() const;
    VAddr GetOwnedAddress() const;

private:
    bool HasIntactMapping(const Process& process, VAddr address) const;
    Level256ProcessViewRecord record;
    std::shared_ptr<BufferMem> backing;
};

} // namespace Kernel::Luma3DS
