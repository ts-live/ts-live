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
    --ar=${CMAKE_C_COMPILER_AR}
    --ranlib=${CMAKE_C_COMPILER_RANLIB}
    --cc=${CMAKE_C_COMPILER}
    --cxx=${CMAKE_CXX_COMPILER}
    --objcc=${CMAKE_C_COMPILER}
    --dep-cc=${CMAKE_C_COMPILER}
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
)

set(OPENFST_CONFIGURE_COMMAND_LIST
  autoreconf -i && emconfigure ./configure
  --prefix=${CMAKE_CURRENT_BINARY_DIR}/install
  --enable-static
  --disable-shared
  --enable-far
  --enable-ngram-fsts
  --enable-lookahead-fsts
  --with-pic
)
list(JOIN OPENFST_CONFIGURE_COMMAND_LIST " " OPENFST_CONFIGURE_COMMAND)

ExternalProject_Add(
  openfst
  GIT_REPOSITORY https://github.com/alphacep/openfst.git
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst-stamp/openfst-done ] || ( ${OPENFST_CONFIGURE_COMMAND} )"
  BUILD_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst-stamp/openfst-done ] || emmake make -j24"
  INSTALL_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst-stamp/openfst-done ] || make install && cp ${CMAKE_CURRENT_BINARY_DIR}/install/lib/libfst*.a ${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst/src/lib/"
  PATCH_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst-stamp/openfst-done ] || patch -N -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/openfst.patch"
)

# ExternalProject_Add(
#   openblas
#   GIT_REPOSITORY https://github.com/xianyi/OpenBLAS.git
#   GIT_TAG v0.3.19
#   BUILD_IN_SOURCE TRUE
#   INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/install
#   # CONFIGURE_COMMAND emcmake cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_CFLAGS=-DFORCE_GENERIC=1 -DTARGET=generic ../openblas
#   # BUILD_COMMAND cmake --build .
#   BUILD_COMMAND emmake make CC=emcc AR=emar LD=emcc NOFORTRAN=1 FORCE_GENERIC=1 BINARY=32 TARGET=generic NOLAPACK=1 NOSHARED=1 CROSS=1
#   INSTALL_COMMAND false
# )

ExternalProject_Add(
  clapack
  GIT_REPOSITORY https://gitlab.inria.fr/kaldi.web/clapack-wasm.git
  GIT_TAG 1.0.0
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/clapack-prefix/src/clapack-stamp/clapack-done ]" || bash -c "emmake make \\\"CFLAGS=-s EXPORTED_FUNCTIONS=MAIN__,_MAIN__,_f_exit,_f_init,_sig_die\\\" all"
  INSTALL_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/clapack-prefix/src/clapack-stamp/clapack-done ]" || bash -c "LIBDIR=${CMAKE_CURRENT_BINARY_DIR}/install/lib make install || pwd ; false"
  PATCH_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/clapack-prefix/src/clapack-stamp/clapack-done ]" || bash -c "patch -N -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/clapack-wasm.patch && patch -N -p3 < ${CMAKE_CURRENT_SOURCE_DIR}/clapack-wasm-libf2c.patch"
)


set(KALDI_CONFIGURE_COMMAND_LIST
  emconfigure ./configure
  --use-cuda=no
  --static
  --static-fst=yes
  --static-math=yes
  --clapack-root=${CMAKE_CURRENT_BINARY_DIR}/clapack-prefix/src/clapack
  --fst-root=${CMAKE_CURRENT_BINARY_DIR}/openfst-prefix/src/openfst/src
)
list(JOIN KALDI_CONFIGURE_COMMAND_LIST " " KALDI_CONFIGURE_COMMAND)

ExternalProject_Add(
  kaldi
  GIT_REPOSITORY https://github.com/ccoreilly/kaldi.git
  GIT_TAG lookahead-1.8.0
  BUILD_IN_SOURCE TRUE
  SOURCE_SUBDIR src
  CONFIGURE_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/kaldi-prefix/src/kaldi-stamp/kaldi-done ]" || bash -c "${KALDI_CONFIGURE_COMMAND}"
  BUILD_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/kaldi-prefix/src/kaldi-stamp/kaldi-done ]" || bash -c "emmake make"
  # CONFIGURE_COMMAND emcmake cmake -DCMAKE_BUILD_TYPE=Release ../kaldi
  # BUILD_COMMAND cmake --build .
  INSTALL_COMMAND "false"
  # PATCH_COMMAND bash -c "[ -e ${CMAKE_CURRENT_BINARY_DIR}/kaldi-prefix/src/kaldi-stamp/kaldi-done ]" || bash -c "patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/kaldi.patch"
)

ExternalProject_Add(
  vosk
  GIT_REPOSITORY https://github.com/alphacep/vosk-api.git
  GIT_TAG v0.3.32
  BUILD_IN_SOURCE TRUE
  SOURCE_SUBDIR src
  CONFIGURE_COMMAND ""
  BUILD_COMMAND emmake make HAVE_OPENBLAS_CLAPACK=0 KALDI_ROOT=${CMAKE_CURRENT_BINARY_DIR}/kaldi-prefix/src/kaldi OPENFST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install OUTDIR=${CMAKE_CURRENT_BINARY_DIR}/install/lib
  INSTALL_COMMAND ""
)

add_dependencies(kaldi clapack openfst)
add_dependencies(vosk openfst kaldi)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*/hpp)
file(GLOB SHADERS src/video/shaders/*.wgsl)

add_executable(ts-live ${SOURCES} ${HEADERS} ${SHADERS} src/audio/processor.js)
# set_target_properties(ts-live PROPERTIES OUTPUT_NAME ts-live)
add_dependencies(ts-live ffmpeg vosk)
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
  "SHELL:-s INITIAL_MEMORY=39321600"
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
install(FILES ${CMAKE_BINARY_DIR}/ts-live.wasm ${CMAKE_BINARY_DIR}/ts-live.worker.js DESTINATION .)
