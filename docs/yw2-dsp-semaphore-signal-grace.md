# Experimental generic DSP semaphore signal grace

## Reproduction evidence

On October 9, 2026, a YW2 Japanese LLE HOME / HLE DSP run with
diagnostic PR #19 reproduced a DSP semaphore closure-to-signal race:

- `31.675151`: PID 16 / TID 31 closes `DSP_DSP::semaphore_event`,
  object ID 77, guest handle `0x00120037`, emulated ticks `7294522338`.
- `31.675333`: PID 16 / TID 40 / PC `0x00181234` attempts
  `SignalEvent(handle=0)`, at ticks `7294538721`.
- `31.675337`: `ResultInvalidHandle`, followed by `err:f` Generic Fatal
  at `31.675934`.

The event's object ID matches `DSP::GetSemaphoreEventHandle`. Current
libctru's NDSP shutdown path closes and zeroes `dspSem` while a separate
NDSP worker can reach `svcSignalEvent(dspSem)`. This makes a guest-side
race strongly plausible, although the full frozen game state is not yet
causally proven to follow from that one fatal.

The earlier PR #17 guards *WaitSynchronization1* on a retired DSP IRQ.
It does **not** cover `SignalEvent` on the DSP semaphore.

## Experimental behavior

This PR implements a **process-scoped, one-shot, five-millisecond emulated-time**
compatibility guard:

1. When any guest successfully calls `SignalEvent` on the actual
   `DSP_DSP::semaphore_event`, remember its object ID and guest thread ID.
2. When that exact event is closed by a *different* guest thread, arm a
   five-millisecond token specifically for the prior signaler; clear the
   saved signaler identity so the token cannot be rearmed accidentally.
3. If that exact guest thread calls `SignalEvent(handle=0)` within the
   window, consume the token and return `ResultSuccess` **without signaling
   any event** (the semaphore was already torn down).
4. Otherwise, preserve the existing kernel SVC behavior, including
   `ResultInvalidHandle` on unrelated invalid signals.

This does not rely on game title, guest PC, or fixed kernel object IDs.
It does rely on the HLE DSP semaphore event name as the identity marker.

**Limitations:** The grace state is intentionally transient and not included
in `Process::serialize`; loading older savestates does not revive stale
tokens. A savestate captured in the middle of a teardown transition may
not preserve the short-lived compatibility token. This is a targeted
compatibility workaround, not a change to the real 3DS kernel's invalid
handle semantics.

## Local reproduction test (fish shell)

Build this experimental branch in a separate worktree or after ensuring
your current worktree has no uncommitted changes:

```fish
cd ~/Projects/azahar-yw2-camera-fix
git status --short
git fetch origin
git switch --track origin/experiment/generic-dsp-semaphore-signal-grace
cmake --build build --parallel 6
./build/bin/Release/azahar
```

Cold-boot LLE HOME, start YW2, and exercise HOME entry/return repeatedly.
Do not rely on savestate loading for the initial reproduction run.

Capture the complete log **before restarting Azahar**, and inspect:

```fish
set log ~/azahar/azahar-alpha/user/log/azahar_log.txt
cp "$log" ~/azahar/azahar-alpha/user/log/azahar_dsp_sem_grace_experiment.txt
rg -n 'DSP-SEM-GRACE|DSP-IRQ-GRACE|JumpToHomeMenu|LeaveHomeMenu|INVALID HANDLE SVC|Fatal error' "$log" | tail -n 100
```

## Automated regression tests

`src/tests/core/hle/kernel/dsp_semaphore_signal_grace.cpp` covers the
process-scoped guard without relying on an intermittent HOME timing race:

- Same original signaler can consume the token exactly once.
- Wrong thread cannot consume the token or steal it from the original signaler.
- The deadline is inclusive and an expired token is rejected.
- A future token is not accepted before the close timestamp.
- New successful semaphore signaling invalidates an old grace token.
- A token is not armed without a prior signal, for the wrong object, or
  when the closer is the signaler.
- Closing twice without a new signal cannot rearm the token.
- Tokens cannot cross guest process boundaries.

These tests prove the token bookkeeping conditions; they do **not** prove
the game invokes the `SignalEvent(0)` consumption path during real HOME
transitions, nor prove that the later video/audio freeze is fixed.

Run with `cmake --build build --target tests --parallel 6` followed by
`ctest --test-dir build --output-on-failure` (for builds configured
with `ENABLE_TESTS`; verify that CMake enabled the test target).

## Acceptance criteria

- Repeated HOME->YW2 and YW2->HOME cycles with video, input and audio
  continuing normally on return (including after many cycles).
- At least one genuine `[DSP-SEM-GRACE] Consumed retired semaphore signal`
  in a run where the old `SignalEvent(0)` Fatal was reproducible.
- No `svc=0x18 name=SignalEvent` invalid-handle Fatal for that
  guarded teardown race.
- Other invalid handles are still reported, not silently accepted.
- No new build or runtime failures (Linux / Windows CI).

**Do not merge this experiment as a confirmed fix until CI and local
HOME cycle tests have completed.** If video/audio/input still freeze
without the previous Fatal, collect guest thread states / APT and DSP
handoff logs to investigate that *separate* stalled-resume mechanism.
