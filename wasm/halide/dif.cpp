#include <Halide.h>

using Halide::ConciseCasts::i16;
using Halide::ConciseCasts::u8;

class DeinterlaceFilter : public Halide::Generator<DeinterlaceFilter> {
public:
  Halide::GeneratorInput<int> parity{"parity"};
  Halide::GeneratorInput<int> tff{"tff"};
  Halide::GeneratorInput<Halide::Buffer<std::uint8_t>> prev_input{"prev", 2};
  Halide::GeneratorInput<Halide::Buffer<std::uint8_t>> cur_input{"cur", 2};
  Halide::GeneratorInput<Halide::Buffer<std::uint8_t>> next_input{"next", 2};
  Halide::GeneratorOutput<Halide::Buffer<std::uint8_t>> out{"out", 2};
  Var x, y, j;

  void generate() {
    //
    auto prev = Halide::BoundaryConditions::mirror_image(prev_input);
    auto cur = Halide::BoundaryConditions::mirror_image(cur_input);
    auto next = Halide::BoundaryConditions::mirror_image(next_input);

    Func score0, spatial_pred0, score, spatial_pred;

    score0(x, y) = i16(Halide::absd(cur(x - 1, y - 1), cur(x - 1, y + 1))) +
                   i16(Halide::absd(cur(x, y - 1), cur(x, y + 1))) +
                   i16(Halide::absd(cur(x + 1, y - 1), cur(x + 1, y + 1)));
    spatial_pred0(x, y) = ((cur(x, y - 1) >> 1) + (cur(x, y + 1) >> 1));

    score(x, y, j) =
        i16(Halide::absd(cur(x - 1 + j, y - 1), cur(x - 1 + j, y + 1))) +
        i16(Halide::absd(cur(x + j, y - 1), cur(x + j, y + 1))) +
        i16(Halide::absd(cur(x + 1 + j, y - 1), cur(x + 1 + j, y + 1)));
    spatial_pred(x, y, j) = (cur(x + j, y - 1) >> 1) + (cur(x + j, y + 1) >> 1);

    Func d;
    d(x, y) = Halide::select(parity != 0, (prev(x, y) >> 1) + (cur(x, y) >> 1),
                             (cur(x, y) >> 1) + (next(x, y) >> 1));

    Func tmp_diff0, tmp_diff1, tmp_diff2, diff_min, diff_max, diff, diff2;
    tmp_diff0(x, y) =
        Halide::absd(Halide::select(parity != 0, prev(x, y), cur(x, y)),
                     Halide::select(parity != 0, cur(x, y), next(x, y))) >>
        1;
    tmp_diff1(x, y) = (Halide::absd(prev(x, y - 1), cur(x, y - 1)) >> 1) +
                      (Halide::absd(prev(x, y + 1), cur(x, y + 1)) >> 1);

    tmp_diff2(x, y) = (Halide::absd(next(x, y - 1), cur(x, y - 1)) >> 1) +
                      (Halide::absd(next(x, y + 1), cur(x, y + 1)) >> 1);

    diff(x, y) = Halide::max(Halide::max(tmp_diff0(x, y), tmp_diff1(x, y)),
                             tmp_diff2(x, y));

    Func b, f;
    b(x, y) = Halide::select(parity != 0,
                             (prev(x, y - 2) >> 1) + (cur(x, y - 2) >> 1),
                             (cur(x, y - 2) >> 1) + (next(x, y - 2)));
    f(x, y) = Halide::select(parity != 0,
                             (prev(x, y + 2) >> 1) + (cur(x, y + 2) >> 1),
                             (cur(x, y + 2) >> 1) + (next(x, y + 2)));

    diff_min(x, y) =
        Halide::min(Halide::min(i16(d(x, y)) - i16(cur(x, y + 1)),
                                i16(d(x, y)) - i16(cur(x, y - 1))),
                    Halide::max(i16(b(x, y)) - i16(cur(x, y - 1)),
                                i16(f(x, y)) - i16(cur(x, y + 1))));

    diff_max(x, y) =
        Halide::max(Halide::min(i16(d(x, y)) - i16(cur(x, y + 1)),
                                i16(d(x, y)) - i16(cur(x, y - 1))),
                    Halide::min(i16(b(x, y)) - i16(cur(x, y - 1)),
                                i16(f(x, y)) - i16(cur(x, y + 1))));

    diff2(x, y) =
        Halide::max(Halide::max(diff(x, y), diff_min(x, y)), -diff_max(x, y));

    Func min_spatial_pred;
    min_spatial_pred(x, y) = Halide::select(
        score0(x, y) < score(x, y, -1) && score0(x, y) < score(x, y, 1),
        spatial_pred0(x, y),
        Halide::select(
            score(x, y, -1) < score(x, y, 1),
            Halide::select(score(x, y, -1) < score(x, y, -2),
                           spatial_pred(x, y, -1), spatial_pred(x, y, -2)),
            Halide::select(score(x, y, 1) < score(x, y, 2),
                           spatial_pred(x, y, 1), spatial_pred(x, y, 2))));

    out(x, y) = Halide::select(
        ((y ^ parity) & 1) == 1,
        u8(Halide::clamp(i16(spatial_pred0(x, y)), d(x, y) - diff2(x, y),
                         d(x, y) + diff2(x, y))),
        cur(x, y));
  }
  void schedule() {
    out.set_estimates({{0, 1920}, {0, 1080}});
    prev_input.set_estimates({{0, 1920}, {0, 1080}});
    cur_input.set_estimates({{0, 1920}, {0, 1080}});
    next_input.set_estimates({{0, 1920}, {0, 1080}});
  }
};

HALIDE_REGISTER_GENERATOR(DeinterlaceFilter, deinterlace);
