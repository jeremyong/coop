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

project(coop LANGUAGES CXX)

add_library(coop INTERFACE)
target_include_directories(coop INTERFACE include)
target_compile_features(coop INTERFACE cxx_std_20)

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