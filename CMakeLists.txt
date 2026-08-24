cmake_minimum_required(VERSION 3.20)
project(SoftVoiceSAPI5 VERSION 1.0.0 LANGUAGES CXX)

# The SoftVoice engine is a 32-bit 1997 DLL that runs in its own process,
# svwebspeak-host.exe, on every architecture. That is what lets the 32-bit and
# the 64-bit SAPI 5 interfaces be built from one identical set of sources:
# neither loads the engine, both drive the same helper over a loopback socket.
# Configure this project once per architecture (-A Win32 and -A x64); the only
# difference between the two builds is where they install to.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets the Microsoft toolchain.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SV_ARCH "x64")
else()
    set(SV_ARCH "Win32")
endif()
message(STATUS "SoftVoice SAPI 5: building for ${SV_ARCH}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Statically linked runtime, so the installer never has to carry a
# redistributable and a missing one can never make the voices silent.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --------------------------------------------------------- engine payload ---

# The 1997 SoftVoice engine and the 32-bit host that owns it. Copied beside
# the compiler output so the build tree can speak without being installed -
# which is also the layout the installer lays down, so a test in one place
# means the same thing in the other.
set(SV_ENGINE_FILES
    svwebspeak-host.exe
    SVctl32.DLL
    SVENG32.DLL
    Svspan32.dll
)

set(SV_ENGINE_STAGED "")
foreach(_file IN LISTS SV_ENGINE_FILES)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/bin/${_file}")
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR "Missing engine file: ${_src}")
    endif()
    set(_dst "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/${_file}")
    add_custom_command(
        OUTPUT "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
        DEPENDS "${_src}"
        COMMENT "Staging ${_file}"
        VERBATIM
    )
    list(APPEND SV_ENGINE_STAGED "${_dst}")
endforeach()

add_custom_target(sv_engine_payload ALL DEPENDS ${SV_ENGINE_STAGED})

# ---------------------------------------------------------------- common ----

# Everything that is neither COM nor user interface: paths, logging, the
# settings file, the voice catalogue and the host client. Shared by the SAPI
# DLL, the configuration utility and the diagnostic tools.
add_library(sv_common STATIC
    src/common/sv_paths.cpp
    src/common/sv_log.cpp
    src/common/sv_settings.cpp
    src/common/sv_voices.cpp
    src/common/sv_host.cpp
    src/common/sv_synth.cpp
)

target_include_directories(sv_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(sv_common PUBLIC ws2_32 shell32 ole32)

target_compile_definitions(sv_common PUBLIC
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(sv_common PRIVATE /W3 /MP)

# ------------------------------------------------------- the SAPI 5 engine ---

add_library(SoftVoiceSAPI SHARED
    src/sapi/sv_main.cpp
    src/sapi/sv_com.cpp
    src/sapi/sv_registry.cpp
    src/sapi/sv_datakey.cpp
    src/sapi/sv_token.cpp
    src/sapi/sv_enum_tokens.cpp
    src/sapi/sv_engine.cpp
    src/sapi/softvoice_sapi.def
)

target_include_directories(SoftVoiceSAPI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sapi
)

target_link_libraries(SoftVoiceSAPI PRIVATE
    sv_common
    ole32
    oleaut32
    advapi32
    shell32
)

target_compile_options(SoftVoiceSAPI PRIVATE /W3 /MP)

# ------------------------------------------------------ diagnostic tools -----

# Renders a WAV straight from the engine, with no SAPI involved. This is what
# proves the engine works independently of the COM registration.
add_executable(sv_render tools/sv_render.cpp)
target_link_libraries(sv_render PRIVATE sv_common)
target_compile_options(sv_render PRIVATE /W3 /MP)

# Speaks through SAPI 5, so it exercises the DLL exactly as an application
# would - including whether Windows can enumerate the voices at all.
add_executable(sv_speak tools/sv_speak.cpp)
target_link_libraries(sv_speak PRIVATE sv_common ole32 oleaut32 sapi)
target_compile_options(sv_speak PRIVATE /W3 /MP)

# Drives the DLL's class objects directly, so the SAPI implementation can be
# tested without registering anything and therefore without elevation. It
# links nothing from the DLL - it loads it by path - so it also serves to
# check a build before it is installed.
add_executable(sv_selftest tools/sv_selftest.cpp)
target_link_libraries(sv_selftest PRIVATE ole32 oleaut32)
target_compile_definitions(sv_selftest PRIVATE
    _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sv_selftest PRIVATE /W3 /MP)
add_dependencies(sv_selftest SoftVoiceSAPI)

# Measures the whole path an application uses - ISpVoice, SAPI's dispatch and
# audio buffering, this engine and the device - so the engine's share of the
# delay can be told apart from SAPI's own.
add_executable(sv_latency tools/sv_latency.cpp)
target_link_libraries(sv_latency PRIVATE ole32 oleaut32)
target_compile_definitions(sv_latency PRIVATE
    _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sv_latency PRIVATE /W3 /MP)

# --------------------------------------------- the configuration utility -----

# Built 32-bit only and shipped as such: it runs on every Windows this
# package supports, and one copy means one settings file and one place for a
# bug to be. It is a plain Win32 dialog, which screen readers read natively.
if(SV_ARCH STREQUAL "Win32")
    add_executable(SoftVoiceConfig WIN32
        src/config/sv_config.cpp
        src/config/sv_config.rc
    )

    target_include_directories(SoftVoiceConfig PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    )

    target_link_libraries(SoftVoiceConfig PRIVATE
        sv_common
        comctl32
        shell32
        ole32
        winmm
    )

    target_compile_options(SoftVoiceConfig PRIVATE /W3 /MP)

    # Embedded rather than added as an .rc resource, so it merges with the
    # one the linker generates instead of colliding with it.
    target_link_options(SoftVoiceConfig PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/config/sv_config.manifest"
    )
endif()
