cmake_minimum_required(VERSION 3.14)
project(BacktestingEngine CXX)

# Default to an optimized build (about 14x faster than no build type).
# For debugging, configure a separate folder with -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Your code lives in a library so both the app and the tests can use it
add_library(engine src/engine.cpp src/loader.cpp src/portfolio.cpp src/backtester.cpp src/timestamp.cpp src/aligned.cpp src/multi_backtester.cpp src/metrics.cpp src/swap.cpp src/density.cpp src/parameter_grid.cpp src/null_series.cpp src/sleeve.cpp src/single_asset.cpp src/registry.cpp src/buy_and_hold.cpp src/job.cpp src/job_json.cpp src/sweep_job.cpp src/sweep_job_json.cpp)
target_include_directories(engine PUBLIC include)

# JSON for the job runner (the request and result text the web app exchanges).
include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

# The local web server (HTTP API). cpp-httplib is one header; no OpenSSL, zlib or brotli is wanted.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
)
FetchContent_MakeAvailable(httplib)

add_library(engine_server src/run_store.cpp src/run_manager.cpp src/api.cpp src/http_server.cpp)
target_link_libraries(engine_server PUBLIC engine httplib::httplib)

# Everything below is only built when this is the top-level project. When another project pulls the engine in
# (FetchContent / add_subdirectory) it gets just the `engine` library above, without our program and tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # The program you run
  add_executable(backtester src/main.cpp)
  target_link_libraries(backtester PRIVATE engine)

  # The local web server (see src/server_main.cpp)
  add_executable(server src/server_main.cpp)
  target_link_libraries(server PRIVATE engine_server)

  # Runs one job described in JSON (see src/runner_main.cpp)
  add_executable(runner src/runner_main.cpp)
  target_link_libraries(runner PRIVATE engine)

  # Google Test (downloaded automatically during the first configure)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp)
  add_executable(engine_tests ${TEST_SOURCES})
  target_link_libraries(engine_tests PRIVATE engine_server GTest::gtest_main)
  target_compile_definitions(engine_tests PRIVATE PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  include(GoogleTest)
  gtest_discover_tests(engine_tests)
endif()
