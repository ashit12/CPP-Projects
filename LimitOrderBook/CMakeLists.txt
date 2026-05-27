cmake_minimum_required(VERSION 3.20)
project(LimitOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so clangd / VS Code picks up -std=c++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(include)

# Core library
add_library(lob
    src/order_book.cpp
    src/matching_engine.cpp
)

# Benchmark executable
add_executable(bench bench/bench_main.cpp)
target_link_libraries(bench PRIVATE lob)

# Test executable
add_executable(tests tests/test_main.cpp)
target_link_libraries(tests PRIVATE lob)
