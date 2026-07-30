cmake_minimum_required(VERSION 3.22)
project(dejaview VERSION 0.1.0 LANGUAGES CXX)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)

include(FetchContent)

FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)

FetchContent_MakeAvailable(googletest json)

add_library(dejaview_core src/version.cpp src/scanner.cpp
        src/hashing.cpp src/exact_dedup.cpp src/decoder.cpp
        src/phash.cpp src/matcher.cpp src/features.cpp
        src/export.cpp src/model.cpp)
target_include_directories(dejaview_core PUBLIC include)
target_link_libraries(dejaview_core PRIVATE JPEG::JPEG PNG::PNG nlohmann_json::nlohmann_json)

add_executable(dejaview apps/cli/main.cpp)
target_link_libraries(dejaview PRIVATE dejaview_core)

enable_testing()
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
add_executable(dejaview_tests tests/version_test.cpp
        tests/scanner_test.cpp tests/exact_dedup_test.cpp
        tests/decoder_test.cpp tests/phash_test.cpp
        tests/matcher_test.cpp tests/features_test.cpp tests/model_test.cpp)
target_link_libraries(dejaview_tests PRIVATE dejaview_core GTest::gtest_main)
target_compile_definitions(dejaview_tests PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
include(GoogleTest)
gtest_discover_tests(dejaview_tests)

