// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/memory.h"

namespace Kernel {

TEST_CASE("DSP semaphore signal grace is one-shot and thread-bound", "[core][kernel][dsp]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    KernelSystem kernel(memory, timing, [] {}, MemoryMode::NewProd, 1);
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("dsp-test", 0));

    constexpr u32 semaphore_object_id = 77;
    constexpr u32 signaler = 40;
    constexpr u32 closer = 31;
    constexpr s64 close_ticks = 100000;
    constexpr s64 grace_ticks = 500;

    process->RememberDspSemaphoreSignal(semaphore_object_id, signaler);
    REQUIRE(process->RetireDspSemaphoreSignal(semaphore_object_id, closer,
                                              close_ticks, grace_ticks));

    SECTION("original signaler consumes the token once") {
        CHECK(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 1));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 2));
    }

    SECTION("wrong thread does not consume another thread's token") {
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(closer, close_ticks + 1));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(41, close_ticks + 2));
        CHECK(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 3));
    }

    SECTION("both ends of the grace window are inclusive") {
        CHECK(process->ConsumeDspSemaphoreSignalGrace(signaler,
                                                       close_ticks + grace_ticks));
    }

    SECTION("expired token cannot be consumed") {
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler,
                                                             close_ticks + grace_ticks + 1));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 1));
    }

    SECTION("token cannot be consumed before the event was retired") {
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks - 1));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 1));
    }

    SECTION("a new successful signal clears any pending teardown grace") {
        process->RememberDspSemaphoreSignal(semaphore_object_id, 55);
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(signaler, close_ticks + 1));
    }
}

TEST_CASE("DSP semaphore grace cannot arm from unrelated event or closer", "[core][kernel][dsp]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    KernelSystem kernel(memory, timing, [] {}, MemoryMode::NewProd, 1);
    auto process = kernel.CreateProcess(kernel.CreateCodeSet("dsp-test", 0));

    constexpr s64 close_ticks = 100000;
    constexpr s64 grace_ticks = 500;

    SECTION("no prior semaphore signal") {
        CHECK_FALSE(process->RetireDspSemaphoreSignal(77, 31, close_ticks, grace_ticks));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(40, close_ticks + 1));
    }

    SECTION("wrong event object cannot arm") {
        process->RememberDspSemaphoreSignal(77, 40);
        CHECK_FALSE(process->RetireDspSemaphoreSignal(78, 31, close_ticks, grace_ticks));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(40, close_ticks + 1));
    }

    SECTION("closing thread was also signaler") {
        process->RememberDspSemaphoreSignal(77, 40);
        CHECK_FALSE(process->RetireDspSemaphoreSignal(77, 40, close_ticks, grace_ticks));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(40, close_ticks + 1));
    }

    SECTION("a second close without a new signal cannot rearm") {
        process->RememberDspSemaphoreSignal(77, 40);
        REQUIRE(process->RetireDspSemaphoreSignal(77, 31, close_ticks, grace_ticks));
        CHECK_FALSE(process->RetireDspSemaphoreSignal(77, 31, close_ticks + 2, grace_ticks));
        CHECK_FALSE(process->ConsumeDspSemaphoreSignalGrace(40, close_ticks + 3));
    }
}

TEST_CASE("DSP semaphore signal grace never crosses guest process boundaries",
          "[core][kernel][dsp]") {
    Core::Timing timing(1, 100);
    Core::System system;
    Memory::MemorySystem memory{system};
    KernelSystem kernel(memory, timing, [] {}, MemoryMode::NewProd, 1);

    auto process_a = kernel.CreateProcess(kernel.CreateCodeSet("dsp-a", 0));
    auto process_b = kernel.CreateProcess(kernel.CreateCodeSet("dsp-b", 0));

    // The service may expose the same underlying semaphore object to both
    // guest processes, but their handle tables and grace tokens remain separate.
    process_a->RememberDspSemaphoreSignal(77, 40);
    REQUIRE(process_a->RetireDspSemaphoreSignal(77, 31, 100000, 500));

    CHECK_FALSE(process_b->ConsumeDspSemaphoreSignalGrace(40, 100001));
    CHECK(process_a->ConsumeDspSemaphoreSignalGrace(40, 100001));
}

} // namespace Kernel
