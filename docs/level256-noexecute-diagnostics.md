# Level256 bootloader return diagnostics

This read-only diagnostic build records the verified Level256 v2.0.1 bootloader handoff for title
`000400000016C700`. It does not change SVC results, guest memory, permissions, or control flow.

## Enable the trace

Use a build based on `feat/level256-signed-plugin-verification` with the diagnostic changes. In the
Qt configuration file, set both values under `[System]`:

```ini
level256_compatibility_enabled=true
level256_bootloader_diagnostics_enabled=true
```

The diagnostic is additionally gated on the official signature or exact reviewed legacy hash,
target title, loaded plugin owner process, and the thread that made the plugin's first
`GetSystemInfo(type=0x20000, param=0)` call. Its setting defaults to false. The emulator logs its
effective value with the other system settings.

With this opt-in enabled, the existing bounded plugin SVC trace also records at most eight
`MapProcessMemoryEx` register sets for this title, plus decoded v1/v2 handles, addresses, size,
flags, resolved PIDs, and source/destination VMA metadata. It never dumps mapped memory contents.

## v2.0.1 binary findings

The local `Level256.3gx` sample is 938,376 bytes and its SHA-256 matches the published v2.0.1
digest. Its ARM text has the v2 wrapper at code offset `0x9D15C`: it saves the original `r0` to
`r6`, sets `r0` to `0xFFFFFFF2`, and issues SVC `0xA0` at offset `0x9D170`. With the plugin text
base at `0x07000100`, the SVC instruction is at `0x0709D270`; the reported `pc=0x0709D274` is the
following instruction address. The call site at code offset `0x4C470` checks the returned result
and branches on a negative value to the failure stub at `0x0704C5A0`.

That stub loads `0xDEADCFFF`, stores `r0` at offset `-0xF21` (address `0xDEADC0DE`), then loops.
The reported write value `0xE0E01BF4` is Azahar's `ResultNotImplemented`. This links the observed
capability rejection to that trap path in this binary. The source address and its mapped VMA are
runtime-derived and still need the new request record; this static analysis does not establish
that all later startup failures or the earlier `NoExecuteFault` share the same cause.

## Run and capture

Start the game once with the official Level256 3GX and save the Azahar log. Do not enable the
diagnostic with other titles or plugins. The bounded trace contains:

- the saved game entry words and return pointer immediately after `MapBootloader()`;
- the original game instructions at `0x00100000` and `0x00100004`, alongside the saved E8/EC words;
- the same three bootloader words and PC/LR/SP/CPSR/T state immediately before and after the
  `GetSystemInfo` call;
- instruction and register state at `0x06FFF0D0` and `0x06FFF0D4`, including D4's decoded effective
  address, value loaded, resulting PC, and whether the loaded value matches the original game
  instruction at `0x00100000`;
- changes to `0x06FFF0F0` seen between instructions on the target thread (at most eight records);
- the last 16 target-thread instructions if execution leaves the mapped plugin/bootloader range,
  and the scoped exception record if the fault follows.

Relevant records begin with `Level256 bootloader snapshot`, `Level256 bootloader instruction`,
`Level256 bootloader return pointer changed`, `Level256 bootloader trace recent`, or
`Level256 bootloader exception`. Keep the SVC rejection records immediately before and after these
lines too; they help distinguish a denied operation from the branch itself. The diagnostic does not
log account data, LFCS, IPC buffers, or guest register values outside the listed control-flow
registers.

This trace currently steps Dynarmic one instruction at a time. ARM interpreter comparison and
actual game execution remain manual; the build/test result alone does not establish that the game
returns to `0x00100000` or continues running.
