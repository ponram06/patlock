cmake_minimum_required(VERSION 3.24)
project(Patlock LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use Visual Studio 2022 with -A x64 inside the Windows 11 VM.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(patlock_common STATIC src/common/pattern_window.cpp src/common/crypto.cpp src/common/store.cpp src/common/tpm.cpp)
target_include_directories(patlock_common PUBLIC src/common)
target_compile_definitions(patlock_common PUBLIC UNICODE _UNICODE NOMINMAX)
target_compile_options(patlock_common PRIVATE /W4 /sdl)
target_link_libraries(patlock_common PUBLIC user32 gdi32 gdiplus bcrypt advapi32 ncrypt)
add_executable(patlock_enroll WIN32 src/enroll/main.cpp src/enroll/enroll.manifest)
target_link_libraries(patlock_enroll PRIVATE patlock_common credui)
target_link_options(patlock_enroll PRIVATE /MANIFESTUAC:level='requireAdministrator')
add_executable(pattern_window_demo WIN32 src/tools/pattern_window_demo/main.cpp)
target_link_libraries(pattern_window_demo PRIVATE patlock_common)
enable_testing()
add_executable(pattern_tests tests/pattern_tests.cpp)
target_link_libraries(pattern_tests PRIVATE patlock_common)
add_test(NAME pattern_rules COMMAND pattern_tests)
add_executable(arming_tests tests/arming_tests.cpp)
target_link_libraries(arming_tests PRIVATE patlock_common)
add_test(NAME arming_gate COMMAND arming_tests)
add_executable(security_tests tests/security_tests.cpp)
target_link_libraries(security_tests PRIVATE patlock_common)
add_test(NAME crypto_policy COMMAND security_tests)

add_library(PatlockProvider SHARED
    src/provider/CSampleCredential.cpp
    src/provider/CSampleProvider.cpp
    src/provider/Dll.cpp
    src/provider/guid.cpp
    src/provider/helpers.cpp
    src/provider/resources.rc
    src/provider/samplev2credentialprovider.def)
target_compile_definitions(PatlockProvider PRIVATE UNICODE _UNICODE WIN32 _WINDOWS)
target_include_directories(PatlockProvider PRIVATE src/provider)
target_compile_options(PatlockProvider PRIVATE /W4 /sdl /guard:cf)
target_link_options(PatlockProvider PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
target_link_libraries(PatlockProvider PRIVATE patlock_common credui shlwapi secur32)
