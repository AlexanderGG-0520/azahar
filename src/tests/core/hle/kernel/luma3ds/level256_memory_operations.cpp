// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/luma3ds/level256_memory_operations.h"

TEST_CASE("Level256 memory backend accepts only the observed unsafe COMMIT operation",
          "[core][kernel][luma3ds][memory-operations]") {
    using namespace Kernel;
    using namespace Kernel::Luma3DS;
    const auto matches = [](u32 output = 0x070F5664,
                            u32 target = LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET,
                            u32 size = LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE,
                            u32 operation = LEVEL256_CONTROL_MEMORY_UNSAFE_OPERATION,
                            u32 permissions = LEVEL256_CONTROL_MEMORY_UNSAFE_PERMISSIONS) {
        return ShouldHandleLevel256ControlMemoryUnsafe(
            true, true, LEVEL256_DIAGNOSTIC_TITLE_ID, 16, 16, true, LEVEL256_DIAGNOSTIC_CODE_BEGIN,
            LEVEL256_DIAGNOSTIC_CODE_END, LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_PC,
            LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID, output, target, size, operation, permissions);
    };

    REQUIRE(matches());
    REQUIRE_FALSE(matches(0));
    REQUIRE_FALSE(matches(0x070F5664, LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET + 0x1000));
    REQUIRE_FALSE(matches(0x070F5664, LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET,
                          LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE - 0x1000));
    REQUIRE_FALSE(matches(0x070F5664, LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET,
                          LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE, 0x10203));
    REQUIRE_FALSE(matches(0x070F5664, LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET,
                          LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE,
                          LEVEL256_CONTROL_MEMORY_UNSAFE_OPERATION, 0x7));
}
