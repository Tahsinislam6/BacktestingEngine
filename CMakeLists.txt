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
add_library(engine src/engine.cpp src/loader.cpp src/portfolio.cpp src/backtester.cpp src/timestamp.cpp src/aligned.cpp src/multi_backtester.cpp src/metrics.cpp src/swap.cpp)
target_include_directories(engine PUBLIC include)

# Everything below is only built when this is the top-level project. When another project pulls the engine in
# (FetchContent / add_subdirectory) it gets just the `engine` library above, without our program and tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # The program you run
  add_executable(backtester src/main.cpp)
  target_link_libraries(backtester PRIVATE engine)

  # Google Test (downloaded automatically during the first configure)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp)
  add_executable(engine_tests ${TEST_SOURCES})
  target_link_libraries(engine_tests PRIVATE engine GTest::gtest_main)
  target_compile_definitions(engine_tests PRIVATE PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  include(GoogleTest)
  gtest_discover_tests(engine_tests)
endif()
