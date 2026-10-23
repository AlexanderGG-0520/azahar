# Level256 3GX signature verification

Azahar accepts two separately constrained publisher identities:

- The previously reviewed plugin identified by the legacy full-file SHA-256.
- A plugin with a valid Level256 release signature, independent of its manifest artifact version.

A valid signature authenticates the publisher and payload. It does not establish that an arbitrary
future plugin uses an HLE ABI Azahar understands. HLE authorization is therefore evaluated per
operation using the capability table in `core/hle/kernel/luma3ds/compatibility.h`; the artifact
version is recorded for diagnosis but is not an authorization input. A signed plugin using a new
artifact version can use an existing capability only when it reaches the same implemented SVC or
IPC handler and satisfies that handler's ABI, argument, memory, title, process, and code-range
checks. Unknown extension SVCs and unimplemented `ControlMemoryEx` (`0xA2`) remain denied. An
unrecognized IPC command receives no emulator-side Level256 translation and follows its ordinary
session path unchanged. The explicit compatibility option,
supported-title check, plugin-owner PID check, and plugin PC range checks remain required.

## Capability and ABI policy

Authenticity and compatibility are separate. `IsTrustedPluginIdentity()` accepts either the exact
legacy digest or a valid release manifest. `CapabilitiesForVerifiedPlugin()` supplies the set of
implemented operation adapters, and `DispatchExtensionSvc()` maps each recognized SVC ID to its
own capability before dispatch. The version number is not consulted. The capability mask is not a
blanket permission: each operation retains its existing handler-level checks.

| Capability | Interface | Enforcement retained |
|---|---|---|
| `SystemInfo` | `svcGetSystemInfo` (`0x2A`) | Exact type/parameter pairs, explicit mode, supported title, verified identity, loaded plugin, owning PID and plugin PC range. |
| `CustomBackdoor` | Luma SVC `0x80` | Owning process and plugin/bootloader PC; aligned callback inside plugin code; only individually recognized callback adapters and exact argument/state checks are handled. Other callbacks fail as unimplemented. |
| `ControlMemoryUnsafe` | Luma SVC `0xA3` | Opt-in, verified identity, supported title, owner PID, and the SVC PC inside the actual loaded plugin image; the observed SYSTEM COMMIT address/size/operation/permissions, writable output pointer, free VMAs and protected-range overlaps are checked independently. |
| `ConvertVaToPa` | Luma SVC `0x90` | Only the existing conversion implementation is dispatched for an authorized plugin call. |
| `DataCacheMaintenance` | Luma SVCs `0x91`/`0x92` | Existing cache-operation handlers; they do not grant memory-write access. |
| `InstructionCacheMaintenance` | Luma SVCs `0x93`/`0x94` | Existing cache invalidation handlers; they do not grant memory-write access. |
| `PluginUdsControlIPC` | `plg:UDS_Client` control IPC | Only the existing `ProcessPlgUdsControl` command/header/owner checks can alter Level256 connection state. |
| `PluginUdsDataIPC` | `plg:UDS_Client` data IPC | Only command IDs and parameter shapes recognized by `TranslateLevel256UdsCommand` are translated; all other requests pass through unchanged. |
| `NdmControlIPC` | `ndm:u_Client` | Only header `0x00010042`, argument `2`, while connected and unpaused is normalized to `1`. |
| `SocAttachIPC` | `soc:U_Client` | Only the existing exact attach/detach headers trigger the plugin event path; requests still use the normal session. |
| `MapProcessMemoryEx` | Luma SVC `0xA0` | Both official wrapper ABIs are decoded; handles, flags, page truncation, source backing, destination-Free state, range overflow and non-executable RW permissions are validated before alias mapping. |
| `Level256LFCS` | `plg:ldr` command `0x2560` | Exact IPC header, PID and writable mapped-buffer shape; caller PID must own the plugin and match the PID descriptor; output is zeroed on denial and requests are rate limited. Seed data is still validated by its existing cryptographic loader. |

