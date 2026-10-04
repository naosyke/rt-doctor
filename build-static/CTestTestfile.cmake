# CMake generated Testfile for 
# Source directory: /workspace
# Build directory: /workspace/build-static
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/workspace/build-static/rtdoctor_tests[1]_include.cmake")
add_test(check_runs_on_this_system "sh" "-c" "/workspace/build-static/rtdoctor check --json > /dev/null; test \$? -le 1")
set_tests_properties(check_runs_on_this_system PROPERTIES  _BACKTRACE_TRIPLES "/workspace/CMakeLists.txt;38;add_test;/workspace/CMakeLists.txt;0;")
