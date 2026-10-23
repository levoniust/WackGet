cmake_minimum_required(VERSION 3.21)
project(WackGet VERSION 2.0.0 LANGUAGES C)

option(WG_BUNDLED_CURL "Build libcurl from pinned source (default on Windows)" ${WIN32})
option(WG_WERROR "Treat warnings as errors (CI)" OFF)
option(WG_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (gcc/clang)" OFF)
option(WG_FUZZ "Build libFuzzer targets (clang only)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/hardening.cmake)
include(cmake/curl.cmake)

if(WG_FUZZ) # instrument the core too, not just the fuzz entry points
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------- core library
add_library(wgcore STATIC
  core/src/config.c
  core/src/dl.c
  core/src/fs.c
  core/src/mem.c
  core/src/queue.c
  core/src/text.c
  core/src/url.c)
target_include_directories(wgcore PUBLIC core/include)
target_link_libraries(wgcore PUBLIC CURL::libcurl)
if(WIN32)
  target_compile_definitions(wgcore PUBLIC UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(wgcore PUBLIC shell32 ole32 uuid)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(wgcore PUBLIC Threads::Threads)
endif()
wg_harden(wgcore)

# ---------------------------------------------------------------- command-line downloader (testing aid)
add_executable(wgdl tools/wgdl.c)
target_link_libraries(wgdl PRIVATE wgcore)
wg_harden(wgdl)

# ---------------------------------------------------------------- tests
enable_testing()

add_executable(wg_unit tests/unit.c)
target_link_libraries(wg_unit PRIVATE wgcore)
wg_harden(wg_unit)
add_test(NAME unit COMMAND wg_unit WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_executable(wg_engine tests/engine.c)
  target_link_libraries(wg_engine PRIVATE wgcore)
  wg_harden(wg_engine)
  add_test(NAME engine
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_engine.py $<TARGET_FILE:wg_engine>
           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(engine PROPERTIES TIMEOUT 300)
endif()

# Fuzz targets: libFuzzer with clang (WG_FUZZ), otherwise a replay driver that runs the seed corpus
# plus random inputs, so every compiler exercises them in `ctest`.
foreach(target queue html text name)
  if(WG_FUZZ)
    add_executable(fuzz_${target} fuzz/fuzz_${target}.c)
    target_compile_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  else()
    add_executable(fuzz_${target} fuzz/fuzz_${target}.c fuzz/replay.c)
    wg_harden(fuzz_${target})
    add_test(NAME fuzz_replay_${target}
             COMMAND fuzz_${target} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/${target})
  endif()
  target_link_libraries(fuzz_${target} PRIVATE wgcore)
endforeach()
