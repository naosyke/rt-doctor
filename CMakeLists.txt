cmake_minimum_required(VERSION 3.16)
project(rt_doctor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# A single static binary is easy to copy onto a robot or embedded target.
option(RTDOCTOR_STATIC "Link rtdoctor statically" OFF)

add_library(rtdoctor_core
  src/system_view.cpp
  src/checks.cpp
  src/output.cpp
)
target_include_directories(rtdoctor_core PUBLIC include)
target_compile_options(rtdoctor_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(rtdoctor src/main.cpp)
target_link_libraries(rtdoctor PRIVATE rtdoctor_core)
target_compile_definitions(rtdoctor PRIVATE RTDOCTOR_VERSION="${PROJECT_VERSION}")
target_compile_options(rtdoctor PRIVATE -Wall -Wextra)
if(RTDOCTOR_STATIC)
  target_link_options(rtdoctor PRIVATE -static)
endif()

enable_testing()
find_package(GTest REQUIRED)
add_executable(rtdoctor_tests tests/test_checks.cpp tests/test_system_view.cpp)
target_link_libraries(rtdoctor_tests PRIVATE rtdoctor_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(rtdoctor_tests)

# The real system: whatever the result, the command must run and produce JSON.
add_test(NAME check_runs_on_this_system COMMAND sh -c "$<TARGET_FILE:rtdoctor> check --json > /dev/null; test $? -le 1")
