cmake_minimum_required(VERSION 3.24)
project(Patlock LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use Visual Studio 2022 with -A x64 inside the Windows 11 VM.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(SampleV2CredentialProvider SHARED
    src/provider/CSampleCredential.cpp
    src/provider/CSampleProvider.cpp
    src/provider/Dll.cpp
    src/provider/guid.cpp
    src/provider/helpers.cpp
    src/provider/resources.rc
    src/provider/samplev2credentialprovider.def)
target_compile_definitions(SampleV2CredentialProvider PRIVATE UNICODE _UNICODE WIN32 _WINDOWS)
target_include_directories(SampleV2CredentialProvider PRIVATE src/provider)
target_compile_options(SampleV2CredentialProvider PRIVATE /W4 /sdl /guard:cf)
target_link_options(SampleV2CredentialProvider PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
target_link_libraries(SampleV2CredentialProvider PRIVATE credui shlwapi secur32)
