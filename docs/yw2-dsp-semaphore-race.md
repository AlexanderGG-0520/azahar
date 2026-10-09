# YW2 HOME handoff: DSP semaphore race diagnostics

This is a **diagnostic-only** branch. It does **not** suppress invalid handles, change
`SignalEvent` return values, or alter APT/DSP scheduling.

## Evidence from 2026-10-09

A local HLE-audio / LLE-HOME run of `release/yw-v0.2.1` (version `06a8158`) captured:

- `41.019325`: audio DSP sleep requested.
- `41.019634`: guest closes the audio IRQ event.
- `41.019645`: unregister logs `eligible_threads=1`.
- `41.019698`: guest closes `DSP_DSP::semaphore_event`.
- `41.019752`: **thread 40**, PC `0x00181234`, invokes `SignalEvent(handle=0)` and receives `0xD8E007F7` (`ResultInvalidHandle`).
- `41.021062`: `err:f` reports a Generic Fatal (ADR `0x00181240`, RSL `0xD8E007F7`).
- Later HOME cycles continue. Following `LeaveHomeMenu` at `88.911536`, the DSP IRQ is
  re-registered and DSP wakeup requested at `88.928113`, but game activity appears to
  stop while the GPU event-wait thread keeps producing logs.

This supports a **possible second NDSP teardown race**, distinct from PR #17's
`WaitSynchronization1(handle=0)` IRQ workaround. A fatal error earlier in the
session is not yet proven to cause the later frozen video/audio.

libctru `ndsp.c` closes `dspSem` during `ndspFinalize` while `ndspThreadMain`
may reach `svcSignalEvent(dspSem)` after its `bSleeping`/`bDspReady` check.
That is a plausible explanation for this trace, **not proof of the exact guest
source location** (shipping game binaries may differ from current libctru).

## New log markers

- `[DSP-SEM-RACE] GetSemaphoreEventHandle object_id=... ticks=...`
  records which HLE semaphore event DSP handed out.
- `[DSP-SEM-RACE] Close DSP semaphore: pid=... thread=... pc=... handle=... object_id=... ticks=...`
  records a YW2 guest close of the named semaphore event.
- `[DSP-SEM-RACE] Invalid zero-handle SignalEvent: pid=... thread=... pc=... ticks=... -> ResultInvalidHandle`
  records a zero-handle signal attempt **only when its handle lookup fails**.
  Its tag names the hypothesis; the event identity cannot be recovered
  from a zero-valued handle alone.

All original SVC return values and event signaling behavior are unchanged.

## Local validation (fish)

Run from a clean build tree and a cold boot; avoid loading savestates for this test.

```fish
cd ~/Projects/azahar-yw2-camera-fix
git status --short
git fetch origin
git switch --track origin/diagnostic/yw2-dsp-semaphore-signal-race
cmake --build build --parallel 6
./build/bin/Release/azahar
```

Verify the Azahar startup log identifies this branch. Launch the Japanese HOME
Menu, start YW2, and repeat HOME entry/return using HLE DSP audio. Check normal
gameplay and audio *after* each return; record whether video/input/audio freeze
on entry, on return, or both.

Preserve the **full log from the same process session** immediately after a
failure (Azahar rotates log files on restart):

```fish
set log ~/azahar/azahar-alpha/user/log/azahar_log.txt
cp "$log" ~/azahar/azahar-alpha/user/log/azahar_dsp_sem_diagnostic.txt

rg -n 'DSP-SEM-RACE|DSP-IRQ-GRACE|JumpToHomeMenu|LeaveHomeMenu|PipeWrite:.*(sleep|wakeup)|Fatal error|INVALID HANDLE SVC' "$log" | tail -n 100
```

## Diagnostic acceptance / next decisions

1. Correlate `GetSemaphoreEventHandle` and `Close DSP semaphore` by
   `object_id`, then look for an invalid zero-handle signal by the YW2 NDSP
   worker within the same HOME transition. If this does not line up,
   investigate alternate handles/PCs; **do not assume** a semaphore race.
2. Determine whether the **first** failure of `SignalEvent(0)` is followed
   by `err:f`, and whether the *same run* later has a frozen resume.
3. Compare with `integration/yw` (diagnostics disabled), and if needed
   with parent commit `2c48233952004947c9aa595c537a8dd1c9ec5d7b` before PR #17,
   using a separate build tree and cold boot for control runs.
4. Only after correlation is established, consider an event-identity-bound,
   time-bounded, single-use mitigation; do **not** blanket-return success
   for `SignalEvent(0)`.

**No compatibility fix is included and this PR should not be merged as a fix.**
