// Copyright 2026 Azahar Emulator Project contributors
// Licensed under GPLv2 or any later version.
// Synthetic fixtures only. No player save data is stored in this test.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <boost/crc.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/ccm.h>
#include <cryptopp/filters.h>
#include "common/file_util.h"
#include "core/file_sys/disk_archive.h"
#include "core/file_sys/ywb_save_date.h"

namespace FileSys {
namespace {

using Bytes = std::vector<std::uint8_t>;
using Key = std::array<std::uint8_t, 16>;
namespace fs = std::filesystem;

constexpr std::size_t kHeadSize = 0x3B00;
constexpr std::size_t kGameSize = 0x4000;
constexpr std::size_t kSavedDateOffset = 0x4C;
constexpr std::size_t kHeadDateOffset = 0x3A20;
constexpr std::uint32_t kKeySeed = 0x13579BDF;

std::uint32_t GetU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void PutU32(std::uint8_t* p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

class TestXorshift {
public:
    explicit TestXorshift(std::uint32_t seed) {
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

void ApplyTestCipher(Bytes& data) {
    std::array<std::uint16_t, 256> primes{};
    std::array<std::uint8_t, 256> table{};
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

    TestXorshift rng(GetU32(data.data() + data.size() - 4));
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

void SetCrc(Bytes& data) {
    boost::crc_32_type crc;
    crc.process_bytes(data.data(), data.size() - 8);
    PutU32(data.data() + data.size() - 8, crc.checksum());
}

Bytes MakeHead() {
    Bytes head(kHeadSize, 0);
    head[0] = 0xFE;
    head[1] = 0xFF;
    PutU32(head.data() + 0x0C, 0x2468ACE0);
    PutU32(head.data() + 0x10, 1);
    PutU32(head.data() + 0x39E0, 0x4000); // Set the Getto-gumi key inversion flag.
    PutU32(head.data() + 0x3A08, 5);      // Number of key-cipher table shuffles.
    PutU32(head.data() + 0x3A10, 0x2468ACE0 ^ kKeySeed);
    head[kHeadDateOffset] = 0xEB;
    head[kHeadDateOffset + 1] = 0x07;
    head[kHeadDateOffset + 2] = 3;
    head[kHeadDateOffset + 3] = 21;
    head[kHeadDateOffset + 4] = 14;
    head[kHeadDateOffset + 5] = 54;
    head[kHeadDateOffset + 6] = 20;
    PutU32(head.data() + head.size() - 4, 0xA55A1234);
    ApplyTestCipher(head);
    SetCrc(head);
    return head;
}

Key DeriveTestKey() {
    TestXorshift rng(~kKeySeed);
    for (unsigned i = 0; i < 5; ++i)
        rng.Next(0x10000);
    Key key{};
    for (auto& byte : key)
        byte = static_cast<std::uint8_t>(rng.Next(0x100));
    return key;
}

Bytes MakeGame() {
    Bytes plain(kGameSize - 0x20, 0);
    plain[0] = 0xFE;
    plain[1] = 0xFF;
    PutU32(plain.data() + 4, (static_cast<std::uint32_t>(plain.size() - 8) << 8) | 0xF1);
    plain[8] = 0xFE;
    plain[9] = 0xFF;
    PutU32(plain.data() + 12, (0x100U << 8) | 0xF2);
    plain[16] = 2;
    plain[kSavedDateOffset] = 0xEB; // 2027-03-21 14:54:20
    plain[kSavedDateOffset + 1] = 0x07;
    plain[kSavedDateOffset + 2] = 3;
    plain[kSavedDateOffset + 3] = 21;
    plain[kSavedDateOffset + 4] = 14;
    plain[kSavedDateOffset + 5] = 54;
    plain[kSavedDateOffset + 6] = 20;
    PutU32(plain.data() + plain.size() - 4, 0xBEEFCAFE);
    ApplyTestCipher(plain);
    SetCrc(plain);

    std::array<std::uint8_t, 12> nonce{};
    for (unsigned i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::uint8_t>(i + 1);
    Bytes encrypted(plain.size() + 16);
    CryptoPP::CCM<CryptoPP::AES, 16>::Encryption encryptor;
    const Key key = DeriveTestKey();
    encryptor.SetKeyWithIV(key.data(), key.size(), nonce.data(), nonce.size());
    encryptor.SpecifyDataLengths(0, plain.size(), 0);
    CryptoPP::ArraySource source(
        plain.data(), plain.size(), true,
        new CryptoPP::AuthenticatedEncryptionFilter(
            encryptor, new CryptoPP::ArraySink(encrypted.data(), encrypted.size())));

    Bytes output(0x20);
    std::copy(nonce.begin(), nonce.end(), output.begin());
    std::copy(encrypted.end() - 16, encrypted.end(), output.begin() + 0x10);
    output.insert(output.end(), encrypted.begin(), encrypted.end() - 16);
    return output;
}

std::tm FixedNow(int year, int month, int day) {
    std::tm now{};
    now.tm_year = year - 1900;
    now.tm_mon = month - 1;
    now.tm_mday = day;
    now.tm_hour = 12;
    return now;
}

void WriteFile(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(static_cast<bool>(output));
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

Bytes ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(static_cast<bool>(input));
    return Bytes(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
}

struct TempSave {
    fs::path root = fs::temp_directory_path() /
                    ("ywb-date-reader-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path game = root / "extdata" / "00000000" / "000016c6" / "user" / "yw-b_ca" / "game1.yw";
    fs::path head = game.parent_path() / "head.yw";

    TempSave() {
        fs::create_directories(game.parent_path());
        WriteFile(head, MakeHead());
        WriteFile(game, MakeGame());
    }
    ~TempSave() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

} // namespace

TEST_CASE("YWB Getto-gumi date reader authenticates without modifying saves",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    const Bytes original_game = ReadFile(fixture.game);
    const Bytes original_head = ReadFile(fixture.head);

    const char* diagnostic_stage = nullptr;
    const auto date =
        ReadYwbGettoSaveDate(fixture.game.string(), fixture.head.string(), &diagnostic_stage);
    REQUIRE(date.has_value());
    CHECK(std::string(diagnostic_stage) == "validated");
    CHECK(date->tm_year + 1900 == 2027);
    CHECK(date->tm_mon + 1 == 3);
    CHECK(date->tm_mday == 21);
    CHECK(date->tm_hour == 14);
    CHECK(date->tm_min == 54);
    CHECK(date->tm_sec == 20);
    CHECK(ReadFile(fixture.game) == original_game);
    CHECK(ReadFile(fixture.head) == original_head);

    Bytes damaged = original_game;
    damaged[0x10] ^= 0x80;
    WriteFile(fixture.game, damaged);
    diagnostic_stage = nullptr;
    CHECK_FALSE(
        ReadYwbGettoSaveDate(fixture.game.string(), fixture.head.string(), &diagnostic_stage)
            .has_value());
    CHECK(std::string(diagnostic_stage) == "aes-ccm-authentication");
    CHECK(ReadFile(fixture.game) == damaged);
}

TEST_CASE("YWB future date normalization re-encrypts and authenticates output",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    const Bytes original_game = ReadFile(fixture.game);
    const Bytes original_head = ReadFile(fixture.head);
    Bytes normalized;

    CHECK(NormalizeYwbGettoSaveDateAt(fixture.game.string(), fixture.head.string(),
                                      FixedNow(2026, 10, 22),
                                      normalized) == YwbSaveDateNormalizationResult::Updated);
    CHECK(normalized != original_game);
    CHECK(ReadFile(fixture.game) == original_game);
    CHECK(ReadFile(fixture.head) == original_head);

    const fs::path copy_dir = fixture.root / "corrected" / "yw-b_ca";
    fs::create_directories(copy_dir);
    const fs::path corrected_game = copy_dir / "game1.yw";
    const fs::path copied_head = copy_dir / "head.yw";
    WriteFile(corrected_game, normalized);
    WriteFile(copied_head, original_head);
    const auto corrected_date = ReadYwbGettoSaveDate(corrected_game.string(), copied_head.string());
    REQUIRE(corrected_date.has_value());
    CHECK(corrected_date->tm_year + 1900 == 2026);
    CHECK(corrected_date->tm_mon + 1 == 10);
    CHECK(corrected_date->tm_mday == 22);
}

TEST_CASE("YWB non-future date normalization leaves input unchanged", "[core][file_sys][ywb]") {
    TempSave fixture;
    const Bytes original_game = ReadFile(fixture.game);
    Bytes normalized;

    CHECK(NormalizeYwbGettoSaveDateAt(fixture.game.string(), fixture.head.string(),
                                      FixedNow(2027, 3, 22),
                                      normalized) == YwbSaveDateNormalizationResult::Unchanged);
    CHECK(normalized.empty());
    CHECK(ReadFile(fixture.game) == original_game);
}

TEST_CASE("YWB date normalization rejects invalid authentication without output",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    Bytes damaged = ReadFile(fixture.game);
    damaged[0x10] ^= 0x80;
    WriteFile(fixture.game, damaged);
    Bytes normalized;
    const char* diagnostic_stage = nullptr;

    CHECK(NormalizeYwbGettoSaveDateAt(fixture.game.string(), fixture.head.string(),
                                      FixedNow(2026, 10, 22), normalized, &diagnostic_stage) ==
          YwbSaveDateNormalizationResult::Invalid);
    CHECK(std::string(diagnostic_stage) == "aes-ccm-authentication");
    CHECK(normalized.empty());
    CHECK(ReadFile(fixture.game) == damaged);
}

TEST_CASE("YWB opt-in inspection compares APIs on a supplied completed save pair",
          "[core][file_sys][ywb][manual]") {
    const char* game_filename = std::getenv("YWB_DIAGNOSTIC_GAME1");
    const char* head_filename = std::getenv("YWB_DIAGNOSTIC_HEAD");
    if (game_filename == nullptr || head_filename == nullptr) {
        SUCCEED("Set YWB_DIAGNOSTIC_GAME1 and YWB_DIAGNOSTIC_HEAD to inspect a pair read-only");
        return;
    }

    const Bytes original_game = ReadFile(game_filename);
    const char* read_stage = nullptr;
    const auto date = ReadYwbGettoSaveDate(game_filename, head_filename, &read_stage);
    REQUIRE(date.has_value());
    CHECK(std::string(read_stage) == "validated");
    Bytes corrected;
    const char* normalization_stage = nullptr;
    CHECK(NormalizeYwbGettoSaveDateAt(game_filename, head_filename, FixedNow(2026, 10, 22),
                                      corrected, &normalization_stage) ==
          YwbSaveDateNormalizationResult::Updated);
    CHECK(std::string(normalization_stage) == "validated-corrected");
    CHECK_FALSE(corrected.empty());
    CHECK(ReadFile(game_filename) == original_game);
}

TEST_CASE("YWB automatic commit tracking is limited to Busters save locations",
          "[core][file_sys][ywb]") {
    const fs::path getto_extdata = fs::path("profile/user/extdata/00000000/000016c6/user");
    for (const char* slot : {"yw-b_ca", "yw-b_da", "yw-b_cb", "yw-b_db"}) {
        const auto mount = (getto_extdata / slot).string();
        CHECK(IsYwbObservedSaveFile(mount, "game1.yw"));
        CHECK(IsYwbObservedSaveFile(mount, "head.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile(mount, "bind.yw"));
    }
    const fs::path uppercase_getto_extdata =
        fs::path("profile/user/extdata/00000000/000016C6/user");
    for (const char* slot : {"yw-b_ca", "yw-b_da", "yw-b_cb", "yw-b_db"}) {
        const auto mount = (uppercase_getto_extdata / slot).string();
        CHECK(IsYwbObservedSaveFile(mount, "game1.yw"));
        CHECK(IsYwbObservedSaveFile(mount, "head.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile(mount, "bind.yw"));
    }
    const fs::path busters_extdata = fs::path("profile/user/extdata/00000000/000016c7/user");
    for (const char* slot : {"yw-b_a", "yw-b_b"}) {
        CHECK(IsYwbObservedSaveFile((busters_extdata / slot).string(), "head.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile((busters_extdata / slot).string(), "game1.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile((busters_extdata / slot).string(), "bind.yw"));
    }
    const fs::path uppercase_busters_extdata =
        fs::path("profile/user/extdata/00000000/000016C7/user");
    for (const char* slot : {"yw-b_a", "yw-b_b"}) {
        CHECK(IsYwbObservedSaveFile((uppercase_busters_extdata / slot).string(), "head.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile((uppercase_busters_extdata / slot).string(), "game1.yw"));
        CHECK_FALSE(IsYwbObservedSaveFile((uppercase_busters_extdata / slot).string(), "bind.yw"));
    }
    CHECK_FALSE(IsYwbObservedSaveFile("profile/user/extdata/00000000/000016c7/yw-b_ca", "head.yw"));
    CHECK(IsYwbObservedSaveFile("profile/user/title/00040000/0016c700/data/00000001", "head.yw"));
    CHECK(IsYwbObservedSaveFile("profile/user/title/00040000/0016c600/data/00000001", "head.yw"));
    CHECK(IsYwbObservedSaveFile("profile/user/title/00040000/0016C700/data/00000001", "head.yw"));
    CHECK(IsYwbObservedSaveFile("profile/user/title/00040000/0016C600/data/00000001", "head.yw"));
    CHECK_FALSE(
        IsYwbObservedSaveFile("profile/user/title/00040000/0016c700/data/00000001", "game1.yw"));
    CHECK_FALSE(
        IsYwbObservedSaveFile("profile/user/title/00040000/0016C800/data/00000001", "head.yw"));
    CHECK_FALSE(
        IsYwbObservedSaveFile("profile/user/extdata/00000000/000016C8/user/yw-b_ca", "game1.yw"));
    CHECK_FALSE(IsYwbObservedSaveFile("profile/other/00040000/0016c700/data/00000001", "head.yw"));
}

TEST_CASE("YWB game1 save path resolves the exact Getto-gumi ExtData ID", "[core][file_sys][ywb]") {
    for (const char* extdata_id : {"000016c6", "000016C6"}) {
        for (const char* slot : {"yw-b_ca", "yw-b_da", "yw-b_cb", "yw-b_db"}) {
            const auto path =
                fs::path("profile/user/extdata/00000000") / extdata_id / "user" / slot / "game1.yw";
            CHECK(IsYwbGettoGameSavePath(path.string()));
        }
    }
    CHECK_FALSE(
        IsYwbGettoGameSavePath("profile/user/extdata/00000000/000016c7/user/yw-b_ca/game1.yw"));
    CHECK_FALSE(
        IsYwbGettoGameSavePath("profile/user/extdata/00000000/000016c8/user/yw-b_ca/game1.yw"));
    CHECK_FALSE(
        IsYwbGettoGameSavePath("profile/user/title/00040000/0016c700/data/00000001/game1.yw"));
    CHECK_FALSE(IsYwbGettoGameSavePath("profile/elsewhere/000016c6/user/yw-b_ca/game1.yw"));
    CHECK_FALSE(IsYwbGettoGameSavePath("profile/user/extdata/00000000/000016c6/yw-b_ca/game1.yw"));
}

TEST_CASE("YWB DiskFile commit hook requires an observed path and complete write",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    const Bytes payload{0x10, 0x20, 0x30, 0x40};
    std::vector<std::string> hooked_paths;
    const auto hook = [&hooked_paths](const std::string& path) { hooked_paths.push_back(path); };
    FileSys::Mode write_mode{};
    write_mode.read_flag.Assign(1);
    write_mode.write_flag.Assign(1);

    SECTION("ExtData game1.yw reaches the close hook after a successful write") {
        const auto full_path = fixture.game.string();
        REQUIRE(IsYwbObservedSaveFile(fixture.game.parent_path().string(), "game1.yw"));
        FileUtil::IOFile file(full_path, "r+b");
        REQUIRE(file.IsOpen());
        FileSys::DiskFile disk_file(std::move(file), write_mode, {}, {}, full_path, hook);
        auto result = disk_file.Write(0, payload.size(), true, false, payload.data());
        REQUIRE(result.Succeeded());
        CHECK(*result == payload.size());
        CHECK(disk_file.Close());
        CHECK(hooked_paths.empty());

        const auto head_path = fixture.head.string();
        FileUtil::IOFile head_file(head_path, "r+b");
        REQUIRE(head_file.IsOpen());
        FileSys::DiskFile head_disk_file(std::move(head_file), write_mode, {}, {}, head_path, hook);
        const auto head_write =
            head_disk_file.Write(0, payload.size(), true, false, payload.data());
        REQUIRE(head_write.Succeeded());
        CHECK(*head_write == payload.size());
        CHECK(head_disk_file.Close());
        REQUIRE(hooked_paths.size() == 2);
        CHECK(hooked_paths[0] == head_path);
        CHECK(hooked_paths[1] == full_path);
    }

    SECTION("non-target ExtData files do not receive a close hook") {
        const auto full_path = (fixture.game.parent_path() / "bind.yw").string();
        CHECK_FALSE(IsYwbObservedSaveFile(fixture.game.parent_path().string(), "bind.yw"));
        FileUtil::IOFile file(full_path, "w+b");
        REQUIRE(file.IsOpen());
        FileSys::DiskFile disk_file(std::move(file), write_mode, {}, {}, {}, hook);
        const auto result = disk_file.Write(0, payload.size(), true, false, payload.data());
        REQUIRE(result.Succeeded());
        CHECK(disk_file.Close());
        CHECK(hooked_paths.empty());
    }

    SECTION("read-only close does not invoke the hook") {
        FileSys::Mode read_mode{};
        read_mode.read_flag.Assign(1);
        const auto full_path = fixture.game.string();
        FileUtil::IOFile file(full_path, "rb");
        REQUIRE(file.IsOpen());
        FileSys::DiskFile disk_file(std::move(file), read_mode, {}, {}, full_path, hook);
        std::array<std::uint8_t, 1> buffer{};
        auto read = disk_file.Read(0, buffer.size(), buffer.data());
        REQUIRE(read.Succeeded());
        CHECK(disk_file.Close());
        CHECK(hooked_paths.empty());
    }

    SECTION("failed write does not invoke the hook") {
        const auto full_path = fixture.game.string();
        FileUtil::IOFile file(full_path, "rb");
        REQUIRE(file.IsOpen());
        FileSys::DiskFile disk_file(std::move(file), write_mode, {}, {}, full_path, hook);
        const auto result = disk_file.Write(0, payload.size(), true, false, payload.data());
        REQUIRE(result.Succeeded());
        CHECK(*result == 0);
        CHECK(disk_file.Close());
        CHECK(hooked_paths.empty());
    }
}

TEST_CASE("YWB game1 correction waits for the matching head close", "[core][file_sys][ywb]") {
    TempSave fixture;
    YwbSaveCommitCoordinator coordinator;
    std::vector<std::string> normalized;
    const auto normalize = [&normalized](const std::string& path) { normalized.push_back(path); };

    coordinator.OnFileClose(fixture.game.string(), true, true, true, normalize);
    CHECK(normalized.empty());

    coordinator.OnFileClose(fixture.head.string(), true, true, true, normalize);
    REQUIRE(normalized.size() == 2);
    CHECK(normalized[0] == fixture.head.string());
    CHECK(normalized[1] == fixture.game.string());
}

TEST_CASE("YWB pending correction is slot-local and requires successful writes",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    const auto other_game =
        fixture.root / "extdata" / "00000000" / "000016c6" / "user" / "yw-b_da" / "game1.yw";
    const auto other_head = other_game.parent_path() / "head.yw";
    YwbSaveCommitCoordinator coordinator;
    std::vector<std::string> normalized;
    const auto normalize = [&normalized](const std::string& path) { normalized.push_back(path); };

    coordinator.OnFileClose(fixture.game.string(), true, true, true, normalize);
    coordinator.OnFileClose(other_head.string(), true, true, true, normalize);
    REQUIRE(normalized.size() == 1);
    CHECK(normalized[0] == other_head.string());

    coordinator.OnFileClose(fixture.head.string(), true, true, true, normalize);
    REQUIRE(normalized.size() == 3);
    CHECK(normalized[1] == fixture.head.string());
    CHECK(normalized[2] == fixture.game.string());

    const auto failed_game = other_game.parent_path().parent_path() / "yw-b_db" / "game1.yw";
    const auto failed_head = failed_game.parent_path() / "head.yw";
    coordinator.OnFileClose(failed_game.string(), true, false, true, normalize);
    const auto before_failed_head = normalized.size();
    coordinator.OnFileClose(failed_head.string(), true, true, true, normalize);
    CHECK(normalized.size() == before_failed_head + 1);
    CHECK(normalized.back() == failed_head.string());

    const auto close_failed_game = other_game.parent_path().parent_path() / "yw-b_cb" / "game1.yw";
    const auto close_failed_head = close_failed_game.parent_path() / "head.yw";
    coordinator.OnFileClose(close_failed_game.string(), true, true, false, normalize);
    const auto before_close_failed_head = normalized.size();
    coordinator.OnFileClose(close_failed_head.string(), true, true, true, normalize);
    CHECK(normalized.size() == before_close_failed_head + 1);
    CHECK(normalized.back() == close_failed_head.string());
}

TEST_CASE("YWB no-write head close does not run or consume deferred correction",
          "[core][file_sys][ywb]") {
    TempSave fixture;
    YwbSaveCommitCoordinator coordinator;
    std::vector<std::string> normalized;
    const auto normalize = [&normalized](const std::string& path) { normalized.push_back(path); };

    coordinator.OnFileClose(fixture.game.string(), true, true, true, normalize);
    coordinator.OnFileClose(fixture.head.string(), false, false, true, normalize);
    CHECK(normalized.empty());

    coordinator.OnFileClose(fixture.head.string(), true, true, true, normalize);
    REQUIRE(normalized.size() == 2);
    CHECK(normalized[0] == fixture.head.string());
    CHECK(normalized[1] == fixture.game.string());
}

TEST_CASE("YWB head date normalization re-encrypts XOR and CRC data", "[core][file_sys][ywb]") {
    TempSave fixture;
    const Bytes original_head = ReadFile(fixture.head);
    Bytes normalized;

    CHECK(NormalizeYwbHeadDateAt(fixture.head.string(), FixedNow(2026, 10, 22), normalized) ==
          YwbSaveDateNormalizationResult::Updated);
    CHECK(normalized != original_head);
    CHECK(ReadFile(fixture.head) == original_head);

    const fs::path copied_head = fixture.root / "corrected" / "yw-b_ca" / "head.yw";
    fs::create_directories(copied_head.parent_path());
    WriteFile(copied_head, normalized);
    const auto corrected_game_date =
        ReadYwbGettoSaveDate(fixture.game.string(), copied_head.string());
    REQUIRE(corrected_game_date.has_value());
    const auto date =
        NormalizeYwbHeadDateAt(copied_head.string(), FixedNow(2026, 10, 22), normalized);
    CHECK(date == YwbSaveDateNormalizationResult::Unchanged);

    Bytes damaged_head = original_head;
    damaged_head[0x10] ^= 0x80;
    WriteFile(fixture.head, damaged_head);
    CHECK(NormalizeYwbHeadDateAt(fixture.head.string(), FixedNow(2026, 10, 22), normalized) ==
          YwbSaveDateNormalizationResult::Invalid);
    CHECK(normalized.empty());
    CHECK(ReadFile(fixture.head) == damaged_head);
}

} // namespace FileSys
