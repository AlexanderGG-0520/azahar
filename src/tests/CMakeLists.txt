add_executable(tests
    common/bit_field.cpp
    common/file_util.cpp
    common/param_package.cpp
    core/core_timing.cpp
    core/file_sys/path_parser.cpp
    core/file_sys/ywb_save_date.cpp
    core/file_sys/plugin_3gx.cpp
    core/file_sys/level256_signature.cpp
    core/file_sys/yw2_save_date.cpp
    core/gdbstub/thread_debug_info.cpp
    core/hle/service/sm/process_service_redirect.cpp
    core/hle/service/sm/srv_redirect.cpp
    core/hle/service/fs/fs_user_media_type.cpp
    core/hle/service/plgldr_level256_lfcs.cpp
    core/hle/service/plgldr_level256_random.cpp
    core/hle/service/plgldr_level256_restore.cpp
    core/hle/service/plgldr_level256_process_view.cpp
    core/hle/service/soc/soc_u_address.cpp
    core/hle/service/plgldr_path.cpp
    core/hle/kernel/hle_ipc.cpp
    core/hle/kernel/dsp_semaphore_signal_grace.cpp
    core/hle/kernel/level256_diagnostic.cpp
    core/hle/kernel/luma3ds/compatibility.cpp
    core/hle/kernel/luma3ds/compatibility_dispatch.cpp
    core/hle/kernel/luma3ds/level256_capabilities.cpp
    core/hle/kernel/luma3ds/custom_backdoor.cpp
    core/hle/kernel/luma3ds/level256_process_view.cpp
    core/hle/kernel/luma3ds/level256_memory_operations.cpp
    core/hle/kernel/luma3ds/level256_system_info.cpp
    core/memory/memory.cpp
    core/memory/vm_manager.cpp
    precompiled_headers.h
    audio_core/hle/hle.cpp
    audio_core/hle/source.cpp
    audio_core/lle/lle.cpp
    audio_core/audio_fixures.h
    audio_core/decoder_tests.cpp
    video_core/pica_types.cpp
    video_core/shader.cpp
    audio_core/merryhime_3ds_audio/merry_audio/merry_audio.cpp
    audio_core/merryhime_3ds_audio/merry_audio/merry_audio.h
    audio_core/merryhime_3ds_audio/merry_audio/service_fixture.cpp
    audio_core/merryhime_3ds_audio/merry_audio/service_fixture.h
    audio_core/merryhime_3ds_audio/audio_test_biquad_filter.cpp
)

create_target_directory_groups(tests)

if (BSD STREQUAL "NetBSD")
    include(DisablePaxMprotect)
    disable_pax_mprotect(tests)
endif()

target_link_libraries(tests PRIVATE citra_common citra_core video_core audio_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} catch2 cryptopp nihstro-headers Threads::Threads)

if (ENABLE_LIBRETRO)
    target_link_libraries(tests PRIVATE $<TARGET_OBJECTS:citra_libretro_common>)
endif()

add_test(NAME tests COMMAND tests)
if(NOT ANDROID AND (CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME))
    catch_discover_tests(tests)
endif()


if (CITRA_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(tests PRIVATE precompiled_headers.h)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (MSVC)
    include(BundleTarget)
    bundle_target_in_place(tests)
endif()
