// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

// Busters save decoding follows the format documented by yw_save.py:
// Copyright (c) 2016 togenyan
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "core/file_sys/ywb_save_date.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>
#include <boost/crc.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/ccm.h>
#include <cryptopp/filters.h>
#include <cryptopp/osrng.h>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/shared_page.h"

namespace FileSys {
namespace {

constexpr std::size_t kYwHeaderSize = 0x20;
constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kGameDateOffset = 0x4C;
constexpr std::size_t kHeadDateOffset = 0x3A20;
constexpr std::size_t kMinimumFileSize = 0x2000;
constexpr std::size_t kMaximumFileSize = 0x20000;
using Bytes = std::vector<std::uint8_t>;
using SaveKey = std::array<std::uint8_t, 16>;

void SetDiagnosticStage(const char** diagnostic_stage, const char* stage) {
    if (diagnostic_stage)
        *diagnostic_stage = stage;
}

std::uint32_t Read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint32_t Read32(const Bytes& data, std::size_t offset) {
    return Read32(data.data() + offset);
}

void Write32(std::uint8_t* data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

class Xorshift {
public:
    explicit Xorshift(std::uint32_t seed) {
        if (!seed)
            return;
        for (std::uint32_t i = 0; i < 3; ++i) {
            seed ^= seed >> 30;
            seed = seed * (0x6C078966U - 1U) + i + 1U;
            states[i] = seed;
        }
    }

    std::uint32_t Next(std::uint32_t limit) {
        std::uint32_t x = states[0];
        std::uint32_t y = states[3];
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
    std::array<std::uint32_t, 4> states{0x6C078966U, 0xDD5254A5U, 0xB9523B81U, 0x03DF95B3U};
};

void ApplyYwCipher(Bytes& data) {
    std::array<std::uint8_t, 256> table{};
    std::array<std::uint16_t, 256> primes{};
    for (unsigned i = 0; i < table.size(); ++i)
        table[i] = static_cast<std::uint8_t>(i);

    for (std::uint32_t candidate = 3, count = 0; count < primes.size(); candidate += 2) {
        bool prime = true;
        for (std::uint32_t divisor = 3; divisor * divisor <= candidate; divisor += 2) {
            if (candidate % divisor == 0) {
                prime = false;
                break;
            }
        }
        if (prime)
            primes[count++] = static_cast<std::uint16_t>(candidate);
    }

    Xorshift rng(Read32(data, data.size() - 4));
    for (unsigned i = 0; i < 0x1000; ++i) {
        const std::uint32_t value = rng.Next(0x10000);
        const std::uint8_t a = table[value & 0xFF];
        const std::uint8_t b = table[(value >> 8) & 0xFF];
        if ((value & 0xFF) != ((value >> 8) & 0xFF))
            std::swap(table[a], table[b]);
    }

    for (std::size_t i = 0; i < data.size() - 8; ++i) {
        const std::uint16_t prime = primes[table[(i & 0xFF00) >> 8]];
        data[i] ^= table[(prime * (i + 1)) & 0xFF];
    }
}

std::uint32_t Crc32(const Bytes& data) {
    boost::crc_32_type crc;
    crc.process_bytes(data.data(), data.size() - 8);
    return crc.checksum();
}

bool ReadFile(const std::string& filename, Bytes& data) {
    std::ifstream stream(filename, std::ios::binary);
    if (!stream)
        return false;
    data.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
    return data.size() >= kMinimumFileSize && data.size() <= kMaximumFileSize;
}

bool DecodeHead(const Bytes& encrypted_head, Bytes& head, const char** diagnostic_stage = nullptr) {
    if (encrypted_head.size() < kYwHeaderSize) {
        SetDiagnosticStage(diagnostic_stage, "head-format-size");
        return false;
    }
    if (Crc32(encrypted_head) != Read32(encrypted_head, encrypted_head.size() - 8)) {
        SetDiagnosticStage(diagnostic_stage, "head-internal-crc");
        return false;
    }
    head = encrypted_head;
    ApplyYwCipher(head);
    if (head[0] != 0xFE || head[1] != 0xFF) {
        SetDiagnosticStage(diagnostic_stage, "head-format");
        return false;
    }
    SetDiagnosticStage(diagnostic_stage, "validated");
    return true;
}

bool MatchesExceptCrc(const Bytes& expected, const Bytes& actual);

bool HexPathComponentEquals(const std::filesystem::path& component, std::string_view expected) {
    const std::string actual = component.string();
    return actual.size() == expected.size() &&
           std::equal(actual.begin(), actual.end(), expected.begin(),
                      [](const unsigned char left, const unsigned char right) {
                          return std::tolower(left) == std::tolower(right);
                      });
}

bool EncryptHead(const Bytes& plain, Bytes& encrypted) {
    if (plain.size() < kYwHeaderSize || plain.size() < 8)
        return false;
    encrypted = plain;
    ApplyYwCipher(encrypted);
    Write32(encrypted.data() + encrypted.size() - 8, Crc32(encrypted));
    Bytes verification;
    if (!DecodeHead(encrypted, verification) || !MatchesExceptCrc(plain, verification))
        return false;
    return true;
}

bool ReadHeadField(const Bytes& head, std::uint32_t field, std::uint32_t& value) {
    const std::uint32_t record = Read32(head, 0x10);
    if (record == 0)
        return false;
    const std::uint64_t offset = static_cast<std::uint64_t>(record - 1) * 0x78 + 0x39C8 +
                                 static_cast<std::uint64_t>(field) * 4 + 0x18;
    if (offset + 4 > head.size() - 8)
        return false;
    value = Read32(head, static_cast<std::size_t>(offset));
    return true;
}

bool DeriveGettoKey(const Bytes& head, SaveKey& key, const char** diagnostic_stage = nullptr) {
    std::uint32_t seed = Read32(head, 0x0C);
    std::uint32_t field_0 = 0;
    std::uint32_t field_a = 0;
    std::uint32_t field_c = 0;
    if (!ReadHeadField(head, 0, field_0) || !ReadHeadField(head, 0x0A, field_a) ||
        !ReadHeadField(head, 0x0C, field_c)) {
        SetDiagnosticStage(diagnostic_stage, "key-derivation-fields");
        return false;
    }

    seed ^= field_c;
    if ((field_0 & 0x4000) != 0)
        seed = ~seed;
    Xorshift rng(seed);
    for (std::uint32_t i = 0; i < (field_a & 0xFF); ++i)
        rng.Next(0x10000);
    for (std::uint8_t& byte : key)
        byte = static_cast<std::uint8_t>(rng.Next(0x100));
    SetDiagnosticStage(diagnostic_stage, "validated");
    return true;
}

bool DecryptGame(const Bytes& encrypted, const SaveKey& key, Bytes& plain,
                 const char** diagnostic_stage = nullptr) {
    if (encrypted.size() < kYwHeaderSize) {
        SetDiagnosticStage(diagnostic_stage, "game-format-size");
        return false;
    }
    try {
        const std::size_t body_size = encrypted.size() - kYwHeaderSize;
        Bytes authenticated(encrypted.begin() + kYwHeaderSize, encrypted.end());
        authenticated.insert(authenticated.end(), encrypted.begin() + 0x10,
                             encrypted.begin() + kYwHeaderSize);
        plain.resize(body_size);
        CryptoPP::CCM<CryptoPP::AES, 16>::Decryption decryptor;
        decryptor.SetKeyWithIV(key.data(), key.size(), encrypted.data(), kNonceSize);
        decryptor.SpecifyDataLengths(0, body_size, 0);
        CryptoPP::AuthenticatedDecryptionFilter filter(
            decryptor, new CryptoPP::ArraySink(plain.data(), plain.size()));
        CryptoPP::ArraySource source(authenticated.data(), authenticated.size(), true,
                                     new CryptoPP::Redirector(filter));
        if (!filter.GetLastResult()) {
            SetDiagnosticStage(diagnostic_stage, "aes-ccm-authentication");
            return false;
        }
        if (plain.size() < 8 || Crc32(plain) != Read32(plain, plain.size() - 8)) {
            SetDiagnosticStage(diagnostic_stage, "game-internal-crc");
            return false;
        }
        ApplyYwCipher(plain);
        if (plain.size() < 8 || plain[0] != 0xFE || plain[1] != 0xFF ||
            (Read32(plain, 4) & 0xFF) != 0xF1) {
            SetDiagnosticStage(diagnostic_stage, "game-format");
            return false;
        }
        SetDiagnosticStage(diagnostic_stage, "validated");
        return true;
    } catch (const CryptoPP::Exception&) {
        SetDiagnosticStage(diagnostic_stage, "aes-ccm-authentication");
        return false;
    }
}

bool EncryptGame(const Bytes& original, const SaveKey& key, const Bytes& plain, Bytes& updated) {
    try {
        Bytes authenticated = plain;
        ApplyYwCipher(authenticated);
        Write32(authenticated.data() + authenticated.size() - 8, Crc32(authenticated));

        std::array<std::uint8_t, kNonceSize> nonce{};
        CryptoPP::AutoSeededRandomPool random;
        random.GenerateBlock(nonce.data(), nonce.size());

        Bytes ciphertext(authenticated.size() + kTagSize);
        CryptoPP::CCM<CryptoPP::AES, 16>::Encryption encryptor;
        encryptor.SetKeyWithIV(key.data(), key.size(), nonce.data(), nonce.size());
        encryptor.SpecifyDataLengths(0, authenticated.size(), 0);
        CryptoPP::ArraySource source(
            authenticated.data(), authenticated.size(), true,
            new CryptoPP::AuthenticatedEncryptionFilter(
                encryptor, new CryptoPP::ArraySink(ciphertext.data(), ciphertext.size())));

        updated.assign(original.begin(), original.begin() + kYwHeaderSize);
        std::copy(nonce.begin(), nonce.end(), updated.begin());
        std::copy(ciphertext.end() - kTagSize, ciphertext.end(), updated.begin() + 0x10);
        updated.insert(updated.end(), ciphertext.begin(), ciphertext.end() - kTagSize);
        return updated.size() == original.size();
    } catch (const CryptoPP::Exception&) {
        return false;
    }
}

std::optional<std::tm> ReadDateAt(const Bytes& plain, std::size_t offset) {
    if (plain.size() < offset + 7 || offset + 7 > plain.size() - 8)
        return std::nullopt;

    const std::uint16_t year = static_cast<std::uint16_t>(plain[offset]) |
                               (static_cast<std::uint16_t>(plain[offset + 1]) << 8);
    const std::uint8_t month = plain[offset + 2];
    const std::uint8_t day = plain[offset + 3];
    const std::uint8_t hour = plain[offset + 4];
    const std::uint8_t minute = plain[offset + 5];
    const std::uint8_t second = plain[offset + 6];
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || hour > 23 || minute > 59 ||
        second > 59 ||
        !std::chrono::year_month_day{std::chrono::year{year}, std::chrono::month{month},
                                     std::chrono::day{day}}
             .ok())
        return std::nullopt;

    std::tm result{};
    result.tm_year = year - 1900;
    result.tm_mon = month - 1;
    result.tm_mday = day;
    result.tm_hour = hour;
    result.tm_min = minute;
    result.tm_sec = second;
    return result;
}

void WriteDateAt(Bytes& plain, std::size_t offset, const std::tm& time) {
    const std::uint16_t year = static_cast<std::uint16_t>(time.tm_year + 1900);
    plain[offset] = static_cast<std::uint8_t>(year);
    plain[offset + 1] = static_cast<std::uint8_t>(year >> 8);
    plain[offset + 2] = static_cast<std::uint8_t>(time.tm_mon + 1);
    plain[offset + 3] = static_cast<std::uint8_t>(time.tm_mday);
    plain[offset + 4] = static_cast<std::uint8_t>(time.tm_hour);
    plain[offset + 5] = static_cast<std::uint8_t>(time.tm_min);
    plain[offset + 6] = static_cast<std::uint8_t>(time.tm_sec);
}

bool IsValidDateTime(const std::tm& time) {
    const int year = time.tm_year + 1900;
    const int month = time.tm_mon + 1;
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || time.tm_hour < 0 ||
        time.tm_hour > 23 || time.tm_min < 0 || time.tm_min > 59 || time.tm_sec < 0 ||
        time.tm_sec > 59)
        return false;
    return std::chrono::year_month_day{std::chrono::year{year},
                                       std::chrono::month{static_cast<unsigned>(month)},
                                       std::chrono::day{static_cast<unsigned>(time.tm_mday)}}
        .ok();
}

auto DateTimeTuple(int year, int month, int day, int hour, int minute, int second) {
    return std::tuple{year, month, day, hour, minute, second};
}

auto DateTimeTuple(const std::tm& time) {
    return DateTimeTuple(time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour,
                         time.tm_min, time.tm_sec);
}

bool MatchesExceptCrc(const Bytes& expected, const Bytes& actual) {
    if (expected.size() != actual.size() || expected.size() < 8)
        return false;
    return std::equal(expected.begin(), expected.end(), actual.begin(),
                      [crc_begin = expected.size() - 8, crc_end = expected.size() - 4,
                       index = std::size_t{0}](std::uint8_t a, std::uint8_t b) mutable {
                          const std::size_t current = index++;
                          return current >= crc_begin && current < crc_end ? true : a == b;
                      });
}

bool EmulatedLocalTime(std::tm& result) {
    // Match the clock exposed to the guest, including Offset Time and fixed-clock modes.
    auto& system = Core::System::GetInstance();
    if (!system.KernelRunning())
        return false;
    const u64 elapsed_ms = system.Kernel().GetSharedPageHandler().GetSystemTimeSince2000();
    std::tm epoch{};
    epoch.tm_mday = 1;
    epoch.tm_year = 100; // 2000-01-01, the 3DS epoch.
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

bool ReplaceSaveFile(const std::filesystem::path& path, const Bytes& updated,
                     const std::string& kind) {
    // Keep recovery data and the temporary file outside the game's visible slot directory.
    const auto slot = path.parent_path();
    const auto backup =
        slot.parent_path() / (slot.filename().string() + "-" + kind + ".before-ywb-datefix.bak");
    const auto temporary =
        slot.parent_path() / (slot.filename().string() + "-" + kind + ".ywb-datefix.tmp");
    std::error_code error;
    const bool backup_exists = std::filesystem::exists(backup, error);
    if (error)
        return false;
    if (!backup_exists &&
        !std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error)) {
        LOG_WARNING(Service_FS, "YWB datefix skipped: unable to create save backup");
        return false;
    }
    if (error)
        return false;

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(reinterpret_cast<const char*>(updated.data()),
                     static_cast<std::streamsize>(updated.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        LOG_WARNING(Service_FS, "YWB datefix skipped: atomic save replacement failed");
        return false;
    }
    return true;
}

std::optional<std::tm> ReadYwbHeadSaveDate(const std::string& filename) {
    Bytes encrypted;
    Bytes plain;
    if (!ReadFile(filename, encrypted) || !DecodeHead(encrypted, plain))
        return std::nullopt;
    return ReadDateAt(plain, kHeadDateOffset);
}

std::string FormatYwbDate(const std::optional<std::tm>& date) {
    if (!date)
        return "unavailable";
    std::array<char, 32> formatted{};
    if (std::strftime(formatted.data(), formatted.size(), "%Y-%m-%d %H:%M:%S", &*date) == 0)
        return "invalid";
    return formatted.data();
}

const char* YwbNormalizationResultName(YwbSaveDateNormalizationResult result) {
    switch (result) {
    case YwbSaveDateNormalizationResult::Invalid:
        return "Invalid";
    case YwbSaveDateNormalizationResult::Unchanged:
        return "Unchanged";
    case YwbSaveDateNormalizationResult::Updated:
        return "Corrected";
    }
    return "Invalid";
}

} // namespace

std::optional<std::tm> ReadYwbGettoSaveDate(const std::string& game_filename,
                                            const std::string& head_filename,
                                            const char** diagnostic_stage) {
    SetDiagnosticStage(diagnostic_stage, "invalid-input");
    if (std::filesystem::path(game_filename).filename() != "game1.yw" ||
        std::filesystem::path(head_filename).filename() != "head.yw")
        return std::nullopt;

    Bytes encrypted_game;
    Bytes encrypted_head;
    if (!ReadFile(head_filename, encrypted_head)) {
        SetDiagnosticStage(diagnostic_stage, "head-read-or-size");
        return std::nullopt;
    }
    if (!ReadFile(game_filename, encrypted_game)) {
        SetDiagnosticStage(diagnostic_stage, "game-read-or-size");
        return std::nullopt;
    }

    Bytes head;
    if (!DecodeHead(encrypted_head, head, diagnostic_stage))
        return std::nullopt;

    SaveKey key{};
    if (!DeriveGettoKey(head, key, diagnostic_stage))
        return std::nullopt;

    Bytes plain;
    if (!DecryptGame(encrypted_game, key, plain, diagnostic_stage))
        return std::nullopt;
    const auto date = ReadDateAt(plain, kGameDateOffset);
    SetDiagnosticStage(diagnostic_stage, date ? "validated" : "date-decode");
    return date;
}

YwbSaveDateNormalizationResult NormalizeYwbGettoSaveDateAt(
    const std::string& game_filename, const std::string& head_filename,
    const std::tm& emulated_local_time, std::vector<std::uint8_t>& updated_game_file,
    const char** diagnostic_stage) {
    updated_game_file.clear();
    SetDiagnosticStage(diagnostic_stage, "invalid-input");
    if (!IsValidDateTime(emulated_local_time) ||
        std::filesystem::path(game_filename).filename() != "game1.yw" ||
        std::filesystem::path(head_filename).filename() != "head.yw")
        return YwbSaveDateNormalizationResult::Invalid;

    Bytes encrypted_game;
    Bytes encrypted_head;
    if (!ReadFile(head_filename, encrypted_head)) {
        SetDiagnosticStage(diagnostic_stage, "head-read-or-size");
        return YwbSaveDateNormalizationResult::Invalid;
    }
    if (!ReadFile(game_filename, encrypted_game)) {
        SetDiagnosticStage(diagnostic_stage, "game-read-or-size");
        return YwbSaveDateNormalizationResult::Invalid;
    }

    Bytes head;
    if (!DecodeHead(encrypted_head, head, diagnostic_stage))
        return YwbSaveDateNormalizationResult::Invalid;
    SaveKey key{};
    if (!DeriveGettoKey(head, key, diagnostic_stage))
        return YwbSaveDateNormalizationResult::Invalid;

    Bytes plain;
    if (!DecryptGame(encrypted_game, key, plain, diagnostic_stage))
        return YwbSaveDateNormalizationResult::Invalid;
    const std::optional<std::tm> saved_date = ReadDateAt(plain, kGameDateOffset);
    if (!saved_date) {
        SetDiagnosticStage(diagnostic_stage, "date-decode");
        return YwbSaveDateNormalizationResult::Invalid;
    }
    if (DateTimeTuple(*saved_date) <= DateTimeTuple(emulated_local_time)) {
        SetDiagnosticStage(diagnostic_stage, "validated-unchanged");
        return YwbSaveDateNormalizationResult::Unchanged;
    }

    WriteDateAt(plain, kGameDateOffset, emulated_local_time);

    Bytes encrypted_updated;
    if (!EncryptGame(encrypted_game, key, plain, encrypted_updated)) {
        SetDiagnosticStage(diagnostic_stage, "reencryption");
        return YwbSaveDateNormalizationResult::Invalid;
    }

    Bytes verification;
    const char* verification_stage = nullptr;
    if (!DecryptGame(encrypted_updated, key, verification, &verification_stage)) {
        SetDiagnosticStage(diagnostic_stage, verification_stage == nullptr
                                                 ? "post-edit-verification"
                                                 : verification_stage);
        return YwbSaveDateNormalizationResult::Invalid;
    }
    if (!MatchesExceptCrc(plain, verification)) {
        SetDiagnosticStage(diagnostic_stage, "post-edit-content-verification");
        return YwbSaveDateNormalizationResult::Invalid;
    }
    const std::optional<std::tm> verified_date = ReadDateAt(verification, kGameDateOffset);
    if (!verified_date || DateTimeTuple(*verified_date) != DateTimeTuple(emulated_local_time)) {
        SetDiagnosticStage(diagnostic_stage, "post-edit-date-verification");
        return YwbSaveDateNormalizationResult::Invalid;
    }

    updated_game_file = std::move(encrypted_updated);
    SetDiagnosticStage(diagnostic_stage, "validated-corrected");
    return YwbSaveDateNormalizationResult::Updated;
}

YwbSaveDateNormalizationResult NormalizeYwbHeadDateAt(
    const std::string& head_filename, const std::tm& emulated_local_time,
    std::vector<std::uint8_t>& updated_head_file) {
    updated_head_file.clear();
    if (!IsValidDateTime(emulated_local_time) ||
        std::filesystem::path(head_filename).filename() != "head.yw")
        return YwbSaveDateNormalizationResult::Invalid;

    Bytes encrypted_head;
    if (!ReadFile(head_filename, encrypted_head))
        return YwbSaveDateNormalizationResult::Invalid;
    Bytes plain;
    if (!DecodeHead(encrypted_head, plain))
        return YwbSaveDateNormalizationResult::Invalid;
    const auto saved_date = ReadDateAt(plain, kHeadDateOffset);
    if (!saved_date)
        return YwbSaveDateNormalizationResult::Invalid;
    if (DateTimeTuple(*saved_date) <= DateTimeTuple(emulated_local_time))
        return YwbSaveDateNormalizationResult::Unchanged;

    WriteDateAt(plain, kHeadDateOffset, emulated_local_time);
    Bytes encrypted_updated;
    if (!EncryptHead(plain, encrypted_updated))
        return YwbSaveDateNormalizationResult::Invalid;
    Bytes verification;
    if (!DecodeHead(encrypted_updated, verification) || !MatchesExceptCrc(plain, verification))
        return YwbSaveDateNormalizationResult::Invalid;
    const auto verified_date = ReadDateAt(verification, kHeadDateOffset);
    if (!verified_date || DateTimeTuple(*verified_date) != DateTimeTuple(emulated_local_time))
        return YwbSaveDateNormalizationResult::Invalid;

    updated_head_file = std::move(encrypted_updated);
    return YwbSaveDateNormalizationResult::Updated;
}

bool IsYwbObservedSaveFile(const std::string& mount_point, const std::string& filename) {
    if (filename != "game1.yw" && filename != "head.yw")
        return false;
    const std::filesystem::path slot(mount_point);
    const auto slot_name = slot.filename();
    const auto parent = slot.parent_path();
    const bool inside_user_directory = parent.filename() == "user";
    const auto id = inside_user_directory ? parent.parent_path().filename() : parent.filename();
    const auto extdata_root =
        inside_user_directory ? parent.parent_path().parent_path() : parent.parent_path();
    // Moon Rabbit save data: Red Cat uses ca/da, White Dog uses cb/db.
    const bool getto_extdata = HexPathComponentEquals(id, "000016c6") &&
                               (slot_name == "yw-b_ca" || slot_name == "yw-b_da" ||
                                slot_name == "yw-b_cb" || slot_name == "yw-b_db") &&
                               extdata_root.filename() == "00000000" &&
                               extdata_root.parent_path().filename() == "extdata";
    // The base-game ExtData contains the matching b_a/b_b head only.
    const bool busters_extdata = filename == "head.yw" && HexPathComponentEquals(id, "000016c7") &&
                                 (slot_name == "yw-b_a" || slot_name == "yw-b_b") &&
                                 extdata_root.filename() == "00000000" &&
                                 extdata_root.parent_path().filename() == "extdata";
    // Base title saves: Red Cat is 0016c700 and White Dog is 0016c600.
    const bool busters_title_save =
        filename == "head.yw" && slot_name == "00000001" &&
        slot.parent_path().filename() == "data" &&
        (HexPathComponentEquals(slot.parent_path().parent_path().filename(), "0016c700") ||
         HexPathComponentEquals(slot.parent_path().parent_path().filename(), "0016c600")) &&
        slot.parent_path().parent_path().parent_path().filename() == "00040000" &&
        slot.parent_path().parent_path().parent_path().parent_path().filename() == "title";
    return getto_extdata || busters_extdata || busters_title_save;
}

bool IsYwbGettoGameSavePath(const std::string& filename) {
    const std::filesystem::path game(filename);
    if (game.filename() != "game1.yw")
        return false;

    const auto slot = game.parent_path();
    const auto user = slot.parent_path();
    if (user.filename() != "user")
        return false;

    const auto extdata_id = user.parent_path();
    const auto extdata_root = extdata_id.parent_path();
    const auto slot_name = slot.filename();
    return HexPathComponentEquals(extdata_id.filename(), "000016c6") &&
           (slot_name == "yw-b_ca" || slot_name == "yw-b_da" || slot_name == "yw-b_cb" ||
            slot_name == "yw-b_db") &&
           extdata_root.filename() == "00000000" &&
           extdata_root.parent_path().filename() == "extdata";
}

void YwbSaveCommitCoordinator::OnFileClose(
    const std::string& filename, bool has_write, bool write_succeeded, bool close_succeeded,
    const std::function<void(const std::string&)>& normalize) {
    const std::filesystem::path path(filename);
    const auto slot = path.parent_path().lexically_normal();
    const std::string slot_key = slot.string();
    const bool is_getto_game = IsYwbGettoGameSavePath(path.string());
    const auto game = slot / "game1.yw";
    const auto head = slot / "head.yw";
    const bool is_getto_head =
        path.filename() == "head.yw" && IsYwbGettoGameSavePath(game.string());

    bool normalize_current = false;
    bool normalize_game_after_head = false;
    {
        std::lock_guard lock(mutex);
        if (is_getto_game) {
            if (close_succeeded && has_write && write_succeeded) {
                pending_getto_game_slots.insert(slot_key);
                LOG_INFO(Service_FS, "YWB datefix deferred: slot={} waiting for head.yw close",
                         slot.filename().string());
            } else {
                pending_getto_game_slots.erase(slot_key);
            }
            return;
        }

        if (is_getto_head) {
            if (!close_succeeded || (has_write && !write_succeeded)) {
                pending_getto_game_slots.erase(slot_key);
                return;
            }
            if (!has_write)
                return;

            normalize_current = true;
            normalize_game_after_head = pending_getto_game_slots.erase(slot_key) != 0;
        } else {
            normalize_current = close_succeeded && has_write && write_succeeded;
        }
    }

    if (normalize_current)
        normalize(filename);
    if (normalize_game_after_head)
        normalize(game.string());
}

void NormalizeYwbSaveDateOnCommit(const std::string& filename) {
    const std::filesystem::path committed(filename);
    const std::string name = committed.filename().string();
    if (!IsYwbObservedSaveFile(committed.parent_path().string(), name))
        return;
    LOG_INFO(Service_FS, "YWB datefix normalizer entered: file={}", name);

    std::tm now{};
    if (!EmulatedLocalTime(now)) {
        LOG_WARNING(Service_FS, "YWB datefix skipped: guest clock unavailable");
        return;
    }

    if (name == "head.yw") {
        const auto date_before = ReadYwbHeadSaveDate(committed.string());
        Bytes updated_head;
        const auto head_result = NormalizeYwbHeadDateAt(committed.string(), now, updated_head);
        const bool replacement_attempted = head_result == YwbSaveDateNormalizationResult::Updated;
        bool replaced = false;
        if (replacement_attempted) {
            replaced = ReplaceSaveFile(committed, updated_head, "head.yw");
        }
        const auto date_after = replaced ? ReadYwbHeadSaveDate(committed.string()) : date_before;
        const char* replacement_status =
            !replacement_attempted ? "not-attempted" : (replaced ? "success" : "failed");
        LOG_INFO(Service_FS,
                 "YWB datefix API result: file=head.yw result={} date_before={} target_date={} "
                 "date_after={} replacement={}",
                 YwbNormalizationResultName(head_result), FormatYwbDate(date_before),
                 FormatYwbDate(now), FormatYwbDate(date_after), replacement_status);
        if (head_result == YwbSaveDateNormalizationResult::Invalid)
            LOG_WARNING(Service_FS, "YWB datefix skipped: head.yw validation failed");
        else if (replaced)
            LOG_INFO(Service_FS, "Normalized future timestamp in YWB head.yw");
        return;
    }

    // Only the Getto-gumi ExtData directories contain the AES-CCM game1.yw format handled here.
    const auto slot = committed.parent_path();
    if (!IsYwbGettoGameSavePath(committed.string())) {
        LOG_INFO(Service_FS,
                 "YWB game1 date API not called: ExtData path hierarchy mismatch file={}",
                 committed.string());
        return;
    }
    const auto game = committed.parent_path() / "game1.yw";
    const auto head = committed.parent_path() / "head.yw";
    const std::string slot_name = slot.filename().string();
    const bool same_slot_pair = game.parent_path() == head.parent_path();
    std::error_code game_error;
    std::error_code head_error;
    const bool game_exists = std::filesystem::is_regular_file(game, game_error) && !game_error;
    const bool head_exists = std::filesystem::is_regular_file(head, head_error) && !head_error;
    if (!game_exists || !head_exists) {
        LOG_INFO(Service_FS,
                 "YWB game1 date API not called: paired files unavailable game={} head={}",
                 game_exists, head_exists);
        return;
    }

    const char* read_stage = nullptr;
    const auto date_before = ReadYwbGettoSaveDate(game.string(), head.string(), &read_stage);
    Bytes updated;
    const char* normalization_stage = nullptr;
    const auto result = NormalizeYwbGettoSaveDateAt(game.string(), head.string(), now, updated,
                                                    &normalization_stage);
    const bool replacement_attempted = result == YwbSaveDateNormalizationResult::Updated;
    bool replaced = false;
    if (replacement_attempted) {
        replaced = ReplaceSaveFile(game, updated, "game1.yw");
    }
    const auto date_after =
        replaced ? ReadYwbGettoSaveDate(game.string(), head.string()) : date_before;
    const char* replacement_status =
        !replacement_attempted ? "not-attempted" : (replaced ? "success" : "failed");
    LOG_INFO(Service_FS,
             "YWB datefix API result: slot={} pair={} read_api_date={} read_stage={} result={} "
             "normalization_stage={} date_before={} target_date={} date_after={} replacement={}",
             slot_name, same_slot_pair ? "same-slot" : "mismatched", FormatYwbDate(date_before),
             read_stage == nullptr ? "unknown" : read_stage, YwbNormalizationResultName(result),
             normalization_stage == nullptr ? "unknown" : normalization_stage,
             FormatYwbDate(date_before), FormatYwbDate(now), FormatYwbDate(date_after),
             replacement_status);
    if (result == YwbSaveDateNormalizationResult::Invalid) {
        LOG_WARNING(Service_FS, "YWB datefix skipped: save authentication or validation failed");
        return;
    }
    if (replaced)
        LOG_INFO(Service_FS, "Normalized future timestamp in YWB game1.yw");
}

} // namespace FileSys
