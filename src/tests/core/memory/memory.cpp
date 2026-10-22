// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

TEST_CASE("memory.IsValidVirtualAddress", "[core][memory]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    SECTION("these regions should not be mapped on an empty process") {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));
        CHECK(memory.IsValidVirtualAddress(*process, Memory::PROCESS_IMAGE_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::HEAP_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::LINEAR_HEAP_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::VRAM_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::CONFIG_MEMORY_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::SHARED_PAGE_VADDR) == false);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::TLS_AREA_VADDR) == false);
    }

    SECTION("CONFIG_MEMORY_VADDR and SHARED_PAGE_VADDR should be valid after mapping them") {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));
        kernel.MapSharedPages(process->vm_manager);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::CONFIG_MEMORY_VADDR) == true);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::SHARED_PAGE_VADDR) == true);
    }

    SECTION("special regions should be valid after mapping them") {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));
        SECTION("VRAM") {
            kernel.HandleSpecialMapping(process->vm_manager,
                                        {Memory::VRAM_VADDR, Memory::VRAM_SIZE, false, false});
            CHECK(memory.IsValidVirtualAddress(*process, Memory::VRAM_VADDR) == true);
        }

        SECTION("IO (Not yet implemented)") {
            kernel.HandleSpecialMapping(
                process->vm_manager, {Memory::IO_AREA_VADDR, Memory::IO_AREA_SIZE, false, false});
            CHECK_FALSE(memory.IsValidVirtualAddress(*process, Memory::IO_AREA_VADDR) == true);
        }
    }

    SECTION("Unmapping a VAddr should make it invalid") {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));
        kernel.MapSharedPages(process->vm_manager);
        process->vm_manager.UnmapRange(Memory::CONFIG_MEMORY_VADDR, Memory::CONFIG_MEMORY_SIZE);
        CHECK(memory.IsValidVirtualAddress(*process, Memory::CONFIG_MEMORY_VADDR) == false);
    }
}

TEST_CASE("memory.TryGetFCRAMOffset", "[core][memory]") {
    Core::System system;
    Memory::MemorySystem memory{system};

    CHECK(memory.TryGetFCRAMOffset(memory.GetFCRAMPointer(0)) == 0u);
    CHECK(memory.TryGetFCRAMOffset(memory.GetFCRAMPointer(0x1234)) == 0x1234u);
    CHECK(memory.TryGetFCRAMOffset(
              memory.GetFCRAMPointer(Memory::FCRAM_N3DS_SIZE)) == Memory::FCRAM_N3DS_SIZE);

    std::array<u8, 16> unrelated_backing{};
    CHECK_FALSE(memory.TryGetFCRAMOffset(unrelated_backing.data()).has_value());
}

TEST_CASE("memory physical offsets follow the VMA backing", "[core][memory]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));

    kernel.HandleSpecialMapping(process->vm_manager,
                                {Memory::VRAM_VADDR, Memory::VRAM_SIZE, false, false});
    const auto vram_vma = process->vm_manager.FindVMA(Memory::VRAM_VADDR);
    REQUIRE(vram_vma != process->vm_manager.vma_map.end());
    REQUIRE(vram_vma->second.type == Kernel::VMAType::BackingMemory);

    constexpr std::array conversions = {
        std::pair{0x1F300000u, 0x18300000u},
        std::pair{0x1F08CA00u, 0x1808CA00u},
        std::pair{0x1F346500u, 0x18346500u},
        std::pair{0x1F0C4E00u, 0x180C4E00u},
    };
    for (const auto& [virtual_address, expected_physical] : conversions) {
        const auto* host_pointer = vram_vma->second.backing_memory.GetPtr() +
                                   (virtual_address - vram_vma->second.base);
        const auto vram_offset = memory.TryGetVRAMOffset(host_pointer);
        REQUIRE(vram_offset.has_value());
        CHECK(*vram_offset + Memory::VRAM_PADDR == expected_physical);
        CHECK_FALSE(memory.TryGetFCRAMOffset(host_pointer).has_value());
    }

    auto unrelated = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
    const auto unrelated_map = process->vm_manager.MapBackingMemory(
        Memory::HEAP_VADDR, MemoryRef{unrelated}, Memory::CITRA_PAGE_SIZE,
        Kernel::MemoryState::Private);
    REQUIRE(unrelated_map.Succeeded());
    const auto unrelated_vma = process->vm_manager.FindVMA(Memory::HEAP_VADDR);
    REQUIRE(unrelated_vma != process->vm_manager.vma_map.end());
    const auto* unrelated_pointer = unrelated_vma->second.backing_memory.GetPtr();
    CHECK_FALSE(memory.TryGetFCRAMOffset(unrelated_pointer).has_value());
    CHECK_FALSE(memory.TryGetVRAMOffset(unrelated_pointer).has_value());
}

