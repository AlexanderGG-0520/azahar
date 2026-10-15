// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
//
// Synthetic save fixtures derived from verified format metadata only.
// No actual player saves or player data are stored in this repository.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <boost/crc.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/ccm.h>
#include <cryptopp/filters.h>
#include "core/file_sys/yw2_save_date.h"

namespace FileSys {
namespace {

using Bytes = std::vector<std::uint8_t>;
using Key = std::array<std::uint8_t, 16>;
namespace fs = std::filesystem;

constexpr std::size_t kSavedDate = 0x70;
constexpr std::size_t kGansoHonkeSize = 70980;
constexpr std::size_t kShinuchiSize = 71048;
constexpr std::size_t kHeadSize = 21812;
constexpr Key kStaticKey{'5', '+', 'N', 'I', '8', 'W', 'V', 'q',
                         '0', '9', 'V', '7', 'L', 'I', '5', 'w'};

// Derived independently from the two uploaded real v2.x head.yw files.
// Only non-player cryptographic test vectors are retained.
constexpr std::array<std::uint32_t, 2> kHeadSeeds{0xC66D84D0, 0xCBCA248F};
constexpr std::array<Key, 2> kDerivedKeys{{
    {0xDA, 0x26, 0x9E, 0x60, 0x81, 0x2B, 0xBF, 0x21,
     0x5C, 0x0E, 0xFE, 0xBB, 0xEC, 0x95, 0x94, 0xA0},
    {0x4A, 0xE2, 0x2D, 0x28, 0xC8, 0x53, 0x13, 0x04,
     0xF8, 0xA7, 0xA1, 0x9C, 0x3B, 0x9D, 0x2E, 0xC8},
}};

std::uint32_t GetU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void PutU32(std::uint8_t* p, std::uint32_t x) {
    for (int i = 0; i != 4; ++i)
        p[i] = static_cast<std::uint8_t>(x >> (i * 8));
}

void PutDate(Bytes& body, unsigned year, unsigned month, unsigned day) {
    body[kSavedDate] = static_cast<std::uint8_t>(year);
    body[kSavedDate + 1] = static_cast<std::uint8_t>(year >> 8);
    body[kSavedDate + 2] = static_cast<std::uint8_t>(month);
    body[kSavedDate + 3] = static_cast<std::uint8_t>(day);
    body[kSavedDate + 4] = 15;
    body[kSavedDate + 5] = 15;
    body[kSavedDate + 6] = 0;
}

// Standalone fixture oracle: deliberately separate from production helper.
struct ReferenceXor {
    std::array<std::uint32_t, 4> s{0x6C078966, 0xDD5254A5, 0xB9523B81, 0x03DF95B3};
    explicit ReferenceXor(std::uint32_t seed) {
        if (!seed)
            return;
        for (std::uint32_t n = 0; n < 3; ++n) {
            seed ^= seed >> 30;
            seed = seed * (0x6C078966U - 1U) + n + 1U;
            s[n] = seed;
        }
    }
    std::uint32_t Next(std::uint32_t modulus) {
        auto x = s[0];
        auto y = s[3];
        s[0] = s[1];
        s[1] = s[2];
        s[2] = s[3];
        x ^= x << 11;
        x ^= x >> 8;
        y ^= y >> 19;
        s[3] = x ^ y;
        return modulus ? s[3] % modulus : s[3];
    }
};

void Obfuscate(Bytes& body) {
    std::array<unsigned, 256> primes{};
    unsigned pcount = 0;
    for (unsigned n = 3; pcount < primes.size(); n += 2) {
        bool prime = true;
        for (unsigned j = 3; j * j <= n; j += 2)
            if (n % j == 0)
                prime = false;
        if (prime)
            primes[pcount++] = n;
    }

    std::array<std::uint8_t, 256> table{};
    for (unsigned i = 0; i < table.size(); ++i)
        table[i] = static_cast<std::uint8_t>(i);

    ReferenceXor rng(GetU32(body.data() + body.size() - 4));
    for (unsigned i = 0; i < 4096; ++i) {
        auto r = rng.Next(65536);
        unsigned r1 = r & 255, r2 = (r >> 8) & 255;
        if (r1 != r2) {
            const auto a = table[r1], b = table[r2];
            std::swap(table[a], table[b]);
        }
    }

    for (std::size_t i = 0; i < body.size() - 8; ++i)
        body[i] ^= table[(primes[table[(i >> 8) & 255]] * (i + 1)) & 255];
}

void SetChecksum(Bytes& body) {
    boost::crc_32_type crc;
    crc.process_bytes(body.data(), body.size() - 8);
    PutU32(body.data() + body.size() - 8, crc.checksum());
}

Bytes MakeHead(std::uint32_t head_seed) {
    Bytes body(kHeadSize, 0);
    body[0] = 0xFE;
    body[1] = 0xFF;
    PutU32(body.data() + 0x0C, head_seed);
    PutU32(body.data() + body.size() - 4, 0x00C0FFEE);
    Obfuscate(body);
    SetChecksum(body);
    return body;
}

Bytes EncodeGame(const Bytes& plaintext, const Key& key) {
    Bytes body = plaintext;
    Obfuscate(body);
    SetChecksum(body);
    std::array<std::uint8_t, 12> nonce{};
    for (unsigned i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::uint8_t>(i + 1);

    Bytes ciphertext(body.size() + 16);
    CryptoPP::CCM<CryptoPP::AES, 16>::Encryption cipher;
    cipher.SetKeyWithIV(key.data(), key.size(), nonce.data(), nonce.size());
    cipher.SpecifyDataLengths(0, body.size(), 0);
    CryptoPP::ArraySource source(
        body.data(), body.size(), true,
        new CryptoPP::AuthenticatedEncryptionFilter(
            cipher, new CryptoPP::ArraySink(ciphertext.data(), ciphertext.size())));

    Bytes file(0x20);
    std::copy(nonce.begin(), nonce.end(), file.begin());
    std::copy(ciphertext.end() - 16, ciphertext.end(), file.begin() + 0x10);
    file.insert(file.end(), ciphertext.begin(), ciphertext.end() - 16);
    return file;
}

Bytes DecodeGame(const Bytes& encrypted, const Key& key) {
    Bytes payload(encrypted.begin() + 0x20, encrypted.end());
    payload.insert(payload.end(), encrypted.begin() + 0x10, encrypted.begin() + 0x20);
    Bytes body(encrypted.size() - 0x20);
    CryptoPP::CCM<CryptoPP::AES, 16>::Decryption cipher;
    cipher.SetKeyWithIV(key.data(), key.size(), encrypted.data(), 12);
    cipher.SpecifyDataLengths(0, body.size(), 0);
    CryptoPP::AuthenticatedDecryptionFilter filter(
        cipher, new CryptoPP::ArraySink(body.data(), body.size()));
    CryptoPP::ArraySource source(payload.data(), payload.size(), true,
                                 new CryptoPP::Redirector(filter));
    REQUIRE(filter.GetLastResult());
    Obfuscate(body);
    return body;
}

Bytes SampleGame(std::size_t size, std::uint8_t version, const Key& key) {
    Bytes plain(size - 0x20, 0);
    plain[0] = 0xFE;
    plain[1] = 0xFF;
    plain[0x10] = version;
    plain[0x88] = 0xA5; // gameplay data must never be modified
    PutDate(plain, 2027, 3, 22);
    PutU32(plain.data() + plain.size() - 4, 0x12345ABC);
    return EncodeGame(plain, key);
}

void WriteFile(const fs::path& path, const Bytes& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output));
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    REQUIRE(output.good());
}

Bytes ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(static_cast<bool>(input));
    return Bytes(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
}

struct TempSave {
    fs::path root;
    fs::path save;