The signed-plugin capability set grants `MapProcessMemoryEx` (`0xA0`) through the validated
backing-alias path described above. The resulting mapping is RW and non-executable; no payload is
copied. `UnmapProcessMemoryEx` (`0xA1`) remains withheld from newly signed artifacts because the
VM manager does not yet track enough provenance to prove that an arbitrary requested unmap range
belongs to a prior `0xA0` mapping. The exact historical legacy SHA-256 retains its previous
`0xA1` exposure for compatibility. SVC `0xA2` has no handler and remains denied.

### CustomBackdoor callback contracts

The v2.0.1 `CustomBackdoor` adapters are selected by exact ARM instruction signatures inside the
verified plugin's mapped code range, not by the old callback PCs. The checked contracts cover the
current-process view, view flag, view field, resource-limit adjustment, and process-snapshot copy
callbacks. The old exact-SHA plugin retains its historical callback table. The process view is a
guest mapping owned by the calling process; snapshot copies are limited to the view's known
structures and the loaded 3GX `.data`/`.bss` range recorded by the loader. Level256's model-specific
view layout follows Azahar's emulated hardware model. Unknown or modified callback code remains
unhandled. The signature matcher does not execute guest callbacks as host code.

`PluginUdsControlIPC`, `PluginUdsDataIPC`, `NdmControlIPC`, and `SocAttachIPC` are narrow adapters
inside the normal `SendSyncRequest` path. They match specific service names and command/header
shapes, then preserve ordinary IPC forwarding for commands they do not handle. Service redirection
can route the documented DLP ports to their HLE endpoints when the per-process UDS state enables
it. This does not make all `plg:DLP`, `plg:FKCL`, UDS, NDM, or SOC commands Level256-compatible.
`QueryProcessMemory`, `OpenProcess`, thread creation, and event observations remain ordinary 3DS
SVC behavior or bounded diagnostics; they are not turned into generic success responses. See the
[Luma3DS compatibility matrix](level256-luma3ds-compatibility-matrix.md) for the audited surface.

The legacy SHA-256 path retains its previous SVC exposure, including `0xA1`. New signed files
receive reviewed baseline capabilities, including validated `0xA0`, without requiring a new
baked-in hash or preapproved manifest version. Every identity still passes through the same
per-operation dispatcher; unreviewed operations remain denied. Savestates do not serialize
signature trust; loading a state clears the transient identity and capability basis.

## v2.0.1 startup `NoExecuteFault` investigation

The available run for title `000400000016C700` confirms that the 3GX mapped for PID 16, guest code
reached `0x070A535C`, `GetSystemInfo(type=0x20000,param=0)` returned result 0/value 2, and the next
reported fault was an unmapped read/fetch at `0xEB000FB0`. The supplied register snapshot has
`LR=0x06FFF0F0`. This is consistent with the bootloader's `adr lr,g_gameentrypoint`, but it does
not prove what word the following `ldr pc,[lr]` read.

Statically, `MapBootloader()` replaces the `0xDEAD0002` placeholder with the original code segment
base. The bootloader then restores saved CPSR/registers and executes the `adr lr` / `ldr pc,[lr]`
sequence. The expected saved return pointer in this run is `0x00100000`. The captured log does not
include the post-map/post-SVC values at `0x06FFF0E8`, `0x06FFF0EC`, and `0x06FFF0F0`, the D0/D4
instruction trace, the D4 effective address/load value, or CPSR T-state. Therefore it cannot
establish whether F0 was overwritten, D4 read a different address, or the PC changed elsewhere.
No bootloader, exception, mapping, permission, or SVC behavior is changed on this evidence.

To prove the cause, collect one opt-in run with the bounded diagnostic on title `000400000016C700`
and retain the MapBootloader and post-GetSystemInfo snapshots plus D0/D4 records. The decisive
fields are the executing PID/TID, D0/D4 instruction words, LR before/after D0, F0, D4 effective
address, source word, post-instruction PC and CPSR. A correction should be considered only after
those values identify the faulty transfer and a focused regression reproduces it.

