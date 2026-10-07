# my-passthrough build setup.
# Build from "Developer PowerShell for VS 2026":
#   cmake -B build -A Win32
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.20)
project(my_passthrough LANGUAGES CXX)

# Both games are 32-bit, so our plugins must be too.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "my-passthrough must be built as 32-bit. Delete the build folder and run: cmake -B build -A Win32")
endif()

# --- plugin-sdk (GTA SA library), built beforehand by tools/build-plugin-sdk.ps1 ---
set(PLUGIN_SDK_DIR "${CMAKE_SOURCE_DIR}/extern/plugin-sdk" CACHE PATH "plugin-sdk folder")
set(PLUGIN_SDK_LIB "${PLUGIN_SDK_DIR}/output/lib/Plugin.lib")
if(NOT EXISTS "${PLUGIN_SDK_LIB}")
    message(FATAL_ERROR "plugin-sdk library not found: ${PLUGIN_SDK_LIB}\n"
        "Build it first, from Developer PowerShell for VS 2026:\n"
        "  powershell -ExecutionPolicy Bypass -File tools\\build-plugin-sdk.ps1")
endif()

# --- GTA San Andreas plugin: my-passthrough.asi ---
add_library(gta_plugin SHARED src/gta/main.cpp)

# Match plugin-sdk's own settings for SA plugins (its tools/premake/premake5.lua):
# GTA SA 1.0 US, newest C++ standard, multi-byte characters, /sdl- off.
target_include_directories(gta_plugin PRIVATE
    "${PLUGIN_SDK_DIR}/plugin_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/enums"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/rw"
    "${PLUGIN_SDK_DIR}/shared"
    "${PLUGIN_SDK_DIR}/shared/game")
target_compile_definitions(gta_plugin PRIVATE
    GTASA PLUGIN_SGV_10US RW _CRT_NON_CONFORMING_SWPRINTFS _MBCS)
target_compile_options(gta_plugin PRIVATE /std:c++latest /sdl- /MP)
target_link_libraries(gta_plugin PRIVATE "${PLUGIN_SDK_LIB}")

set_target_properties(gta_plugin PROPERTIES
    OUTPUT_NAME "my-passthrough"
    SUFFIX ".asi"
    # Build the C++ runtime into the plugin so nothing extra needs installing.
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Where the plugin writes its log: the project's logs folder (gitignored),
# not the Mod Loader folder (Mod Loader rescans its folders on every file change).
set(MP_LOG_DIR "${CMAKE_SOURCE_DIR}/logs" CACHE PATH "Folder the plugins write their logs to")
target_compile_definitions(gta_plugin PRIVATE "MP_LOG_DIR=\"${MP_LOG_DIR}\"")

# After each build, copy the plugin to the one approved folder in the GTA install
# (AGENTS.md, Environment section). Set to empty to turn copying off:
#   cmake -B build -A Win32 -DGTA_DEPLOY_DIR=
set(GTA_DEPLOY_DIR "D:/Games/GTA San Andreas/modloader/my-passthrough" CACHE PATH
    "Folder the GTA plugin is copied to after building (empty = don't copy)")
if(GTA_DEPLOY_DIR)
    add_custom_command(TARGET gta_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GTA_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:gta_plugin>" "${GTA_DEPLOY_DIR}"
        COMMENT "Copying my-passthrough.asi to ${GTA_DEPLOY_DIR}")
endif()

# --- Launcher: my-passthrough-launcher.exe (starts reTHAWed) ---
# Stays in the build folder; nothing is copied into the reTHAWed install.
set(RETHAWED_DIR "D:/Games/reTHAWed" CACHE PATH "reTHAWed install folder (read only)")
add_executable(launcher src/launcher/main.cpp)
set_target_properties(launcher PROPERTIES
    OUTPUT_NAME "my-passthrough-launcher"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_features(launcher PRIVATE cxx_std_17)
target_compile_definitions(launcher PRIVATE
    "MP_LOG_DIR=\"${MP_LOG_DIR}\"" "MP_RETHAWED_DIR=\"${RETHAWED_DIR}\"" UNICODE _UNICODE)
