# Level256 signature verification (experimental)

The official Level256 Installer repository documents a **K_release** key and a signed
168-byte manifest appended to each 3GX. Its public specification is
[include/crypto/Manifest.hpp](https://github.com/Tekito-256/level256-installer/blob/main/include/crypto/Manifest.hpp).
Official v2.0.1 release assets include `Level256.3gx` and `level256-release.bin`:
https://github.com/Tekito-256/level256-client/releases/tag/v2.0.1

## Trust setup

The official installer does **not** publish the raw 32-byte K_release public key in
the source repository. It publishes a BLAKE2b-256 fingerprint prefix,
`62beadd94c224db3`. Do not invent a key or use the debug key.

To enable signed-plugin verification in this fork, obtain the authentic 32-byte
`K_release.pub` **from the Level256 maintainers through an independently verified
channel**. Place the raw binary key at:

`<Azahar user SDMC>/Level256/K_release.pub`

The emulator checks that the key's BLAKE2b-256 fingerprint begins with the
official 8-byte fingerprint, verifies the Ed25519 signature, and checks the full
3GX payload against the BLAKE2b-256 digest in the signed footer. Key files of
other sizes or with a different fingerprint are rejected. The published 64-bit
fingerprint is an identification aid, **not as strong as pinning the entire 32-byte
public key**; a future change should embed an independently authenticated full
K_release public key directly in the emulator.

If the key is absent or invalid, signed verification is unavailable and the
historical fixed-SHA-256 compatibility gate remains intact. No implicit
trust-on-first-use, HTTP download, or key replacement is performed. The ordinary
3GX loader still validates its own header and format independently.

## Compatibility boundary

A genuine signature proves Level256 issued the 3GX; **it does not prove every
future signed release is compatible with this fork's existing Luma3DS HLE
emulation**. This verification is for release identity. Any protocol, IPC or SVC
changes still require functional testing. In particular, v2.0.1 startup currently
produces a NoExecuteFault in an observed local run.

No change is made to the owner's local account, signing keys, LFCS, or save data.

## Technical format

- Footer length: 168 bytes; magic `L256MAN\x01`
- 32-bit little-endian format version: 1; artifact type: 1 (3GX)
- The Ed25519 signature is `footer[104:168]` over `footer[0:104]`
- Payload length: LE32 at offset 64
- Payload BLAKE2b-256: bytes `footer[32:64]`, hashed over all file bytes
  except the final 168
- This is **BLAKE2b-256**, not SHA-256 or a truncated BLAKE2b-512 digest

Implementation: `src/core/file_sys/level256_signature.{h,cpp}`.
