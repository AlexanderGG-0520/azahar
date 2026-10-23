// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/file_sys/level256_signature.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cryptopp/blake2.h>
#include <cryptopp/xed25519.h>

namespace FileSys::Level256Signature {
namespace {
constexpr std::array<u8, 8> ManifestMagic{'L', '2', '5', '6', 'M', 'A', 'N', 1};
constexpr std::array<u8, 8> OfficialKeyFingerprint{
    0x62, 0xbe, 0xad, 0xd9, 0x4c, 0x22, 0x4d, 0xb3};
constexpr std::size_t SignedManifestSize = 104;
constexpr std::size_t SignatureSize = 64;
constexpr std::size_t HashSize = 32;
constexpr u32 FormatVersion = 1;
constexpr u32 ArtifactType3gx = 1;

u32 ReadLittleEndian32(const u8* value) {
    return static_cast<u32>(value[0]) | (static_cast<u32>(value[1]) << 8) |
           (static_cast<u32>(value[2]) << 16) | (static_cast<u32>(value[3]) << 24);
}

std::array<u8, HashSize> Blake2b256(std::span<const u8> input) {
    std::array<u8, HashSize> digest{};
    CryptoPP::BLAKE2b hash(false, HashSize);
    hash.Update(input.data(), input.size());
    hash.Final(digest.data());
    return digest;
}
} // namespace

bool HasOfficialReleaseKeyFingerprint(std::span<const u8, PublicKeySize> public_key) {
    const auto fingerprint = Blake2b256(public_key);
    return std::equal(OfficialKeyFingerprint.begin(), OfficialKeyFingerprint.end(),
                      fingerprint.begin());
}

bool VerifyPlugin(std::span<const u8> file,
                  std::span<const u8, PublicKeySize> public_key) {
    if (file.size() <= ManifestSize) {
        return false;
    }
    const auto manifest = file.last(ManifestSize);
    if (!std::equal(ManifestMagic.begin(), ManifestMagic.end(), manifest.begin()) ||
        ReadLittleEndian32(manifest.data() + 8) != FormatVersion ||
        ReadLittleEndian32(manifest.data() + 12) != ArtifactType3gx) {
        return false;
    }
    const std::size_t payload_size = file.size() - ManifestSize;
    if (payload_size > UINT32_MAX ||
        ReadLittleEndian32(manifest.data() + 64) != payload_size) {
        return false;
    }

    try {
        const CryptoPP::ed25519::Verifier verifier(public_key.data());
        if (!verifier.VerifyMessage(manifest.data(), SignedManifestSize,
                                    manifest.data() + SignedManifestSize, SignatureSize)) {
            return false;
        }
        const auto digest = Blake2b256(file.first(payload_size));
        return std::equal(digest.begin(), digest.end(), manifest.begin() + 32);
    } catch (const CryptoPP::Exception&) {
        // Invalid or unsupported public-key encoding must not crash the emulator.
        return false;
    }
}
} // namespace FileSys::Level256Signature
