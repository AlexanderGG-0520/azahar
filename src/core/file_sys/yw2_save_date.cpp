// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

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
constexpr std::size_t kDateOffset = 0x70 - kHeaderSize;
constexpr std::size_t kShinuchiSaveSize = 71048;
// Japanese Shinuchi game1.yw key, not a 3DS hardware key.
constexpr char kShinuchiKey[] = "5+NI8WVq09V7LI5w";

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

bool DecryptSave(const std::vector<u8>& input, std::vector<u8>& plain) {
    try {
        const std::size_t body_size = input.size() - kHeaderSize;
        std::vector<u8> cipher(input.begin() + kHeaderSize, input.end());
        cipher.insert(cipher.end(), input.begin() + 0x10, input.begin() + kHeaderSize);

        plain.resize(body_size);
        CryptoPP::CCM<CryptoPP::AES, 16>::Decryption decryptor;
        decryptor.SetKeyWithIV(reinterpret_cast<const u8*>(kShinuchiKey), 16, input.data(),
                               kNonceSize);
        decryptor.SpecifyDataLengths(0, body_size, 0);
        CryptoPP::AuthenticatedDecryptionFilter filter(
            decryptor, new CryptoPP::ArraySink(plain.data(), plain.size()));
        CryptoPP::ArraySource source(cipher.data(), cipher.size(), true,
                                     new CryptoPP::Redirector(filter));
        if (!filter.GetLastResult() || Crc32(plain) != Read32(plain.data() + plain.size() - 8))
            return false;

        ApplyYwCipher(plain);
        return plain[0] == 0xFE && plain[1] == 0xFF;
    } catch (const CryptoPP::Exception&) {
        return false;
    }
}

bool EncryptSave(const std::vector<u8>& original, std::vector<u8>& plain,
                 std::vector<u8>& output) {
    try {
        ApplyYwCipher(plain);
        Write32(plain.data() + plain.size() - 8, Crc32(plain));

        std::vector<u8> ciphertext(plain.size() + kTagSize);
        CryptoPP::CCM<CryptoPP::AES, 16>::Encryption encryptor;
        encryptor.SetKeyWithIV(reinterpret_cast<const u8*>(kShinuchiKey), 16, original.data(),
                               kNonceSize);
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

bool NormalizeYw2ShinuchiSaveDate(const std::string& filename) {
    std::tm now{};
    if (!EmulatedLocalTime(now))
        return false;

    const std::filesystem::path path(filename);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::vector<u8> encrypted(std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{});
    input.close();
    if (encrypted.size() != kShinuchiSaveSize)
        return false;

    std::vector<u8> plain;
    if (!DecryptSave(encrypted, plain)) {
        LOG_WARNING(Service_FS, "YW2 save-date normalization skipped: authentication failed");
        return false;
    }

    const u16 saved_year = static_cast<u16>(plain[kDateOffset] | (plain[kDateOffset + 1] << 8));
    const u8 month = plain[kDateOffset + 2];
    const u8 day = plain[kDateOffset + 3];
    if (saved_year < 2000 || saved_year > 2199 || month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    const auto saved_date = std::tuple{saved_year, month, day};
    const auto current_date =
        std::tuple{now.tm_year + 1900, now.tm_mon + 1, now.tm_mday};
    if (saved_date <= current_date)
        return false;

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
    if (!EncryptSave(encrypted, plain, updated))
        return false;

    // Refuse to overwrite the save unless the newly encrypted file authenticates.
    std::vector<u8> verification;
    if (!DecryptSave(updated, verification) ||
        !std::equal(expected_plain.begin(), expected_plain.end() - 8, verification.begin()))
        return false;

    // Preserve one original backup and never replace it with a subsequently modified save.
    // Back up outside the emulated archive root so the game never sees this file.
    const std::filesystem::path backup =
        path.parent_path().parent_path() / "game1.yw.before-yw2-datefix.bak";
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

    const std::filesystem::path temporary = filename + ".yw2-datefix.tmp";
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
    LOG_INFO(Service_FS, "Normalized future timestamp in YW2 Shinuchi game1.yw");
    return true;
}

} // namespace FileSys
