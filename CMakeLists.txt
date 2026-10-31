cmake_minimum_required(VERSION 3.16)
project(UnifiedWorkbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Explicit release flags so they can be reported in the Performance view.
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
string(TOUPPER "${CMAKE_BUILD_TYPE}" WB_BT)
set(WB_BUILD_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${WB_BT}}")
string(STRIP "${WB_BUILD_FLAGS}" WB_BUILD_FLAGS)

add_compile_options(-Wall -Wextra -Wpedantic)
# Windows 10+ API level (needed by cpp-httplib and GetLogicalProcessorInformationEx).
add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
set(WB_STATIC_LINK -static -static-libgcc -static-libstdc++)

# --- core library: no HTTP/UI code -------------------------------------------
add_library(wb_core STATIC
  src/core/sysinfo.cpp src/core/fea1d.cpp src/core/fea_study.cpp src/core/svg_plot.cpp
  src/core/fileio.cpp src/core/jobs.cpp src/core/backprop.cpp src/core/thread_pool.cpp src/core/batch.cpp
  src/core/stl_parser.cpp src/core/csv_nodes.cpp)
target_include_directories(wb_core PUBLIC src/core third_party/json)
find_package(Threads REQUIRED)
target_link_libraries(wb_core PUBLIC Threads::Threads)
target_compile_definitions(wb_core PRIVATE WB_BUILD_FLAGS="${WB_BUILD_FLAGS}"
                                           WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

# --- Launch.exe: GUI-subsystem HTTP server + browser launcher -----------------
add_executable(Launch WIN32 src/server/main.cpp src/server/api_compute.cpp src/server/api_geometry.cpp)
target_include_directories(Launch PRIVATE third_party/httplib src/server)
target_link_libraries(Launch PRIVATE wb_core ws2_32 ${WB_STATIC_LINK})
set_target_properties(Launch PROPERTIES OUTPUT_NAME Launch)

# --- headless CLI that regenerates results/ -----------------------------------
add_executable(workbench_cli src/cli/main.cpp)
target_link_libraries(workbench_cli PRIVATE wb_core ${WB_STATIC_LINK})

# --- tests --------------------------------------------------------------------
enable_testing()
add_executable(wb_tests tests/test_main.cpp tests/test_sysinfo.cpp tests/test_fea.cpp tests/test_backprop.cpp tests/test_batch.cpp tests/test_geometry.cpp)
target_include_directories(wb_tests PRIVATE third_party/doctest)
# Build-time only: test fixtures are located from the source tree.
target_compile_definitions(wb_tests PRIVATE WB_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
                                            WB_SAMPLE_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(wb_tests PRIVATE wb_core ${WB_STATIC_LINK})
add_test(NAME wb_tests COMMAND wb_tests)
