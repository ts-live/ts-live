#include <Halide.h>

class DeinterlaceFilter : public Halide::Generator<DeinterlaceFilter> {
public:
  Halide::GeneratorInput<Halide::Buffer<std::uint8_t>> input{"input", 2};
  Halide::GeneratorOutput<Halide::Buffer<std::uint8_t>> output{"output", 2};

  void generate() {
    //
    output(x, y) = input(x, y);
  }
};

HALIDE_REGISTER_GENERATOR(DeinterlaceFilter, deinterlace);
