// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Originally MIT-licensed code from The Pixellizer Group

// Copyright 2022 The Pixellizer Group
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <array>
#include <limits>
#include <span>

#include <cryptopp/blake2.h>
#include <cryptopp/sha.h>

#include "common/settings.h"
#include "core/file_sys/file_backend.h"
#include "core/file_sys/level256_signature.h"
#include "core/file_sys/plugin_3gx.h"
#include "core/file_sys/plugin_3gx_bootloader.h"
#include "core/hle/kernel/config_mem.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/loader/loader.h"

static std::string ReadTextInfo(std::span<const u8> file_data, std::size_t offset,
                                std::size_t max_size) {
    if (offset == 0 || max_size == 0 || max_size > 0x400 ||
        !FileSys::IsValid3GXFileRange(file_data.size(), offset, max_size)) {
        // Limit strings to 0x400 bytes.
        return "";
    }
    std::vector<char> char_data(max_size);
    std::memcpy(char_data.data(), file_data.data() + offset, max_size);
    char_data[max_size - 1] = '\0';
    return std::string(char_data.data());
}

static bool ReadSection(std::vector<u8>& data_out, std::span<const u8> file_data,
                        std::size_t offset, std::size_t size) {
    if (size > 0x5000000 || !FileSys::IsValid3GXFileRange(file_data.size(), offset, size)) {
        // Limit read section size to 5MiB and reject ranges outside the file.
        return false;
    }
    data_out.resize(size);
    if (size != 0) {
        std::memcpy(data_out.data(), file_data.data() + offset, size);
    }
    return true;
}

