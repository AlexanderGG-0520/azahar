// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include <cryptopp/base64.h>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <fmt/format.h>
#include "common/archives.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/archive_systemsavedata.h"
#include "core/file_sys/directory_backend.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/file_backend.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/result.h"
#include "core/hle/service/cecd/cecd.h"
#include "core/hle/service/cecd/cecd_ndm.h"
#include "core/hle/service/cecd/cecd_s.h"
#include "core/hle/service/cecd/cecd_u.h"
#include "core/hle/service/cfg/cfg.h"
#include "network/network.h"
#include "network/room_member.h"

SERVICE_CONSTRUCT_IMPL(Service::CECD::Module)
SERIALIZE_EXPORT_IMPL(Service::CECD::Module)
SERIALIZE_EXPORT_IMPL(Service::CECD::Module::SessionData)

namespace Service::CECD {

template <class Archive>
void Module::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
    ar & cecd_system_save_data_archive;
    ar & cecinfo_event;
    ar & cecinfosys_event;
    ar & change_state_event;
}
SERIALIZE_IMPL(Module)

using CecDataPathType = Module::CecDataPathType;
using CecOpenMode = Module::CecOpenMode;
using CecSystemInfoType = Module::CecSystemInfoType;

constexpr std::size_t MaxRoomStreetPassMessageSize = 0x20000;
constexpr std::size_t StreetPassRoomHeaderSize = sizeof(u32);
constexpr std::size_t StreetPassHmacSize = 0x20;

struct RoomCecTimestamp {
    u32_le year;
    u8 month;
    u8 day;
    u8 week_day;
    u8 hour;
    u8 minute;
    u8 second;
    u16_le millisecond;
};
static_assert(sizeof(RoomCecTimestamp) == 0x0C);

RoomCecTimestamp GetCurrentRoomCecTimestamp() {
    RoomCecTimestamp timestamp{};
    const auto now = std::chrono::system_clock::now();
    const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
    const std::tm* utc_time = std::gmtime(&current_time);
    if (!utc_time) {
        return timestamp;
    }

    timestamp.year = static_cast<u32>(utc_time->tm_year + 1900);
    timestamp.month = static_cast<u8>(utc_time->tm_mon + 1);
    timestamp.day = static_cast<u8>(utc_time->tm_mday);
    timestamp.week_day = static_cast<u8>(utc_time->tm_wday);
    timestamp.hour = static_cast<u8>(utc_time->tm_hour);
    timestamp.minute = static_cast<u8>(utc_time->tm_min);
    timestamp.second = static_cast<u8>(utc_time->tm_sec);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    timestamp.millisecond = static_cast<u16>(milliseconds % 1000);
    return timestamp;
}

std::array<u8, 8> MakeRoomStreetPassMessageId(const std::array<u8, 8>& original_message_id,
                                               const std::array<u8, 6>& sender_mac,
                                               std::span<const u8> original_message) {
    // Original CECD IDs may be reused when players update the registered team or scan
    // a special QR code. Include the message contents so a new payload from the same
    // sender does not get discarded as an already received StreetPass encounter.
    // Identical retries still deduplicate, and cloned NANDs remain distinct by MAC.
    u64 hash = 14695981039346656037ULL;
    constexpr u64 fnv_prime = 1099511628211ULL;

    const auto mix = [&hash](const u8 byte) {
        hash ^= byte;
        hash *= fnv_prime;
    };

    for (const u8 byte : original_message_id) {
        mix(byte);
    }
    for (const u8 byte : sender_mac) {
        mix(byte);
    }
    for (const u8 byte : original_message) {
        mix(byte);
    }

    std::array<u8, 8> derived_id{};
    for (std::size_t i = 0; i < derived_id.size(); ++i) {
        derived_id[i] = static_cast<u8>(hash >> (i * 8));
    }
    return derived_id;
}

void Module::BindRoomMember(const std::shared_ptr<Network::RoomMember>& member) {
    if (!member) {
        return;
    }

    if (const auto current = room_member.lock(); current && current.get() == member.get()) {
        return;
    }

    room_member = member;
    const std::weak_ptr<Module> weak_self = weak_from_this();
    member->BindOnWifiPacketReceived([weak_self](const Network::WifiPacket& packet) {
        const auto self = weak_self.lock();
        if (!self) {
            return;
        }

        if (packet.type == Network::WifiPacket::PacketType::StreetPass) {
            self->QueueStreetPassPacket(packet);
        } else if (packet.type == Network::WifiPacket::PacketType::StreetPassRequest &&
                   packet.data.empty()) {
            const auto room = self->room_member.lock();
            if (!room || !room->IsConnected() ||
                packet.transmitter_address == room->GetMacAddress() ||
                packet.transmitter_address == Network::BroadcastMac) {
                return;
            }

            // CECD's NAND archive is owned by the emulation thread. Reading cached files
            // here would be unsafe and could reply with data from before QR/party updates.
            {
                std::lock_guard lock(self->streetpass_mutex);
                const auto& pending = self->pending_streetpass_requests;
                if (std::find(pending.begin(), pending.end(), packet.transmitter_address) ==
                    pending.end()) {
                    self->pending_streetpass_requests.push_back(packet.transmitter_address);
                }
            }
            LOG_INFO(Service_CECD, "Queued UDS-triggered StreetPass exchange request");
        }
    });

    // Cache already-registered CECD outboxes without sending. Joining a room alone must not
    // generate a StreetPass encounter; a completed UDS association triggers the unicast reply.
    BroadcastAllOutboxMessages();
    {
        std::lock_guard lock(streetpass_mutex);
        LOG_INFO(Service_CECD, "Initialized StreetPass OutBox cache with {} message(s)",
                 cached_streetpass_messages.size());
    }
}

void Module::QueueStreetPassPacket(const Network::WifiPacket& packet) {
    if (packet.data.size() < StreetPassRoomHeaderSize + sizeof(CecMessageHeader) ||
        packet.data.size() > StreetPassRoomHeaderSize + MaxRoomStreetPassMessageSize) {
        LOG_WARNING(Service_CECD, "Ignoring StreetPass room packet with invalid size {} bytes",
                    packet.data.size());
        return;
    }

    const u32 program_id = static_cast<u32>(packet.data[0]) |
                           (static_cast<u32>(packet.data[1]) << 8) |
                           (static_cast<u32>(packet.data[2]) << 16) |
                           (static_cast<u32>(packet.data[3]) << 24);

    std::array<u8, 6> sender_mac{};
    std::copy(packet.transmitter_address.begin(), packet.transmitter_address.end(),
              sender_mac.begin());

    std::vector<u8> message(packet.data.begin() + StreetPassRoomHeaderSize, packet.data.end());
    {
        std::lock_guard lock(streetpass_mutex);
        pending_streetpass_messages.push_back({program_id, sender_mac, std::move(message)});
    }

    LOG_INFO(Service_CECD,
             "Queued StreetPass room message for program {:#010x} from "
              "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
              program_id, sender_mac[0], sender_mac[1], sender_mac[2], sender_mac[3],
              sender_mac[4], sender_mac[5]);

    // Room callbacks run on the network thread. As with NWM::UDS, take the HLE lock before
    // signaling kernel events from that thread. The actual NAND/CECD writes stay deferred until
    // the emulation thread enters CECD again.
    std::scoped_lock hle_lock(system.Kernel().GetHLELock());
    cecinfo_event->Signal();
    cecinfosys_event->Signal();
    change_state_event->Signal();
}

void Module::ProcessPendingStreetPassPackets() {
    if (room_member.expired()) {
        if (const auto member = Network::GetRoomMember().lock()) {
            BindRoomMember(member);
        }
    }

    std::vector<PendingStreetPassMessage> pending;
    std::vector<std::array<u8, 6>> requests;
    {
        std::lock_guard lock(streetpass_mutex);
        pending.swap(pending_streetpass_messages);
        requests.swap(pending_streetpass_requests);
    }

    if (!pending.empty()) {
        LOG_INFO(Service_CECD, "Processing {} pending StreetPass room message(s) on emulation "
                               "thread", pending.size());
    }
    for (auto& packet : pending) {
        if (!InjectStreetPassMessage(packet.program_id, packet.sender_mac,
                                     std::move(packet.message))) {
            LOG_DEBUG(Service_CECD,
                      "StreetPass room message for program {:#010x} was not installed "
                      "(possibly duplicate or invalid)", packet.program_id);
        }
    }

    if (!requests.empty()) {
        // The send payload may have changed since CECD was initialized, especially after
        // a special QR scan and an update at the Wayfarer Manor manager.
        BroadcastAllOutboxMessages();
        if (const auto member = room_member.lock(); member && member->IsConnected()) {
            for (const auto& peer_mac : requests) {
                LOG_INFO(Service_CECD,
                         "Answering UDS-triggered StreetPass exchange request with refreshed "
                         "OutBox cache");
                SendCachedStreetPassMessages(member, peer_mac);
            }
        }
    }
}

