// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/memory.h"

TEST_CASE("Level256 SYSTEM COMMIT creates private read-write non-executable backing",
          "[kernel][memory][luma3ds][level256]") {
    using namespace Kernel;
    constexpr VAddr target = 0x07500000;
    constexpr u32 size = 0x10000;
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    KernelSystem kernel(memory, timing, [] {}, MemoryMode::NewProd, 1);
    Process process(kernel);
    auto backing = std::make_shared<BufferMem>(size);

    const auto result =
        process.vm_manager.MapBackingMemory(target, MemoryRef{backing}, size, MemoryState::Private);
    REQUIRE(result.Succeeded());
    const auto vma = process.vm_manager.FindVMA(target);
    REQUIRE(vma != process.vm_manager.vma_map.end());
    CHECK(vma->second.meminfo_state == MemoryState::Private);
    CHECK(vma->second.permissions == VMAPermission::ReadWrite);
    CHECK((static_cast<u8>(vma->second.permissions) & static_cast<u8>(VMAPermission::Execute)) ==
          0);

    memory.Write32(process, target, 0xA3C0203U);
    CHECK(memory.Read32(process, target) == 0xA3C0203U);
    CHECK(process.vm_manager.UnmapRange(target, size) == ResultSuccess);
}

TEST_CASE("Memory Basics", "[kernel][memory]") {
    auto mem = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
    MemoryRef block{mem};
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    Kernel::Process process(kernel);
    SECTION("mapping memory") {
        // Because of the PageTable, Kernel::VMManager is too big to be created on the stack.
        auto manager = std::make_unique<Kernel::VMManager>(memory, process);
        auto result =
            manager->MapBackingMemory(Memory::HEAP_VADDR, block, static_cast<u32>(block.GetSize()),
                                      Kernel::MemoryState::Private);
        REQUIRE(result.Code() == ResultSuccess);

        auto vma = manager->FindVMA(Memory::HEAP_VADDR);
        CHECK(vma != manager->vma_map.end());
        CHECK(vma->second.size == static_cast<u32>(block.GetSize()));
        CHECK(vma->second.type == Kernel::VMAType::BackingMemory);
        CHECK(vma->second.backing_memory.GetPtr() == block.GetPtr());
        CHECK(vma->second.meminfo_state == Kernel::MemoryState::Private);
    }

    SECTION("unmapping memory") {
        // Because of the PageTable, Kernel::VMManager is too big to be created on the stack.
        auto manager = std::make_unique<Kernel::VMManager>(memory, process);
        auto result =
            manager->MapBackingMemory(Memory::HEAP_VADDR, block, static_cast<u32>(block.GetSize()),
                                      Kernel::MemoryState::Private);
        REQUIRE(result.Code() == ResultSuccess);

        Result code = manager->UnmapRange(Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()));
        REQUIRE(code == ResultSuccess);

        auto vma = manager->FindVMA(Memory::HEAP_VADDR);
        CHECK(vma != manager->vma_map.end());
        CHECK(vma->second.type == Kernel::VMAType::Free);
        CHECK(vma->second.backing_memory.GetPtr() == nullptr);
    }

    SECTION("changing memory permissions") {
        // Because of the PageTable, Kernel::VMManager is too big to be created on the stack.
        auto manager = std::make_unique<Kernel::VMManager>(memory, process);
        auto result =
            manager->MapBackingMemory(Memory::HEAP_VADDR, block, static_cast<u32>(block.GetSize()),
                                      Kernel::MemoryState::Private);
        REQUIRE(result.Code() == ResultSuccess);

        Result code = manager->ReprotectRange(Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()),
                                              Kernel::VMAPermission::Execute);
        CHECK(code == ResultSuccess);

        auto vma = manager->FindVMA(Memory::HEAP_VADDR);
        CHECK(vma != manager->vma_map.end());
        CHECK(vma->second.permissions == Kernel::VMAPermission::Execute);

        code = manager->UnmapRange(Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()));
        REQUIRE(code == ResultSuccess);
    }

    SECTION("changing memory state") {
        // Because of the PageTable, Kernel::VMManager is too big to be created on the stack.
        auto manager = std::make_unique<Kernel::VMManager>(memory, process);
        auto result =
            manager->MapBackingMemory(Memory::HEAP_VADDR, block, static_cast<u32>(block.GetSize()),
                                      Kernel::MemoryState::Private);
        REQUIRE(result.Code() == ResultSuccess);

        SECTION("reprotect memory range") {
            Result code =
                manager->ReprotectRange(Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()),
                                        Kernel::VMAPermission::ReadWrite);
            REQUIRE(code == ResultSuccess);
        }

        SECTION("with invalid address") {
            Result code = manager->ChangeMemoryState(
                0xFFFFFFFF, static_cast<u32>(block.GetSize()), Kernel::MemoryState::Locked,
                Kernel::VMAPermission::ReadWrite, Kernel::MemoryState::Aliased,
                Kernel::VMAPermission::Execute);
            CHECK(code == Kernel::ResultInvalidAddress);
        }

        SECTION("ignoring the original permissions") {
            Result code = manager->ChangeMemoryState(
                Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()), Kernel::MemoryState::Private,
                Kernel::VMAPermission::None, Kernel::MemoryState::Locked,
                Kernel::VMAPermission::Write);
            CHECK(code == ResultSuccess);

            auto vma = manager->FindVMA(Memory::HEAP_VADDR);
            CHECK(vma != manager->vma_map.end());
            CHECK(vma->second.permissions == Kernel::VMAPermission::Write);
            CHECK(vma->second.meminfo_state == Kernel::MemoryState::Locked);
        }

        SECTION("enforcing the original permissions with correct expectations") {
            Result code = manager->ChangeMemoryState(
                Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()), Kernel::MemoryState::Private,
                Kernel::VMAPermission::ReadWrite, Kernel::MemoryState::Aliased,
                Kernel::VMAPermission::Execute);
            CHECK(code == ResultSuccess);

            auto vma = manager->FindVMA(Memory::HEAP_VADDR);
            CHECK(vma != manager->vma_map.end());
            CHECK(vma->second.permissions == Kernel::VMAPermission::Execute);
            CHECK(vma->second.meminfo_state == Kernel::MemoryState::Aliased);
        }

        SECTION("with incorrect permission expectations") {
            Result code = manager->ChangeMemoryState(
                Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()), Kernel::MemoryState::Private,
                Kernel::VMAPermission::Execute, Kernel::MemoryState::Aliased,
                Kernel::VMAPermission::Execute);
            CHECK(code == Kernel::ResultInvalidAddressState);

            auto vma = manager->FindVMA(Memory::HEAP_VADDR);
            CHECK(vma != manager->vma_map.end());
            CHECK(vma->second.permissions == Kernel::VMAPermission::ReadWrite);
            CHECK(vma->second.meminfo_state == Kernel::MemoryState::Private);
        }

        SECTION("with incorrect state expectations") {
            Result code = manager->ChangeMemoryState(
                Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()), Kernel::MemoryState::Locked,
                Kernel::VMAPermission::ReadWrite, Kernel::MemoryState::Aliased,
                Kernel::VMAPermission::Execute);
            CHECK(code == Kernel::ResultInvalidAddressState);

            auto vma = manager->FindVMA(Memory::HEAP_VADDR);
            CHECK(vma != manager->vma_map.end());
            CHECK(vma->second.permissions == Kernel::VMAPermission::ReadWrite);
            CHECK(vma->second.meminfo_state == Kernel::MemoryState::Private);
        }

        Result code = manager->UnmapRange(Memory::HEAP_VADDR, static_cast<u32>(block.GetSize()));
        REQUIRE(code == ResultSuccess);
    }
}

