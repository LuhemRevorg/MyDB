cmake_minimum_required(VERSION 3.20)
project(mydb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------- Dependencies ----------
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
  GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
  GIT_SHALLOW    TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# ---------- Library ----------
set(SOURCES
  src/storage/disk_manager.cpp
  src/storage/lru_replacer.cpp
  src/storage/buffer_pool.cpp
  src/storage/heap_file.cpp
  src/storage/bplus_tree.cpp
  src/parser/lexer.cpp
  src/parser/parser.cpp
  src/catalog/catalog.cpp
  src/executor/seq_scan.cpp
  src/executor/filter.cpp
  src/executor/projection.cpp
  src/executor/hash_join.cpp
  src/executor/sort.cpp
  src/executor/aggregate.cpp
  src/executor/thread_pool.cpp
  src/executor/parallel_seq_scan.cpp
  src/executor/parallel_hash_join.cpp
  src/executor/parallel_sort.cpp
  src/concurrency/lockfree_hashtable.cpp
  src/planner/planner.cpp
  src/database.cpp
)

add_library(mydb_lib ${SOURCES})
target_include_directories(mydb_lib PUBLIC src)
target_compile_options(mydb_lib PRIVATE -Wall -Wextra -Wpedantic)

# ---------- Executable ----------
add_executable(mydb src/main.cpp)
target_link_libraries(mydb PRIVATE mydb_lib)

# ---------- Tests ----------
add_executable(mydb_test
  test/storage_test.cpp
  test/parser_test.cpp
  test/executor_test.cpp
  test/concurrency_test.cpp
)
target_link_libraries(mydb_test PRIVATE mydb_lib GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(mydb_test)

# ---------- Benchmarks ----------
add_executable(mydb_bench bench/query_bench.cpp)
target_link_libraries(mydb_bench PRIVATE mydb_lib benchmark::benchmark_main)
