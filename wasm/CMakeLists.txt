cmake_minimum_required(VERSION 3.10)
project(ts-wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.9.2
)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 8.0.1
)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(fmt spdlog)

set(FFMPEG_CONFIGURE_COMMAND_LIST
  emconfigure ./configure
    --prefix=${CMAKE_CURRENT_BINARY_DIR}/install
    --target-os=none
    --arch=x86_32
    --enable-cross-compile
    --disable-x86asm
    --disable-inline-asm
    --nm=emnm
    --ar=${CMAKE_C_COMPILER_AR}
    --ranlib=${CMAKE_C_COMPILER_RANLIB}
    --cc=${CMAKE_C_COMPILER}
    --cxx=${CMAKE_CXX_COMPILER}
    --objcc=${CMAKE_C_COMPILER}
    --dep-cc=${CMAKE_C_COMPILER}
    --disable-programs
    --disable-doc
    --disable-avdevice
    --disable-network
    --disable-encoders
    --disable-decoders
    --enable-decoder=aac
    --enable-decoder=mpeg2video
    --disable-hwaccels
    --disable-muxers
    --disable-demuxers
    --enable-demuxer=mpegts
    --enable-demuxer=adts
    --disable-parsers
    --enable-parser=aac
    --enable-parser=mpegvideo
    --disable-bsfs
    --disable-protocols
    --disable-devices
    --disable-filters
)
list(JOIN FFMPEG_CONFIGURE_COMMAND_LIST " " FFMPEG_CONFIGURE_COMMAND)

include(ExternalProject)
ExternalProject_Add(
  ffmpeg
  GIT_REPOSITORY https://git.ffmpeg.org/ffmpeg.git
  GIT_TAG n5.0
  BUILD_IN_SOURCE TRUE
  INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/install
  CONFIGURE_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-prefix/src/ffmpeg-stamp/ffmpeg-done ] || ${FFMPEG_CONFIGURE_COMMAND}"
  BUILD_COMMAND ${CMAKE_COMMAND} -E env emmake make
  INSTALL_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-prefix/src/ffmpeg-stamp/ffmpeg-done ] || make install"
)


add_executable(ts-splitter ts-splitter.cpp)
target_link_libraries(ts-splitter fmt::fmt spdlog::spdlog)
target_link_options(ts-splitter PRIVATE
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s WASM=1"
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s USE_PTHREADS=0"
  "SHELL:-s EXPORT_NAME=createSplitterWasmModule"
  "SHELL:--bind"
)

install(TARGETS ts-splitter DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/ts-splitter.wasm DESTINATION .)

set(FFMPEG_LIBRARIES
  ${CMAKE_BINARY_DIR}/install/lib/libavcodec.a
  ${CMAKE_BINARY_DIR}/install/lib/libavformat.a
  ${CMAKE_BINARY_DIR}/install/lib/libavutil.a
)

add_executable(ffmpeg-wasm ffmpeg-wasm.cpp)
add_dependencies(ffmpeg-wasm ffmpeg)
target_include_directories(ffmpeg-wasm PRIVATE ${CMAKE_BINARY_DIR}/install/include)
target_link_libraries(ffmpeg-wasm fmt::fmt spdlog::spdlog ${FFMPEG_LIBRARIES})
target_link_options(ffmpeg-wasm PRIVATE
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s WASM=1"
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s USE_PTHREADS=0"
  "SHELL:-s TOTAL_MEMORY=19660800"
  "SHELL:-s EXPORT_NAME=createFFmpegWasmModule"
  "SHELL:--bind"
)
install(TARGETS ffmpeg-wasm DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/ffmpeg-wasm.wasm DESTINATION .)

add_executable(ffmpeg-wasm-es6 ffmpeg-wasm.cpp)
set_target_properties(ffmpeg-wasm-es6 PROPERTIES OUTPUT_NAME ffmpeg)
add_dependencies(ffmpeg-wasm-es6 ffmpeg)
target_include_directories(ffmpeg-wasm-es6 PRIVATE ${CMAKE_BINARY_DIR}/install/include)
target_link_libraries(ffmpeg-wasm-es6 fmt::fmt spdlog::spdlog ${FFMPEG_LIBRARIES})
target_link_options(ffmpeg-wasm-es6 PRIVATE
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s WASM=1"
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s USE_PTHREADS=0"
  "SHELL:-s TOTAL_MEMORY=19660800"
  "SHELL:-s ENVIRONMENT=web,worker"
  "SHELL:-s EXPORT_ES6=1"
  "SHELL:--bind"
)
install(TARGETS ffmpeg-wasm-es6 DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../lib)
install(FILES ${CMAKE_BINARY_DIR}/ffmpeg.wasm DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../lib)
