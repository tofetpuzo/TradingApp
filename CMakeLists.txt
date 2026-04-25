# TODO: Story 1 (#18) - Set up CMake project with FetchContent dependencies
# C++20, nlohmann/json, spdlog, Google Test, cpp-httplib
cmake_minimum_required(VERSION 3.23)

project(
  TradingApp
  VERSION 0.1.0
  DESCRIPTION "High-performance algorithmic trading platform"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit and integration tests" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

# ── Compiler warnings (applied per-target, not globally) ────────────────────
# Global flags leak into FetchContent deps and break MSVC builds. We define an
# INTERFACE library and link our targets to it instead.
add_library(project_warnings INTERFACE)
target_compile_options(
  project_warnings
  INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

if(ENABLE_ASAN AND NOT MSVC)
  target_compile_options(project_warnings INTERFACE -fsanitize=address)
  target_link_options(project_warnings INTERFACE -fsanitize=address)
endif()

# ── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# nlohmann/json — header-only JSON library (replaces json11)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)

# spdlog — fast logging
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.0
  GIT_SHALLOW TRUE)

# Google Test
if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
endif()

# FTXUI — terminal UI library
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nlohmann_json spdlog ftxui)
if(BUILD_TESTS)
  FetchContent_MakeAvailable(googletest)
endif()

# ── Include paths ────────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ── Libraries ────────────────────────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/utils)
add_subdirectory(src/data)
add_subdirectory(src/network)
add_subdirectory(src/exchange)
add_subdirectory(src/engine)
add_subdirectory(src/strategy)
add_subdirectory(src/risk)
add_subdirectory(src/portfolio)
add_subdirectory(src/ui)

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(trading_app src/main.cpp)
target_link_libraries(
  trading_app
  PRIVATE core
          engine
          strategy
          risk
          data_feed
          network
          exchange
          portfolio
          utils
          ui
          nlohmann_json::nlohmann_json
          spdlog::spdlog
          project_warnings)

# ── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
