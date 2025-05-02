cmake_minimum_required(VERSION 3.10)
project(ts-live LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
#add_compile_options(-matomics -mbulk-memory -std=c++17)

find_package(Threads)
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
FetchContent_Declare(
  tsreadex
  GIT_REPOSITORY https://github.com/xtne6f/tsreadex.git
  GIT_TAG d2a2ae460c98bc7e463b60c03edc1c9380208a00
  PATCH_COMMAND patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/tsreadex-cmake.patch && patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/tsreadex-copy-audio.patch || true
)
set(CMAKE_C_FLAGS "-matomics -mbulk-memory -msimd128 -funroll-loops")
set(CMAKE_CXX_FLAGS "-matomics -mbulk-memory -msimd128 -funroll-loops")
set(EXTRA_CFLAGS "-Wno-error=declaration-after-statement")
FetchContent_MakeAvailable(fmt spdlog tsreadex)

set(FFMPEG_CONFIGURE_COMMAND_LIST
  emconfigure ./configure
    --prefix=${CMAKE_CURRENT_BINARY_DIR}/install
    --target-os=none
    --arch=wasm
    --enable-cross-compile
    --disable-x86asm
    --disable-inline-asm
    --nm=emnm
    --ar=emar
    --ranlib=emranlib
    --cc=emcc
    --cxx=emcc
    --objcc=emcc
    --dep-cc=emcc
    --extra-cflags="-msimd128 -matomics -mbulk-memory -funroll-loops"
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
  BUILD_COMMAND ${CMAKE_COMMAND} -E env emmake make -j24
  INSTALL_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-prefix/src/ffmpeg-stamp/ffmpeg-done ] || make install"
  # PATCH_COMMAND patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-wasm.patch
)


set(FFMPEG_LIBRARIES
  ${CMAKE_BINARY_DIR}/install/lib/libavcodec.a
  ${CMAKE_BINARY_DIR}/install/lib/libavformat.a
  ${CMAKE_BINARY_DIR}/install/lib/libavutil.a
  ${CMAKE_BINARY_DIR}/install/lib/libavfilter.a
  ${CMAKE_BINARY_DIR}/install/lib/libswresample.a
)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*/hpp)
file(GLOB SHADERS src/video/shaders/*.wgsl)

add_executable(ts-live ${SOURCES} ${HEADERS} ${SHADERS} src/audio/processor.js)
# set_target_properties(ts-live PROPERTIES OUTPUT_NAME ts-live)
add_dependencies(ts-live ffmpeg)
target_compile_options(ts-live PUBLIC -matomics -mbulk-memory)
target_compile_features(ts-live PRIVATE cxx_std_20)
target_include_directories(ts-live PRIVATE ${CMAKE_BINARY_DIR}/install/include ${CMAKE_BINARY_DIR}/ffmpeg-prefix/src/ffmpeg)
target_link_libraries(ts-live fmt::fmt spdlog::spdlog ${FFMPEG_LIBRARIES} embind tsreadex::lib)
target_link_options(ts-live PRIVATE
  "-Wno-pthreads-mem-growth"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s WASM=1"
  "SHELL:-s USE_PTHREADS=1"
  "SHELL:-s USE_SDL=0"
  "SHELL:-s USE_WEBGPU=1"
  "SHELL:-s FETCH=1"
  "SHELL:-s INITIAL_MEMORY=174063616"
  "SHELL:-s ENVIRONMENT=web,worker"
  "SHELL:-s MODULARIZE=1 -s EXPORT_NAME=createWasmModule"
  "SHELL:-s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=0"
  "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/processor.js@/processor.js"
  )

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  target_link_options(ts-live PRIVATE
    "SHELL:-s ASSERTIONS=2"
    "SHELL:-s EXCEPTION_DEBUG=1"
    "SHELL:-s DEMANGLE_SUPPORT=1"
  )
else()
  target_link_options(ts-live PRIVATE
  "SHELL:-s ASSERTIONS=0"
  "SHELL:-s EXCEPTION_DEBUG=0"
  "SHELL:-s DEMANGLE_SUPPORT=0"
  )
endif()

install(TARGETS ts-live DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/ts-live.wasm DESTINATION .)