Loader::ResultStatus FileSys::Plugin3GXLoader::Load(
    Service::PLGLDR::PLG_LDR::PluginLoaderContext& plg_context, Kernel::Process& process,
    Kernel::KernelSystem& kernel, Service::PLGLDR::PLG_LDR& plg_ldr) {
    plg_context.ResetTransientLevel256Authorization();
    FileUtil::IOFile file(plg_context.plugin_path, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Service_PLGLDR, "Failed to load 3GX plugin. Not found: {}",
                  plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    // Snapshot the file once so signature verification and all bytes later parsed/mapped have the
    // same immutable source. This also binds the legacy SHA compatibility path to the loaded bytes.
    // Keep the snapshot bounded: each of the three executable sections is independently capped at
    // 80 MiB below, with room for metadata and the manifest.
    constexpr u64 MAX_3GX_FILE_SIZE = 3 * 0x5000000ULL + 0x1000000ULL;
    const u64 file_size = file.GetSize();
    if (file_size == 0 || file_size > MAX_3GX_FILE_SIZE ||
        file_size > std::numeric_limits<std::size_t>::max()) {
        LOG_ERROR(Service_PLGLDR, "3GX size is invalid or exceeds the loader limit: {} ({} bytes)",
                  plg_context.plugin_path, file_size);
        return Loader::ResultStatus::Error;
    }
    std::vector<u8> plugin_data(static_cast<std::size_t>(file_size));
    if (file.ReadBytes(plugin_data.data(), plugin_data.size()) != plugin_data.size()) {
        LOG_ERROR(Service_PLGLDR, "Failed to read 3GX snapshot: {}", plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }
    const std::span<const u8> plugin_bytes{plugin_data};

    {
        CryptoPP::SHA256 sha256;
        CryptoPP::BLAKE2b blake2b{false, 32};
        const u64 payload_size =
            file_size > Level256::MANIFEST_SIZE ? file_size - Level256::MANIFEST_SIZE : 0;
        sha256.Update(plugin_bytes.data(), plugin_bytes.size());
        std::array<u8, CryptoPP::SHA256::DIGESTSIZE> sha256_digest{};
        sha256.Final(sha256_digest.data());
        plg_context.level256_official_plugin_hash_matches =
            sha256_digest == Kernel::LEVEL256_PLUGIN_SHA256;

        if (payload_size != 0) {
            blake2b.Update(plugin_bytes.data(), static_cast<std::size_t>(payload_size));
            std::array<u8, 32> payload_digest{};
            blake2b.TruncatedFinal(payload_digest.data(), payload_digest.size());
            const auto manifest = plugin_bytes.subspan(static_cast<std::size_t>(payload_size));
            Level256::PluginManifest parsed_manifest;
            if (Level256::VerifyPluginManifest(manifest, payload_size, payload_digest,
                                               Level256::TrustedReleasePublicKey().data(),
                                               &parsed_manifest)) {
                plg_context.level256_official_plugin_signature_verified = true;
                plg_context.level256_signed_plugin_artifact_version =
                    parsed_manifest.artifact_version;
            }
        }
        plg_context.level256_plugin_identity_verified = Level256::IsTrustedPluginIdentity(
            plg_context.level256_official_plugin_hash_matches,
            plg_context.level256_official_plugin_signature_verified);

        if (plg_context.level256_official_plugin_signature_verified) {
            LOG_INFO(Service_PLGLDR,
                     "Level256 signed plugin verified pid={} title_id={:016X} "
                     "artifact_version={} publisher_identity=verified",
                     process.process_id, process.codeset ? process.codeset->program_id : 0,
                     plg_context.level256_signed_plugin_artifact_version);
        }
    }

    // Load CIA Header
    std::vector<u8> header_data(sizeof(_3gx_Header));
    if (plugin_bytes.size() < sizeof(_3gx_Header)) {
        LOG_ERROR(Service_PLGLDR, "Failed to load 3GX plugin. File corrupted: {}",
                  plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }
    std::memcpy(header_data.data(), plugin_bytes.data(), sizeof(_3gx_Header));

    std::memcpy(&header, header_data.data(), sizeof(_3gx_Header));

    // Check magic value
    if (std::memcmp(&header.magic, _3GX_magic, 8) != 0) {
        LOG_ERROR(Service_PLGLDR, "Failed to load 3GX plugin. Outdated or invalid 3GX plugin: {}",
                  plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    if (header.infos.flags.compatibility == static_cast<u32>(_3gx_Infos::Compatibility::CONSOLE)) {
        LOG_ERROR(Service_PLGLDR, "Failed to load 3GX plugin. Not compatible with Azahar: {}",
                  plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    // Load strings
    author = ReadTextInfo(plugin_bytes, header.infos.author_msg_offset, header.infos.author_len);
    title = ReadTextInfo(plugin_bytes, header.infos.title_msg_offset, header.infos.title_len);
    description = ReadTextInfo(plugin_bytes, header.infos.description_msg_offset,
                               header.infos.description_len);
    summary = ReadTextInfo(plugin_bytes, header.infos.summary_msg_offset, header.infos.summary_len);

    LOG_INFO(Service_PLGLDR, "Trying to load plugin - Title: {} - Author: {}", title, author);
    LOG_INFO(Service_PLGLDR,
             "3GX metadata pid={} title_id={:016X} version={} flags=0x{:08X} compatibility={} "
             "code=0x{:X} rodata=0x{:X} data=0x{:X} bss=0x{:X} exe_load_func=0x{:X}",
             process.process_id, process.codeset->program_id, header.version,
             static_cast<u32>(header.infos.flags.raw),
             static_cast<u32>(header.infos.flags.compatibility.Value()),
             static_cast<u32>(header.executable.code_size),
             static_cast<u32>(header.executable.rodata_size),
             static_cast<u32>(header.executable.data_size),
             static_cast<u32>(header.executable.bss_size),
             static_cast<u32>(header.executable.exe_load_func_offset));

    // Load compatible TIDs
    {
        std::vector<u8> raw_TID_data;
        const u64 targets_size = static_cast<u64>(header.targets.count) * sizeof(u32);
        if (targets_size > std::numeric_limits<std::size_t>::max() ||
            !ReadSection(raw_TID_data, plugin_bytes, header.targets.title_offsets,
                         static_cast<std::size_t>(targets_size))) {
            LOG_ERROR(Service_PLGLDR,
                      "3GX load failed stage=title-list pid={} title_id={:016X} path={}",
                      process.process_id, process.codeset->program_id, plg_context.plugin_path);
            return Loader::ResultStatus::Error;
        }
        compatible_TID.reserve(header.targets.count); // compatible_TID should be empty right now
        for (u32 i = 0; i < u32(header.targets.count); i++) {
            compatible_TID.push_back(
                u32_le(*reinterpret_cast<u32*>(raw_TID_data.data() + i * sizeof(u32))));
        }
    }

    if (!compatible_TID.empty() &&
        std::find(compatible_TID.begin(), compatible_TID.end(),
                  static_cast<u32>(process.codeset->program_id)) == compatible_TID.end()) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX load failed stage=title-compatibility pid={} title_id={:016X} path={}",
                  process.process_id, process.codeset->program_id, plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    // Load exe load func and args
    if (header.infos.flags.embedded_exe_func.Value() &&
        header.executable.exe_load_func_offset != 0) {
        exe_load_func.clear();
        std::vector<u8> out;
        for (int i = 0; i < 32; i++) {
            const u64 instruction_offset =
                static_cast<u64>(header.executable.exe_load_func_offset) + i * sizeof(u32);
            if (instruction_offset > std::numeric_limits<std::size_t>::max() ||
                !ReadSection(out, plugin_bytes, static_cast<std::size_t>(instruction_offset),
                             sizeof(u32))) {
                LOG_ERROR(Service_PLGLDR,
                          "3GX load failed stage=embedded-loader-function pid={} "
                          "title_id={:016X} offset=0x{:X} path={}",
                          process.process_id, process.codeset->program_id, instruction_offset,
                          plg_context.plugin_path);
                return Loader::ResultStatus::Error;
            }
            u32 instruction = *reinterpret_cast<u32_le*>(out.data());
            if (instruction == 0xE320F000) {
                break;
            }
            exe_load_func.push_back(instruction);
        }
        std::memcpy(exe_load_args, header.infos.builtin_load_exe_args,
                    sizeof(_3gx_Infos::builtin_load_exe_args));
    }

    // Load code sections
    if (!ReadSection(text_section, plugin_bytes, header.executable.code_offset,
                     header.executable.code_size)) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX load failed stage=text-section pid={} title_id={:016X} path={}",
                  process.process_id, process.codeset->program_id, plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }
    if (!ReadSection(rodata_section, plugin_bytes, header.executable.rodata_offset,
                     header.executable.rodata_size)) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX load failed stage=rodata-section pid={} title_id={:016X} path={}",
                  process.process_id, process.codeset->program_id, plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }
    if (!ReadSection(data_section, plugin_bytes, header.executable.data_offset,
                     header.executable.data_size)) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX load failed stage=data-section pid={} title_id={:016X} path={}",
                  process.process_id, process.codeset->program_id, plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    // Read-only CTRPF OSD signature scan for the explicitly enabled, verified Level256 identity
    // and the target title. A signature scan match alone does not prove that InstallOSD enabled
    // a hook.
    if (Kernel::ShouldScanLevel256CtrpfOSD(
            Settings::values.level256_compatibility_enabled.GetValue(),
            plg_context.level256_plugin_identity_verified, process.codeset->program_id)) {
        const auto& code_segment = process.codeset->CodeSegment();
        plg_context.ctrpf_osd_scan = {};
        plg_context.ctrpf_osd_candidate_initial_words = {};
        plg_context.ctrpf_osd_post_init_compared = false;
        if (code_segment.size <= Kernel::LEVEL256_CTRPF_OSD_MAX_SCAN_BYTES &&
            static_cast<u64>(code_segment.addr) + code_segment.size <=
                static_cast<u64>(UINT32_MAX) + 1) {
            std::vector<u8> game_text(code_segment.size);
            kernel.memory.ReadBlock(process, code_segment.addr, game_text.data(), game_text.size());
            plg_context.ctrpf_osd_scan = Kernel::ScanLevel256CtrpfOSDPatterns(
                game_text.data(), game_text.size(), code_segment.addr);
            const std::array<u32, 3> candidates = {
                plg_context.ctrpf_osd_scan.candidate1,
                plg_context.ctrpf_osd_scan.candidate2,
                plg_context.ctrpf_osd_scan.candidate3,
            };
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                if (candidates[i] != Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND) {
                    plg_context.ctrpf_osd_candidate_initial_words[i] =
                        kernel.memory.Read32(process, candidates[i]);
                }
            }
            const auto log_offset = [](u32 offset) {
                return offset == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND
                           ? std::string("not_found")
                           : fmt::format("+0x{:X}", offset);
            };
            const auto log_address = [](u32 address) {
                return address == Kernel::LEVEL256_CTRPF_OSD_NOT_FOUND
                           ? std::string("not_found")
                           : fmt::format("0x{:08X}", address);
            };
            LOG_INFO(Service_PLGLDR,
                     "Level256 CTRPF OSD pattern scan pid={} title_id={:016X} "
                     "text=0x{:08X}+0x{:X} pattern0_offset={} fallback_offset={} "
                     "pattern3_offset={} pattern4_offset={} "
                     "SearchStmfd1={} SearchStmfd2={} SearchStmfd3={} selected={} "
                     "candidate_words={:08X},{:08X},{:08X}",
                     process.process_id, process.codeset->program_id, code_segment.addr,
                     code_segment.size, log_offset(plg_context.ctrpf_osd_scan.pattern0_offset),
                     log_offset(plg_context.ctrpf_osd_scan.pattern0_fallback_offset),
                     log_offset(plg_context.ctrpf_osd_scan.pattern3_offset),
                     log_offset(plg_context.ctrpf_osd_scan.pattern4_offset),
                     log_address(plg_context.ctrpf_osd_scan.candidate1),
                     log_address(plg_context.ctrpf_osd_scan.candidate2),
                     log_address(plg_context.ctrpf_osd_scan.candidate3),
                     log_address(plg_context.ctrpf_osd_scan.selected_candidate),
                     plg_context.ctrpf_osd_candidate_initial_words[0],
                     plg_context.ctrpf_osd_candidate_initial_words[1],
                     plg_context.ctrpf_osd_candidate_initial_words[2]);
        } else {
            LOG_WARNING(Service_PLGLDR,
                        "Level256 CTRPF OSD pattern scan skipped pid={} title_id={:016X} "
                        "text=0x{:08X}+0x{:X} reason=range_limit_or_overflow",
                        process.process_id, process.codeset->program_id, code_segment.addr,
                        code_segment.size);
        }
    }

    const auto result = Map(plg_context, process, kernel, plg_ldr);
    LOG_INFO(Service_PLGLDR,
             "3GX map result pid={} title_id={:016X} status={} plugin_loaded={} owner_pid={}",
             process.process_id, process.codeset->program_id, static_cast<u32>(result),
             plg_context.plugin_loaded, plg_context.plugin_process_id);
    return result;
}

Loader::ResultStatus FileSys::Plugin3GXLoader::Map(
    Service::PLGLDR::PLG_LDR::PluginLoaderContext& plg_context, Kernel::Process& process,
    Kernel::KernelSystem& kernel, Service::PLGLDR::PLG_LDR& plg_ldr) {

    // Verify exe load checksum function is available
    if (exe_load_func.empty() && plg_context.load_exe_func.empty()) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX map failed stage=checksum-function pid={} title_id={:016X} "
                  "embedded_flag={} embedded_offset=0x{:X} external_words={} path={}",
                  process.process_id, process.codeset->program_id,
                  static_cast<u32>(header.infos.flags.embedded_exe_func.Value()),
                  static_cast<u32>(header.executable.exe_load_func_offset),
                  plg_context.load_exe_func.size(), plg_context.plugin_path);
        return Loader::ResultStatus::Error;
    }

    const std::array<u32, 4> mem_region_sizes = {
        5 * 1024 * 1024,  // 5 MiB
        2 * 1024 * 1024,  // 2 MiB
        10 * 1024 * 1024, // 10 MiB
        5 * 1024 * 1024   // 5 MiB (reserved)
    };

    const bool is_mem_private = header.infos.flags.use_private_memory != 0;

    // Map memory block. This behaviour mimics how plugins are loaded on 3DS as much as possible.
    // Calculate the sizes of the different memory regions
    const u32 block_size = mem_region_sizes[header.infos.flags.memory_region_size.Value()];
    const u32 exe_offset = 0;
    const u32 exe_size = (sizeof(PluginHeader) + text_section.size() + rodata_section.size() +
                          data_section.size() + header.executable.bss_size + 0x1000) &
                         ~0xFFFu;
    const u32 bootloader_offset = exe_offset + exe_size;
    const u32 bootloader_size = bootloader_memory_size;
    const u32 heap_offset = bootloader_offset + bootloader_size;
    const u32 heap_size = block_size - heap_offset;

    // Allocate a block of memory for the plugin
    std::optional<u32> offset;
    if (kernel.GetMemoryMode() == Kernel::MemoryMode::NewProd ||
        (plg_context.use_user_load_parameters &&
         plg_context.user_load_parameters.plugin_memory_strategy ==
             Service::PLGLDR::PLG_LDR::PluginMemoryStrategy::PLG_STRATEGY_MODE3)) {
        // Allocate memory block from the end of the APPLICATION region
        offset =
            kernel.GetMemoryRegion(Kernel::MemoryRegion::APPLICATION)->RLinearAllocate(block_size);

        // If the reported available APP mem equals the actual size, remove the plugin block size.
        if (offset) {
            auto& config_mem = kernel.GetConfigMemHandler();
            if (config_mem.GetConfigMem().app_mem_alloc ==
                kernel.GetMemoryRegion(Kernel::MemoryRegion::APPLICATION)->size) {
                config_mem.GetConfigMem().app_mem_alloc -= block_size;
            }
        }
        plg_context.memory_region = Kernel::MemoryRegion::APPLICATION;
    } else {
        // Allocate memory block from the start of the SYSTEM region
        offset = kernel.GetMemoryRegion(Kernel::MemoryRegion::SYSTEM)->LinearAllocate(block_size);
        plg_context.memory_region = Kernel::MemoryRegion::SYSTEM;
    }

    if (!offset) {
        LOG_ERROR(Service_PLGLDR,
                  "3GX map failed stage=memory-allocation pid={} title_id={:016X} bytes=0x{:X} "
                  "path={}",
                  process.process_id, process.codeset->program_id, block_size,
                  plg_context.plugin_path);
        return Loader::ResultStatus::ErrorMemoryAllocationFailed;
    }

    u32 fcram_offset = offset.value();

    auto backing_memory_exe = kernel.memory.GetFCRAMRef(fcram_offset + exe_offset);
    std::fill(backing_memory_exe.GetPtr(), backing_memory_exe.GetPtr() + exe_size, 0);

    // Map the executable
    auto vma_exe = process.vm_manager.MapBackingMemory(
        _3GX_exe_load_addr, backing_memory_exe, exe_size,
        is_mem_private ? Kernel::MemoryState::Private : Kernel::MemoryState::Shared);
    ASSERT(vma_exe.Succeeded());
    process.vm_manager.Reprotect(vma_exe.Unwrap(), Kernel::VMAPermission::ReadWriteExecute);

    // Prepare plugin header and write it
    PluginHeader plugin_header = {0};
    plugin_header.version = header.version;
    plugin_header.exe_size = exe_size;
    plugin_header.heap_VA = _3GX_heap_load_addr;
    plugin_header.heap_size = heap_size;
    plg_context.plg_event = _3GX_exe_load_addr - 0x4;
    plg_context.plg_reply = _3GX_exe_load_addr - 0x8;
    plugin_header.plgldr_event = plg_context.plg_event;
    plugin_header.plgldr_reply = plg_context.plg_reply;
    plugin_header.is_default_plugin = plg_context.is_default_path;
    if (plg_context.use_user_load_parameters) {
        std::memcpy(plugin_header.config, plg_context.user_load_parameters.config,
                    sizeof(PluginHeader::config));
    }
    kernel.memory.WriteBlock(process, _3GX_exe_load_addr, &plugin_header, sizeof(PluginHeader));

    // Write text section
    kernel.memory.WriteBlock(process, _3GX_exe_load_addr + sizeof(PluginHeader),
                             text_section.data(), header.executable.code_size);
    // Write rodata section
    kernel.memory.WriteBlock(
        process, _3GX_exe_load_addr + sizeof(PluginHeader) + header.executable.code_size,
        rodata_section.data(), header.executable.rodata_size);

    // Write data section
    kernel.memory.WriteBlock(process,
                             _3GX_exe_load_addr + sizeof(PluginHeader) +
                                 header.executable.code_size + header.executable.rodata_size,
                             data_section.data(), header.executable.data_size);

    // Map bootloader
    const bool use_internal = plg_context.load_exe_func.empty();
    plg_context.level256_bootloader_map_snapshot_logged = false;
    plg_context.level256_bootloader_pre_svc_snapshot_logged = false;
    plg_context.level256_bootloader_return_snapshot_logged = false;
    plg_context.level256_bootloader_return_trace_started = false;
    MapBootloader(
        process, kernel, fcram_offset + bootloader_offset,
        (use_internal) ? exe_load_func : plg_context.load_exe_func,
        (use_internal) ? exe_load_args : plg_context.load_exe_args,
        header.executable.code_size + header.executable.rodata_size + header.executable.data_size,
        header.infos.exe_load_checksum,
        plg_context.use_user_load_parameters ? plg_context.user_load_parameters.no_flash : 0);
    const auto* map_thread = kernel.GetCurrentThreadManager().GetCurrentThread();
    if (map_thread) {
        plg_ldr.TraceLevel256BootloaderSnapshot(process, "after_map_bootloader",
                                                map_thread->GetThreadId());
    }

    // Map plugin heap
    auto backing_memory_heap = kernel.memory.GetFCRAMRef(fcram_offset + heap_offset);
    std::fill(backing_memory_heap.GetPtr(), backing_memory_heap.GetPtr() + heap_size, 0);

    auto vma_heap = process.vm_manager.MapBackingMemory(
        _3GX_heap_load_addr, backing_memory_heap, heap_size,
        is_mem_private ? Kernel::MemoryState::Private : Kernel::MemoryState::Shared);
    ASSERT(vma_heap.Succeeded());
    process.vm_manager.Reprotect(vma_heap.Unwrap(), Kernel::VMAPermission::ReadWriteExecute);

    kernel.memory.SetPlugin3GXFramebufferAddress(Memory::FCRAM_PADDR + fcram_offset + heap_offset);
    plg_context.plugin_loaded = true;
    plg_context.plugin_process_id = process.process_id;
    plg_context.plugin_code_reached = false;
    plg_context.plugin_code_begin = _3GX_exe_load_addr + sizeof(PluginHeader);
    plg_context.plugin_code_end = _3GX_exe_load_addr + exe_size;
    plg_context.plugin_bootloader_begin = _3GX_exe_load_addr - bootloader_memory_size;
    plg_context.plugin_bootloader_end = _3GX_exe_load_addr;
    plg_context.plugin_trace_count = 0;
    plg_context.plugin_get_system_info_seen = false;
    plg_context.plugin_trace_thread_id = UINT32_MAX;
    plg_context.plugin_followup_trace_count = 0;
    plg_context.plugin_last_return_pc = 0;
    plg_context.plugin_instruction_trace_started = false;
    plg_context.plugin_service_trace_count = 0;
    plg_context.use_user_load_parameters = false;
    plg_context.memory_block = {fcram_offset, block_size};
    plg_context.ctrpf_startup_watchpoints_armed = false;
    plg_context.ctrpf_startup_watch_counts = {};
    plg_context.ctrpf_osd_candidate_execution_seen = false;
    const bool level256_diagnostic_enabled =
        Settings::values.level256_compatibility_enabled.GetValue();
    const bool level256_diagnostic_context =
        process.codeset &&
        Kernel::ShouldCaptureLevel256CtrpfStartupDiagnostic(
            level256_diagnostic_enabled, plg_context.level256_plugin_identity_verified,
            process.codeset->program_id, process.process_id, plg_context.plugin_process_id,
            plg_context.plugin_loaded);
    if (level256_diagnostic_context) {
        u32 text_watch_count = 0;
        bool candidate_watch_armed = false;
        const bool owner_matches = plg_context.plugin_process_id == process.process_id;
        const VAddr candidate = plg_context.ctrpf_osd_scan.selected_candidate;
        if (owner_matches) {
            for (const VAddr address : Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES) {
                if (kernel.memory.IsValidVirtualAddress(process, address)) {
                    kernel.memory.RegisterWatchpoint(process, address, 1);
                    plg_context.ctrpf_startup_watchpoints_armed = true;
                    ++text_watch_count;
                }
            }
            if (candidate == Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS &&
                plg_context.ctrpf_osd_candidate_initial_words[0] == 0xE92D5FF0 &&
                kernel.memory.IsValidVirtualAddress(process, candidate)) {
                kernel.memory.RegisterWatchpoint(process, candidate,
                                                 Kernel::LEVEL256_CTRPF_OSD_CANDIDATE_SIZE);
                plg_context.ctrpf_startup_watchpoints_armed = true;
                candidate_watch_armed = true;
            }
        }
        LOG_INFO(Service_PLGLDR,
                 "Level256 CTRPF startup diagnostic gate pid={} title_id={:016X} "
                 "enabled={} hash_match=true owner_match={} text_watches={} "
                 "candidate_watch={} candidate=0x{:08X}",
                 process.process_id, process.codeset->program_id, level256_diagnostic_enabled,
                 owner_matches, text_watch_count, candidate_watch_armed, candidate);
        if (plg_context.ctrpf_startup_watchpoints_armed) {
            LOG_INFO(Service_PLGLDR,
                     "Level256 CTRPF startup memory watches armed pid={} title_id={:016X} "
                     "hash_match=true title_text=0x{:08X} create_backup=0x{:08X} "
                     "creating_backup=0x{:08X} backup_created=0x{:08X} plugin_ready=0x{:08X} "
                     "title_config_path=0x{:08X} start_as_a_path=0x{:08X} "
                     "osd_candidate=0x{:08X}",
                     process.process_id, process.codeset->program_id,
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[0],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[1],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[2],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[3],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[4],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[5],
                     Kernel::LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[6], candidate);
        }
    }
    LOG_INFO(Service_PLGLDR,
             "3GX memory mapped pid={} title_id={:016X} code=0x{:08X}-0x{:08X} "
             "bootloader=0x{:08X}-0x{:08X} heap=0x{:08X}+0x{:X} private={}",
             process.process_id, process.codeset->program_id,
             _3GX_exe_load_addr + static_cast<u32>(sizeof(PluginHeader)),
             _3GX_exe_load_addr + exe_size, _3GX_exe_load_addr - bootloader_memory_size,
             _3GX_exe_load_addr, _3GX_heap_load_addr, heap_size, is_mem_private);

    return Loader::ResultStatus::Success;
}

void FileSys::Plugin3GXLoader::MapBootloader(Kernel::Process& process, Kernel::KernelSystem& kernel,
                                             u32 memory_offset, std::span<const u32> exe_load_func,
                                             const u32_le* exe_load_args, u32 checksum_size,
                                             u32 exe_checksum, bool no_flash) {

    u32_le game_instructions[2];
    kernel.memory.ReadBlock(process, process.codeset->CodeSegment().addr, game_instructions,
                            sizeof(u32) * 2);

    std::array<u32_le, g_plugin_loader_bootloader.size() / sizeof(u32)> bootloader;
    std::memcpy(bootloader.data(), g_plugin_loader_bootloader.data(),
                g_plugin_loader_bootloader.size());

    for (auto it = bootloader.begin(); it < bootloader.end(); it++) {
        switch (static_cast<u32>(*it)) {
        case 0xDEAD0000: {
            *it = game_instructions[0];
        } break;
        case 0xDEAD0001: {
            *it = game_instructions[1];
        } break;
        case 0xDEAD0002: {
            *it = process.codeset->CodeSegment().addr;
        } break;
        case 0xDEAD0003: {
            for (u32 i = 0;
                 i <
                 sizeof(Service::PLGLDR::PLG_LDR::PluginLoaderContext::load_exe_args) / sizeof(u32);
                 i++) {
                bootloader[i + (it - bootloader.begin())] = exe_load_args[i];
            }
        } break;
        case 0xDEAD0004: {
            *it = _3GX_exe_load_addr + sizeof(PluginHeader);
        } break;
        case 0xDEAD0005: {
            *it = _3GX_exe_load_addr + sizeof(PluginHeader) + checksum_size;
        } break;
        case 0xDEAD0006: {
            *it = exe_checksum;
        } break;
        case 0xDEAD0007: {
            *it = _3GX_exe_load_addr - 0xC;
        } break;
        case 0xDEAD0008: {
            *it = _3GX_exe_load_addr + sizeof(PluginHeader);
        } break;
        case 0xDEAD0009: {
            *it = no_flash ? 1 : 0;
        } break;
        case 0xDEAD000A: {
            for (u32 i = 0; i < exe_load_func.size(); i++) {
                bootloader[i + (it - bootloader.begin())] = exe_load_func[i];
            }
        } break;
        default:
            break;
        }
    }

    // Map bootloader to the offset provided
    auto backing_memory = kernel.memory.GetFCRAMRef(memory_offset);
    std::fill(backing_memory.GetPtr(), backing_memory.GetPtr() + bootloader_memory_size, 0);
    auto vma = process.vm_manager.MapBackingMemory(_3GX_exe_load_addr - bootloader_memory_size,
                                                   backing_memory, bootloader_memory_size,
                                                   Kernel::MemoryState::Private);
    ASSERT(vma.Succeeded());
    process.vm_manager.Reprotect(vma.Unwrap(), Kernel::VMAPermission::ReadWriteExecute);

    // Write bootloader
    kernel.memory.WriteBlock(
        process, _3GX_exe_load_addr - bootloader_memory_size, bootloader.data(),
        std::min<std::size_t>(bootloader.size() * sizeof(u32), bootloader_memory_size));

    game_instructions[0] = 0xE51FF004; // ldr pc, [pc, #-4]
    game_instructions[1] = _3GX_exe_load_addr - bootloader_memory_size;
    kernel.memory.WriteBlock(process, process.codeset->CodeSegment().addr, game_instructions,
                             sizeof(u32) * 2);
}
