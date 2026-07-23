cmake_minimum_required(VERSION 3.22)
project(dejaview VERSION 0.1.0 LANGUAGES CXX)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra)

add_library(dejaview_core src/version.cpp src/scanner.cpp)
target_include_directories(dejaview_core PUBLIC include)

add_executable(dejaview apps/cli/main.cpp)
target_link_libraries(dejaview PRIVATE dejaview_core)

enable_testing()
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
add_executable(dejaview_tests tests/version_test.cpp tests/scanner_test.cpp)
target_link_libraries(dejaview_tests PRIVATE dejaview_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(dejaview_tests)
