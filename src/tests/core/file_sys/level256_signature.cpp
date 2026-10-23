// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cryptopp/blake2.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>

#include "core/file_sys/level256_signature.h"

namespace {

using ManifestBytes = std::array<u8, FileSys::Level256::MANIFEST_SIZE>;
using Payload = std::vector<u8>;

void WriteU32(ManifestBytes& manifest, std::size_t offset, u32 value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        manifest[offset + i] = static_cast<u8>(value >> (i * 8));
    }
}

std::array<u8, 32> HashPayload(const Payload& payload) {
    std::array<u8, 32> digest{};
    CryptoPP::BLAKE2b hash{false, 32};
    hash.Update(payload.data(), payload.size());
    hash.TruncatedFinal(digest.data(), digest.size());
    return digest;
}

struct SignedFixture {
    Payload payload = {0x00, 0x01, 0x02, 0x03, 0xFE, 0xFF};
    ManifestBytes manifest{};
    std::array<u8, 32> public_key{};

    SignedFixture() {
        constexpr std::array<u8, 8> magic = {'L', '2', '5', '6', 'M', 'A', 'N', 0x01};
        std::copy(magic.begin(), magic.end(), manifest.begin());
        WriteU32(manifest, 8, FileSys::Level256::MANIFEST_FORMAT_VERSION);
        WriteU32(manifest, 12, FileSys::Level256::PLUGIN_ARTIFACT_TYPE);
        WriteU32(manifest, 16, 20031);
        WriteU32(manifest, 64, static_cast<u32>(payload.size()));
        const auto digest = HashPayload(payload);
        std::copy(digest.begin(), digest.end(), manifest.begin() + 32);

        constexpr std::array<u8, 32> test_seed = {
            0x9D, 0x61, 0xB1, 0x9D, 0xEF, 0xFD, 0x5A, 0x60, 0xBA, 0x84, 0x4A,
            0xF4, 0x92, 0xEC, 0x2C, 0xC4, 0x44, 0x49, 0xC5, 0x69, 0x7B, 0x32,
            0x69, 0x19, 0x70, 0x3B, 0xAC, 0x03, 0x1C, 0xAE, 0x7F, 0x60,
        };
        public_key = {0xD7, 0x5A, 0x98, 0x01, 0x82, 0xB1, 0x0A, 0xB7, 0xD5, 0x4B, 0xFE,
                      0xD3, 0xC9, 0x64, 0x07, 0x3A, 0x0E, 0xE1, 0x72, 0xF3, 0xDA, 0xA6,
                      0x23, 0x25, 0xAF, 0x02, 0x1A, 0x68, 0xF7, 0x07, 0x51, 0x1A};
        CryptoPP::ed25519::Signer signer{test_seed.data()};
        CryptoPP::AutoSeededRandomPool rng;
        const auto signature_size =
            signer.SignMessage(rng, manifest.data(), FileSys::Level256::SIGNED_MANIFEST_SIZE,
                               manifest.data() + FileSys::Level256::SIGNED_MANIFEST_SIZE);
        if (signature_size != manifest.size() - FileSys::Level256::SIGNED_MANIFEST_SIZE) {
            throw std::runtime_error("Crypto++ returned an unexpected Ed25519 signature size");
        }
    }

    bool Verify(const ManifestBytes& candidate_manifest, const Payload& candidate_payload,
                const u8* key) const {
        return FileSys::Level256::VerifyPluginManifest(candidate_manifest, candidate_payload.size(),
                                                       HashPayload(candidate_payload), key);
    }
};

} // namespace