TEST_CASE("Adjacent VMAs merge only when they share one backing allocation",
          "[kernel][memory][luma3ds][level256]") {
    constexpr u32 page_size = Memory::CITRA_PAGE_SIZE;
    auto shared_backing = std::make_shared<BufferMem>(2 * page_size);
    auto separate_backing = std::make_shared<BufferMem>(page_size);

    Kernel::VirtualMemoryArea first{};
    first.base = 0x08000000;
    first.size = page_size;
    first.type = Kernel::VMAType::BackingMemory;
    first.permissions = Kernel::VMAPermission::ReadWrite;
    first.meminfo_state = Kernel::MemoryState::Shared;
    first.backing_memory = MemoryRef{shared_backing};

    Kernel::VirtualMemoryArea next = first;
    next.base += page_size;
    next.backing_memory = MemoryRef{shared_backing, page_size};
    REQUIRE(first.CanBeMergedWith(next));

    // Independent allocations must not merge, even if their raw host pointers
    // happen to be adjacent (as observed in the macOS CI failure).
    next.backing_memory = MemoryRef{separate_backing};
    REQUIRE_FALSE(first.CanBeMergedWith(next));
}

TEST_CASE("MapProcessMemoryEx aliases a multi-VMA source range with safe target permissions",
          "[kernel][memory][luma3ds][level256]") {
    constexpr u32 page_size = Memory::CITRA_PAGE_SIZE;
    constexpr VAddr source_address = 0x08000000;
    constexpr VAddr destination_address = 0x09000000;
    constexpr VAddr private_destination_address = 0x09010000;

    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto source_process = std::make_unique<Kernel::Process>(kernel);
    auto destination_process = std::make_unique<Kernel::Process>(kernel);
    auto& source_vm = source_process->vm_manager;
    auto& destination_vm = destination_process->vm_manager;

    const auto first_backing = std::make_shared<BufferMem>(page_size);
    const auto second_backing = std::make_shared<BufferMem>(page_size);
    REQUIRE(source_vm
                .MapBackingMemory(source_address, MemoryRef{first_backing}, page_size,
                                  Kernel::MemoryState::Code)
                .Succeeded());
    REQUIRE(source_vm
                .MapBackingMemory(source_address + page_size, MemoryRef{second_backing}, page_size,
                                  Kernel::MemoryState::Private)
                .Succeeded());
    source_vm.Reprotect(source_vm.FindVMA(source_address), Kernel::VMAPermission::ReadExecute);

    const Result map_result = destination_vm.MapBackingMemoryRange(
        destination_address, source_vm, source_address, 2 * page_size, Kernel::MemoryState::Shared);
    REQUIRE(map_result == ResultSuccess);

    const auto first_destination = destination_vm.FindVMA(destination_address);
    const auto second_destination = destination_vm.FindVMA(destination_address + page_size);
    REQUIRE(first_destination != destination_vm.vma_map.end());
    REQUIRE(second_destination != destination_vm.vma_map.end());
    CHECK(first_destination->second.backing_memory.GetPtr() == first_backing->GetPtr());
    CHECK(second_destination->second.backing_memory.GetPtr() == second_backing->GetPtr());
    CHECK(first_destination->second.meminfo_state == Kernel::MemoryState::Shared);
    CHECK(second_destination->second.meminfo_state == Kernel::MemoryState::Shared);
    CHECK(first_destination->second.permissions == Kernel::VMAPermission::ReadWrite);
    CHECK(second_destination->second.permissions == Kernel::VMAPermission::ReadWrite);
    REQUIRE(destination_vm.MapBackingMemoryRange(private_destination_address, source_vm,
                                                 source_address, page_size,
                                                 Kernel::MemoryState::Private) == ResultSuccess);
    CHECK(destination_vm.FindVMA(private_destination_address)->second.meminfo_state ==
          Kernel::MemoryState::Private);
    CHECK(destination_vm.FindVMA(private_destination_address)->second.permissions ==
          Kernel::VMAPermission::ReadWrite);

    memory.Write32(*source_process, source_address, 0x11223344);
    CHECK(memory.Read32(*destination_process, destination_address) == 0x11223344);
    memory.Write32(*destination_process, destination_address + page_size, 0xAABBCCDD);
    CHECK(memory.Read32(*source_process, source_address + page_size) == 0xAABBCCDD);
}