bool Module::InjectStreetPassMessage(const u32 program_id,
                                     const std::array<u8, 6>& sender_mac,
                                     std::vector<u8> message) {
    if (message.size() < sizeof(CecMessageHeader) ||
        message.size() > MaxRoomStreetPassMessageSize) {
        return false;
    }

    CecMessageHeader message_header{};
    std::memcpy(&message_header, message.data(), sizeof(message_header));
    const u64 payload_end =
        static_cast<u64>(message_header.header_size) + message_header.body_size;
    const u64 expected_message_size = payload_end + StreetPassHmacSize;
    if (message_header.magic != 0x6060 || message_header.header_size < sizeof(CecMessageHeader) ||
        message_header.title_id != program_id ||
        static_cast<u64>(message_header.message_size) != expected_message_size ||
        expected_message_size != message.size()) {
        LOG_WARNING(Service_CECD,
                    "Dropping malformed StreetPass message for program {:#010x}", program_id);
        return false;
    }

    FileSys::Mode info_mode;
    info_mode.read_flag.Assign(1);
    info_mode.write_flag.Assign(1);

    const FileSys::Path inbox_info_path(
        GetCecDataPathTypeAsString(CecDataPathType::InboxInfo, program_id).data());
    auto info_result = cecd_system_save_data_archive->OpenFile(inbox_info_path, info_mode);
    if (info_result.Failed()) {
        LOG_WARNING(Service_CECD,
                    "Dropping StreetPass message: no Inbox mailbox registered for CECD "
                    "program {:#010x}", program_id);
        return false;
    }

    auto info_file = std::move(info_result).Unwrap();
    const u32 info_size = static_cast<u32>(info_file->GetSize());
    if (info_size < sizeof(CecBoxInfoHeader)) {
        info_file->Close();
        return false;
    }

    std::vector<u8> info_buffer(info_size);
    if (info_file->Read(0, info_size, info_buffer.data()).Failed()) {
        info_file->Close();
        return false;
    }

    CecBoxInfoHeader info_header{};
    std::memcpy(&info_header, info_buffer.data(), sizeof(info_header));
    if (info_header.magic != 0x6262) {
        info_file->Close();
        return false;
    }

    std::array<u8, 8> original_message_id{};
    std::memcpy(original_message_id.data(), message_header.message_id.data(),
                original_message_id.size());
    const std::array<u8, 8> message_id =
        MakeRoomStreetPassMessageId(original_message_id, sender_mac, message);
    std::memcpy(message_header.message_id.data(), message_id.data(), message_id.size());

    const FileSys::Path inbox_message_path(
        GetCecDataPathTypeAsString(CecDataPathType::InboxMsg, program_id, message_id).data());

    FileSys::Mode read_mode;
    read_mode.read_flag.Assign(1);
    auto existing_result = cecd_system_save_data_archive->OpenFile(inbox_message_path, read_mode);
    if (existing_result.Succeeded()) {
        auto existing_file = std::move(existing_result).Unwrap();
        existing_file->Close();
        info_file->Close();
        LOG_INFO(Service_CECD,
                 "Skipping duplicate StreetPass room message for program {:#010x}", program_id);
        return false;
    }

    const u32 existing_count =
        static_cast<u32>((info_buffer.size() - sizeof(CecBoxInfoHeader)) / sizeof(CecMessageHeader));
    info_header.message_num = existing_count;

    const u32 stored_message_size = static_cast<u32>(message_header.message_size);
    if ((info_header.max_message_num != 0 &&
         info_header.message_num >= info_header.max_message_num) ||
        (info_header.max_message_size != 0 &&
         stored_message_size > info_header.max_message_size) ||
        (info_header.max_box_size != 0 &&
         static_cast<u64>(info_header.box_size) + stored_message_size > info_header.max_box_size)) {
        LOG_WARNING(Service_CECD,
                    "StreetPass inbox is full for program {:#010x}; dropping message", program_id);
        info_file->Close();
        return false;
    }

    const FileSys::Path mbox_info_path(
        GetCecDataPathTypeAsString(CecDataPathType::MboxInfo, program_id).data());
    auto mbox_result = cecd_system_save_data_archive->OpenFile(mbox_info_path, info_mode);
    if (mbox_result.Failed()) {
        LOG_WARNING(Service_CECD,
                    "StreetPass mailbox metadata is missing for program {:#010x}", program_id);
        info_file->Close();
        return false;
    }

    auto mbox_file = std::move(mbox_result).Unwrap();
    const u32 mbox_size = static_cast<u32>(mbox_file->GetSize());
    if (mbox_size < sizeof(CecMBoxInfoHeader)) {
        mbox_file->Close();
        info_file->Close();
        return false;
    }

    std::vector<u8> mbox_buffer(mbox_size);
    if (mbox_file->Read(0, mbox_size, mbox_buffer.data()).Failed()) {
        mbox_file->Close();
        info_file->Close();
        return false;
    }

    CecMBoxInfoHeader mbox_header{};
    std::memcpy(&mbox_header, mbox_buffer.data(), sizeof(mbox_header));
    if (mbox_header.magic != 0x6363) {
        mbox_file->Close();
        info_file->Close();
        return false;
    }

    const RoomCecTimestamp received_timestamp = GetCurrentRoomCecTimestamp();
    message_header.is_unopen = 1;
    message_header.is_new = 1;
    std::memcpy(&message_header.recv_time, &received_timestamp, sizeof(received_timestamp));
    std::memcpy(message.data(), &message_header, sizeof(message_header));

    // Match CECD/NetPass receive semantics by recalculating the title's message HMAC.
    // The HMAC key is title-specific and shared across consoles; this is protocol fidelity rather
    // than a workaround for per-console keys.
    using namespace CryptoPP;
    HMAC<SHA256> hmac(mbox_header.hmac_key.data(), mbox_header.hmac_key.size());
    hmac.CalculateDigest(message.data() + payload_end,
                         message.data() + message_header.header_size,
                         message_header.body_size);

    FileSys::Mode message_mode;
    message_mode.write_flag.Assign(1);
    message_mode.create_flag.Assign(1);
    auto message_result =
        cecd_system_save_data_archive->OpenFile(inbox_message_path, message_mode);
    if (message_result.Failed()) {
        mbox_file->Close();
        info_file->Close();
        return false;
    }

    auto message_file = std::move(message_result).Unwrap();
    message_file->SetSize(message.size());
    const auto write_result =
        message_file->Write(0, message.size(), true, false, message.data());
    message_file->Close();
    if (write_result.Failed()) {
        mbox_file->Close();
        info_file->Close();
        return false;
    }

    const std::size_t header_offset =
        sizeof(CecBoxInfoHeader) + static_cast<std::size_t>(info_header.message_num) *
                                       sizeof(CecMessageHeader);
    info_buffer.resize(header_offset + sizeof(CecMessageHeader));
    std::memcpy(info_buffer.data() + header_offset, &message_header, sizeof(message_header));

    info_header.message_num++;
    info_header.box_size += stored_message_size;
    info_header.box_info_size = static_cast<u32>(info_buffer.size());
    std::memcpy(info_buffer.data(), &info_header, sizeof(info_header));

    info_file->SetSize(info_buffer.size());
    const auto info_write_result =
        info_file->Write(0, info_buffer.size(), true, false, info_buffer.data());
    info_file->Close();
    if (info_write_result.Failed()) {
        mbox_file->Close();
        return false;
    }

    // Match the receive-side mailbox state used by real CECD clients such as NetPass.
    std::memcpy(&mbox_header.last_received, &received_timestamp, sizeof(received_timestamp));
    mbox_header.flag_unread = 1;
    mbox_header.flag_new = 1;
    std::memcpy(mbox_buffer.data(), &mbox_header, sizeof(mbox_header));

    const auto mbox_write_result =
        mbox_file->Write(0, mbox_buffer.size(), true, false, mbox_buffer.data());
    mbox_file->Close();
    if (mbox_write_result.Failed()) {
        LOG_WARNING(Service_CECD,
                    "Failed to update StreetPass mailbox metadata for program {:#010x}",
                    program_id);
    }

    cecinfo_event->Signal();
    cecinfosys_event->Signal();
    change_state_event->Signal();

    LOG_INFO(Service_CECD,
             "Received StreetPass room message for program {:#010x}, {} bytes", program_id,
             message.size());
    return true;
}

void Module::CacheStreetPassMessage(const u32 program_id,
                                    const std::vector<u8>& message) {
    if (message.size() < sizeof(CecMessageHeader)) {
        return;
    }

    CecMessageHeader header{};
    std::memcpy(&header, message.data(), sizeof(header));

    CachedStreetPassMessage cached{};
    cached.program_id = program_id;
    std::copy(header.message_id.begin(), header.message_id.end(), cached.message_id.begin());
    cached.message = message;

    std::lock_guard lock(streetpass_mutex);
    const auto existing =
        std::find_if(cached_streetpass_messages.begin(), cached_streetpass_messages.end(),
                     [&cached](const CachedStreetPassMessage& entry) {
                         return entry.program_id == cached.program_id &&
                                entry.message_id == cached.message_id;
                     });
    if (existing != cached_streetpass_messages.end()) {
        *existing = std::move(cached);
    } else {
        cached_streetpass_messages.push_back(std::move(cached));
    }
}

void Module::SendStreetPassMessage(const std::shared_ptr<Network::RoomMember>& member,
                                   const u32 program_id, const std::vector<u8>& message,
                                   const std::array<u8, 6>& destination) {
    if (!member || message.size() < sizeof(CecMessageHeader) ||
        message.size() > MaxRoomStreetPassMessageSize) {
        return;
    }

    Network::WifiPacket packet{};
    packet.type = Network::WifiPacket::PacketType::StreetPass;
    packet.channel = 0;
    packet.transmitter_address = member->GetMacAddress();
    packet.destination_address = destination;
    packet.data.reserve(StreetPassRoomHeaderSize + message.size());
    packet.data.push_back(static_cast<u8>(program_id));
    packet.data.push_back(static_cast<u8>(program_id >> 8));
    packet.data.push_back(static_cast<u8>(program_id >> 16));
    packet.data.push_back(static_cast<u8>(program_id >> 24));
    packet.data.insert(packet.data.end(), message.begin(), message.end());

    member->SendWifiPacket(packet);
    LOG_INFO(Service_CECD,
             "Sent StreetPass room message for program {:#010x}, {} bytes", program_id,
              message.size());
}

