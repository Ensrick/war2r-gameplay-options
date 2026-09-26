cmake_minimum_required(VERSION 3.20)
project(war2r_gameplay_options VERSION 1.34.0 LANGUAGES CXX RC)

# Pre-release builds carry a -dev.N suffix; every change bumps N (docs/DEV_HISTORY.md). Empty for a release build.
set(MOD_PRERELEASE "")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Warcraft II Remastered is a 32-bit exe: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT, no redist needed

set(CORE_SOURCES src/autocast.cpp src/config.cpp src/damagetypes.cpp src/datatweaks.cpp src/dodge.cpp src/eye.cpp src/farms.cpp src/hook.cpp src/log.cpp src/mod.cpp src/production.cpp src/resume.cpp src/scouts.cpp src/spells.cpp src/trees.cpp src/upgrades.cpp src/tweaks.cpp src/units.cpp src/workers.cpp
    src/world.cpp src/resources.rc)
# The .rc embeds the default config; rebuild the resource when the toml changes.
set_source_files_properties(src/resources.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/config/gameplay_options.default.toml)
set(COMMON_DEFS MOD_VERSION="${PROJECT_VERSION}${MOD_PRERELEASE}" WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_library(version SHARED src/dllmain.cpp ${CORE_SOURCES} src/version.def)
target_compile_definitions(version PRIVATE ${COMMON_DEFS})
target_compile_options(version PRIVATE /W4 /permissive- /utf-8)
target_link_options(version PRIVATE /SAFESEH:NO)

# Offline smoke test against the real exe image, see test/selftest.cpp.
add_executable(selftest test/selftest.cpp ${CORE_SOURCES})
target_compile_definitions(selftest PRIVATE ${COMMON_DEFS})
target_compile_options(selftest PRIVATE /W4 /permissive- /utf-8)
target_link_options(selftest PRIVATE /SAFESEH:NO)

# Stand-in for the game (an exe NAMED "Warcraft II.exe" that imports version.dll): proves the proxy takes the game
# path in DllMain and writes its log. Run through test/activation_test.ps1.
add_executable(activation_host test/activation_host.cpp)
set_target_properties(activation_host PROPERTIES OUTPUT_NAME "Warcraft II"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/activation RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/activation)
target_compile_definitions(activation_host PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
# The Windows SDK import library, NOT our "version" target: its .def exports carry no stdcall decoration.
target_link_libraries(activation_host PRIVATE Version.lib)