TEST_CASE("Level256 signed 3GX manifest validates payload and Ed25519 signature",
          "[core][file_sys][level256][signature]") {
    const SignedFixture fixture;
    FileSys::Level256::PluginManifest parsed;

    REQUIRE(FileSys::Level256::VerifyPluginManifest(fixture.manifest, fixture.payload.size(),
                                                    HashPayload(fixture.payload),
                                                    fixture.public_key.data(), &parsed));
    REQUIRE(parsed.artifact_version == 20031);
    REQUIRE(parsed.payload_size == fixture.payload.size());
    REQUIRE_FALSE(fixture.Verify(fixture.manifest, fixture.payload, nullptr));

    std::array<u8, 32> different_key = fixture.public_key;
    different_key[0] ^= 1;
    REQUIRE_FALSE(fixture.Verify(fixture.manifest, fixture.payload, different_key.data()));

    auto bad_signature = fixture.manifest;
    bad_signature.back() ^= 1;
    REQUIRE_FALSE(fixture.Verify(bad_signature, fixture.payload, fixture.public_key.data()));

    auto changed_payload = fixture.payload;
    changed_payload[0] ^= 1;
    REQUIRE_FALSE(fixture.Verify(fixture.manifest, changed_payload, fixture.public_key.data()));
}

TEST_CASE("Level256 signed 3GX manifest rejects malformed fields and truncation",
          "[core][file_sys][level256][signature]") {
    const SignedFixture fixture;
    const auto verify = [&](const ManifestBytes& manifest, u64 payload_size) {
        return FileSys::Level256::VerifyPluginManifest(
            manifest, payload_size, HashPayload(fixture.payload), fixture.public_key.data());
    };

    auto changed = fixture.manifest;
    changed[0] ^= 1;
    REQUIRE_FALSE(verify(changed, fixture.payload.size())); // Magic mutation
    changed = fixture.manifest;
    changed[8] = 2;
    REQUIRE_FALSE(verify(changed, fixture.payload.size())); // Format version mutation
    changed = fixture.manifest;
    changed[12] = 2;
    REQUIRE_FALSE(verify(changed, fixture.payload.size())); // Artifact type mutation
    changed = fixture.manifest;
    changed[16] ^= 1;
    REQUIRE_FALSE(verify(changed, fixture.payload.size())); // Signed manifest byte mutation
    changed = fixture.manifest;
    changed[64] ^= 1;
    REQUIRE_FALSE(verify(changed, fixture.payload.size())); // Payload length mutation
    REQUIRE_FALSE(verify(fixture.manifest, fixture.payload.size() + 1));
    REQUIRE_FALSE(FileSys::Level256::VerifyPluginManifest(
        std::span<const u8>{fixture.manifest}.first(FileSys::Level256::MANIFEST_SIZE - 1),
        fixture.payload.size(), HashPayload(fixture.payload), fixture.public_key.data()));
    REQUIRE_FALSE(FileSys::Level256::VerifyPluginManifest(
        std::span<const u8>{fixture.payload}, fixture.payload.size(), HashPayload(fixture.payload),
        fixture.public_key.data())); // Ordinary 3GX bytes have no manifest.
}

TEST_CASE("Level256 signature trust and HLE compatibility profiles are independent",
          "[core][file_sys][level256][signature]") {
    const auto& release_key = FileSys::Level256::TrustedReleasePublicKey();
    std::array<u8, 32> fingerprint{};
    CryptoPP::BLAKE2b fingerprint_hash{false, fingerprint.size()};
    fingerprint_hash.Update(release_key.data(), release_key.size());
    fingerprint_hash.TruncatedFinal(fingerprint.data(), fingerprint.size());
    constexpr std::array<u8, 8> expected_fingerprint = {0x62, 0xBE, 0xAD, 0xD9,
                                                        0x4C, 0x22, 0x4D, 0xB3};
    REQUIRE(
        std::equal(expected_fingerprint.begin(), expected_fingerprint.end(), fingerprint.begin()));

    using FileSys::Level256::IsHleCompatibilityVerified;
    using FileSys::Level256::IsHleCompatibleArtifactVersion;

    REQUIRE(IsHleCompatibleArtifactVersion(20031));
    REQUIRE_FALSE(IsHleCompatibleArtifactVersion(20032));
    REQUIRE(IsHleCompatibilityVerified(true, false, 99999)); // Exact legacy SHA path.
    REQUIRE(IsHleCompatibilityVerified(false, true, 20031));
    REQUIRE_FALSE(IsHleCompatibilityVerified(false, true, 20032));
    REQUIRE_FALSE(IsHleCompatibilityVerified(false, false, 20031));
}
