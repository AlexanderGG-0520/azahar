# Luma3DS HLE Stage 3 Implementation Plan

> **For agentic workers:** This plan is executed inline in the current session. Steps use checkbox syntax for tracking. Do not create commits; the user explicitly forbids commits, push, and merge.

**Goal:** Move Level256-specific HLE effects out of `svc.cpp` while preserving observed Shinuchi behavior and making Luma authorization, Level256 policy, process-view lifetime, and one-dispatch-path-per-SVC-entry explicit.

**Architecture:** `svc.cpp` captures guest SVC arguments and applies dispatcher results. A Luma-owned dispatcher handles only authorized extension calls; a separate Level256 backend performs the currently verified callback and exact memory operations. A process-view manager owns the guest mapping, retains ownership after a live-process unmap failure, and clears it only after successful unmap or confirmed Process destruction.

**Tech Stack:** C++20, Azahar kernel HLE, Catch2, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-10-22-luma3ds-hle-stage3-design.md`, plus the user's stage-3 requirements in the conversation.

## Global Constraints

- Preserve all existing uncommitted changes and `.local-backups/level256-hle-stage2-20261022/`.
- Do not use destructive Git operations or commit, push, or merge.
- Do not modify the official 3GX or guest game code.
- Keep ordinary Azahar SVC behavior unchanged; only authorized Luma extension requests use the compatibility dispatcher.
- Keep Luma environment context separate from Level256 title/hash policy.
- Never execute arbitrary guest callback code as a host pointer; unknown callbacks and unsupported memory operations fail explicitly.
- Ensure each migrated SVC has exactly one implementation path; remove its old `svc.cpp` branch.
- Do not discard process-view ownership after an unmap failure while its Process remains alive; retry release and clear without unmapping only after Process destruction is confirmed.
- Keep the existing verified instruction patch limited to the plugin guest RAM mapping; never patch the 3GX disk file or game executable code.
- Prevent the same SVC entry from reaching both old and new handlers without suppressing later valid calls to the same callback.
- Preserve HOME, SOC, 3GX path, LFCS, rendering, and existing camera changes.
- Do not add generic SVC semantics beyond what existing evidence and Azahar APIs support.

## Review Focus

- Compatibility mode off: normal SVC results and guest registers remain unchanged; covered by Task 1 disabled-dispatch regression.
- Correct Luma context but wrong owner, code range, or verification facts: denied before effects; covered by Task 1 authorization matrix.
- Process exits and the same PID/address is reused: no old view address or backing mapping survives; covered by Task 2 release/recreate test.
- A live process rejects unmapping: manager ownership is retained and replacement is blocked until retry or confirmed Process destruction; covered by Task 2 failed-release test.
- Invalid view, copy, and memory ranges: no guest memory or resource-limit mutation; covered by Tasks 2–4 invalid-input tests.
- A later valid callback call is not blocked by a global consumed flag, and an unknown callback does not succeed; covered by Task 3 repeat-call/unknown-callback tests.
- Instruction patch target: remains in verified plugin guest RAM and never targets disk 3GX or game code; covered by Task 3 target-range test.

---

### Task 1: Separate extension authorization and centralize SVC dispatch

**Files:**
- Modify: `src/core/hle/kernel/luma3ds/compatibility.h/.cpp`
- Modify: `src/core/hle/kernel/luma3ds/level256_system_info.h`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/hle/service/plgldr/plgldr.h/.cpp`
- Modify: `src/core/hle/kernel/svc.cpp`
- Test: `src/tests/core/hle/kernel/luma3ds/compatibility_dispatch.cpp`
- Modify: `src/tests/CMakeLists.txt`

**Interfaces:**
- `CompatibilityContext` contains only Luma compatibility enablement and active plugin mapping facts; it contains no Level256 title ID or official hash field.
- `PluginAuthorization` contains verification status and the authorized owner/code ranges, without defining SVC semantics.
- `CanDispatchExtension(context, authorization, pid, pc)` is the sole authorization predicate for extension dispatch.
- `SvcRequest` carries SVC ID, entry PC, and guest register arguments r0–r7.
- `SvcDispatchOutcome` distinguishes ordinary-HLE delegation, explicit rejection, and a handled response with only the guest registers to update.
- `DispatchExtensionSvc(request, execution_context)` handles verified extension operations once. A post-handler GetSystemInfo hook uses the same response module but does not alter ordinary dispatch.

