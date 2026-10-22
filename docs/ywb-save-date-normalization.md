# Yo-kai Watch Busters save-date normalization

Azahar's **Offset Time** can leave a future timestamp in a Busters save after the
emulated clock is moved back. This change normalizes recognized future save
timestamps to the emulated local clock when a completed writable save is
committed. It does not reset daily-use counters or force daily content to
become available.

## Coverage and behavior

- Red Cat Corps and White Dog Squad, including Moon Rabbit Team.
- Moon Rabbit ExtData slots `yw-b_ca`, `yw-b_da`, `yw-b_cb`, and `yw-b_db`
  under ExtData `000016C6`; the matching `game1.yw` and `head.yw` are handled as
  a slot pair.
- Base-game ExtData `000016C7` (`yw-b_a` and `yw-b_b`) and the Red Cat
  (`0016C700`) / White Dog (`0016C600`) title save areas are recognized for
  `head.yw`.
- `game1.yw`: decrypted body date at `0x4C`. `head.yw`: decrypted date at
  `0x3A20`.
- Moon Rabbit saves write `game1.yw` before updating `head.yw`. A successful
  game write is therefore deferred until that same slot's successful `head.yw`
  close, so key derivation uses the matching updated head. Other slots cannot
  consume the pending operation.

Each candidate is checked for its save format and integrity (AES-CCM
authentication for `game1.yw`, internal CRC, and format markers). The output is
re-encrypted and validated before replacement. A backup and temporary file are
kept beside, but outside, the game's slot directory. Validation, write, or
close failures leave the original file in place; a date already at or before
the emulated clock is left unchanged. No keys, nonces, or save bytes are
written to logs.

## Verification and limitations

Automated coverage is in `src/tests/core/file_sys/ywb_save_date.cpp` and
`src/tests/core/file_sys/yw2_save_date.cpp`; the former uses synthetic save
fixtures. The feature was also verified in-game with Red Cat Corps / Moon
Rabbit Team: the corrected save authenticated, retained its game data, and
loaded successfully. An A/B comparison showed daily content unavailable on
October 22 and available on October 23 from the same initial state. Returning
Offset Time to the present does not itself restore daily availability. White
Dog Squad has not been verified in-game. This work does not establish the
purpose of other unknown date fields or daily-state flags.

## AI assistance disclosure

AI assistance was used to implement and document this change, at the user's
request. The save-format implementation retains the
`yw_save.py` MIT license attribution in `src/core/file_sys/ywb_save_date.cpp`.
