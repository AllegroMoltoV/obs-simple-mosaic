cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# C++17 を要求する
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# src/ と data/ を自動で拾う (追加のたびに CMakeLists.txt を編集しないため)
file(
  GLOB PLUGIN_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

file(
  GLOB_RECURSE PLUGIN_RESOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/data/*.effect"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/*.ini"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

# Solution Explorer のツリー表示を実ディレクトリに揃える
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# 描画のテスト用の実行ファイル。Windows では常にビルドし、macOS と Linux では CI の描画検証 (render-test.yaml) で有効にする
# - mosaic-integration-test: モザイクの出力画素を検査する統合テスト
# - mosaic-benchmark: 1920x1080 の GPU 時間を測る。環境で値が変わるため CTest には登録しない
option(ENABLE_RENDER_TESTS "Build the render tests and benchmark on macOS and Linux" OFF)
if(WIN32 OR ENABLE_RENDER_TESTS)
  add_executable(mosaic-integration-test tests/integration-test.cpp)
  add_executable(mosaic-benchmark tests/benchmark.cpp)
  foreach(_test_target IN ITEMS mosaic-integration-test mosaic-benchmark)
    target_compile_features(${_test_target} PRIVATE cxx_std_17)
    target_link_libraries(${_test_target} PRIVATE OBS::libobs)
    add_dependencies(${_test_target} ${CMAKE_PROJECT_NAME})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      find_package(X11 REQUIRED)
      target_link_libraries(${_test_target} PRIVATE X11::X11)
    endif()
  endforeach()
endif()

if(WIN32)
  enable_testing()

  # 統合テストを D3D11 と OpenGL の両方で実行する。グラフィックスの DLL は OBS のビルドで
  # cmake --build .deps/obs-studio-31.1.1/build_x64 --config Release --target libobs-d3d11 libobs-opengl を実行して作る
  set(_obs_build "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64")
  set(_graphics_d3d11 "${_obs_build}/libobs-d3d11/Release/libobs-d3d11.dll")
  set(_graphics_opengl "${_obs_build}/libobs-opengl/Release/libobs-opengl.dll")
  set(
    _scenarios
    default-full
    region
    overflow
    large-cell
    zero-size
    resize-keeps-px
    translucent
    scene
    settings-update
  )
  foreach(_backend IN ITEMS d3d11 opengl)
    foreach(_scenario IN LISTS _scenarios)
      set(_test_name mosaic-${_backend}-${_scenario})
      add_test(
        NAME ${_test_name}
        COMMAND
          mosaic-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_SOURCE_DIR}/data"
          "${_graphics_${_backend}}" "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" ${_scenario}
      )
      set_tests_properties(
        ${_test_name}
        PROPERTIES
          ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/.deps/bin/64bit;PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2025-07-11-x64/bin"
      )
    endforeach()
  endforeach()
endif()

# ビルド直後に cmake --install を実行する (Windows 開発用)。導入先の %PROGRAMDATA%\obs-studio\plugins は
# ほかのプラグインや並行して動く OBS と共有のため、既定では導入しない
option(ENABLE_POST_BUILD_INSTALL "Run cmake --install after build" OFF)

if(WIN32 AND ENABLE_POST_BUILD_INSTALL)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    COMMENT "Installing OBS plugin (cmake --install)"
    VERBATIM
  )
endif()