TEST_CASE("MapProcessMemoryEx rejects invalid source and occupied destination ranges without "
          "partial maps",
          "[kernel][memory][luma3ds][level256]") {
    constexpr u32 page_size = Memory::CITRA_PAGE_SIZE;
    constexpr VAddr source_address = 0x08000000;
    constexpr VAddr destination_address = 0x09000000;

    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto source_process = std::make_unique<Kernel::Process>(kernel);
    auto destination_process = std::make_unique<Kernel::Process>(kernel);
    auto& source_vm = source_process->vm_manager;
    auto& destination_vm = destination_process->vm_manager;
    const auto source_backing = std::make_shared<BufferMem>(page_size);
    const auto destination_backing = std::make_shared<BufferMem>(page_size);

    REQUIRE(source_vm
                .MapBackingMemory(source_address, MemoryRef{source_backing}, page_size,
                                  Kernel::MemoryState::Private)
                .Succeeded());
    REQUIRE(destination_vm
                .MapBackingMemory(destination_address + page_size, MemoryRef{destination_backing},
                                  page_size, Kernel::MemoryState::Private)
                .Succeeded());

    CHECK(destination_vm.MapBackingMemoryRange(destination_address, source_vm, source_address,
                                               2 * page_size, Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddressState);
    CHECK(destination_vm.FindVMA(destination_address)->second.type == Kernel::VMAType::Free);

    CHECK(destination_vm.MapBackingMemoryRange(
              destination_address, source_vm, source_address + page_size, page_size,
              Kernel::MemoryState::Shared) == Kernel::ResultInvalidAddressState);
    CHECK(destination_vm.FindVMA(destination_address)->second.type == Kernel::VMAType::Free);

    CHECK(destination_vm.MapBackingMemoryRange(destination_address + 1, source_vm, source_address,
                                               page_size, Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddress);
    CHECK(destination_vm.MapBackingMemoryRange(destination_address, source_vm, source_address, 0,
                                               Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddress);
    CHECK(destination_vm.MapBackingMemoryRange(destination_address, source_vm, source_address,
                                               page_size - 1, Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddress);
    CHECK(destination_vm.MapBackingMemoryRange(0x3FFFF000, source_vm, source_address, 2 * page_size,
                                               Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddress);
}

TEST_CASE("MapProcessMemoryEx rejects IO-backed source memory", "[kernel][memory][luma3ds]") {
    constexpr u32 page_size = Memory::CITRA_PAGE_SIZE;
    constexpr VAddr source_address = 0x08000000;
    constexpr VAddr destination_address = 0x09000000;

    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto source_process = std::make_unique<Kernel::Process>(kernel);
    auto destination_process = std::make_unique<Kernel::Process>(kernel);
    auto& source_vm = source_process->vm_manager;
    auto& destination_vm = destination_process->vm_manager;
    const auto source_backing = std::make_shared<BufferMem>(page_size);

    REQUIRE(source_vm
                .MapBackingMemory(source_address, MemoryRef{source_backing}, page_size,
                                  Kernel::MemoryState::IO)
                .Succeeded());
    CHECK(destination_vm.MapBackingMemoryRange(destination_address, source_vm, source_address,
                                               page_size, Kernel::MemoryState::Shared) ==
          Kernel::ResultInvalidAddressState);
    CHECK(destination_vm.FindVMA(destination_address)->second.type == Kernel::VMAType::Free);
}
