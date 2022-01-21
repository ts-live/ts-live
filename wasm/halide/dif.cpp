#include <Halide.h>

class DeinterlaceFilter : public Halide::Generator<DeinterlaceFilter> {
public:
  Halide::GeneratorInput<Halide::Buffer<std::uint8_t>> input{"input", 2};
  Halide::GeneratorOutput<Halide::Buffer<std::uint8_t>> output{"output", 2};
  Var x, y;

  void generate() {
    //
    output(x, y) = input(x, y);
  }
  void schedule() {
    output.set_estimates({{0, 1920}, {0, 1080}});
    input.set_estimates({{0, 1920}, {0, 1080}});
  }
};

HALIDE_REGISTER_GENERATOR(DeinterlaceFilter, deinterlace);
