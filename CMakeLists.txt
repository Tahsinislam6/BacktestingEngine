cmake_minimum_required(VERSION 3.14)
project(BacktestingEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Your code lives in a library so both the app and the tests can use it
add_library(engine src/engine.cpp src/loader.cpp src/portfolio.cpp)
target_include_directories(engine PUBLIC include)

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
add_executable(engine_tests tests/engine_test.cpp tests/loader_test.cpp tests/trade_test.cpp tests/portfolio_test.cpp tests/order_test.cpp)
target_link_libraries(engine_tests PRIVATE engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(engine_tests)
