cmake_minimum_required(VERSION 3.22)
project(zero_fb_pure VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "ビルドの種類" FORCE)
endif()

option(ZFP_BUILD_TESTS "テストと計測用プログラムを作る" ON)

# 計算の順序や融合（FMA）をコンパイラに勝手に変えさせない。
# 「ブロック長を変えても結果が完全に一致」「バイパス時にビット完全一致」を、どの環境でも保つため。
if(MSVC)
  add_compile_options(/utf-8 /fp:precise)
else()
  add_compile_options(-ffp-contract=off)
endif()

# ---- DSP コア（JUCE に依存しない）
add_library(zfp_dsp STATIC
  source/dsp/Analyzer.cpp
  source/dsp/BandLayout.cpp
  source/dsp/Decoupler.cpp
  source/dsp/Engine.cpp
  source/dsp/Fft.cpp)
target_include_directories(zfp_dsp PUBLIC source)

if(MSVC)
  target_compile_options(zfp_dsp PRIVATE /W4)
else()
  target_compile_options(zfp_dsp PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(ZFP_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(zfp_tests
    tests/AllocCounter.cpp
    tests/ControlTests.cpp
    tests/LayoutAndDecouplerTests.cpp
    tests/ZeroLatencyTests.cpp)
  target_link_libraries(zfp_tests PRIVATE zfp_dsp Catch2::Catch2WithMain)

  add_executable(zfp_bench bench/Bench.cpp)
  target_link_libraries(zfp_bench PRIVATE zfp_dsp)

  add_executable(zfp_loop bench/LoopExplore.cpp)
  target_link_libraries(zfp_loop PRIVATE zfp_dsp)

  enable_testing()
  add_test(NAME zfp_tests COMMAND zfp_tests)
endif()
