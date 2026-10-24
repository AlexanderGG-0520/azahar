# Level256 Luma3DS compatibility matrix

## Audit basis and scope

The primary source audited for this document is `Tekito-256/Luma3DS-level256`, branch
`master`, commit `4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e` (checked out locally for
read-only inspection). The main SVC hook table is
`k11_extension/source/svc.c:buildAlteredSvcTable()`. ABI details are in
`k11_extension/source/svc/`, `k11_extension/include/svc/`, and the wrapper assembly in
`k11_extension/source/svc/wrappers.s`. Rosalina and loader service behavior is in
`sysmodules/rosalina/source/plugin/` and `k11_extension/source/svc/SendSyncRequest.c`.

Pinned source references: [altered SVC table](https://github.com/Tekito-256/Luma3DS-level256/blob/4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e/k11_extension/source/svc.c),
[wrapper assembly](https://github.com/Tekito-256/Luma3DS-level256/blob/4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e/k11_extension/source/svc/wrappers.s),
[MapProcessMemoryEx](https://github.com/Tekito-256/Luma3DS-level256/blob/4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e/k11_extension/source/svc/MapProcessMemoryEx.c),
[ControlProcess](https://github.com/Tekito-256/Luma3DS-level256/blob/4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e/k11_extension/source/svc/ControlProcess.c),
and [SendSyncRequest hooks](https://github.com/Tekito-256/Luma3DS-level256/blob/4f2d5b0cfd50aca45fc10ac41d15fc351bfb689e/k11_extension/source/svc/SendSyncRequest.c).

This is a source-to-source compatibility audit, not proof that every path is used by
Level256 v2.0.1. “Complete” is reserved for a specifically tested ABI path. A source
implementation that Azahar does not safely model remains unavailable to signed plugins.
The existing read-only startup diagnostics are in
[`level256-noexecute-diagnostics.md`](level256-noexecute-diagnostics.md).

## Altered SVC table

`buildAlteredSvcTable()` copies the stock table and replaces the entries below. Standard
SVC numbers use the ordinary Azahar HLE unless noted. Luma-only extension numbers are
individually capability-gated; a valid publisher signature does not make unknown ABI
operations available.

| SVC | Level256 Luma handler | Azahar status | Capability / decision | Evidence and remaining gap |
|---:|---|---|---|---|
| `01` | `ControlMemoryHookWrapper` | Partial | Ordinary SVC path; no new Level256 capability | Luma adds its own loader/region checks and memory-layout notifications; those kernel internals are not represented as a Level256-only operation. |
| `03` | `ExitProcessHookWrapper` | Partial | Ordinary process exit | Luma signals its Rosalina exit event before forwarding. No Level256-specific exit-event ABI is exposed. |
| `08` | `CreateThreadHookWrapper` (New 3DS only) | Partial | Ordinary `CreateThread` | Luma applies affinity/thread-redirection logic; exact scheduler-hook behavior is not equivalent to the normal HLE. |
| `29` | `GetHandleInfoHookWrapper` | Partial | Ordinary `GetHandleInfo` | Luma adds Rosalina/debug-specific observations. No equivalent private kernel object metadata is fabricated. |
| `2A` | `GetSystemInfoHookWrapper` | Partial | `SystemInfo`, opt-in + verified owner/code scope | The Level256/Luma marker query is narrowly implemented; other Luma-private info values are not implied. |
| `2B` | `GetProcessInfoHookWrapper` | Partial | Ordinary `GetProcessInfo` | Luma adds private process information; unsupported private selectors remain unimplemented. |
| `2C` | `GetThreadInfoHookWrapper` | Partial | Ordinary `GetThreadInfo` | Luma adds TLS/private selector behavior; exact hook-specific semantics are not generally reproduced. |
| `2D` | `ConnectToPortHookWrapper` | Partial | Ordinary service-manager connection plus process redirect | Level256 service redirection is handled separately and is scoped by its service policy. |
| `32` | `SendSyncRequestHook` | Partial | Separate UDS/NDM/SOC IPC capabilities | Only recognized service/command combinations are transformed; all other IPC goes through the ordinary HLE server. |
| `3C` | `BreakHook` | Partial | Ordinary `Break` | Rosalina debug processes receive the stock handler; the debug-event distinction is not a Level256 plugin ABI. |
| `59` | `SetGpuProt` | Unavailable | No capability | This changes GPU memory protection and has no safe Level256 HLE adapter. |
| `5A` | `SetWifiEnabled` | Unavailable | No capability | Luma-specific wireless control is not mapped to a guest-wide host toggle. |
| `7B` | `Backdoor` | Unavailable | No capability | Supervisor-mode arbitrary callback is not safely representable as a general guest HLE. |
| `7C` | `KernelSetStateHook` | Partial / unavailable selectors | No Level256 capability | Luma's private kernel-state selectors depend on internal kernel objects and side effects. |
| `80` | `CustomBackdoor` | Partial, operation-specific | `CustomBackdoor`, opt-in + verified owner/code/callback and per-operation checks | The implemented adapter supports only audited Level256 callback shapes. Other callbacks fail closed. |
| `90` | `convertVAToPA` | Partial | `ConvertVaToPa`, scoped to verified plugin | Returns a physical address only when Azahar can establish the backing allocation. No host pointer is exposed. |
| `91` | `flushDataCacheRange` | Complete as coherent-memory no-op | `DataCacheMaintenance` | Azahar's emulated guest memory is coherent for this operation. |
| `92` | `flushEntireDataCache` | Complete as coherent-memory no-op | `DataCacheMaintenance` | Same coherent-memory contract. |
| `93` | `invalidateInstructionCacheRange` | Partial | `InstructionCacheMaintenance` | Invalidates Dynarmic translated code; range details follow Azahar's cache interface. |
| `94` | `invalidateEntireInstructionCache` | Partial | `InstructionCacheMaintenance` | Invalidates translated code across emulated cores. |
| `A0` | `MapProcessMemoryExWrapper` | Partial, validated mapping path | `MapProcessMemoryEx`, opt-in + verified owner/code | Both wrapper ABIs, `CUR_PROCESS_HANDLE`, page truncation, source/destination ranges, backing aliases, PRIVATE/SHARED state and non-executable RW mappings are handled. The implementation rejects IO/reserved/non-readable backing and preflights destination freedom. Runtime Level256 startup confirmation remains outstanding. |
| `A1` | `UnmapProcessMemoryEx` | Implemented internally; withheld from newly signed artifacts | Legacy exact-hash profile only | Correctly supports process-handle resolution and cache invalidation, but Azahar does not yet track map ownership tightly enough to prove an unmap is limited to a prior `A0` mapping. |
| `A2` | `ControlMemoryEx` | Unimplemented | No capability | Its ABI includes allocation, free, map/unmap, permission, region and loader-mode semantics. A standard `ControlMemory` call is not equivalent. |
| `A3` | `ControlMemoryUnsafeWrapper` | Partial, observed operations only | `ControlMemoryUnsafe`, opt-in + verified owner/code | The adapter limits operations and arguments to audited requests. It does not reproduce Luma's unchecked allocator or arbitrary memory-state changes. |
| `B0` | `ControlService` | Unimplemented | No capability | Steals globally registered client sessions or writes service names; Azahar has no safe equivalent to Luma's internal session registry. |
| `B1` | `CopyHandleWrapper` | Unimplemented | No capability | Cross-process kernel handle-table copying needs object rights/lifetime semantics absent from the public HLE interface. |
| `B2` | `TranslateHandleWrapper` | Unimplemented | No capability | The official output includes a raw kernel address. A host address is not an ABI-compatible or safe substitute. |
| `B3` | `ControlProcess` | Partial existing HLE handler | Not a newly authorized Level256 capability | Several process operations are not implemented; `SET_MMU_TO_RWX` is a broad permission change and is not enabled by this Level256 change. Existing handler exposure predates this audit and requires a separate compatibility/security change to alter without breaking general plugin behavior. |

### Extension authorization

For `0x80`, `0x90`–`0x94`, and `0xA0`–`0xA3`, the central SVC dispatch requires
compatibility opt-in, verified plugin integrity, the mapped plugin owner PID, an address
inside the plugin/bootloader code scope, and the capability for that SVC. `A2` and unknown
extension IDs return `ResultNotImplemented`. `A1`, `B0`–`B2`, and unknown contracts are
not granted to newly signed artifacts. The existing exact legacy hash profile remains a
separate compatibility exception.

## Memory and process extension ABI notes

| Contract | Official source behavior | Azahar mapping / boundary |
|---|---|---|
| `A0` ABI v1 | `r0=dst process`, `r1=dst VA`, `r2=src process`, `r3=src VA`, size on stack; wrapper clears flags | `DecodeMapProcessMemoryExRequest()` preserves the old ABI and forces flags to zero. |
| `A0` ABI v2 | Wrapper recognizes `r0=0xFFFFFFF2`; `r6=dst process`, `r5=flags`; other arguments remain in `r1`–`r4`/stack | Exact marker selects v2; only `PRIVATE` bit 0 is accepted. |
| `A0` size/range | Size is truncated to whole `0x1000` pages; source block list can cross blocks; destination must be Free | Azahar truncates the byte size, checks address overflow/range, source VMA readability/backing and destination Free state before aliasing actual backing blocks. It does not copy data. |
| `A0` state/permission | `PRIVATE=0xBB05`, otherwise `SHARED=0x5806`; `MEMPERM_RW | 0x18`, non-executable | VM state is PRIVATE/SHARED; permissions remain RW and non-X. Failed preflight leaves destination unchanged; unexpected partial map failure rolls back. |
| `A0` cache | Official implementation invalidates I-cache and flushes D-cache | Azahar memory is coherent; translated instruction caches are invalidated across cores after success. |
| `A1` | Unmaps a page-count range in chosen process; supports current-process pseudo-handle on newer kernels; cache maintenance follows | Generic unmap semantics exist, but newly signed artifacts do not receive the capability because exact prior-map ownership is not recorded. |
| `A2` | Extended `ControlMemory` supports map/unmap/alloc/free, permission and loader-region behavior | Not implemented; cannot safely alias it to ordinary `ControlMemory`. |
| `A3` | Only FREE and COMMIT are documented by the official wrapper; implementation removes checks, truncates size, forces additional page flags and may allocate linear backing | Azahar handles a narrow observed subset after validating the exact request and backing allocator. Unsupported operations remain rejected. |

Official return values are operation-specific (invalid handle, invalid address/state,
out-of-resource, unsupported operation, and user-copy failure). Azahar returns its typed
`Result` equivalents for implemented contracts. Byte-for-byte Result parity for all Luma
kernel-internal failure cases is not claimed.

## IPC and service-hook audit

`SendSyncRequest.c` is the primary source for the Luma hook behavior. It performs
service-name-specific rewriting and selected commands are synthesized or forwarded. The
table records the Luma-side command shape; this is not a claim that every command is used by
Level256 v2.0.1.

| Service / hook | Official Luma behavior inspected | Azahar status |
|---|---|---|
| `plg:ldr` | Loader service and 3GX plugin lifecycle in Rosalina/loader | Existing `Service::PLGLDR` implements the emulator's plugin-loader IPC. Level256 identity and process scope are attached there. |
| `plg:UDS` control | Commands 100 Initialize (2 normal words), 101 Finalize, 104 SetSignalState, 105 SetPause, 106 SetDlpEnable; owner PID must match; malformed headers rejected | `PluginUdsControlIPC` capability gates the recognized control subset. State is per process and the HLE validates command header/owner. |
| `plg:UDS` data | Commands 14/31 GetNodeInfoList, 15 Scan, 16 SetBeaconData, 17 GetBeaconData, 20 Receive, 23 Send, 27 Initialize, 29 CreateNetwork, 30 ConnectToNetwork; command header lengths are checked and static-buffer words are moved | `PluginUdsDataIPC` gates the adapted command set; unrecognized IPC reaches the ordinary service path. |
| `plg:UDS_Client` | Rosalina process redirection/UDS session state | HLE's service-name redirect and `SendSyncRequest` hooks handle the scoped Level256 session. |
| `plg:DLP`, `plg:FKCL` | Redirect targets for `dlp:SRVR`, `dlp:FKCL`, `dlp:CLNT` while DLP is enabled and UDS is not paused | Redirect mapping exists; support still depends on registered HLE endpoints and the UDS control state. |
| `ndm:u` | When Rosalina network functions are active and the caller is a non-system application: command `0x10042` Local-Communication is rewritten to Infrastructure; `0x20002` suspend and `0x90000` resume may be normalized | `NdmControlIPC` applies only the observed command rewrites, with process/session/capability scope. Other NDM commands are forwarded. |
| `soc:U` | On command `0x10044` AttachProcess, sends `plg:UDS` event 102; command `0x190000` tracks detach | `SocAttachIPC` implements the event path and bounded per-process state. Socket IPC otherwise remains normal SOC HLE. |
| `srv:` | Hook for `0x50100` GetServiceHandle redirects selected names; `0x00C0080` `publishToSubscriber` may wake the app thread | Existing SRV HLE and process service redirect handle registered ports; special service-name mapping is separate. |
| `APT:` | `0x00D0080` ReceiveParameter hook changes behavior based on plugin status | Existing APT HLE remains authoritative; no general Level256 APT override is enabled here. |
| `cfg:u/s/i` | Commands `0x10082` GetConfigInfoBlk2 and `0x20000` SecureInfoGetRegion may be language-emulated | Not enabled as a Level256 compatibility capability; normal CFG HLE handles requests. |
| `err:f` | `0x10800` Throw hook | Not enabled as a Level256 compatibility capability. |
| `fs:USER`, `soc:U`, `cam:u` | Select file-delete (`0x8040142`, `0x8070142`), socket connect (`0x60084`), camera start (`0x10040`) paths are inspected/re-written for plugin restrictions | Azahar's ordinary FS/SOC/CAM HLE remains in place; these Luma Rosalina policy hooks are not duplicated as broad Level256 IPC grants. |

The IPC capability gate is reached only for sessions whose client-side service names are
recognized, on a supported title, with opt-in and plugin-owner/code authorization. IPC
header and argument parsing stays inside the selected adapter. A capability does not grant
arbitrary access to a named service or command family.

## Test mapping and known gaps

| Area | Existing automated coverage | Remaining validation |
|---|---|---|
| Capability boundaries | `src/tests/core/hle/kernel/luma3ds/level256_capabilities.cpp` | Runtime game trace for the actual SVC call sequence. |
| A0 ABI decode | v1/v2 wrapper marker, handle/register positions, flags | Runtime argument values and mapping contents on v2.0.1. |
| A0 VM behavior | `src/tests/core/memory/vm_manager.cpp`: multi-VMA aliasing, coherence, states/perms, occupied target, invalid source and IO | Cross-process game launch and actual plugin startup. |
| A3 subset | `src/tests/core/hle/kernel/level256_diagnostic.cpp` | Broader official A3 operations deliberately remain unsupported. |
| IPC state/policy | Existing SVC and service-manager tests, plus capability tests | Full Level256 multiplayer/online scenario; do not infer it from unit tests. |
| A1, A2, B0–B2 | No signed-profile enablement test by design | Require ownership metadata or safe kernel-object/session abstraction before considering authorization. |
| B3 | Existing generic ControlProcess behavior | Separate audit needed before changing compatibility exposure; `SET_MMU_TO_RWX` is not a safe new Level256 capability. |

The Level256 v2.0.1 startup failure previously observed was an `A0` capability rejection
followed by a plugin failure path. Static disassembly associated that call site with the
`0xDEADC0DE` sentinel write. Enabling the scoped, non-executable `A0` implementation removes
that known rejection path in HLE logic, but only a real game run can establish whether the
sentinel write, later startup failures, or the earlier `NoExecuteFault` are resolved.

## Update checklist

For a future official update, record the Luma fork commit; diff the altered SVC table,
wrapper assembly and `SendSyncRequest.c`; verify the signed manifest and payload; trace any
new command headers/arguments; add ABI tests before granting a capability; and keep unknown
SVCs, IPC commands, titles, processes and PC ranges fail-closed. Artifact version and payload
SHA are authenticity metadata, not an ABI compatibility switch.
