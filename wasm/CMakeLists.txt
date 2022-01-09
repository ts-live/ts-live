cmake_minimum_required(VERSION 3.10)
project(ts-wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG v1.9.2
)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 8.0.1
)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(fmt spdlog)

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
