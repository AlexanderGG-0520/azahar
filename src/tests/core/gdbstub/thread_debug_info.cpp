// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>

#include "core/gdbstub/thread_debug_info.h"

TEST_CASE("GDB thread diagnostics expose wait state without changing execution",
          "[core][gdbstub]") {
    GDBStub::ThreadDebugInfo arbiter_wait{
        .thread_id = 36,
        .pc = 0x070A1274,
        .status = Kernel::ThreadStatus::WaitArb,
        .wait_address = 0x060B93DC,
        .wait_objects = {{7, "AddressArbiter"}},
    };

    REQUIRE(GDBStub::FormatThreadDebugName(arbiter_wait) ==
            "Thread 36 [WaitArb pc=070A1274 wait=060B93DC object0=AddressArbiter:7]");

    GDBStub::ThreadDebugInfo event_wait{
        .thread_id = 31,
        .pc = 0x070A0000,
        .status = Kernel::ThreadStatus::WaitSynchAny,
        .wait_address = 0x06000000,
        .wait_objects = {{9, "Event"}, {10, "Thread"}},
    };

    const std::string event_name = GDBStub::FormatThreadDebugName(event_wait);
    REQUIRE(event_name.find("WaitSynchAny") != std::string::npos);
    REQUIRE(event_name.find("wait=") == std::string::npos);
    REQUIRE(event_name.find("object0=Event:9") != std::string::npos);
    REQUIRE(event_name.find("object1=Thread:10") != std::string::npos);

    event_wait.wait_objects = {{11, "Event&Timer"}};
    REQUIRE(GDBStub::FormatThreadDebugName(event_wait).find("Event&amp;Timer") !=
            std::string::npos);
}

TEST_CASE("GDB thread diagnostics bound wait-object output", "[core][gdbstub]") {
    GDBStub::ThreadDebugInfo info{
        .thread_id = 1,
        .pc = 0x00100000,
        .status = Kernel::ThreadStatus::WaitSynchAll,
    };
    for (u32 i = 0; i < 6; ++i) {
        info.wait_objects.push_back({i, "Event"});
    }
    info.wait_object_count = info.wait_objects.size();

    const std::string name = GDBStub::FormatThreadDebugName(info);
    REQUIRE(name.find("object3=Event:3") != std::string::npos);
    REQUIRE(name.find("object4=") == std::string::npos);
    REQUIRE(name.find("objects_truncated=2") != std::string::npos);
}
