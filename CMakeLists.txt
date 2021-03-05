# USAGE
# Link against the interface target "coop" or add include/ to your header path

cmake_minimum_required(VERSION 3.17)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STANDALONE ON)
else()
    set(STANDALONE OFF)
endif()

# Configure which targets to build. Defaults set based on whether this project is included transitively or not
option(COOP_BUILD_PROCESSOR "Build the provided coop processor" ${STANDALONE})
option(COOP_BUILD_TESTS "Build coop tests" ${STANDALONE})
option(COOP_ENABLE_TRACER "Verbose logging of all coroutine and scheduler events" ${STANDALONE})
option(COOP_ENABLE_ASAN "Enable ASAN" OFF)

project(coop LANGUAGES CXX)

# Output artifacts to the binary root
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(COOP_ENABLE_ASAN AND NOT WIN32)
    # For ASAN usage with MSVC, it's recommended to drive CMake from Visual Studio and use the
    # addressSantizerEnabled: true
    # flag in CMakeSettings.json
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif()

find_package(Threads REQUIRED)

add_library(coop INTERFACE)
target_include_directories(coop INTERFACE include)
target_compile_features(coop INTERFACE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT WIN32)
        target_compile_options(coop INTERFACE -stdlib=libc++)
        target_link_options(coop INTERFACE -stdlib=libc++ -latomic)
    else()
        target_compile_definitions(coop INTERFACE _SILENCE_CLANG_COROUTINE_MESSAGE)
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Currently, GCC requires this flag for coroutine language support
    target_compile_options(coop INTERFACE -fcoroutines)
endif()
target_link_libraries(coop INTERFACE Threads::Threads)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(coop INTERFACE COOP_BUILD_SHARED)
endif()

if(COOP_ENABLE_TRACER)
    target_compile_definitions(coop INTERFACE COOP_TRACE)
endif()

if(COOP_BUILD_PROCESSOR OR COOP_BUILD_TESTS)
    add_subdirectory(src)
endif()

if(STANDALONE OR COOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()