void Module::SendCachedStreetPassMessages(
    const std::shared_ptr<Network::RoomMember>& member, const std::array<u8, 6>& destination) {
    if (!member) {
        return;
    }

    const auto state = member->GetState();
    if (state != Network::RoomMember::State::Joined &&
        state != Network::RoomMember::State::Moderator) {
        return;
    }

    std::vector<CachedStreetPassMessage> cached;
    {
        std::lock_guard lock(streetpass_mutex);
        cached = cached_streetpass_messages;
    }

    LOG_INFO(Service_CECD, "Replying with {} cached StreetPass message(s) to UDS peer",
             cached.size());
    for (const auto& entry : cached) {
        SendStreetPassMessage(member, entry.program_id, entry.message, destination);
    }
}

void Module::BroadcastStreetPassMessage(const u32 program_id,
                                        const std::vector<u8>& message) {
    if (message.size() < sizeof(CecMessageHeader) ||
        message.size() > MaxRoomStreetPassMessageSize) {
        return;
    }

    // Store the OutBox tag for the next real UDS connection. A write while merely present
    // in a multiplayer room must not create an encounter.
    CacheStreetPassMessage(program_id, message);

    if (room_member.expired()) {
        if (const auto member = Network::GetRoomMember().lock()) {
            BindRoomMember(member);
        }
    }
}

void Module::BroadcastOutboxMessages(const u32 program_id) {
    const FileSys::Path outbox_path(
        GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, program_id).data());
    auto dir_result = cecd_system_save_data_archive->OpenDirectory(outbox_path);
    if (dir_result.Failed()) {
        return;
    }

    constexpr u32 max_entries = 101;
    auto outbox_dir = std::move(dir_result).Unwrap();
    std::vector<FileSys::Entry> entries(max_entries);
    const u32 entry_count = outbox_dir->Read(max_entries, entries.data());
    outbox_dir->Close();

    for (u32 i = 0; i < entry_count; ++i) {
        const std::string filename = Common::UTF16ToUTF8(std::u16string(entries[i].filename));
        if (entries[i].is_directory || filename == "BoxInfo_____" || filename == "OBIndex_____") {
            continue;
        }

        const FileSys::Path message_path(
            (GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, program_id) + "/" + filename)
                .data());
        FileSys::Mode mode;
        mode.read_flag.Assign(1);
        auto message_result = cecd_system_save_data_archive->OpenFile(message_path, mode);
        if (message_result.Failed()) {
            continue;
        }

        auto message_file = std::move(message_result).Unwrap();
        const u32 message_size = static_cast<u32>(message_file->GetSize());
        if (message_size < sizeof(CecMessageHeader) ||
            message_size > MaxRoomStreetPassMessageSize) {
            message_file->Close();
            continue;
        }

        std::vector<u8> message(message_size);
        const auto read_result = message_file->Read(0, message_size, message.data());
        message_file->Close();
        if (read_result.Failed()) {
            continue;
        }

        BroadcastStreetPassMessage(program_id, message);
    }
}

void Module::BroadcastAllOutboxMessages() {
    const FileSys::Path root_path(GetCecDataPathTypeAsString(CecDataPathType::RootDir, 0).data());
    auto dir_result = cecd_system_save_data_archive->OpenDirectory(root_path);
    if (dir_result.Failed()) {
        return;
    }

    constexpr u32 max_entries = 25;
    auto root_dir = std::move(dir_result).Unwrap();
    std::vector<FileSys::Entry> entries(max_entries);
    const u32 entry_count = root_dir->Read(max_entries, entries.data());
    root_dir->Close();

    for (u32 i = 0; i < entry_count; ++i) {
        if (!entries[i].is_directory) {
            continue;
        }

        const std::string name = Common::UTF16ToUTF8(std::u16string(entries[i].filename));
        if (name.size() != 8) {
            continue;
        }

        u32 program_id{};
        const auto [end, error] =
            std::from_chars(name.data(), name.data() + name.size(), program_id, 16);
        if (error != std::errc{} || end != name.data() + name.size()) {
            continue;
        }

        BroadcastOutboxMessages(program_id);
    }
}

void Module::Interface::Open(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const CecDataPathType path_type = rp.PopEnum<CecDataPathType>();
    CecOpenMode open_mode;
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.read_flag.Assign(1);
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    SessionData* session_data = GetSessionData(ctx.Session());
    session_data->ncch_program_id = ncch_program_id;
    session_data->open_mode.raw = open_mode.raw;
    session_data->data_path_type = path_type;
    session_data->path = path;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    switch (path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir: {
        auto dir_result = cecd->cecd_system_save_data_archive->OpenDirectory(path);
        if (dir_result.Failed()) {
            if (open_mode.create) {
                cecd->cecd_system_save_data_archive->CreateDirectory(path);
                rb.Push(ResultSuccess);
            } else {
                LOG_DEBUG(Service_CECD, "Failed to open directory: {}", path.AsString());
                rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                               ErrorLevel::Status));
            }
            rb.Push<u32>(0); // Zero entries
        } else {
            constexpr u32 max_entries = 32; // reasonable value, just over max boxes 24
            auto directory = std::move(dir_result).Unwrap();

            // Actual reading into vector seems to be required for entry count
            std::vector<FileSys::Entry> entries(max_entries);
            const u32 entry_count = directory->Read(max_entries, entries.data());

            LOG_DEBUG(Service_CECD, "Number of entries found: {}", entry_count);

            rb.Push(ResultSuccess);
            rb.Push<u32>(entry_count); // Entry count
            directory->Close();
        }
        break;
    }
    default: { // If not directory, then it is a file
        auto file_result = cecd->cecd_system_save_data_archive->OpenFile(path, mode);
        if (file_result.Failed()) {
            LOG_DEBUG(Service_CECD, "Failed to open file: {}", path.AsString());
            rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
            rb.Push<u32>(0); // No file size
        } else {
            session_data->file = std::move(file_result).Unwrap();
            rb.Push(ResultSuccess);
            rb.Push<u32>(static_cast<u32>(session_data->file->GetSize())); // Return file size
        }

        if (path_type == CecDataPathType::MboxProgramId) {
            std::vector<u8> program_id(8);
            u64_le le_program_id = cecd->system.Kernel().GetCurrentProcess()->codeset->program_id;
            std::memcpy(program_id.data(), &le_program_id, sizeof(u64));
            session_data->file->Write(0, sizeof(u64), true, false, program_id.data());
            session_data->file->Close();
        }
    }
    }

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, path_type, path.AsString(), open_mode.raw, open_mode.unknown.Value(),
              open_mode.read.Value(), open_mode.write.Value(), open_mode.create.Value(),
              open_mode.check.Value());
}

