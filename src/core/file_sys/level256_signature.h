// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <span>
#include "common/common_types.h"

namespace FileSys::Level256Signature {

constexpr std::size_t PublicKeySize = 32;
constexpr std::size_t ManifestSize = 168;

// Checks the official K_release fingerprint published by the Level256 installer.
// This fingerprint is an identifier, not a replacement for independent key provenance.
bool HasOfficialReleaseKeyFingerprint(std::span<const u8, PublicKeySize> public_key);

// Verifies a signed 3GX footer: Ed25519 over the first 104 manifest bytes,
// followed by BLAKE2b-256 of the entire payload before the 168-byte footer.
// No privileges are granted by this function.
bool VerifyPlugin(std::span<const u8> file,
                  std::span<const u8, PublicKeySize> public_key);

} // namespace FileSys::Level256Signature
