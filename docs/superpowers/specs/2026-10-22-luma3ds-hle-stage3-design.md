# Luma3DS HLE Stage 3: Structural Separation Design

## Goal

Remove Level256 ABI handling and compatibility side effects from `svc.cpp` while preserving the currently observed Shinuchi behavior. Keep Luma extension semantics independent from Level256 title and digest policy, and label the existing title-specific backend as temporary rather than generic.

## Constraints

- Preserve the current uncommitted worktree and the verified backup at `.local-backups/level256-hle-stage2-20261022/`.
- Do not modify the official 3GX, perform destructive Git operations, or commit, push, or merge.
- Unknown CustomBackdoor callbacks and unsupported memory operations must fail explicitly.
- Compatibility mode off must preserve the normal Azahar SVC behavior.
- Preserve HOME, SOC, 3GX path, LFCS, and rendering changes.

## Architecture

### Authorization and policy

Keep two independent inputs:

- A Luma extension execution context describing whether the opt-in compatibility environment is active and which verified plugin mapping owns it.
- A Level256 plugin policy describing supported title, official digest verification, load state, owner PID, and mapped code ranges.

Generic Luma SVC meaning consumes only Luma context and caller identity. Level256 policy may authorize a Level256-specific backend operation, but must not alter generic SVC semantics.

### SVC request and result boundary

Add a kernel-owned compatibility dispatcher in `src/core/hle/kernel/luma3ds/`. `svc.cpp` captures the SVC ID, entry PC, process/thread identity, and argument registers into a request; supplies the required kernel/process/memory services through an execution context; invokes one dispatcher; and reflects only the returned result/register updates. Each recognized SVC is handled once. Existing ordinary SVC dispatch remains unchanged when the extension context does not authorize the caller.

The dispatcher owns common `GetSystemInfo(0x20000, 0)` response generation and routes Level256 `0x30000` verification through a policy-gated response provider. It does not change Azahar's emulator identity.

### CustomBackdoor

Implement the Luma CustomBackdoor call convention at the compatibility boundary. Do not execute arbitrary guest callback addresses as host pointers or as privileged host code. The common backend rejects callbacks it cannot safely execute. A separate temporary Level256 backend recognizes only the existing verified callback operations and validates the callback bytes, plugin ownership, arguments, target memory ranges, and expected prior values before applying effects.

For each supported callback, keep the operation name, preconditions, guest-visible side effect, and return register result explicit. The temporary backend remains identified as Level256-specific; it is not presented as general CustomBackdoor support.

### Level256 process-view manager

Move guest mapping allocation, ABI projection construction, field synchronization, and lifetime cleanup into a Level256-specific manager. The manager owns its guest address and associated backing mapping; it accepts the target process and current runtime values explicitly. `KProcessViewV1` remains a narrow Level256 guest ABI projection, not an Azahar or generic Luma kernel object.

The manager exposes bounded operations needed by the current callbacks, such as ensure/create view, update a resource field, update the guest flag, and copy a validated snapshot. A failure does not report success to the guest.

### ControlMemoryUnsafe

Move the existing exact COMMIT compatibility path into an isolated Level256 memory-operation backend. Keep the current memory-state, permission, range, ownership, overlap, and output-pointer checks. All operations outside that verified request return an explicit unsupported/failure result; this stage does not implement generic `ControlMemoryUnsafe` or `ControlMemoryEx`.

### Diagnostics

Keep game-specific fixed-PC and OSD instrumentation in `level256_diagnostic.h` or a dedicated diagnostics module. Remove operation execution from diagnostic helpers as it migrates. Diagnostic state must not be used as the authoritative lifetime owner for compatibility mappings.

## Intended file boundaries

- `src/core/hle/kernel/luma3ds/compatibility.h/.cpp`: common Luma context, request/result types, authorization, and dispatcher interface.
- `src/core/hle/kernel/luma3ds/system_info.h/.cpp`: common Luma marker response and Level256 policy-gated verification response.
- `src/core/hle/kernel/luma3ds/custom_backdoor.h/.cpp`: dispatcher and explicit unsupported result for arbitrary callbacks.
- `src/core/hle/kernel/luma3ds/level256_backend.h/.cpp`: temporary verified Level256 callback and memory operation routing.
- `src/core/hle/kernel/luma3ds/level256_process_view.h/.cpp`: Level256 ABI projection lifecycle and validated field/copy operations.
- `src/core/hle/kernel/svc.cpp`: SVC argument capture, common/Level256 authorization facts, one dispatch call, and guest register/result reflection.
- `src/tests/core/hle/kernel/luma3ds/`: authorization, callback, process-view lifetime, memory-operation, and SVC-off regression coverage.

Exact filenames may follow the existing CMake conventions if inspection shows a better local pattern; the responsibility boundaries above remain.

## Failure behavior

- Compatibility mode off or unauthenticated caller: no extension dispatch; ordinary Azahar behavior stays intact, and extension IDs fail with the current explicit unsupported result.
- Unknown CustomBackdoor callback: return an explicit failure; never echo the callback pointer as success.
- Invalid process/view/range/permission: do not mutate state and return the appropriate existing kernel result.
- Unsupported memory operation: explicit failure, no mapping or permission changes.
- No Level256 policy: generic Luma behavior remains available only under its own Luma context; Level256 verification response is not supplied.

## Verification

- Unit tests prove the dispatcher is separate from Level256 policy, exact known callback operations preserve current semantics, unknown callbacks fail, cross-process calls are rejected, view allocation/update/cleanup are owner-scoped, and invalid memory requests have no side effects.
- Existing tests continue to cover HOME/service redirects, SOC address conversion, 3GX path and loader verification, LFCS IPC, graphics, and memory behavior.
- Run `ninja -C build tests azahar`, `ctest --test-dir build --output-on-failure`, and `git diff --check`.
- No real-game result is claimed unless the game is launched in this session.

## Known limitations after this stage

This stage separates the observed Shinuchi callback backend but does not make arbitrary CustomBackdoor callback execution safe or implement all Luma SVC hooks. `CreateThreadHook`, `ControlMemoryEx`, and generic `ControlMemoryUnsafe` remain separate future work unless source inspection proves a small existing-compatible operation is needed for extraction.