void Module::Interface::Read(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 write_buffer_size = rp.Pop<u32>();
    auto& write_buffer = rp.PopMappedBuffer();

    SessionData* session_data = GetSessionData(ctx.Session());
    LOG_DEBUG(Service_CECD,
              "SessionData: ncch_program_id={:#010x}, data_path_type={:#04x}, "
              "path={}, open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              session_data->ncch_program_id, session_data->data_path_type,
              session_data->path.AsString(), session_data->open_mode.raw,
              session_data->open_mode.unknown.Value(), session_data->open_mode.read.Value(),
              session_data->open_mode.write.Value(), session_data->open_mode.create.Value(),
              session_data->open_mode.check.Value());

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    switch (session_data->data_path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        rb.Push(Result(ErrorDescription::NotAuthorized, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        rb.Push<u32>(0); // No bytes read
        break;
    default: // If not directory, then it is a file
        std::vector<u8> buffer(write_buffer_size);
        const u32 bytes_read = static_cast<u32>(
            session_data->file->Read(0, write_buffer_size, buffer.data()).Unwrap());

        write_buffer.Write(buffer.data(), 0, write_buffer_size);
        session_data->file->Close();

        rb.Push(ResultSuccess);
        rb.Push<u32>(bytes_read);
    }
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(Service_CECD, "called, write_buffer_size={:#x}, path={}", write_buffer_size,
              session_data->path.AsString());
}

void Module::Interface::ReadMessage(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const bool is_outbox = rp.Pop<bool>();
    const u32 message_id_size = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    auto& message_id_buffer = rp.PopMappedBuffer();
    auto& write_buffer = rp.PopMappedBuffer();

    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path =
        cecd->GetCecDataPathTypeAsString(is_outbox ? CecDataPathType::OutboxMsg
                                                   : CecDataPathType::InboxMsg,
                                         ncch_program_id, id_buffer)
            .data();

    auto message_result = cecd->cecd_system_save_data_archive->OpenFile(message_path, mode);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 4);
    if (message_result.Succeeded()) {
        auto message = std::move(message_result).Unwrap();
        std::vector<u8> buffer(buffer_size);

        const u32 bytes_read =
            static_cast<u32>(message->Read(0, buffer_size, buffer.data()).Unwrap());
        write_buffer.Write(buffer.data(), 0, buffer_size);
        message->Close();

        CecMessageHeader msg_header;
        std::memcpy(&msg_header, buffer.data(), sizeof(CecMessageHeader));

        LOG_DEBUG(Service_CECD,
                  "magic={:#06x}, message_size={:#010x}, header_size={:#010x}, "
                  "body_size={:#010x}, title_id={:#010x}, title_id_2={:#010x}, "
                  "batch_id={:#010x}",
                  msg_header.magic, msg_header.message_size, msg_header.header_size,
                  msg_header.body_size, msg_header.title_id, msg_header.title_id2,
                  msg_header.batch_id);
        LOG_DEBUG(Service_CECD,
                  "unknown_id={:#010x}, version={:#010x}, flag={:#04x}, "
                  "send_method={:#04x}, is_unopen={:#04x}, is_new={:#04x}, "
                  "sender_id={:#018x}, sender_id2={:#018x}, send_count={:#04x}, "
                  "forward_count={:#04x}, user_data={:#06x}, ",
                  msg_header.unknown_id, msg_header.version, msg_header.flag,
                  msg_header.send_method, msg_header.is_unopen, msg_header.is_new,
                  msg_header.sender_id, msg_header.sender_id2, msg_header.send_count,
                  msg_header.forward_count, msg_header.user_data);

        rb.Push(ResultSuccess);
        rb.Push<u32>(bytes_read);
    } else {
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        rb.Push<u32>(0); // zero bytes read
    }
    rb.PushMappedBuffer(message_id_buffer);
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::ReadMessageWithHMAC(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const bool is_outbox = rp.Pop<bool>();
    const u32 message_id_size = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    auto& message_id_buffer = rp.PopMappedBuffer();
    auto& hmac_key_buffer = rp.PopMappedBuffer();
    auto& write_buffer = rp.PopMappedBuffer();

    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path =
        cecd->GetCecDataPathTypeAsString(is_outbox ? CecDataPathType::OutboxMsg
                                                   : CecDataPathType::InboxMsg,
                                         ncch_program_id, id_buffer)
            .data();

    auto message_result = cecd->cecd_system_save_data_archive->OpenFile(message_path, mode);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 6);
    if (message_result.Succeeded()) {
        auto message = std::move(message_result).Unwrap();
        std::vector<u8> buffer(buffer_size);

        const u32 bytes_read =
            static_cast<u32>(message->Read(0, buffer_size, buffer.data()).Unwrap());
        write_buffer.Write(buffer.data(), 0, buffer_size);
        message->Close();

        CecMessageHeader msg_header;
        std::memcpy(&msg_header, buffer.data(), sizeof(CecMessageHeader));

        LOG_DEBUG(Service_CECD,
                  "magic={:#06x}, message_size={:#010x}, header_size={:#010x}, "
                  "body_size={:#010x}, title_id={:#010x}, title_id_2={:#010x}, "
                  "batch_id={:#010x}",
                  msg_header.magic, msg_header.message_size, msg_header.header_size,
                  msg_header.body_size, msg_header.title_id, msg_header.title_id2,
                  msg_header.batch_id);
        LOG_DEBUG(Service_CECD,
                  "unknown_id={:#010x}, version={:#010x}, flag={:#04x}, "
                  "send_method={:#04x}, is_unopen={:#04x}, is_new={:#04x}, "
                  "sender_id={:#018x}, sender_id2={:#018x}, send_count={:#04x}, "
                  "forward_count={:#04x}, user_data={:#06x}, ",
                  msg_header.unknown_id, msg_header.version, msg_header.flag,
                  msg_header.send_method, msg_header.is_unopen, msg_header.is_new,
                  msg_header.sender_id, msg_header.sender_id2, msg_header.send_count,
                  msg_header.forward_count, msg_header.user_data);

        std::vector<u8> hmac_digest(0x20);
        std::memcpy(hmac_digest.data(),
                    buffer.data() + msg_header.header_size + msg_header.body_size, 0x20);

        std::vector<u8> message_body(msg_header.body_size);
        std::memcpy(message_body.data(), buffer.data() + msg_header.header_size,
                    msg_header.body_size);

        using namespace CryptoPP;
        SecByteBlock key(0x20);
        hmac_key_buffer.Read(key.data(), 0, key.size());

        HMAC<SHA256> hmac(key, key.size());

        const bool verify_hmac =
            hmac.VerifyDigest(hmac_digest.data(), message_body.data(), message_body.size());

        if (verify_hmac)
            LOG_DEBUG(Service_CECD, "Verification succeeded");
        else
            LOG_DEBUG(Service_CECD, "Verification failed");

        rb.Push(ResultSuccess);
        rb.Push<u32>(bytes_read);
    } else {
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        rb.Push<u32>(0); // zero bytes read
    }

    rb.PushMappedBuffer(message_id_buffer);
    rb.PushMappedBuffer(hmac_key_buffer);
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::Write(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 read_buffer_size = rp.Pop<u32>();
    auto& read_buffer = rp.PopMappedBuffer();

    SessionData* session_data = GetSessionData(ctx.Session());
    LOG_DEBUG(Service_CECD,
              "SessionData: ncch_program_id={:#010x}, data_path_type={:#04x}, "
              "path={}, open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              session_data->ncch_program_id, session_data->data_path_type,
              session_data->path.AsString(), session_data->open_mode.raw,
              session_data->open_mode.unknown.Value(), session_data->open_mode.read.Value(),
              session_data->open_mode.write.Value(), session_data->open_mode.create.Value(),
              session_data->open_mode.check.Value());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    switch (session_data->data_path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        rb.Push(Result(ErrorDescription::NotAuthorized, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        break;
    default: // If not directory, then it is a file
        std::vector<u8> buffer(read_buffer_size);
        read_buffer.Read(buffer.data(), 0, read_buffer_size);

        if (session_data->file->GetSize() != read_buffer_size) {
            session_data->file->SetSize(read_buffer_size);
        }

        if (session_data->open_mode.check) {
            cecd->CheckAndUpdateFile(session_data->data_path_type, session_data->ncch_program_id,
                                     buffer);
        }

        [[maybe_unused]] const u32 bytes_written = static_cast<u32>(
            session_data->file->Write(0, buffer.size(), true, false, buffer.data()).Unwrap());
        session_data->file->Close();

        if (session_data->data_path_type == CecDataPathType::OutboxMsg) {
            cecd->BroadcastStreetPassMessage(session_data->ncch_program_id, buffer);
        }

        rb.Push(ResultSuccess);
    }
    rb.PushMappedBuffer(read_buffer);

    LOG_DEBUG(Service_CECD, "called, read_buffer_size={:#x}", read_buffer_size);
}

void Module::Interface::WriteMessage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const bool is_outbox = rp.Pop<bool>();
    const u32 message_id_size = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    auto& read_buffer = rp.PopMappedBuffer();
    auto& message_id_buffer = rp.PopMappedBuffer();

    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path =
        cecd->GetCecDataPathTypeAsString(is_outbox ? CecDataPathType::OutboxMsg
                                                   : CecDataPathType::InboxMsg,
                                         ncch_program_id, id_buffer)
            .data();

    auto message_result = cecd->cecd_system_save_data_archive->OpenFile(message_path, mode);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    if (message_result.Succeeded()) {
        auto message = std::move(message_result).Unwrap();

        std::vector<u8> buffer(buffer_size);
        read_buffer.Read(buffer.data(), 0, buffer_size);

        CecMessageHeader msg_header;
        std::memcpy(&msg_header, buffer.data(), sizeof(CecMessageHeader));

        LOG_DEBUG(Service_CECD,
                  "magic={:#06x}, message_size={:#010x}, header_size={:#010x}, "
                  "body_size={:#010x}, title_id={:#010x}, title_id_2={:#010x}, "
                  "batch_id={:#010x}",
                  msg_header.magic, msg_header.message_size, msg_header.header_size,
                  msg_header.body_size, msg_header.title_id, msg_header.title_id2,
                  msg_header.batch_id);
        LOG_DEBUG(Service_CECD,
                  "unknown_id={:#010x}, version={:#010x}, flag={:#04x}, "
                  "send_method={:#04x}, is_unopen={:#04x}, is_new={:#04x}, "
                  "sender_id={:#018x}, sender_id2={:#018x}, send_count={:#04x}, "
                  "forward_count={:#04x}, user_data={:#06x}, ",
                  msg_header.unknown_id, msg_header.version, msg_header.flag,
                  msg_header.send_method, msg_header.is_unopen, msg_header.is_new,
                  msg_header.sender_id, msg_header.sender_id2, msg_header.send_count,
                  msg_header.forward_count, msg_header.user_data);

        [[maybe_unused]] const u32 bytes_written =
            static_cast<u32>(message->Write(0, buffer_size, true, false, buffer.data()).Unwrap());
        message->Close();

        if (is_outbox) {
            cecd->BroadcastStreetPassMessage(ncch_program_id, buffer);
        }

        rb.Push(ResultSuccess);
    } else {
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
    }

    rb.PushMappedBuffer(read_buffer);
    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::WriteMessageWithHMAC(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const bool is_outbox = rp.Pop<bool>();
    const u32 message_id_size = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    auto& read_buffer = rp.PopMappedBuffer();
    auto& hmac_key_buffer = rp.PopMappedBuffer();
    auto& message_id_buffer = rp.PopMappedBuffer();

    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    std::vector<u8> id_buffer(message_id_size);
    message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

    FileSys::Path message_path =
        cecd->GetCecDataPathTypeAsString(is_outbox ? CecDataPathType::OutboxMsg
                                                   : CecDataPathType::InboxMsg,
                                         ncch_program_id, id_buffer)
            .data();

    auto message_result = cecd->cecd_system_save_data_archive->OpenFile(message_path, mode);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 6);
    if (message_result.Succeeded()) {
        auto message = std::move(message_result).Unwrap();

        std::vector<u8> buffer(buffer_size);
        read_buffer.Read(buffer.data(), 0, buffer_size);

        CecMessageHeader msg_header;
        std::memcpy(&msg_header, buffer.data(), sizeof(CecMessageHeader));

        LOG_DEBUG(Service_CECD,
                  "magic={:#06x}, message_size={:#010x}, header_size={:#010x}, "
                  "body_size={:#010x}, title_id={:#010x}, title_id_2={:#010x}, "
                  "batch_id={:#010x}",
                  msg_header.magic, msg_header.message_size, msg_header.header_size,
                  msg_header.body_size, msg_header.title_id, msg_header.title_id2,
                  msg_header.batch_id);
        LOG_DEBUG(Service_CECD,
                  "unknown_id={:#010x}, version={:#010x}, flag={:#04x}, "
                  "send_method={:#04x}, is_unopen={:#04x}, is_new={:#04x}, "
                  "sender_id={:#018x}, sender_id2={:#018x}, send_count={:#04x}, "
                  "forward_count={:#04x}, user_data={:#06x}, ",
                  msg_header.unknown_id, msg_header.version, msg_header.flag,
                  msg_header.send_method, msg_header.is_unopen, msg_header.is_new,
                  msg_header.sender_id, msg_header.sender_id2, msg_header.send_count,
                  msg_header.forward_count, msg_header.user_data);

        const u32 hmac_offset = msg_header.header_size + msg_header.body_size;
        const u32 hmac_size = 0x20;

        std::vector<u8> hmac_digest(hmac_size);
        std::vector<u8> message_body(msg_header.body_size);
        std::memcpy(message_body.data(), buffer.data() + msg_header.header_size,
                    msg_header.body_size);

        using namespace CryptoPP;
        SecByteBlock key(hmac_size);
        hmac_key_buffer.Read(key.data(), 0, hmac_size);

        HMAC<SHA256> hmac(key, hmac_size);
        hmac.CalculateDigest(hmac_digest.data(), message_body.data(), msg_header.body_size);
        std::memcpy(buffer.data() + hmac_offset, hmac_digest.data(), hmac_size);

        [[maybe_unused]] const u32 bytes_written =
            static_cast<u32>(message->Write(0, buffer_size, true, false, buffer.data()).Unwrap());
        message->Close();

        if (is_outbox) {
            cecd->BroadcastStreetPassMessage(ncch_program_id, buffer);
        }

        rb.Push(ResultSuccess);
    } else {
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
    }

    rb.PushMappedBuffer(read_buffer);
    rb.PushMappedBuffer(hmac_key_buffer);
    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(
        Service_CECD,
        "called, ncch_program_id={:#010x}, is_outbox={}, message_id_size={:#x}, buffer_size={:#x}",
        ncch_program_id, is_outbox, message_id_size, buffer_size);
}

void Module::Interface::Delete(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const CecDataPathType path_type = rp.PopEnum<CecDataPathType>();
    const bool is_outbox = rp.Pop<bool>();
    const u32 message_id_size = rp.Pop<u32>();
    auto& message_id_buffer = rp.PopMappedBuffer();

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.write_flag.Assign(1);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    switch (path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        rb.Push(cecd->cecd_system_save_data_archive->DeleteDirectoryRecursively(path));
        break;
    default: // If not directory, then it is a file
        if (message_id_size == 0) {
            rb.Push(cecd->cecd_system_save_data_archive->DeleteFile(path));
        } else {
            std::vector<u8> id_buffer(message_id_size);
            message_id_buffer.Read(id_buffer.data(), 0, message_id_size);

            FileSys::Path message_path =
                cecd->GetCecDataPathTypeAsString(is_outbox ? CecDataPathType::OutboxMsg
                                                           : CecDataPathType::InboxMsg,
                                                 ncch_program_id, id_buffer)
                    .data();
            rb.Push(cecd->cecd_system_save_data_archive->DeleteFile(message_path));
        }
    }

    rb.PushMappedBuffer(message_id_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, "
              "is_outbox={}, message_id_size={:#x}",
              ncch_program_id, path_type, path.AsString(), is_outbox, message_id_size);
}

void Module::Interface::SetData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 ncch_program_id = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    const u32 option = rp.Pop<u32>();
    auto& read_buffer = rp.PopMappedBuffer();

    if (option == 2 && buffer_size > 0) { // update obindex?
        FileSys::Path path(
            cecd->GetCecDataPathTypeAsString(CecDataPathType::OutboxIndex, ncch_program_id).data());
        FileSys::Mode mode;
        mode.write_flag.Assign(1);
        mode.create_flag.Assign(1);

        auto file_result = cecd->cecd_system_save_data_archive->OpenFile(path, mode);
        if (file_result.Succeeded()) {
            auto file = std::move(file_result).Unwrap();
            std::vector<u8> buffer(buffer_size);
            read_buffer.Read(buffer.data(), 0, buffer_size);

            cecd->CheckAndUpdateFile(CecDataPathType::OutboxIndex, ncch_program_id, buffer);

            file->Write(0, buffer.size(), true, false, buffer.data());
            file->Close();
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(read_buffer);

    LOG_DEBUG(Service_CECD, "called, ncch_program_id={:#010x}, buffer_size={:#x}, option={:#x}",
              ncch_program_id, buffer_size, option);
}

void Module::Interface::ReadData(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const auto dest_buffer_size = rp.Pop<u32>();
    const auto info_type = rp.PopEnum<CecSystemInfoType>();
    const auto param_buffer_size = rp.Pop<u32>();
    auto& param_buffer = rp.PopMappedBuffer();
    auto& dest_buffer = rp.PopMappedBuffer();

    // TODO: Other CecSystemInfoTypes
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    std::vector<u8> buffer;
    switch (info_type) {
    case CecSystemInfoType::EulaVersion: {
        const auto cfg = Service::CFG::GetModule(cecd->system);
        const auto version = cfg->GetEULAVersion();
        buffer = {version.minor, version.major};
        break;
    }
    case CecSystemInfoType::Eula:
        buffer = {true}; // Eula agreed
        break;
    case CecSystemInfoType::ParentControl:
        buffer = {false}; // No parent control
        break;
    default:
        LOG_ERROR(Service_CECD, "Unknown system info type={:#x}", info_type);
        buffer = {};
    }
    dest_buffer.Write(buffer.data(), 0,
                      std::min(static_cast<std::size_t>(dest_buffer_size), buffer.size()));

    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(param_buffer);
    rb.PushMappedBuffer(dest_buffer);

    LOG_DEBUG(Service_CECD,
              "called, dest_buffer_size={:#x}, info_type={:#x}, param_buffer_size={:#x}",
              dest_buffer_size, info_type, param_buffer_size);
}

void Module::Interface::Start(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();

    IPC::RequestParser rp(ctx);
    const CecCommand command = rp.PopEnum<CecCommand>();

    if (command == CecCommand::StartScan || command == CecCommand::Rescan) {
        cecd->BroadcastAllOutboxMessages();
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CECD, "(STUBBED) called, command={}", cecd->GetCecCommandAsString(command));
}

void Module::Interface::Stop(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const CecCommand command = rp.PopEnum<CecCommand>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_CECD, "(STUBBED) called, command={}", cecd->GetCecCommandAsString(command));
}

void Module::Interface::GetCecInfoBuffer(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 possible_info_type = rp.Pop<u32>();
    const u32 buffer_size = rp.Pop<u32>();
    auto& buffer = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(buffer);

    LOG_DEBUG(Service_CECD, "called, possible_info_type={}, buffer_size={}", possible_info_type,
              buffer_size);
}

void Module::Interface::GetCecdState(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.PushEnum(CecdState::NdmStatusIdle);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::GetCecInfoEventHandle(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(cecd->cecinfo_event);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::GetChangeStateEventHandle(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(cecd->change_state_event);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

void Module::Interface::OpenAndWrite(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 buffer_size = rp.Pop<u32>();
    const u32 ncch_program_id = rp.Pop<u32>();
    const CecDataPathType path_type = rp.PopEnum<CecDataPathType>();
    CecOpenMode open_mode;
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();
    auto& read_buffer = rp.PopMappedBuffer();

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.write_flag.Assign(1);
    mode.create_flag.Assign(1);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    switch (path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        rb.Push(Result(ErrorDescription::NotAuthorized, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        break;
    default: // If not directory, then it is a file
        auto file_result = cecd->cecd_system_save_data_archive->OpenFile(path, mode);
        if (file_result.Succeeded()) {
            auto file = std::move(file_result).Unwrap();

            std::vector<u8> buffer(buffer_size);
            read_buffer.Read(buffer.data(), 0, buffer_size);

            if (file->GetSize() != buffer_size) {
                file->SetSize(buffer_size);
            }

            if (open_mode.check) {
                cecd->CheckAndUpdateFile(path_type, ncch_program_id, buffer);
            }

            [[maybe_unused]] const u32 bytes_written = static_cast<u32>(
                file->Write(0, buffer.size(), true, false, buffer.data()).Unwrap());
            file->Close();

            if (path_type == CecDataPathType::OutboxMsg) {
                cecd->BroadcastStreetPassMessage(ncch_program_id, buffer);
            }

            rb.Push(ResultSuccess);
        } else {
            rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
        }
    }
    rb.PushMappedBuffer(read_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, buffer_size={:#x} "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, path_type, path.AsString(), buffer_size, open_mode.raw,
              open_mode.unknown.Value(), open_mode.read.Value(), open_mode.write.Value(),
              open_mode.create.Value(), open_mode.check.Value());
}

void Module::Interface::OpenAndRead(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    const u32 buffer_size = rp.Pop<u32>();
    const u32 ncch_program_id = rp.Pop<u32>();
    const CecDataPathType path_type = rp.PopEnum<CecDataPathType>();
    CecOpenMode open_mode;
    open_mode.raw = rp.Pop<u32>();
    rp.PopPID();
    auto& write_buffer = rp.PopMappedBuffer();

    FileSys::Path path(cecd->GetCecDataPathTypeAsString(path_type, ncch_program_id).data());
    FileSys::Mode mode;
    mode.read_flag.Assign(1);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    switch (path_type) {
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        rb.Push(Result(ErrorDescription::NotAuthorized, ErrorModule::CEC, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        rb.Push<u32>(0); // No entries read
        break;
    default: // If not directory, then it is a file
        auto file_result = cecd->cecd_system_save_data_archive->OpenFile(path, mode);
        if (file_result.Succeeded()) {
            auto file = std::move(file_result).Unwrap();
            std::vector<u8> buffer(buffer_size);

            const u32 bytes_read =
                static_cast<u32>(file->Read(0, buffer_size, buffer.data()).Unwrap());
            write_buffer.Write(buffer.data(), 0, buffer_size);
            file->Close();

            rb.Push(ResultSuccess);
            rb.Push<u32>(bytes_read);
        } else {
            rb.Push(Result(ErrorDescription::NoData, ErrorModule::CEC, ErrorSummary::NotFound,
                           ErrorLevel::Status));
            rb.Push<u32>(0); // No bytes read
        }
    }
    rb.PushMappedBuffer(write_buffer);

    LOG_DEBUG(Service_CECD,
              "called, ncch_program_id={:#010x}, path_type={:#04x}, path={}, buffer_size={:#x} "
              "open_mode: raw={:#x}, unknown={}, read={}, write={}, create={}, check={}",
              ncch_program_id, path_type, path.AsString(), buffer_size, open_mode.raw,
              open_mode.unknown.Value(), open_mode.read.Value(), open_mode.write.Value(),
              open_mode.create.Value(), open_mode.check.Value());
}

void Module::Interface::GetCecInfoEventHandleSys(Kernel::HLERequestContext& ctx) {
    cecd->ProcessPendingStreetPassPackets();
    IPC::RequestParser rp(ctx);
    rp.PopPID();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(cecd->cecinfosys_event);

    LOG_WARNING(Service_CECD, "(STUBBED) called");
}

std::string Module::EncodeBase64(std::span<const u8> in) const {
    using namespace CryptoPP;
    using Name::EncodingLookupArray;
    using Name::InsertLineBreaks;
    using Name::Pad;

    std::string out;
    Base64Encoder encoder;
    AlgorithmParameters params =
        MakeParameters(EncodingLookupArray(), (const byte*)base64_dict.data())(InsertLineBreaks(),
                                                                               false)(Pad(), false);

    encoder.IsolatedInitialize(params);
    encoder.Attach(new StringSink(out));
    encoder.Put(in.data(), in.size());
    encoder.MessageEnd();

    return out;
}

std::string Module::GetCecDataPathTypeAsString(const CecDataPathType type, const u32 program_id,
                                               std::span<const u8> msg_id) const {
    switch (type) {
    case CecDataPathType::MboxList:
        return "/CEC/MBoxList____";
    case CecDataPathType::MboxInfo:
        return fmt::format("/CEC/{:08x}/MBoxInfo____", program_id);
    case CecDataPathType::InboxInfo:
        return fmt::format("/CEC/{:08x}/InBox___/BoxInfo_____", program_id);
    case CecDataPathType::OutboxInfo:
        return fmt::format("/CEC/{:08x}/OutBox__/BoxInfo_____", program_id);
    case CecDataPathType::OutboxIndex:
        return fmt::format("/CEC/{:08x}/OutBox__/OBIndex_____", program_id);
    case CecDataPathType::InboxMsg:
        return fmt::format("/CEC/{:08x}/InBox___/_{}", program_id, EncodeBase64(msg_id));
    case CecDataPathType::OutboxMsg:
        return fmt::format("/CEC/{:08x}/OutBox__/_{}", program_id, EncodeBase64(msg_id));
    case CecDataPathType::RootDir:
        return "/CEC";
    case CecDataPathType::MboxDir:
        return fmt::format("/CEC/{:08x}", program_id);
    case CecDataPathType::InboxDir:
        return fmt::format("/CEC/{:08x}/InBox___", program_id);
    case CecDataPathType::OutboxDir:
        return fmt::format("/CEC/{:08x}/OutBox__", program_id);
    case CecDataPathType::MboxData:
    case CecDataPathType::MboxIcon:
    case CecDataPathType::MboxTitle:
    default:
        return fmt::format("/CEC/{:08x}/MBoxData.{:03}", program_id, static_cast<u32>(type) - 100);
    }
}

std::string Module::GetCecCommandAsString(const CecCommand command) const {
    switch (command) {
    case CecCommand::None:
        return "None";
    case CecCommand::Start:
        return "Start";
    case CecCommand::ResetStart:
        return "ResetStart";
    case CecCommand::ReadyScan:
        return "ReadyScan";
    case CecCommand::ReadyScanWait:
        return "ReadyScanWait";
    case CecCommand::StartScan:
        return "StartScan";
    case CecCommand::Rescan:
        return "Rescan";
    case CecCommand::NdmResume:
        return "NdmResume";
    case CecCommand::NdmSuspend:
        return "NdmSuspend";
    case CecCommand::NdmSuspendImmediate:
        return "NdmSuspendImmediate";
    case CecCommand::StopWait:
        return "StopWait";
    case CecCommand::Stop:
        return "Stop";
    case CecCommand::StopForce:
        return "StopForce";
    case CecCommand::StopForceWait:
        return "StopForceWait";
    case CecCommand::ResetFilter:
        return "ResetFilter";
    case CecCommand::DaemonStop:
        return "DaemonStop";
    case CecCommand::DaemonStart:
        return "DaemonStart";
    case CecCommand::Exit:
        return "Exit";
    case CecCommand::OverBoss:
        return "OverBoss";
    case CecCommand::OverBossForce:
        return "OverBossForce";
    case CecCommand::OverBossForceWait:
        return "OverBossForceWait";
    case CecCommand::End:
        return "End";
    default:
        return "Unknown";
    }
}

void Module::CheckAndUpdateFile(const CecDataPathType path_type, const u32 ncch_program_id,
                                std::vector<u8>& file_buffer) {
    constexpr u32 max_num_boxes = 24;
    constexpr u32 name_size = 16;      // fixed size 16 characters long
    constexpr u32 valid_name_size = 8; // 8 characters are valid, the rest are null
    const u32 file_size = static_cast<u32>(file_buffer.size());

    switch (path_type) {
    case CecDataPathType::MboxList: {
        CecMBoxListHeader mbox_list_header = {};
        std::memcpy(&mbox_list_header, file_buffer.data(), sizeof(CecMBoxListHeader));

        LOG_DEBUG(Service_CECD, "CecMBoxList: magic={:#06x}, version={:#06x}, num_boxes={:#06x}",
                  mbox_list_header.magic, mbox_list_header.version, mbox_list_header.num_boxes);

        if (file_size != sizeof(CecMBoxListHeader)) { // 0x18C
            LOG_DEBUG(Service_CECD, "CecMBoxListHeader size is incorrect: {}", file_size);
        }

        if (mbox_list_header.magic != 0x6868) { // 'hh'
            if (mbox_list_header.magic == 0 || mbox_list_header.magic == 0xFFFF) {
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader magic number is not set");
            } else {
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader magic number is incorrect: {}",
                          mbox_list_header.magic);
            }
            std::memset(&mbox_list_header, 0, sizeof(CecMBoxListHeader));
            mbox_list_header.magic = 0x6868;
        }

        if (mbox_list_header.version != 0x01) { // Not quite sure if it is a version
            if (mbox_list_header.version == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader version is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxListHeader version is incorrect: {}",
                          mbox_list_header.version);
            mbox_list_header.version = 0x01;
        }

        if (mbox_list_header.num_boxes > 24) {
            LOG_DEBUG(Service_CECD, "CecMBoxListHeader number of boxes is too large: {}",
                      mbox_list_header.num_boxes);
        } else {
            std::vector<u8> name_buffer(name_size);
            std::memset(name_buffer.data(), 0, name_size);

            if (ncch_program_id != 0) {
                std::string name = fmt::format("{:08x}", ncch_program_id);
                std::memcpy(name_buffer.data(), name.data(), name.size());

                bool already_activated = false;
                for (auto i = 0; i < mbox_list_header.num_boxes; i++) {
                    // Box names start at offset 0xC, are 16 char long, first 8 id, last 8 null
                    if (std::memcmp(name_buffer.data(), &mbox_list_header.box_names[i],
                                    valid_name_size) == 0) {
                        LOG_DEBUG(Service_CECD, "Title already activated");
                        already_activated = true;
                    }
                };

                if (!already_activated) {
                    if (mbox_list_header.num_boxes < max_num_boxes) { // max boxes
                        LOG_DEBUG(Service_CECD, "Adding title to mboxlist____: {}", name);
                        std::memcpy(&mbox_list_header.box_names[mbox_list_header.num_boxes],
                                    name_buffer.data(), name_size);
                        mbox_list_header.num_boxes++;
                    }
                }
            } else { // ncch_program_id == 0, rebuild the registered title list
                // The input header can already contain registrations. Appending the directories
                // again would duplicate entries and eventually overflow the 24-slot array.
                FileSys::Path root_path(
                    GetCecDataPathTypeAsString(CecDataPathType::RootDir, 0).data());
                auto dir_result = cecd_system_save_data_archive->OpenDirectory(root_path);
                if (dir_result.Failed()) {
                    LOG_WARNING(Service_CECD,
                                "Could not rebuild StreetPass title list: /CEC is unavailable");
                    break;
                }

                constexpr u32 max_directory_entries = 128;
                auto root_dir = std::move(dir_result).Unwrap();
                std::vector<FileSys::Entry> entries(max_directory_entries);
                const u32 entry_count = root_dir->Read(max_directory_entries, entries.data());
                root_dir->Close();

                mbox_list_header.num_boxes = 0;
                mbox_list_header.box_names = {};
                for (u32 i = 0; i < entry_count; ++i) {
                    if (!entries[i].is_directory) {
                        continue;
                    }

                    const std::string file_name =
                        Common::UTF16ToUTF8(std::u16string(entries[i].filename));
                    if (file_name.size() != valid_name_size) {
                        continue;
                    }

                    u32 title_id{};
                    const auto [end, error] = std::from_chars(
                        file_name.data(), file_name.data() + file_name.size(), title_id, 16);
                    if (error != std::errc{} || end != file_name.data() + file_name.size()) {
                        continue;
                    }

                    if (mbox_list_header.num_boxes >= max_num_boxes) {
                        LOG_WARNING(Service_CECD,
                                    "StreetPass title list reached {} entries; ignoring {}",
                                    max_num_boxes, file_name);
                        break;
                    }

                    std::memcpy(mbox_list_header.box_names[mbox_list_header.num_boxes].data(),
                                file_name.data(), valid_name_size);
                    ++mbox_list_header.num_boxes;
                }
                LOG_INFO(Service_CECD, "Rebuilt StreetPass title list with {} registered title(s)",
                         mbox_list_header.num_boxes);
            }
        }
        std::memcpy(file_buffer.data(), &mbox_list_header, sizeof(CecMBoxListHeader));
        break;
    }
    case CecDataPathType::MboxInfo: {
        CecMBoxInfoHeader mbox_info_header = {};
        std::memcpy(&mbox_info_header, file_buffer.data(), sizeof(CecMBoxInfoHeader));

        LOG_DEBUG(Service_CECD,
                  "CecMBoxInfoHeader: magic={:#06x}, program_id={:#010x}, "
                  "private_id={:#010x}, flag={:#04x}, flag2={:#04x}",
                  mbox_info_header.magic, mbox_info_header.program_id, mbox_info_header.private_id,
                  mbox_info_header.flag, mbox_info_header.flag2);

        if (file_size != sizeof(CecMBoxInfoHeader)) { // 0x60
            LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader size is incorrect: {}", file_size);
        }

        if (mbox_info_header.magic != 0x6363) { // 'cc'
            if (mbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader magic number is incorrect: {}",
                          mbox_info_header.magic);
            mbox_info_header.magic = 0x6363;
        }

        if (mbox_info_header.program_id != ncch_program_id) {
            if (mbox_info_header.program_id == 0)
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader program id is not set");
            else
                LOG_DEBUG(Service_CECD, "CecMBoxInfoHeader program id doesn't match current id: {}",
                          mbox_info_header.program_id);
        }

        std::memcpy(file_buffer.data(), &mbox_info_header, sizeof(CecMBoxInfoHeader));
        break;
    }
    case CecDataPathType::InboxInfo: {
        CecBoxInfoHeader inbox_info_header = {};
        std::memcpy(&inbox_info_header, file_buffer.data(), sizeof(CecBoxInfoHeader));

        LOG_DEBUG(Service_CECD,
                  "CecBoxInfoHeader: magic={:#06x}, box_info_size={:#010x}, "
                  "max_box_size={:#010x}, box_size={:#010x}, "
                  "max_message_num={:#010x}, message_num={:#010x}, "
                  "max_batch_size={:#010x}, max_message_size={:#010x}",
                  inbox_info_header.magic, inbox_info_header.box_info_size,
                  inbox_info_header.max_box_size, inbox_info_header.box_size,
                  inbox_info_header.max_message_num, inbox_info_header.message_num,
                  inbox_info_header.max_batch_size, inbox_info_header.max_message_size);

        if (inbox_info_header.magic != 0x6262) { // 'bb'
            if (inbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader magic number is incorrect: {}",
                          inbox_info_header.magic);
            inbox_info_header.magic = 0x6262;
        }

        if (inbox_info_header.box_info_size != file_size) {
            if (inbox_info_header.box_info_size == 0)
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader box info size is not set");
            else
                LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader box info size is incorrect:",
                          inbox_info_header.box_info_size);
            inbox_info_header.box_info_size = sizeof(CecBoxInfoHeader);
        }

        if (inbox_info_header.max_box_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max box size is not set");
        } else if (inbox_info_header.max_box_size > 0x100000) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max box size is too large: {}",
                      inbox_info_header.max_box_size);
        }

        if (inbox_info_header.max_message_num == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message number is not set");
        } else if (inbox_info_header.max_message_num > 99) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message number is too large: {}",
                      inbox_info_header.max_message_num);
        }

        if (inbox_info_header.max_message_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message size is not set");
        } else if (inbox_info_header.max_message_size > 0x019000) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max message size is too large");
        }

        if (inbox_info_header.max_batch_size == 0) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max batch size is not set");
            inbox_info_header.max_batch_size = inbox_info_header.max_message_num;
        } else if (inbox_info_header.max_batch_size != inbox_info_header.max_message_num) {
            LOG_DEBUG(Service_CECD, "CecInBoxInfoHeader max batch size != max message number");
        }

        std::memcpy(file_buffer.data(), &inbox_info_header, sizeof(CecBoxInfoHeader));
        break;
    }
    case CecDataPathType::OutboxInfo: {
        CecBoxInfoHeader outbox_info_header = {};
        std::memcpy(&outbox_info_header, file_buffer.data(), sizeof(CecBoxInfoHeader));

        LOG_DEBUG(Service_CECD,
                  "CecBoxInfoHeader: magic={:#06x}, box_info_size={:#010x}, "
                  "max_box_size={:#010x}, box_size={:#010x}, "
                  "max_message_num={:#010x}, message_num={:#010x}, "
                  "max_batch_size={:#010x}, max_message_size={:#010x}",
                  outbox_info_header.magic, outbox_info_header.box_info_size,
                  outbox_info_header.max_box_size, outbox_info_header.box_size,
                  outbox_info_header.max_message_num, outbox_info_header.message_num,
                  outbox_info_header.max_batch_size, outbox_info_header.max_message_size);

        if (outbox_info_header.magic != 0x6262) { // 'bb'
            if (outbox_info_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader magic number is incorrect: {}",
                          outbox_info_header.magic);
            outbox_info_header.magic = 0x6262;
        }

        if (outbox_info_header.box_info_size != file_buffer.size()) {
            if (outbox_info_header.box_info_size == 0)
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader box info size is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader box info size is incorrect:",
                          outbox_info_header.box_info_size);
            outbox_info_header.box_info_size = sizeof(CecBoxInfoHeader);
            outbox_info_header.message_num = 0;
        }

        if (outbox_info_header.max_box_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max box size is not set");
        } else if (outbox_info_header.max_box_size > 0x100000) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max box size is too large");
        }

        if (outbox_info_header.max_message_num == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message number is not set");
        } else if (outbox_info_header.max_message_num > 99) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message number is too large");
        }

        if (outbox_info_header.max_message_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message size is not set");
        } else if (outbox_info_header.max_message_size > 0x019000) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max message size is too large");
        }

        if (outbox_info_header.max_batch_size == 0) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max batch size is not set");
            outbox_info_header.max_batch_size = outbox_info_header.max_message_num;
        } else if (outbox_info_header.max_batch_size != outbox_info_header.max_message_num) {
            LOG_DEBUG(Service_CECD, "CecOutBoxInfoHeader max batch size != max message number");
        }

        /// We need to read the /CEC/<id>/OutBox directory to find out which messages, if any,
        /// are present. The num_of_messages = (total_read_count) - 2, to adjust for
        /// the BoxInfo____ and OBIndex_____files that are present in the directory as well.
        FileSys::Path outbox_path(
            GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, ncch_program_id).data());

        auto dir_result = cecd_system_save_data_archive->OpenDirectory(outbox_path);

        auto outbox_dir = std::move(dir_result).Unwrap();
        std::vector<FileSys::Entry> entries(outbox_info_header.max_message_num + 2);
        const u32 entry_count =
            outbox_dir->Read(outbox_info_header.max_message_num + 2, entries.data());
        outbox_dir->Close();

        LOG_DEBUG(Service_CECD, "Number of entries found in /OutBox: {}", entry_count);
        std::array<CecMessageHeader, 8> message_headers;

        std::string boxinfo_name("BoxInfo_____");
        std::string obindex_name("OBIndex_____");
        std::string file_name;
        std::u16string u16_filename;

        for (u32 i = 0; i < entry_count; i++) {
            u16_filename = std::u16string(entries[i].filename);
            file_name = Common::UTF16ToUTF8(u16_filename);

            if (boxinfo_name.compare(file_name) != 0 && obindex_name.compare(file_name) != 0) {
                LOG_DEBUG(Service_CECD, "Adding message to BoxInfo_____: {}", file_name);

                FileSys::Path message_path(
                    (GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, ncch_program_id) + "/" +
                     file_name)
                        .data());

                FileSys::Mode mode;
                mode.read_flag.Assign(1);

                auto message_result = cecd_system_save_data_archive->OpenFile(message_path, mode);

                auto message = std::move(message_result).Unwrap();
                const u32 message_size = static_cast<u32>(message->GetSize());
                std::vector<u8> buffer(message_size);

                void(message->Read(0, message_size, buffer.data()).Unwrap());
                message->Close();

                std::memcpy(&message_headers[outbox_info_header.message_num++], buffer.data(),
                            sizeof(CecMessageHeader));
            }
        }

        if (outbox_info_header.message_num > 0) {
            const u32 message_headers_size =
                outbox_info_header.message_num * sizeof(CecMessageHeader);

            file_buffer.resize(sizeof(CecBoxInfoHeader) + message_headers_size, 0);
            outbox_info_header.box_info_size += message_headers_size;

            std::memcpy(file_buffer.data() + sizeof(CecBoxInfoHeader), &message_headers,
                        message_headers_size);
        }

        std::memcpy(file_buffer.data(), &outbox_info_header, sizeof(CecBoxInfoHeader));
        break;
    }
    case CecDataPathType::OutboxIndex: {
        CecOBIndexHeader obindex_header = {};
        std::memcpy(&obindex_header, file_buffer.data(), sizeof(CecOBIndexHeader));

        if (file_size < sizeof(CecOBIndexHeader)) { // 0x08, minimum size
            LOG_DEBUG(Service_CECD, "CecOBIndexHeader size is too small: {}", file_size);
        }

        if (obindex_header.magic != 0x6767) { // 'gg'
            if (obindex_header.magic == 0)
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader magic number is not set");
            else
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader magic number is incorrect: {}",
                          obindex_header.magic);
            obindex_header.magic = 0x6767;
        }

        if (obindex_header.message_num == 0) {
            if (file_size > sizeof(CecOBIndexHeader)) {
                LOG_DEBUG(Service_CECD, "CecOBIndexHeader message number is not set");
                obindex_header.message_num = (file_size % 8) - 1; // 8 byte message id - 1 header
            }
        } else if (obindex_header.message_num != (file_size % 8) - 1) {
            LOG_DEBUG(Service_CECD, "CecOBIndexHeader message number is incorrect: {}",
                      obindex_header.message_num);
            obindex_header.message_num = 0;
        }

        /// We need to read the /CEC/<id>/OutBox directory to find out which messages, if any,
        /// are present. The num_of_messages = (total_read_count) - 2, to adjust for
        /// the BoxInfo____ and OBIndex_____files that are present in the directory as well.
        FileSys::Path outbox_path(
            GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, ncch_program_id).data());

        auto dir_result = cecd_system_save_data_archive->OpenDirectory(outbox_path);

        auto outbox_dir = std::move(dir_result).Unwrap();
        std::vector<FileSys::Entry> entries(8);
        const u32 entry_count = outbox_dir->Read(8, entries.data());
        outbox_dir->Close();

        LOG_DEBUG(Service_CECD, "Number of entries found in /OutBox: {}", entry_count);
        std::array<std::array<u8, 8>, 8> message_ids;

        std::string boxinfo_name("BoxInfo_____");
        std::string obindex_name("OBIndex_____");
        std::string file_name;
        std::u16string u16_filename;

        for (u32 i = 0; i < entry_count; i++) {
            u16_filename = std::u16string(entries[i].filename);
            file_name = Common::UTF16ToUTF8(u16_filename);

            if (boxinfo_name.compare(file_name) != 0 && obindex_name.compare(file_name) != 0) {
                FileSys::Path message_path(
                    (GetCecDataPathTypeAsString(CecDataPathType::OutboxDir, ncch_program_id) + "/" +
                     file_name)
                        .data());

                FileSys::Mode mode;
                mode.read_flag.Assign(1);

                auto message_result = cecd_system_save_data_archive->OpenFile(message_path, mode);

                auto message = std::move(message_result).Unwrap();
                const u32 message_size = static_cast<u32>(message->GetSize());
                std::vector<u8> buffer(message_size);

                void(message->Read(0, message_size, buffer.data()).Unwrap());
                message->Close();

                // Message id is at offset 0x20, and is 8 bytes
                std::memcpy(&message_ids[obindex_header.message_num++], buffer.data() + 0x20, 8);
            }
        }

        if (obindex_header.message_num > 0) {
            const u32 message_ids_size = obindex_header.message_num * 8;
            file_buffer.resize(sizeof(CecOBIndexHeader) + message_ids_size);
            std::memcpy(file_buffer.data() + sizeof(CecOBIndexHeader), &message_ids,
                        message_ids_size);
        }

        std::memcpy(file_buffer.data(), &obindex_header, sizeof(CecOBIndexHeader));
        break;
    }
    case CecDataPathType::InboxMsg:
        break;
    case CecDataPathType::OutboxMsg:
        break;
    case CecDataPathType::RootDir:
    case CecDataPathType::MboxDir:
    case CecDataPathType::InboxDir:
    case CecDataPathType::OutboxDir:
        break;
    case CecDataPathType::MboxData:
    case CecDataPathType::MboxIcon:
    case CecDataPathType::MboxTitle:
    default: {
    }
    }
}

