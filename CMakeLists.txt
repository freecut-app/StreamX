cmake_minimum_required(VERSION 3.28)
project(StreamX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# libobs e Qt 6 vêm da build do OBS Studio em deps/obs-studio (preset "streamx")
set(OBS_SRC "${CMAKE_SOURCE_DIR}/deps/obs-studio")
set(OBS_BUILD "${OBS_SRC}/build_streamx")
file(GLOB OBS_QT_DEPS LIST_DIRECTORIES true "${OBS_SRC}/.deps/obs-deps-qt6-*-x64")
file(GLOB OBS_DEPS LIST_DIRECTORIES true "${OBS_SRC}/.deps/obs-deps-20*-x64")
list(APPEND CMAKE_PREFIX_PATH ${OBS_QT_DEPS} ${OBS_DEPS})
list(APPEND CMAKE_MODULE_PATH "${OBS_SRC}/cmake/finders")
set(libobs_DIR "${OBS_BUILD}/libobs")
set(w32-pthreads_DIR "${OBS_BUILD}/deps/w32-pthreads")

find_package(libobs REQUIRED CONFIG)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# A pasta de execução segue o layout do OBS (bin/64bit, data, obs-plugins),
# que é onde o libobs procura os próprios dados e os plugins.
set(STREAMX_RUNDIR "${CMAKE_SOURCE_DIR}/rundir")

add_executable(StreamX WIN32
  src/main.cpp
  src/core/Engine.cpp src/core/Engine.h
  src/core/Log.cpp src/core/Log.h
  src/core/SceneManager.cpp src/core/SceneManager.h
  src/core/Recorder.cpp src/core/Recorder.h
  src/core/Settings.cpp src/core/Settings.h
  src/core/Encoders.cpp src/core/Encoders.h
  src/core/ClipIt.cpp src/core/ClipIt.h
  src/core/Streamer.cpp src/core/Streamer.h
  src/ui/MainWindow.cpp src/ui/MainWindow.h
  src/ui/PreviewWidget.cpp src/ui/PreviewWidget.h
  resources/streamx.qrc
)
target_include_directories(StreamX PRIVATE src)
target_compile_definitions(StreamX PRIVATE STREAMX_VERSION="${PROJECT_VERSION}" UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(StreamX PRIVATE /utf-8 /Zc:__cplusplus /permissive- /W3 /MP)
target_link_libraries(StreamX PRIVATE OBS::libobs Qt6::Widgets Qt6::Network)
set_target_properties(StreamX PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${STREAMX_RUNDIR}/bin/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STREAMX_RUNDIR}/bin/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${STREAMX_RUNDIR}/bin/64bit"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STREAMX_RUNDIR}/bin/64bit")