## Manifest and algorithms

The installer source defines a 168-byte `L256MAN\x01` manifest. It uses format version 1 and
artifact type 1 for 3GX plugins. The Ed25519 signature covers bytes `[0, 104)`, and the 64-byte
signature occupies `[104, 168)`. The payload is the file before its trailing manifest. Its length
and BLAKE2b-256 digest must match the signed manifest. BLAKE2b is configured with a 32-byte digest
size; Azahar does not truncate a BLAKE2b-512 result. The loader takes one bounded snapshot of the
opened 3GX and computes both legacy SHA-256 and the signed payload digest from that snapshot. All
metadata and executable sections are then parsed from the same bytes, binding authorization to the
content actually mapped and avoiding a verification-to-load race.

Azahar uses its existing Crypto++ dependency for Ed25519 verification and BLAKE2b-256 hashing.

## Release key provenance

The pinned 32-byte public key is:

```text
2d0758afc1f37409367f51ea820e67731ae03b151d5c7733821eaf3f1e230030
```

It was extracted from the ExeFS `.code` section of the official
[Level256Installer.cia v2.0.1 release asset](https://github.com/Tekito-256/level256-client/releases/download/v2.0.1/Level256Installer.cia).
The downloaded CIA was 1,364,928 bytes and its SHA-256 was
`39a1c310dda19e556139e874189a684f7f2ebbad80430cd6ee5bfc9770bfa565`, matching the GitHub Releases
asset digest. The key was located in the extracted `.code` bytes by calculating BLAKE2b-256 for
32-byte windows and finding the published fingerprint. Its BLAKE2b-256 fingerprint prefix is
`62beadd94c224db3`, matching the release key fingerprint documented by the installer source.

This is a reproducible extraction from an official GitHub release asset plus a fingerprint
consistency check. The fingerprint match alone is not an independent proof of the publisher's
identity; the binary-to-release association still relies on GitHub's official release channel.
The installer source does not publish the raw key file in the repository. No development key or
user-provided key is trusted.

## v2.0.1 artifact check

The [v2.0.1 release page](https://github.com/Tekito-256/level256-client/releases/tag/v2.0.1)
lists `Level256.3gx` as 938,376 bytes with SHA-256
`1d1f61dfcc0875bcddd3907c827be89d54657bd1d977068b23ab1c1f1f89cea4`. The downloaded file matched
both. Its trailing manifest parsed as format 1, artifact type 1, artifact version `20031`, and
payload length 938,208 bytes. The Ed25519 signature verified with the extracted key, and the
manifest's BLAKE2b-256 digest matched the payload.

The same release's 827-byte `level256-release.bin` begins with a format 1, artifact type 5 release
bundle manifest. Its 659-byte payload also matched the signed length and BLAKE2b-256 digest, and
its Ed25519 signature verified with the same key.

## Sources

- [Installer manifest layout](https://github.com/Tekito-256/level256-installer/blob/main/include/crypto/Manifest.hpp)
- [Installer manifest parsing and verification](https://github.com/Tekito-256/level256-installer/blob/main/source/crypto/Manifest.cpp)
- [Installer cryptographic primitives](https://github.com/Tekito-256/level256-installer/blob/main/source/crypto/Crypto.cpp)
- [Installer bundle verification](https://github.com/Tekito-256/level256-installer/blob/main/source/core/Bundle.cpp)
- [Installer release key build checks and fingerprint](https://github.com/Tekito-256/level256-installer/blob/main/tools/gen_build_info.py)
- [Installer README and documented release fingerprint](https://github.com/Tekito-256/level256-installer/blob/main/README.md)
- [Level256 v2.0.1 release and assets](https://github.com/Tekito-256/level256-client/releases/tag/v2.0.1)
