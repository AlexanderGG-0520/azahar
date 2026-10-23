// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/file_sys/level256_signature.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include <cryptopp/blake2.h>
#include <cryptopp/xed25519.h>

namespace FileSys::Level256 {
namespace {

constexpr std::array<u8, 8> MANIFEST_MAGIC = {'L', '2', '5', '6', 'M', 'A', 'N', 0x01};

// Extracted from the official Level256Installer.cia v2.0.1 release's ExeFS .code section.
// Its BLAKE2b-256 fingerprint is 62beadd94c224db3, as published by the installer source.
constexpr std::array<u8, 32> RELEASE_PUBLIC_KEY = {
    0x2D, 0x07, 0x58, 0xAF, 0xC1, 0xF3, 0x74, 0x09, 0x36, 0x7F, 0x51, 0xEA, 0x82, 0x0E, 0x67, 0x73,
    0x1A, 0xE0, 0x3B, 0x15, 0x1D, 0x5C, 0x77, 0x33, 0x82, 0x1E, 0xAF, 0x3F, 0x1E, 0x23, 0x00, 0x30,
};

u32 ReadLittleEndian32(const u8* data) {
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

} // namespace

const std::array<u8, 32>& TrustedReleasePublicKey() {
    return RELEASE_PUBLIC_KEY;
}

bool VerifyPluginManifest(std::span<const u8> raw_manifest, u64 payload_size,
                          const std::array<u8, 32>& payload_digest, const u8* public_key,
                          PluginManifest* parsed) {
    if (public_key == nullptr || raw_manifest.size() != MANIFEST_SIZE ||
        payload_size > std::numeric_limits<u32>::max()) {
        return false;
    }

    const u8* const data = raw_manifest.data();
    if (!std::equal(MANIFEST_MAGIC.begin(), MANIFEST_MAGIC.end(), data) ||
        ReadLittleEndian32(data + 8) != MANIFEST_FORMAT_VERSION ||
        ReadLittleEndian32(data + 12) != PLUGIN_ARTIFACT_TYPE) {
        return false;
    }

    PluginManifest manifest;
    manifest.artifact_version = ReadLittleEndian32(data + 16);
    manifest.payload_size = ReadLittleEndian32(data + 64);
    std::copy_n(data + 32, manifest.payload_digest.size(), manifest.payload_digest.begin());
    if (manifest.payload_size != payload_size || manifest.payload_digest != payload_digest) {
        return false;
    }

    const CryptoPP::ed25519::Verifier verifier{public_key};
    if (!verifier.VerifyMessage(data, SIGNED_MANIFEST_SIZE, data + SIGNED_MANIFEST_SIZE,
                                MANIFEST_SIZE - SIGNED_MANIFEST_SIZE)) {
        return false;
    }

    if (parsed != nullptr) {
        *parsed = manifest;
    }
    return true;
}

} // namespace FileSys::Level256
