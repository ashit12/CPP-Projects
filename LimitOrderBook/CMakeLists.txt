cmake_minimum_required(VERSION 3.20)
project(LimitOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so clangd / VS Code picks up -std=c++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Google Test ───────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/heads/main.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
# Prevent GTest from overriding our compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(lob
    src/price_level.cpp
    src/order_book.cpp
    src/matching_engine.cpp
)

target_include_directories(lob PUBLIC include)

# ── Benchmark executable ──────────────────────────────────────────────────────
add_executable(bench bench/bench_main.cpp)
target_link_libraries(bench PRIVATE lob)

# ── Test executable ───────────────────────────────────────────────────────────
enable_testing()

add_executable(tests tests/test_main.cpp)
target_link_libraries(tests PRIVATE lob gtest gtest_main)

include(GoogleTest)
gtest_discover_tests(tests)
