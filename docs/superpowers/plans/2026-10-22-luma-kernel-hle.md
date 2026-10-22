# Luma3DS Kernel Extension HLE Implementation Plan

> **For agentic workers:** This plan is executed inline in the authorized user task. No commits are permitted.

**Goal:** Replace title-gated Level256 kernel patches with a reusable, authenticated Luma3DS kernel-extension HLE layer while preserving unrelated Azahar fixes.

**Architecture:** Keep Level256 title selection, official plugin verification, and LFCS IPC in PLGLDR. Provide a title-independent kernel compatibility context carrying mode, loaded image verification, owner PID, and mapped plugin range. Dispatch implemented Luma extension semantics through focused kernel modules; keep proprietary Level256 verification behind a separate verified-plugin adapter. Keep game-specific address diagnostics outside the common SVC layer.

**Tech Stack:** C++20, Azahar HLE kernel, Dynarmic invalidation API, Catch2 tests, Ninja/CMake.

**Spec:** User request dated 2026-10-22.

## Global Constraints

- Preserve HOME return, generic 3GX loading, Level256 path selection, SOC address safety, and PLGLDR LFCS IPC.
- Do not modify the official 3GX or fabricate LFCS/credentials.
- Do not change ordinary Azahar GetSystemInfo identifiers or bypass process authorization.
- Do not use title-specific guest addresses in common kernel dispatch.
- Do not commit, push, merge, reset hard, or clean the repository.

## Review Focus

- Unauthenticated or cross-process SVC requests remain denied; test owner/hash/range mismatches.
- Unsupported CustomBackdoor callbacks return a real unsupported result; test callback and argument mismatches.
- Cache operations preserve Dynarmic code invalidation semantics; test range and whole-cache paths.
- Memory extensions validate guest mappings and permissions; test invalid handles, overflow, and conflicting mappings.
- Level256 path/LFCS and HOME/SOC changes survive targeted rollback; run their existing tests.

## File Boundaries

- `src/core/hle/kernel/luma3ds/`: reusable compatibility context and SVC dispatch for extension semantics.
- `src/core/hle/service/plgldr/`: Level256 title policy, official plugin digest, path selection, and LFCS only.
- `src/core/hle/kernel/svc.cpp`: ordinary SVC entry points delegate to the new layer without title-specific behavior.
- `src/tests/core/hle/kernel/luma3ds/`: unit tests for context authorization, SVC semantics, and memory/cache behavior.
- Existing `src/core/arm/dynarmic` and kernel wait diagnostics: remove title-specific tracing from execution paths; retain or replace only bounded generic events.

## Tasks

### Task 1: Preserve and classify current work

- [x] Record branch/status/diff/log/reflog and save tracked, untracked, and submodule changes under `/tmp/azahar-level256-hle-backup-20261022`.
- [ ] Classify each Level256 patch versus HOME, loader/path, SOC, LFCS, and generic 3GX changes before editing.
- [ ] Read authoritative Level256 Luma sources and compare existing Azahar implementations.

### Task 2: Add the title-independent compatibility context

- [ ] Add tests for mode, verified image, owner PID, and mapped code-range authorization; Red Cat Corps and White Dog Corps must share the same policy, and an unrelated PID/hash/range must fail.
- [ ] Implement the context and expose it from PLGLDR without embedding title policy in kernel SVC code.
- [ ] Keep Level256 title list, official digest, path selection, and LFCS in the plugin policy/service layer.

### Task 3: Implement verified Luma SVC dispatch

- [ ] Test regular Azahar fall-through and authenticated Luma marker responses.
- [ ] Implement GetSystemInfo, CustomBackdoor callback dispatch, and cache operations in focused modules.
- [ ] Implement only memory operations whose Luma ABI and Azahar mapping semantics are confirmed; reject unsupported or unsafe requests with ordinary errors.
- [ ] Keep CreateThread's normal resource/priority checks and expose only generic bounded lifecycle diagnostics.

### Task 4: Remove old patch dispatch and diagnostics

- [ ] Remove title-specific callbacks, memory grants, forced responses, and per-SVC Level256 branches from `SVC::CallSVC`.
- [ ] Remove game-specific OSD address reads from common ARM and kernel tracing; retain them only in an explicitly Shinuchi-scoped diagnostic.
- [ ] Preserve plugin loader path/hash/LFCS, SOC safety, HOME, Wayland/OpenGL, and unrelated memory mapping changes.

### Task 5: Verify

- [ ] Run `ninja -C build tests azahar`.
- [ ] Run `ctest --test-dir build --output-on-failure`.
- [ ] Run `git diff --check` and review the protected-file diff.
- [ ] Report runtime verification separately; do not claim game behavior without launching the game.
