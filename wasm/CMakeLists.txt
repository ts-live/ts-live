cmake_minimum_required(VERSION 3.10)
project(ts-wasm LANGUAGES CXX C)

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
set(BENCHMARK_ENABLE_TESTING OFF)
set(CMAKE_C_FLAGS "-matomics -mbulk-memory -msimd128 -funroll-loops")
set(CMAKE_CXX_FLAGS "-matomics -mbulk-memory -msimd128 -funroll-loops")
set(EXTRA_CFLAGS "-Wno-error=declaration-after-statement")
FetchContent_MakeAvailable(fmt spdlog)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools)
file(TO_NATIVE_PATH ${CMAKE_COMMAND} native_cmake_command)
file(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/halide native_cmake_current_source_dir)
execute_process(
    COMMAND "sh" "-c" "CC= CXX= CMAKE_CROSSCOMPILING= ${native_cmake_command} -G\"${CMAKE_GENERATOR}\" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} ${native_cmake_current_source_dir}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools
)

add_custom_target(generator
    COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG> --parallel 24
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/host_tools
)
include(${CMAKE_CURRENT_BINARY_DIR}/host_tools/host_tools.cmake)

# include(${CMAKE_CURRENT_BINARY_DIR}/host_tools/_deps/halide-src/cmake/HalideTargetHelpers.cmake)
# include(${CMAKE_CURRENT_BINARY_DIR}/host_tools/_deps/halide-src/cmake/HalideGeneratorHelpers.cmake)
# set(HALIDE_TARGET_WASM "wasm-32-wasmrt-wasm_bulk_memory-wasm_simd128-wasm_sat_float_to_int-wasm_signext")
# add_halide_library(deinterlace_wasm FROM dif_gen GENERATOR deinterlace TARGETS ${HALIDE_TARGET_WASM} AUTOSCHEDULER Halide::Adams2019 PARAMS auto_schedule=true)

add_dependencies(dif_gen generator)
make_directory(${CMAKE_CURRENT_BINARY_DIR}/halide_lib)
add_custom_target(deinterlace_wasm
  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/host_tools/dif_gen
    -g deinterlace
    -o ${CMAKE_CURRENT_BINARY_DIR}/halide_lib
    -p ${CMAKE_CURRENT_BINARY_DIR}/host_tools/_deps/halide-build/src/autoschedulers/adams2019/libautoschedule_adams2019.so
    -s Adams2019
    target=wasm-32-wasmrt-wasm_bulk_memory-wasm_simd128-wasm_sat_float_to_int-wasm_signext-wasm_threads
)
add_dependencies(deinterlace_wasm dif_gen)

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
    --enable-filter=yadif
    --enable-filter=vflip
    --enable-filter=bwdif
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

add_executable(ffmpeg-sdl2 ffmpeg-sdl2.cpp yadif_replace.c halide-yadif.cpp audio/audioworklet.cpp video/webgpu.cpp)
# set_target_properties(ffmpeg-sdl2 PROPERTIES OUTPUT_NAME ffmpeg-sdl2)
add_dependencies(ffmpeg-sdl2 ffmpeg)
target_compile_options(ffmpeg-sdl2 PUBLIC -matomics -mbulk-memory)
target_compile_features(ffmpeg-sdl2 PRIVATE cxx_std_20)
target_include_directories(ffmpeg-sdl2 PRIVATE ${CMAKE_BINARY_DIR}/install/include ${CMAKE_BINARY_DIR}/ffmpeg-prefix/src/ffmpeg ${CMAKE_CURRENT_BINARY_DIR}/halide_lib ${CMAKE_CURRENT_BINARY_DIR}/host_tools/_deps/halide-build/include/)
target_link_libraries(ffmpeg-sdl2 fmt::fmt spdlog::spdlog ${FFMPEG_LIBRARIES} ${CMAKE_CURRENT_BINARY_DIR}/halide_lib/deinterlace.a)
add_dependencies(ffmpeg-sdl2 deinterlace_wasm)
target_link_options(ffmpeg-sdl2 PRIVATE
  "-Wno-pthreads-mem-growth"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s WASM=1"
  "SHELL:-s USE_PTHREADS=1"
  # "SHELL:-s PROXY_TO_PTHREAD=1"
  "SHELL:-s ASSERTIONS=1"
  "SHELL:-s USE_SDL=0"
  "SHELL:-s USE_WEBGPU=1"
  "SHELL:-s FETCH=1"
  "SHELL:-s TOTAL_MEMORY=39321600"
  "SHELL:-s ENVIRONMENT=web,worker"
  # "SHELL:-s EXPORTED_FUNCTIONS='[\"_main\"]'"
  # "SHELL:-s EXPORTED_RUNTIME_METHODS='[\"ccall\"]'"
  "SHELL:--bind"
  "SHELL:-s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=0"
  "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/audio/processor.js@/processor.js"
  "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/video/shaders@/shaders"
  )
install(TARGETS ffmpeg-sdl2 DESTINATION .)
install(FILES ${CMAKE_BINARY_DIR}/ffmpeg-sdl2.wasm ${CMAKE_BINARY_DIR}/ffmpeg-sdl2.worker.js DESTINATION .)
