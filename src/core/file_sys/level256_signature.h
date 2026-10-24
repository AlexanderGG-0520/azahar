// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <span>

#include "common/common_types.h"

namespace FileSys::Level256 {

constexpr std::size_t MANIFEST_SIZE = 168;
constexpr std::size_t SIGNED_MANIFEST_SIZE = 104;
constexpr u32 MANIFEST_FORMAT_VERSION = 1;
constexpr u32 PLUGIN_ARTIFACT_TYPE = 1;

struct PluginManifest {
    u32 artifact_version = 0;
    u32 payload_size = 0;
    std::array<u8, 32> payload_digest{};
};

const std::array<u8, 32>& TrustedReleasePublicKey();

/// Verify a trailing Level256 3GX manifest against the pinned release key.
/// `payload_digest` must be BLAKE2b configured with digest size 32, not a truncated BLAKE2b-512.
bool VerifyPluginManifest(std::span<const u8> raw_manifest, u64 payload_size,
                          const std::array<u8, 32>& payload_digest, const u8* public_key,
                          PluginManifest* parsed = nullptr);

/// Publisher identity only. HLE compatibility is decided separately by per-operation ABI policy;
/// the signed artifact version must never act as an authorization profile.
constexpr bool IsTrustedPluginIdentity(bool legacy_hash_verified, bool signature_verified) {
    return legacy_hash_verified || signature_verified;
}

} // namespace FileSys::Level256
