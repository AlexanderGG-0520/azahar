// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

// YW save obfuscation logic adapted from yw_save.py:
// Copyright (c) 2016 togenyan
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "core/file_sys/yw2_save_date.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <tuple>
#include <vector>
#include <boost/crc.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/ccm.h>
#include <cryptopp/filters.h>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/shared_page.h"

namespace FileSys {
namespace {

constexpr std::size_t kHeaderSize = 0x20;
constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kDateOffset = 0x70;
constexpr std::size_t kVersionOffset = 0x10;
constexpr std::size_t kMinimumSaveSize = 0x2000;
constexpr std::size_t kMaximumSaveSize = 0x20000;
constexpr std::size_t kShinuchiSaveSize = 71048;
// Japanese Shinuchi game1.yw key, not a 3DS hardware key.
constexpr char kShinuchiKey[] = "5+NI8WVq09V7LI5w";
using SaveKey = std::array<u8, 16>;

SaveKey StaticSaveKey() {
    SaveKey key{};
    std::copy_n(reinterpret_cast<const u8*>(kShinuchiKey), key.size(), key.begin());
    return key;
}

u32 Read32(const u8* data) {
    return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
           (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
}

void Write32(u8* data, u32 value) {
    for (int i = 0; i < 4; ++i)
        data[i] = static_cast<u8>(value >> (i * 8));
}

class Xorshift {
public:
    explicit Xorshift(u32 seed) {
        if (seed == 0)
            return;
        for (u32 i = 0; i < 3; ++i) {
            seed ^= seed >> 30;
            seed = seed * (0x6C078966U - 1U) + i + 1U;
            states[i] = seed;
        }
    }

    u32 Next(u32 limit) {
        u32 x = states[0];
        u32 y = states[3];
        states[0] = states[1];
        states[1] = states[2];
        states[2] = states[3];
        x ^= x << 11;
        x ^= x >> 8;
        y ^= y >> 19;
        states[3] = x ^ y;
        return limit == 0 ? states[3] : states[3] % limit;
    }

private:
    std::array<u32, 4> states{0x6C078966U, 0xDD5254A5U, 0xB9523B81U, 0x03DF95B3U};
};

void ApplyYwCipher(std::vector<u8>& data) {
    std::array<u8, 256> table{};
    std::array<u16, 256> primes{};
    for (unsigned i = 0; i < table.size(); ++i)
        table[i] = static_cast<u8>(i);

    for (u32 candidate = 3, count = 0; count < primes.size(); candidate += 2) {
        bool prime = true;
        for (u32 divisor = 3; divisor * divisor <= candidate; divisor += 2) {
            if (candidate % divisor == 0) {
                prime = false;
                break;
            }
        }
        if (prime)
            primes[count++] = static_cast<u16>(candidate);
    }

    Xorshift rng(Read32(data.data() + data.size() - 4));
    for (unsigned i = 0; i < 0x1000; ++i) {
        const u32 value = rng.Next(0x10000);
        const u8 a = table[value & 0xFF];
        const u8 b = table[(value >> 8) & 0xFF];
        if ((value & 0xFF) != ((value >> 8) & 0xFF))
            std::swap(table[a], table[b]);
    }

    const std::size_t body_size = data.size() - 8;
    for (std::size_t i = 0; i < body_size; ++i) {
        const u16 prime = primes[table[(i & 0xFF00) >> 8]];
        data[i] ^= table[(prime * (i + 1)) & 0xFF];
    }
}

u32 Crc32(const std::vector<u8>& data) {
    boost::crc_32_type crc;
    crc.process_bytes(data.data(), data.size() - 8);
    return crc.checksum();
}

bool DeriveKeyFromHead(const std::filesystem::path& file, SaveKey& key) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return false;

    std::vector<u8> head(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
    if (head.size() < 0x20 || head.size() > kMaximumSaveSize ||
        Crc32(head) != Read32(head.data() + head.size() - 8))
        return false;

    ApplyYwCipher(head);
    if (head[0] != 0xFE || head[1] != 0xFF)
        return false;

    Xorshift rng(Read32(head.data() + 0x0C));
    for (u8& byte : key)
        byte = static_cast<u8>(rng.Next(0x100));
    return true;
}

bool DecryptSave(const std::vector<u8>& input, const SaveKey& key, u8 version,
                 std::vector<u8>& plain) {
    try {
        const std::size_t body_size = input.size() - kHeaderSize;
        std::vector<u8> cipher(input.begin() + kHeaderSize, input.end());
        cipher.insert(cipher.end(), input.begin() + 0x10, input.begin() + kHeaderSize);

        plain.resize(body_size);
        CryptoPP::CCM<CryptoPP::AES, 16>::Decryption decryptor;
        decryptor.SetKeyWithIV(key.data(), key.size(), input.data(), kNonceSize);
        decryptor.SpecifyDataLengths(0, body_size, 0);
        CryptoPP::AuthenticatedDecryptionFilter filter(
            decryptor, new CryptoPP::ArraySink(plain.data(), plain.size()));
        CryptoPP::ArraySource source(cipher.data(), cipher.size(), true,
                                     new CryptoPP::Redirector(filter));
        if (!filter.GetLastResult() || Crc32(plain) != Read32(plain.data() + plain.size() - 8))
            return false;

        ApplyYwCipher(plain);
        return plain[0] == 0xFE && plain[1] == 0xFF && plain[kVersionOffset] == version;
    } catch (const CryptoPP::Exception&) {
        return false;
    }
}

bool EncryptSave(const std::vector<u8>& original, const SaveKey& key, std::vector<u8>& plain,
                 std::vector<u8>& output) {
    try {
        ApplyYwCipher(plain);
        Write32(plain.data() + plain.size() - 8, Crc32(plain));

        std::vector<u8> ciphertext(plain.size() + kTagSize);
        CryptoPP::CCM<CryptoPP::AES, 16>::Encryption encryptor;
        encryptor.SetKeyWithIV(key.data(), key.size(), original.data(), kNonceSize);
        encryptor.SpecifyDataLengths(0, plain.size(), 0);
        CryptoPP::ArraySource source(
            plain.data(), plain.size(), true,
            new CryptoPP::AuthenticatedEncryptionFilter(
                encryptor, new CryptoPP::ArraySink(ciphertext.data(), ciphertext.size())));

        output.assign(original.begin(), original.begin() + kHeaderSize);
        std::copy(ciphertext.end() - kTagSize, ciphertext.end(), output.begin() + 0x10);
        output.insert(output.end(), ciphertext.begin(), ciphertext.end() - kTagSize);
        return output.size() == original.size();
    } catch (const CryptoPP::Exception&) {
        return false;
    }
}

bool EmulatedLocalTime(std::tm& result) {
    // Use the same clock exposed to the guest, including offset/fixed-clock modes
    // and emulated elapsed time. The host wall clock can differ while paused.
    auto& system = Core::System::GetInstance();
    if (!system.KernelRunning())
        return false;
    const u64 elapsed_ms = system.Kernel().GetSharedPageHandler().GetSystemTimeSince2000();
    std::tm epoch{};
    epoch.tm_mday = 1;
    epoch.tm_year = 100; // 2000-01-01, local 3DS epoch
    epoch.tm_isdst = 0;
    const std::time_t start = std::mktime(&epoch);
    if (start == static_cast<std::time_t>(-1))
        return false;
    const std::time_t emulated_time = start + static_cast<std::time_t>(elapsed_ms / 1000);
    const std::tm* local = std::localtime(&emulated_time);
    if (!local)
        return false;
    result = *local;
    return true;
}

} // namespace

bool NormalizeYw2SaveDateAt(const std::string& filename, const std::tm& now) {
    const std::filesystem::path path(filename);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::vector<u8> encrypted(std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{});
    input.close();
    const std::string title_id = path.parent_path().parent_path().parent_path().filename().string();
    const bool shinuchi = title_id == "00155100";
    const bool ganso_honke = title_id == "0012f900" || title_id == "0012f800";
    if ((!shinuchi && !ganso_honke) || encrypted.size() < kMinimumSaveSize ||
        encrypted.size() > kMaximumSaveSize || (shinuchi && encrypted.size() != kShinuchiSaveSize))
        return false;

    const u8 expected_version = shinuchi ? 4 : 2;
    std::vector<u8> plain;
    SaveKey key = StaticSaveKey();
    bool authenticated = false;

    // Ganso/Honke v2.x derive the AES key from head.yw, while v1.x use
    // the same static key as Shinuchi. Never edit an unauthenticated save.
    if (ganso_honke) {
        SaveKey derived{};
        if (DeriveKeyFromHead(path.parent_path() / "head.yw", derived) &&
            DecryptSave(encrypted, derived, expected_version, plain)) {
            key = derived;
            authenticated = true;
        }
    }
    if (!authenticated)
        authenticated = DecryptSave(encrypted, key, expected_version, plain);
    if (!authenticated) {
        LOG_WARNING(Service_FS, "YW2 save-date normalization skipped: authentication failed");
        return false;
    }

    const u16 saved_year = static_cast<u16>(plain[kDateOffset] | (plain[kDateOffset + 1] << 8));
    const u8 month = plain[kDateOffset + 2];
    const u8 day = plain[kDateOffset + 3];
    if (saved_year < 2000 || saved_year > 2199 || month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    const auto saved_date = std::tuple{saved_year, month, day};
    const auto current_date = std::tuple{now.tm_year + 1900, now.tm_mon + 1, now.tm_mday};
    if (saved_date <= current_date) {
        LOG_INFO(Service_FS, "YW2 datefix skipped {}: saved {:04}-{:02}-{:02}, guest {:04}-{:02}-{:02}",
                 path.filename().string(), saved_year, month, day, now.tm_year + 1900,
                 now.tm_mon + 1, now.tm_mday);
        return false;
    }

    const u16 year = static_cast<u16>(now.tm_year + 1900);
    plain[kDateOffset] = static_cast<u8>(year);
    plain[kDateOffset + 1] = static_cast<u8>(year >> 8);
    plain[kDateOffset + 2] = static_cast<u8>(now.tm_mon + 1);
    plain[kDateOffset + 3] = static_cast<u8>(now.tm_mday);
    plain[kDateOffset + 4] = static_cast<u8>(now.tm_hour);
    plain[kDateOffset + 5] = static_cast<u8>(now.tm_min);
    plain[kDateOffset + 6] = static_cast<u8>(now.tm_sec);

    const std::vector<u8> expected_plain = plain;
    std::vector<u8> updated;
    if (!EncryptSave(encrypted, key, plain, updated))
        return false;

    // Refuse to overwrite the save unless the newly encrypted file authenticates.
    std::vector<u8> verification;
    if (!DecryptSave(updated, key, expected_version, verification) ||
        !std::equal(expected_plain.begin(), expected_plain.end() - 8, verification.begin()))
        return false;

    // Preserve one original backup and never replace it with a subsequently modified save.
    // Back up outside the emulated archive root so the game never sees this file.
    const std::filesystem::path backup =
        path.parent_path().parent_path() / (path.filename().string() + ".before-yw2-datefix.bak");
    std::error_code error;
    const bool backup_exists = std::filesystem::exists(backup, error);
    if (error)
        return false;
    if (!backup_exists) {
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error);
        if (error) {
            LOG_WARNING(Service_FS, "YW2 save-date normalization skipped: backup failed");
            return false;
        }
    }

    // Keep the temporary file out of the emulated save archive as well.
    const std::filesystem::path temporary =
        path.parent_path().parent_path() / (path.filename().string() + ".yw2-datefix.tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(reinterpret_cast<const char*>(updated.data()),
                     static_cast<std::streamsize>(updated.size()));
        output.flush();
        if (!output.good())
            return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        LOG_WARNING(Service_FS, "YW2 save-date normalization skipped: atomic replace failed");
        return false;
    }
    LOG_INFO(Service_FS, "Normalized future timestamp in YW2 save {}", path.filename().string());
    return true;
}

bool NormalizeYw2SaveDate(const std::string& filename) {
    std::tm now{};
    if (!EmulatedLocalTime(now)) {
        LOG_WARNING(Service_FS, "YW2 datefix skipped: guest clock unavailable");
        return false;
    }
    return NormalizeYw2SaveDateAt(filename, now);
}

bool IsYw2ObservedSaveFile(const std::string& mount_point, const std::string& filename) {
    if (filename != "head.yw" && filename != "game1.yw" && filename != "game2.yw" &&
        filename != "game3.yw")
        return false;
    const std::filesystem::path path = std::filesystem::path(mount_point) / filename;
    const auto slot = path.parent_path();
    const auto data = slot.parent_path();
    const auto title = data.parent_path();
    const auto category = title.parent_path();
    if (slot.filename() != "00000001" || data.filename() != "data" ||
        category.filename() != "00040000")
        return false;
    const auto id = title.filename();
    return id == "0012f900" || id == "0012f800" || id == "00155100";
}

void NormalizeYw2SaveDateOnCommit(const std::string& filename) {
    const std::filesystem::path path(filename);
    if (!IsYw2ObservedSaveFile(path.parent_path().string(), path.filename().string()))
        return;
    LOG_INFO(Service_FS, "YW2 datefix observed commit for {}", path.filename().string());
    if (path.filename() != "head.yw") {
        NormalizeYw2SaveDate(filename);
        return;
    }
    // Version 2.x can replace head.yw after gameN.yw. Retry the slots
    // after the new key has been committed.
    for (const char* slot : {"game1.yw", "game2.yw", "game3.yw"}) {
        const auto game = path.parent_path() / slot;
        std::error_code ec;
        if (std::filesystem::is_regular_file(game, ec))
            NormalizeYw2SaveDate(game.string());
    }
}

} // namespace FileSys
