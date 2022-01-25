#include "deinterlace.h"
#include <HalideBuffer.h>
#include <spdlog/spdlog.h>

extern "C" {

void halide_yadif(int w, int linesize, int h, int parity, int tff, uint8_t *out,
                  uint8_t *prev, uint8_t *cur, uint8_t *next) {

  // outだけstrideが違う
  halide_dimension_t outdims[2] = {{0, w, 1}, {0, h, linesize}};
  halide_dimension_t indims[2] = {{0, w, 1}, {0, h, w}};
  Halide::Runtime::Buffer<uint8_t> outBuffer{out, 2, outdims};
  Halide::Runtime::Buffer<uint8_t> prevBuffer{prev, 2, indims};
  Halide::Runtime::Buffer<uint8_t> curBuffer{cur, 2, indims};
  Halide::Runtime::Buffer<uint8_t> nextBuffer{next, 2, indims};

  // spdlog::info("w:{} h:{} linesize:{} parity:{} tff:{}", w, h, linesize,
  // parity,
  //              tff);

  deinterlace(parity, tff, prevBuffer, curBuffer, nextBuffer, outBuffer);
}
}
