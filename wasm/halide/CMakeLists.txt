cmake_minimum_required(VERSION 3.16)
project(halide-gen CXX)

enable_language(CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  Halide
  GIT_REPOSITORY https://github.com/halide/Halide.git
  GIT_TAG v13.0.4
)
set(Halide_SHARED_LLVM YES)
FetchContent_MakeAvailable(Halide)



# find_package(Halide)
# find_package(HalideHelpers)
include(${Halide_SOURCE_DIR}/cmake/HalideTargetHelpers.cmake)
include(${Halide_SOURCE_DIR}/cmake/HalideGeneratorHelpers.cmake)

add_executable(dif_gen dif.cpp)
target_link_libraries(dif_gen PRIVATE Halide::Generator)

# set(HALIDE_TARGET_WASM "wasm-32-wasmrt-wasm_bulk_memory-wasm_simd128-wasm_sat_float_to_int-wasm_signext")
# add_halide_library(deinterlace_wasm FROM dif_gen GENERATOR deinterlace TARGETS ${HALIDE_TARGET_WASM} AUTOSCHEDULER Halide::Adams2019 PARAMS auto_schedule=true)
# set_target_properties(deinterlace_wasm PROPERTIES LINKER_LANGUAGE CXX)
# set_target_properties(deinterlace_wasm.runtime PROPERTIES LINKER_LANGUAGE CXX)

set_target_properties(Halide_Runtime PROPERTIES EXPORT_NAME Halide::Runtime)
set_target_properties(Halide_Adams2019 PROPERTIES EXPORT_NAME Halide::Adams2019)

export(
  TARGETS
  Halide_Runtime dif_gen Halide_Adams2019 # deinterlace_wasm deinterlace_wasm.runtime
  FILE host_tools.cmake EXPORT_LINK_INTERFACE_LIBRARIES)
