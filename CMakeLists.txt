cmake_minimum_required(VERSION 3.22)

# CiZ-synth: a MAME wrapper for the Casio CZ-101 and CZ-1 (with a tone editor).
# MIDI in -> embedded MAME cz101 driver -> audio out through the sound card.
#
# The MAME core is not rebuilt here: this project links the static libraries
# and driver objects already produced by the Vintage Emulator Studio MAME tree
# (SUBTARGET=vesembedded), but only pulls in the CZ-101 driver.

if(APPLE)
    project(CiZSynth VERSION 0.1.0 LANGUAGES C CXX OBJCXX)
else()
    project(CiZSynth VERSION 0.1.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(JUCE_DIR "/Volumes/STORAGE/JUCE" CACHE PATH "JUCE checkout")
    set(MAME_ROOT "/Volumes/STORAGE/GITS/VintageEmulatorStudio/validation/mame-0.289-patched" CACHE PATH "Patched MAME 0.289 tree with a vesembedded build")
    set(SDL3_PREFIX "/Volumes/STORAGE/GITS/VintageEmulatorStudio/Builds/MacOSX/release-validation/deps/sdl3-arm64-macos11" CACHE PATH "Static SDL3 install prefix")
    set(MAME_OBJ "${MAME_ROOT}/build/osx_clang/obj/x64/Release")
    set(MAME_BIN "${MAME_ROOT}/build/osx_clang/bin/x64/Release")
    set(MAME_OSD_SUFFIX sdl3)
    set(MAME_SDL_LIB "${SDL3_PREFIX}/lib/libSDL3.a")
else()
    # Linux: the patched MAME tree built with `build/linux_gcc` (SDL2 OSD).
    set(JUCE_DIR "$ENV{HOME}/src/juce-checkouts/JUCE-8.0.13" CACHE PATH "JUCE checkout")
    set(MAME_ROOT "/media/eric/sauvegardes/github/VintageEmulatorStudio/validation/mame-0.289-patched" CACHE PATH "Patched MAME 0.289 tree with a linux_gcc vesembedded build")
    set(MAME_OBJ "${MAME_ROOT}/build/linux_gcc/obj/x64/Release")
    set(MAME_BIN "${MAME_ROOT}/build/linux_gcc/bin/x64/Release")
    set(MAME_OSD_SUFFIX sdl)
    set(MAME_SDL_LIB "")
endif()

foreach(required
        "${JUCE_DIR}/CMakeLists.txt"
        "${MAME_OBJ}/src/mame/casio/cz101.o"
        "${MAME_BIN}/libemu.a"
        "${MAME_BIN}/mame_vesembedded/liboptional.a")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing build input: ${required}")
    endif()
endforeach()
if(MAME_SDL_LIB AND NOT EXISTS "${MAME_SDL_LIB}")
    message(FATAL_ERROR "Missing build input: ${MAME_SDL_LIB}")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

juce_add_plugin(CiZSynth
    PRODUCT_NAME "CiZ-synth"
    COMPANY_NAME "WJ"
    BUNDLE_ID "com.wj.CiZSynth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Wjst
    PLUGIN_CODE Cz11
    FORMATS Standalone)

# Files that include MAME headers get exactly the defines libemu.a was built with.
set(MAME_SOURCES
    Engine/EmbeddedEmulatorEngine.cpp
    Source/FrontendStubs.cpp
    "${MAME_ROOT}/src/frontend/mame/luaengine.cpp"
    "${MAME_ROOT}/src/frontend/mame/luaengine_render.cpp"
    "${MAME_ROOT}/src/frontend/mame/pluginopts.cpp")

target_sources(CiZSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ToneEditor.cpp
    ${MAME_SOURCES})

set_source_files_properties(${MAME_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "NDEBUG;CRLF=2;LSB_FIRST;FLAC__NO_DLL;PUGIXML_HEADER_ONLY;ASMJIT_STATIC;VES_EMBEDDED_BUILD;VES_VIRTUAL_MIDI_RETROFIT;VES_LAYOUT_PLUGIN_SUPPORT;__STDC_CONSTANT_MACROS;__STDC_LIMIT_MACROS"
    COMPILE_OPTIONS "-fno-strict-aliasing;-Wno-deprecated-declarations;-w"
    INCLUDE_DIRECTORIES "${MAME_ROOT}/src;${MAME_ROOT}/src/osd;${MAME_ROOT}/src/emu;${MAME_ROOT}/src/devices;${MAME_ROOT}/src/mame;${MAME_ROOT}/src/lib;${MAME_ROOT}/src/lib/util;${MAME_ROOT}/src/frontend/mame;${MAME_ROOT}/3rdparty;${MAME_ROOT}/3rdparty/sol2;${MAME_ROOT}/3rdparty/asio/include;${MAME_ROOT}/3rdparty/expat/lib;${MAME_ROOT}/3rdparty/zlib;${MAME_ROOT}/3rdparty/flac/include;${MAME_ROOT}/3rdparty/lua/src;${MAME_ROOT}/3rdparty/rapidjson/include;${MAME_ROOT}/build/generated;${MAME_ROOT}/build/generated/emu;${MAME_ROOT}/build/generated/emu/layout;${MAME_ROOT}/build/generated/mame/layout")

target_include_directories(CiZSynth PRIVATE Engine)

target_compile_definitions(CiZSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        CIZ_SOURCE_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/Resources")

target_compile_options(CiZSynth PRIVATE -Wno-deprecated-declarations)

set(MAME_DRIVER_OBJECTS
    "${MAME_OBJ}/src/mame/casio/cz1.o"
    "${MAME_OBJ}/src/mame/casio/cz101.o"
    "${MAME_OBJ}/src/mame/casio/ra3.o"
    "${MAME_OBJ}/src/devices/bus/midi/midi.o"
    "${MAME_OBJ}/src/devices/bus/midi/midiinport.o"
    "${MAME_OBJ}/src/devices/bus/midi/midioutport.o")

set(MAME_ARCHIVES
    "${MAME_BIN}/mame_vesembedded/liboptional.a"
    "${MAME_BIN}/mame_vesembedded/libformats.a"
    "${MAME_BIN}/mame_vesembedded/libdasm.a"
    "${MAME_BIN}/libemu.a"
    "${MAME_BIN}/libutils.a"
    "${MAME_BIN}/libocore_${MAME_OSD_SUFFIX}.a"
    "${MAME_BIN}/libosd_${MAME_OSD_SUFFIX}.a"
    "${MAME_BIN}/libqtdbg_${MAME_OSD_SUFFIX}.a"
    "${MAME_BIN}/libsoftfloat3.a"
    "${MAME_BIN}/libymfm.a"
    "${MAME_BIN}/lib7z.a"
    "${MAME_BIN}/libzlib.a"
    "${MAME_BIN}/libzstd.a"
    "${MAME_BIN}/libexpat.a"
    "${MAME_BIN}/libflac.a"
    "${MAME_BIN}/libjpeg.a"
    "${MAME_BIN}/libutf8proc.a"
    "${MAME_BIN}/libwdlfft.a"
    "${MAME_BIN}/libasmjit.a"
    "${MAME_BIN}/liblua.a"
    "${MAME_BIN}/liblualibs.a"
    "${MAME_BIN}/libsqlite3.a"
    "${MAME_BIN}/libportmidi.a"
    "${MAME_BIN}/liblinenoise.a"
    "${MAME_BIN}/libbx.a"
    "${MAME_BIN}/libbimg.a"
    "${MAME_BIN}/libbgfx.a")

target_link_libraries(CiZSynth
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)

if(APPLE)
    target_link_libraries(CiZSynth PUBLIC
        ${MAME_DRIVER_OBJECTS}
        ${MAME_ARCHIVES}
        "${MAME_BIN}/libportaudio.a"
        "${MAME_SDL_LIB}"
        "-framework Carbon" "-framework Cocoa" "-framework Foundation" "-framework IOKit"
        "-framework OpenGL" "-framework Metal" "-framework QuartzCore" "-framework CoreMedia"
        "-framework CoreVideo" "-framework UniformTypeIdentifiers" "-framework ForceFeedback"
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AVFoundation"
        "-framework GameController" "-weak_framework CoreHaptics")
else()
    # GNU ld resolves archives left to right: put the MAME payload in a group.
    target_link_libraries(CiZSynth PUBLIC
        -Wl,--start-group
        ${MAME_DRIVER_OBJECTS}
        ${MAME_ARCHIVES}
        -Wl,--end-group
        -ldl -lrt -lSDL2 -lm -lpthread -lutil -lGL -lasound
        -lX11 -lXinerama -lXext -lXi -lSDL2_ttf -lfontconfig -lfreetype)
endif()

# MAME needs boot.lua + layout/init.lua at runtime (clickable panel layout),
# and the panel artwork generated by tools/make_artwork.py.
if(APPLE)
    set(RESOURCES_DEST "$<TARGET_BUNDLE_CONTENT_DIR:CiZSynth_Standalone>/Resources")
else()
    set(RESOURCES_DEST "$<TARGET_FILE_DIR:CiZSynth_Standalone>/Resources")
endif()
add_custom_command(TARGET CiZSynth_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/plugins" "${RESOURCES_DEST}/plugins"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/artwork" "${RESOURCES_DEST}/artwork")

# Headless probe (Linux): boots a machine without audio and talks SysEx over MIDI.
if(NOT APPLE)
    add_executable(cz_probe EXCLUDE_FROM_ALL tools/cz_probe.cpp ${MAME_SOURCES})
    target_include_directories(cz_probe PRIVATE Engine)
    target_compile_options(cz_probe PRIVATE -Wno-deprecated-declarations)
    target_link_libraries(cz_probe PRIVATE
        -Wl,--start-group ${MAME_DRIVER_OBJECTS} ${MAME_ARCHIVES} -Wl,--end-group
        -ldl -lrt -lSDL2 -lm -lpthread -lutil -lGL -lasound
        -lX11 -lXinerama -lXext -lXi -lSDL2_ttf -lfontconfig -lfreetype)
endif()
