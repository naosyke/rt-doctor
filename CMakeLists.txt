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
  src/trace_event.cpp
  src/analyzer.cpp
  src/ftrace.cpp
  src/trace_report.cpp
  src/html_report.cpp
  src/i18n.cpp
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
add_executable(rtdoctor_tests tests/test_checks.cpp tests/test_system_view.cpp tests/test_trace.cpp tests/test_html_report.cpp)
target_link_libraries(rtdoctor_tests PRIVATE rtdoctor_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(rtdoctor_tests)

# The real system: whatever the result, the command must run and produce JSON.
add_test(NAME check_runs_on_this_system COMMAND sh -c "$<TARGET_FILE:rtdoctor> check --json > /dev/null; test $? -le 1")

# End-to-end: a periodic task shares CPU 1 with a CPU hog; rtdoctor trace must
# name the hog as what delays it. Needs ftrace (root; in a container --privileged),
# skipped otherwise.
add_executable(periodic_task tests/workloads/periodic_task.cpp)
add_executable(cpu_hog tests/workloads/cpu_hog.cpp)
add_test(NAME trace_names_the_task_that_delays_the_target
  COMMAND sh -c "test -w /sys/kernel/tracing/instances 2>/dev/null || mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || exit 77; \
    $<TARGET_FILE:cpu_hog> 1 6 & \
    $<TARGET_FILE:periodic_task> 1 10 2 6 & \
    sleep 0.5; $<TARGET_FILE:rtdoctor> trace --comm periodic --duration 3 --deadline-ms 4 > trace.txt; status=$?; wait; \
    cat trace.txt; test $status -eq 0 && grep -q 'Thread periodic' trace.txt && grep -A2 'Who delayed it' trace.txt | grep -q 'task hog'")
set_tests_properties(trace_names_the_task_that_delays_the_target PROPERTIES SKIP_RETURN_CODE 77)

# The same scenario as one HTML report: checks plus the trace, naming the hog.
add_test(NAME report_writes_checks_and_trace
  COMMAND sh -c "test -w /sys/kernel/tracing/instances 2>/dev/null || mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || exit 77; \
    $<TARGET_FILE:cpu_hog> 1 6 & \
    $<TARGET_FILE:periodic_task> 1 10 2 6 & \
    sleep 0.5; $<TARGET_FILE:rtdoctor> report -o report.html --comm periodic --duration 3 --deadline-ms 4; status=$?; wait; \
    test $status -eq 0 && grep -q 'System checks' report.html && grep -q 'Thread periodic' report.html && grep -q 'task hog' report.html")
set_tests_properties(report_writes_checks_and_trace PROPERTIES SKIP_RETURN_CODE 77)