TEST_CASE("physical VRAM aliases share the mapped VRAM backing", "[core][memory]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));
    kernel.HandleSpecialMapping(process->vm_manager,
                                {Memory::VRAM_VADDR, Memory::VRAM_SIZE, false, false});

    constexpr u32 offset = 0x324F10;
    constexpr VAddr alias = 0x80000000u | (Memory::VRAM_PADDR + offset);
    constexpr VAddr mapped = Memory::VRAM_VADDR + offset;

    memory.Write8(*process, alias + 1, 0xAB);
    memory.Write16(*process, alias + 2, 0xCDEF);
    memory.Write32(*process, alias + 4, 0x12345678);
    CHECK(memory.Read8(*process, mapped + 1) == 0xAB);
    CHECK(memory.Read16(*process, mapped + 2) == 0xCDEF);
    CHECK(memory.Read32(*process, mapped + 4) == 0x12345678);

    memory.Write8(*process, mapped + 5, 0x42);
    CHECK(memory.Read8(*process, alias + 5) == 0x42);
}

TEST_CASE("plugin framebuffer cache is released before its physical alias", "[core][memory]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1);
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("", 0));

    constexpr VAddr framebuffer_address = Memory::PLUGIN_3GX_FB_VADDR + 0x2E000;
    constexpr u32 framebuffer_offset = 0x04000000;
    constexpr PAddr framebuffer_physical =
        Memory::FCRAM_PADDR + framebuffer_offset + 0x2E000;
    const auto map_result = process->vm_manager.MapBackingMemory(
        framebuffer_address, memory.GetFCRAMRef(framebuffer_offset + 0x2E000),
        Memory::CITRA_PAGE_SIZE, Kernel::MemoryState::Private);
    REQUIRE(map_result.Succeeded());

    memory.SetPlugin3GXFramebufferAddress(Memory::FCRAM_PADDR + framebuffer_offset);
    memory.RasterizerMarkRegionCached(framebuffer_physical, Memory::CITRA_PAGE_SIZE, true);
    const auto page_index = framebuffer_address >> Memory::CITRA_PAGE_BITS;
    REQUIRE(process->vm_manager.page_table->attributes[page_index] ==
            Memory::PageType::RasterizerCachedMemory);

    bool cache_cleared_while_alias_was_valid = false;
    memory.ClearPlugin3GXFramebufferAddress([&] {
        CHECK(memory.Plugin3GXFramebufferAddress() == Memory::FCRAM_PADDR + framebuffer_offset);
        memory.RasterizerMarkRegionCached(framebuffer_physical, Memory::CITRA_PAGE_SIZE, false);
        cache_cleared_while_alias_was_valid = true;
        CHECK(process->vm_manager.page_table->attributes[page_index] == Memory::PageType::Memory);
    });

    CHECK(cache_cleared_while_alias_was_valid);
    CHECK(memory.Plugin3GXFramebufferAddress() == 0);
    CHECK(process->vm_manager.page_table->attributes[page_index] == Memory::PageType::Memory);
}