    explicit TempSave(std::string title) {
        root = fs::temp_directory_path() /
               ("yw2-regression-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        save = root / "00040000" / title / "data" / "00000001" / "game1.yw";
        fs::create_directories(save.parent_path());
    }

    ~TempSave() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

std::tm PresentClock() {
    std::tm time{};
    time.tm_year = 2026 - 1900;
    time.tm_mon = 9;
    time.tm_mday = 15;
    time.tm_hour = 15;
    time.tm_min = 15;
    return time;
}

void CheckPreservesOtherBytes(const Bytes& original, const Bytes& updated, const Key& key) {
    auto before = DecodeGame(original, key);
    auto after = DecodeGame(updated, key);
    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size() - 8; ++i) {
        if (i >= kSavedDate && i < kSavedDate + 7)
            continue;
        REQUIRE(before[i] == after[i]);
    }
    CHECK(after[kSavedDate] == 0xEA); // 2026 little endian
    CHECK(after[kSavedDate + 1] == 0x07);
    CHECK(after[kSavedDate + 2] == 10);
    CHECK(after[kSavedDate + 3] == 15);
}

} // namespace

TEST_CASE("YW2 real-head-derived key vectors and future-date correction",
          "[core][file_sys][yw2]") {
    const std::array<std::string, 2> title_ids{"0012f900", "0012f800"};
    for (std::size_t i = 0; i < kHeadSeeds.size(); ++i) {
        DYNAMIC_SECTION("v2.x title " << title_ids[i] << " head vector " << i) {
            TempSave fixture(title_ids[i]);
            WriteFile(fixture.save.parent_path() / "head.yw", MakeHead(kHeadSeeds[i]));
            const Bytes input = SampleGame(kGansoHonkeSize, 2, kDerivedKeys[i]);
            WriteFile(fixture.save, input);
            REQUIRE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
            const Bytes output = ReadFile(fixture.save);
            CHECK(output != input);
            CheckPreservesOtherBytes(input, output, kDerivedKeys[i]);
            const fs::path backup =
                fixture.save.parent_path().parent_path() / "game1.yw.before-yw2-datefix.bak";
            CHECK(ReadFile(backup) == input);
            CHECK_FALSE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
            CHECK(ReadFile(fixture.save) == output);
        }
    }
}

TEST_CASE("YW2 fixed-key save date normalization", "[core][file_sys][yw2]") {
    for (const auto& title : {"0012f900", "0012f800", "00155100"}) {
        DYNAMIC_SECTION("title " << title) {
            TempSave fixture(title);
            const bool shinuchi = std::string(title) == "00155100";
            const Bytes original =
                SampleGame(shinuchi ? kShinuchiSize : kGansoHonkeSize, shinuchi ? 4 : 2, kStaticKey);
            WriteFile(fixture.save, original);
            REQUIRE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
            CheckPreservesOtherBytes(original, ReadFile(fixture.save), kStaticKey);
        }
    }
}

TEST_CASE("YW2 corrupted or unrelated save is never overwritten", "[core][file_sys][yw2]") {
    SECTION("wrong or missing head for v2.x") {
        TempSave fixture("0012f800");
        const Bytes original = SampleGame(kGansoHonkeSize, 2, kDerivedKeys[0]);
        WriteFile(fixture.save, original);
        CHECK_FALSE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
        CHECK(ReadFile(fixture.save) == original);
        WriteFile(fixture.save.parent_path() / "head.yw", MakeHead(kHeadSeeds[1]));
        CHECK_FALSE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
        CHECK(ReadFile(fixture.save) == original);
    }
    SECTION("invalid AES-CCM authentication") {
        TempSave fixture("00155100");
        Bytes damaged = SampleGame(kShinuchiSize, 4, kStaticKey);
        damaged[0x24] ^= 0x10;
        WriteFile(fixture.save, damaged);
        CHECK_FALSE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
        CHECK(ReadFile(fixture.save) == damaged);
    }
    SECTION("other title") {
        TempSave fixture("abcdef01");
        const Bytes original = SampleGame(kGansoHonkeSize, 2, kStaticKey);
        WriteFile(fixture.save, original);
        CHECK_FALSE(NormalizeYw2SaveDateAt(fixture.save.string(), PresentClock()));
        CHECK(ReadFile(fixture.save) == original);
    }
}

} // namespace FileSys
