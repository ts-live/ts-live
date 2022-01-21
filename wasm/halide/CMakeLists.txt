cmake_minimum_required(VERSION 3.16)
project(halide-gen)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  Halide
  GIT_REPOSITORY https://github.com/halide/Halide.git
  GIT_TAG v13.0.3
)
set(Halide_SHARED_LLVM YES)
FetchContent_MakeAvailable(Halide)



# find_package(Halide)
# find_package(HalideHelpers)
include(${Halide_SOURCE_DIR}/cmake/HalideTargetHelpers.cmake)
include(${Halide_SOURCE_DIR}/cmake/HalideGeneratorHelpers.cmake)

add_executable(dif_gen dif.cpp)
target_link_libraries(dif_gen PRIVATE Halide::Generator)

set(HALIDE_TARGET_WASM "wasm-32-wasmrt-wasm_bulk_memory-wasm_simd128-wasm_sat_float_to_int-wasm_signext")
add_halide_library(deinterlace.wasm FROM dif_gen_gen GENERATOR deinterlace TARGETS ${HALIDE_TARGET} AUTOSCHEDULER Halide::Adams2019 PARAMS auto_schedule=true)


export(
  TARGETS
  deinterlace.wasm deinterlace.wasm.runtime
  FILE host_tools.cmake)