- [x] **Step 1: Add failing dispatch and authorization tests** for mode-off, invalid plugin proof, wrong PID/range, authorized extension, and a normal non-extension SVC.
- [x] **Step 2: Run the focused test** and confirm failure was due to the missing separated authorization/dispatch behavior.
- [x] **Step 3: Implement the common context, authority input, request/result types, and dispatcher interface.** Move Luma marker response selection into the compatibility module; PLGLDR supplies verified authority separately from Level256's `0x30000` response policy.
- [x] **Step 4: Route extension IDs through the dispatcher before ordinary fallback.** `svc.cpp` captures r0-r7 and applies result registers; the old inline response choice is removed.
- [x] **Step 5: Run the focused tests** and verify the disabled path leaves ordinary register/result behavior intact.

### Task 2: Give the Level256 process view an explicit owner and lifetime

**Files:**
- Create: `src/core/hle/kernel/luma3ds/level256_process_view.h/.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/hle/kernel/process.cpp` for confirmed destruction notification
- Modify: `src/core/hle/service/plgldr/plgldr.h/.cpp`
- Modify: `src/core/hle/kernel/svc.cpp`
- Test: `src/tests/core/hle/kernel/luma3ds/level256_process_view.cpp`
- Modify: `src/tests/CMakeLists.txt`

**Interfaces:**
- `Level256ProcessViewManager::Ensure(Process&, MemorySystem&, ViewData)` creates or returns the view owned by that process.
- `Level256ProcessViewManager::UpdateResourceLimit(...)`, `UpdateGuestFlag(...)`, and `CopySnapshot(...)` validate the owned mapping and perform the existing bounded operations.
- `Level256ProcessViewManager::Release(Process&)` unmaps only the matching owner view. On success it clears the record; on failure while the process remains alive it retains owner/address/backing metadata in a release-pending state and blocks replacement until retry succeeds.
- `Level256ProcessViewManager::OnProcessDestroyed(Process&)` clears only the matching record after destruction is confirmed; remaining VMAs are then reclaimed by the dying Process's VM manager. A live unmap failure is never treated as destruction.
- `ResetForNewPlugin(Process&)` retries release for a same-process stale view and refuses replacement if it still cannot be unmapped.
- The manager is owned by PLGLDR runtime state, not by serialized diagnostics or an address-only field in `PluginLoaderContext`.

- [x] **Step 1: Add lifetime tests** for ownership retention, wrong-owner cleanup rejection, confirmed destruction, and same-PID/address reuse after clearing the prior lifetime.
- [x] **Step 2: Run the focused process-view test** and establish the address-only plugin context did not satisfy the manager lifecycle contract.
- [x] **Step 3: Implement the manager** using `KProcessViewV1` as the only guest ABI layout; move mapping allocation, construction, field access, bounded copies, retryable release, and confirmed-destruction cleanup into it.
- [x] **Step 4: Connect PLGLDR process start/exit hooks and `Process` destruction** to manager reset/release/destruction notification; remove `level256_process_view_address` as lifecycle source of truth. Live unmap failure retains the record until retry or confirmed destruction.
- [x] **Step 5: Run process-view tests** for retained ownership and lifetime/address reuse.

### Task 3: Move CustomBackdoor operations into explicit backends

**Files:**
- Create: `src/core/hle/kernel/luma3ds/custom_backdoor.h/.cpp`
- Create: `src/core/hle/kernel/luma3ds/level256_backend.h/.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/hle/kernel/svc.cpp`
- Modify: `src/core/hle/kernel/level256_diagnostic.h`
- Test: `src/tests/core/hle/kernel/luma3ds/custom_backdoor.cpp`
- Modify: `src/tests/CMakeLists.txt`

**Interfaces:**
- Common `CustomBackdoor::Dispatch(request, execution_context)` validates the Luma ABI request and returns explicit unsupported for arbitrary callbacks.
- `Level256Backend::HandleCustomBackdoor(request, process_view_manager, execution_context)` recognizes only callbacks with matching verified plugin mapping, callback code signature, caller, expected arguments, owned view, and expected old guest/HLE values.
- Each operation returns `{handled, result, operation}`; `svc.cpp` applies the guest r0 result only when handled. The backend never receives or calls a host function pointer derived from guest r0.
- Supported operations retain explicit names for the observed max-priority update, current-process view creation, guest view-flag update, thread-limit update, validated snapshot copy, and exact instruction patch. Unknown or mismatched operations are rejected.
- The instruction patch writes only the existing verified target in the plugin guest RAM mapping after checking that it lies within the verified plugin code range and the expected old instruction matches. The 3GX disk file and game code are never write targets.
- Single dispatch is ensured by one SVC call site after migration. The backend stores no consumed-callback set; later legitimate calls are evaluated from current guest/HLE state and may run again when their preconditions match.