Module::SessionData::SessionData() {}

Module::SessionData::~SessionData() {
    if (file)
        file->Close();
}

Module::Interface::Interface(std::shared_ptr<Module> cecd, const char* name, u32 max_session)
    : ServiceFramework(name, max_session), cecd(std::move(cecd)) {}

Module::Module(Core::System& system) : system(system) {
    using namespace Kernel;
    cecinfo_event = system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "CECD::cecinfo_event");
    cecinfosys_event =
        system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "CECD::cecinfosys_event");
    change_state_event =
        system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "CECD::change_state_event");

    const std::string& nand_directory = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir);
    FileSys::ArchiveFactory_SystemSaveData systemsavedata_factory(nand_directory);

    // Open the SystemSaveData archive 0x00010026
    FileSys::Path archive_path(cecd_system_savedata_id);
    auto archive_result = systemsavedata_factory.Open(archive_path, 0);

    // If the archive didn't exist, create the files inside
    if (archive_result.Code() != FileSys::ResultNotFound) {
        ASSERT_MSG(archive_result.Succeeded(), "Could not open the CECD SystemSaveData archive!");
        cecd_system_save_data_archive = std::move(archive_result).Unwrap();
    } else {
        // Format the archive to create the directories
        systemsavedata_factory.Format(archive_path, FileSys::ArchiveFormatInfo(), 0, 0, 0);

        // Open it again to get a valid archive now that the folder exists
        cecd_system_save_data_archive = systemsavedata_factory.Open(archive_path, 0).Unwrap();

        /// Now that the archive is formatted, we need to create the root CEC directory,
        /// eventlog.dat, and CEC/MBoxList____
        const FileSys::Path root_dir_path(
            GetCecDataPathTypeAsString(CecDataPathType::RootDir, 0).data());
        cecd_system_save_data_archive->CreateDirectory(root_dir_path);

        FileSys::Mode mode;
        mode.write_flag.Assign(1);
        mode.create_flag.Assign(1);

        /// eventlog.dat resides in the root of the archive beside the CEC directory
        /// Initially created, at offset 0x0, are bytes 0x01 0x41 0x12, followed by
        /// zeroes until offset 0x1000, where it changes to 0xDD until the end of file
        /// at offset 0x30d53. 0xDD means that the cec module hasn't written data to that
        /// region yet.
        FileSys::Path eventlog_path("/eventlog.dat");

        auto eventlog_result = cecd_system_save_data_archive->OpenFile(eventlog_path, mode);

        constexpr u32 eventlog_size = 0x30d54;
        auto eventlog = std::move(eventlog_result).Unwrap();
        std::vector<u8> eventlog_buffer(eventlog_size);

        std::memset(&eventlog_buffer[0], 0, 0x1000);
        eventlog_buffer[0] = 0x01;
        eventlog_buffer[1] = 0x41;
        eventlog_buffer[2] = 0x12;

        eventlog->Write(0, eventlog_size, true, false, eventlog_buffer.data());
        eventlog->Close();

        /// MBoxList____ resides within the root CEC/ directory.
        /// Initially created, at offset 0x0, are bytes 0x68 0x68 0x00 0x00 0x01, with 0x6868 'hh',
        /// being the magic number. The rest of the file is filled with zeroes, until the end of
        /// file at offset 0x18b
        FileSys::Path mboxlist_path(
            GetCecDataPathTypeAsString(CecDataPathType::MboxList, 0).data());

        auto mboxlist_result = cecd_system_save_data_archive->OpenFile(mboxlist_path, mode);

        constexpr u32 mboxlist_size = 0x18c;
        auto mboxlist = std::move(mboxlist_result).Unwrap();
        std::vector<u8> mboxlist_buffer(mboxlist_size);

        std::memset(&mboxlist_buffer[0], 0, mboxlist_size);
        mboxlist_buffer[0] = 0x68;
        mboxlist_buffer[1] = 0x68;
        // mboxlist_buffer[2-3] are already zeroed
        mboxlist_buffer[4] = 0x01;

        mboxlist->Write(0, mboxlist_size, true, false, mboxlist_buffer.data());
        mboxlist->Close();
    }

    // On real hardware the CECD sysmodule processes StreetPass in the background. Azahar's
    // older implementation only drained the queue during game-driven CECD IPC requests,
    // so successfully received packets could remain invisible indefinitely during gameplay.
    // Schedule delivery on the emulation thread instead of doing NAND I/O on ENet's thread.
    streetpass_delivery_event = system.CoreTiming().RegisterEvent(
        "CECD::StreetPassDeliveryCallback", [this](std::uintptr_t, s64 cycles_late) {
            ProcessPendingStreetPassPackets();
            this->system.CoreTiming().ScheduleEvent(
                std::max<s64>(msToCycles(1), msToCycles(250) - cycles_late),
                streetpass_delivery_event);
        });
    system.CoreTiming().ScheduleEvent(msToCycles(250), streetpass_delivery_event);
}

Module::~Module() {
    if (streetpass_delivery_event) {
        system.CoreTiming().UnscheduleEvent(streetpass_delivery_event, 0);
    }
}

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    auto cecd = std::make_shared<Module>(system);
    if (const auto member = Network::GetRoomMember().lock()) {
        cecd->BindRoomMember(member);
    }
    std::make_shared<CECD_NDM>(cecd)->InstallAsService(service_manager);
    std::make_shared<CECD_S>(cecd)->InstallAsService(service_manager);
    std::make_shared<CECD_U>(cecd)->InstallAsService(service_manager);
}

} // namespace Service::CECD
