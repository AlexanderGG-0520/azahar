# Yo-kai Watch 2 save-date normalization (experimental)

Azahar's **Offset Time** can write future dates into Yo-kai Watch 2's
`game*.yw` saves. Returning the clock to the present may leave daily content
locked until the future date is reached.

## Coverage

- Japanese **Ganso (元祖)**: `000400000012F900`
- Japanese **Honke (本家)**: `000400000012F800`
- Japanese **Shinuchi (真打)**: `0004000000155100`
- Slots `game1.yw`, `game2.yw`, `game3.yw` within their local savedata archive
- Ganso/Honke **1.x**: standard AES key; **2.x**: key derived from the matching
  `head.yw` (per the public `yw_save` format). Each candidate must pass AES-CCM
  authentication, inner CRC32, and the title-specific save-format marker.
- Shinuchi: standard AES key and the checked 71,048-byte file size.

The internal date field is **at offset 0x70 in the decrypted game body**
(not at 0x70 in the full encrypted file). This was cross-checked against the
original future-dated Shinuchi save and the successfully repaired Shinuchi save.

After the game closes a writable game save, the emulator verifies the file,
compares that saved date to the **emulated 3DS clock**, and only changes it if
it is in the future. It then re-encrypts, verifies, and atomically replaces the
save. The first original is backed up to the parent `data/` directory, outside
the emulated `00000001/` archive, as
`gameN.yw.before-yw2-datefix.bak`.

The temporary file is also kept outside the emulated archive.

If any format/authentication/backup/write check fails, the existing save stays
untouched. This does not alter game executable code or any other title.

## Limitations and verification

**Experimental.** Only Shinuchi's offset and standard-key decrypt were verified
against provided real saves. Ganso/Honke key selection follows `yw_save`, but
**real Ganso/Honke save pairs have not been tested** in this work. Do not use
unbacked-up main saves until built and verified. A newly written `head.yw` must
be available when `gameN.yw` closes; otherwise v2.x authentication fails
safely and normalization is skipped. Remote Artic saves are not supported.
The game may cache dates until restarted. A restored savestate with an
already-open save handle does not retain the transient post-close hook.

Suggested test matrix, each with a copied save directory:
1. Ganso v1.x: offset +150d, save, reset offset, save again, confirm date.
2. Ganso v2.x: same test with valid `head.yw`.
3. Honke v1.x and v2.x: repeat both variants.
4. Shinuchi: regression test (especially daily content after 06:00).
5. No-op: save on the same day; verify bytes unchanged by this feature.
6. Fault path: remove or corrupt `head.yw` in a v2.x copy and confirm no write.
7. Cross-title: confirm no save data modifications for unrelated games.

## AI assistance disclosure

The initial feature and this expansion were authored with ChatGPT assistance
at the fork owner's request. Independent human review, compilation, and
real-game tests are required before merging or releasing. The implementation
adapts the original `yw_save` MIT-licensed obfuscation/key-derivation ideas,
with the original attribution retained in the C++ source.


## Automated regression tests

`src/tests/core/file_sys/yw2_save_date.cpp` runs inside the existing Catch2/CTest
suite. All executable test fixtures are synthetic and contain **no user save
data**. Fixture dimensions, version markers, day offset, and the two
independently checked v2.x head-key seed/expected-key pairs were derived
from two authenticated user-provided saves (70,980-byte `game1.yw`,
21,812-byte `head.yw`).

The tests cover Ganso and Honke v2.x with two independent head-derived keys,
Ganso/Honke v1.x fixed-key compatibility, Shinuchi, future-to-present rewrite,
unmodified gameplay bytes, idempotent/no-op same-day saves, authentication
failure, and absent `head.yw`. They test the **production**
`NormalizeYw2SaveDateAt` routine with a fixed emulated time.

The actual uploaded saves were only inspected privately and are not tracked
in Git. Passing these tests does not prove that all YW2 gameplay daily-reset
state has been fixed; a follow-up emulator-level test is still needed.
