cmake_minimum_required(VERSION 3.20)
project(war2r_autocast VERSION 0.1.2 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Warcraft II Remastered is a 32-bit exe: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT, no redist needed

set(CORE_SOURCES src/autocast.cpp src/config.cpp src/hook.cpp src/log.cpp)
set(COMMON_DEFS AUTOCAST_VERSION="${PROJECT_VERSION}" WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_library(version SHARED src/dllmain.cpp ${CORE_SOURCES} src/version.def)
target_compile_definitions(version PRIVATE ${COMMON_DEFS})
target_compile_options(version PRIVATE /W4 /permissive- /utf-8)
target_link_options(version PRIVATE /SAFESEH:NO)

# Offline smoke test against the real exe image, see test/selftest.cpp.
add_executable(selftest test/selftest.cpp ${CORE_SOURCES})
target_compile_definitions(selftest PRIVATE ${COMMON_DEFS})
target_compile_options(selftest PRIVATE /W4 /permissive- /utf-8)
target_link_options(selftest PRIVATE /SAFESEH:NO)