- [x] **Step 1: Add regression tests** for verified ABI admission, invalid callbacks, ordinary SVC bypass, and repeat requests without a global consumed flag.
- [x] **Step 2: Run the focused CustomBackdoor tests** before extracting the existing callback backend.
- [x] **Step 3: Implement common ABI validation and the temporary Level256 backend** using the Task 2 manager for view generation/access/copy. Existing checks remain; the patch target must be inside verified plugin guest RAM.
- [x] **Step 4: Replace the inline CustomBackdoor block in `svc.cpp` with one backend call** and remove the old callback effect branch.
- [x] **Step 5: Run CustomBackdoor tests** and compare the retained operation result paths by code review.

### Task 4: Isolate the exact ControlMemoryUnsafe operation

**Files:**
- Create: `src/core/hle/kernel/luma3ds/level256_memory_operations.h/.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/hle/kernel/luma3ds/level256_backend.h/.cpp`
- Modify: `src/core/hle/kernel/svc.cpp`
- Modify: `src/core/hle/kernel/level256_diagnostic.h`
- Test: `src/tests/core/hle/kernel/luma3ds/level256_memory_operations.cpp`
- Modify: `src/tests/CMakeLists.txt`

**Interfaces:**
- `Level256MemoryOperations::ControlMemoryUnsafe(request, execution_context, view_manager)` owns the current verified exact COMMIT behavior and returns a kernel `Result`.
- Existing VMA, permission, overflow, output-pointer, overlap, backing-memory allocation, and map-result checks remain in this function.
- Any request outside the current exact supported operation returns the current explicit unsupported/failure result and has no side effects; no generic `ControlMemoryUnsafe` or `ControlMemoryEx` semantics are added.

- [x] **Step 1: Add regression tests** for the exact valid COMMIT ABI and invalid output, target, size, operation, and permissions.
- [x] **Step 2: Run the focused memory-operation test** before extraction.
- [x] **Step 3: Implement the module** against existing `VMManager`/`MemorySystem` APIs without changing global memory semantics.
- [x] **Step 4: Replace the A3 effect branch in `svc.cpp` with one backend call**; retain only its low-frequency ABI diagnostic.
- [x] **Step 5: Run memory-operation tests** for strict request matching and explicit failure of unhandled calls.

### Task 5: Remove duplicate paths, audit residual Level256 coupling, and verify

**Files:**
- Modify: `src/core/hle/kernel/svc.cpp`
- Modify: `src/core/hle/kernel/level256_diagnostic.h` only for execution helpers no longer needed by diagnostics
- Modify: `src/core/hle/service/plgldr/plgldr.h/.cpp` for obsolete address-only state
- Modify: relevant `src/tests/core/hle/kernel/luma3ds/` tests

- [x] **Step 1: Audit every Level256 symbol reachable from `SVC::CallSVC`** and remove migrated callback/view/memory execution branches and duplicate state ownership. The remaining Process View overlap diagnostic now delegates range arithmetic to the manager; SVC no longer reads its ABI fields.
- [x] **Step 2: Verify diagnostics remain observational only** and are not called as compatibility behavior. Remaining Level256-specific traces only inspect/log guest state; effectful callback and A3 operations are delegated to their dedicated backends.
- [x] **Step 3: Run `ninja -C build tests azahar`.** Both test and GUI binaries linked successfully.
- [x] **Step 4: Run `ctest --test-dir build --output-on-failure`.** 129/136 passed, 7 skipped, 0 failed.
- [x] **Step 5: Run `git diff --check` and inspect `git status --short`.** No whitespace errors; pre-existing dirty files and `.local-backups/` remain present.
- [x] **Step 6: Report residual Level256-dependent code by file and line, classifying each item as Luma common, Level256 backend isolated, temporary compatibility, or unimplemented.** Real-game execution was not performed in this session.

## Completion Criteria

- `svc.cpp` does not manage the Level256 ABI layout or contain migrated CustomBackdoor and ControlMemoryUnsafe effect branches.
- Only authorized extension calls enter the dispatcher; normal Azahar SVC behavior remains unchanged when compatibility is off.
- Luma environment context and Level256 title/hash authorization are different types and separate inputs.
- Process-view allocation, update, release, and restart cleanup have one owner and regression coverage.
- A live-process unmap failure retains recoverable ownership; only confirmed Process destruction discards the residual view record.
- The existing instruction patch writes only plugin guest RAM; the 3GX on-disk file and game executable are not targets.
- Later legitimate callback calls are not suppressed by artificial one-shot state.
- Each migrated SVC has one execution path; the prior `svc.cpp` branch is removed.
- Existing regression suite passes, `git diff --check` is clean, and remaining temporary operations/unimplemented SVCs are identified precisely.
