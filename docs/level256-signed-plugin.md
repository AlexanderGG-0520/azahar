# Level256 3GX signature verification

Azahar accepts two separately constrained Level256 HLE compatibility profiles:

- The previously reviewed plugin identified by the legacy full-file SHA-256.
- A plugin with a valid Level256 release signature and manifest artifact version `20031` (the
  official v2.0.1 `Level256.3gx`).

A valid signature authenticates the publisher and payload. It does not establish that an arbitrary
future plugin uses an HLE ABI Azahar understands. Other signed artifact versions are therefore
recognized by the verifier but do not enable the privileged Level256 HLE paths until reviewed.
The existing explicit compatibility option, supported-title check, plugin-owner PID check, and
plugin PC range checks remain required.

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
