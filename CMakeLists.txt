cmake_minimum_required(VERSION 3.22)

option(ZFP_BUILD_PLUGIN "プラグイン（VST3 / AU）を作る。JUCE を取得する" OFF)

# macOS は Intel と Apple シリコンの両対応（Universal）。project() より前に決める必要がある。
if(ZFP_BUILD_PLUGIN AND APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(zero_fb_pure VERSION 0.1.0 LANGUAGES C CXX)

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
set_target_properties(zfp_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

# ---- プラグイン（VST3 / AU）。SPEC §1、§6、§7
if(ZFP_BUILD_PLUGIN)
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)

  set(ZFP_FORMATS VST3)
  if(APPLE)
    list(APPEND ZFP_FORMATS AU)
  endif()

  juce_add_plugin(ZeroFbPure
    PRODUCT_NAME "ZERO-FB PURE"
    COMPANY_NAME "SEVENTHWELL"
    COMPANY_WEBSITE "https://seventh-well.com"
    BUNDLE_ID "com.seventhwell.zerofbpure"
    PLUGIN_MANUFACTURER_CODE Svwl
    PLUGIN_CODE Zfbp
    FORMATS ${ZFP_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Restoration
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

  juce_add_binary_data(ZfpAssets SOURCES
    assets/fonts/SpaceMono-Regular.ttf
    assets/fonts/SpaceMono-Bold.ttf
    assets/fonts/NotoSansJP-Regular-subset.ttf)
  set_target_properties(ZfpAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

  target_sources(ZeroFbPure PRIVATE
    source/plugin/PluginProcessor.cpp
    source/ui/PluginEditor.cpp)
  target_include_directories(ZeroFbPure PRIVATE source)
  target_compile_definitions(ZeroFbPure PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
  target_link_libraries(ZeroFbPure
    PRIVATE
      zfp_dsp
      ZfpAssets
      juce::juce_audio_processors
      juce::juce_gui_basics
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  # 画面を画像に書き出す確認用の道具（配布物には入れない）。
  option(ZFP_BUILD_UI_SNAPSHOT "画面を PNG に書き出す確認用プログラムを作る" OFF)
  if(ZFP_BUILD_UI_SNAPSHOT)
    # プラグインと同じソースを、ホストなしで直接組み込む。
    juce_add_console_app(zfp_ui_snapshot PRODUCT_NAME "zfp_ui_snapshot")
    target_sources(zfp_ui_snapshot PRIVATE
      tools/UiSnapshot.cpp
      source/plugin/PluginProcessor.cpp
      source/ui/PluginEditor.cpp)
    target_include_directories(zfp_ui_snapshot PRIVATE source)
    target_compile_definitions(zfp_ui_snapshot PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      JUCE_MODAL_LOOPS_PERMITTED=1
      JucePlugin_Name="ZERO-FB PURE"
      JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(zfp_ui_snapshot PRIVATE
      zfp_dsp
      ZfpAssets
      juce::juce_audio_processors
      juce::juce_gui_basics
      juce::juce_recommended_config_flags)
  endif()
endif()
