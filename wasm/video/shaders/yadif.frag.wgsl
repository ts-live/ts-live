[[group(0), binding(0)]] var mySampler : sampler;
[[group(0), binding(1)]] var currentY : texture_2d<f32>;
[[group(0), binding(2)]] var currentU : texture_2d<f32>;
[[group(0), binding(3)]] var currentV : texture_2d<f32>;
[[group(0), binding(4)]] var prevY : texture_2d<f32>;
[[group(0), binding(5)]] var prevU : texture_2d<f32>;
[[group(0), binding(6)]] var prevV : texture_2d<f32>;
[[group(0), binding(7)]] var nextY : texture_2d<f32>;
[[group(0), binding(8)]] var nextU : texture_2d<f32>;
[[group(0), binding(9)]] var nextV : texture_2d<f32>;

fn to_coord(tex: texture_2d<f32>, fragUV: vec2<f32>) -> vec2<i32> {
  var dim = textureDimensions(tex);
  return vec2<i32>(
    i32(fragUV[0] * f32(dim[0])),
    i32(fragUV[1] * f32(dim[1]))
  );
}

fn load(tex: texture_2d<f32>, coord: vec2<i32>) -> f32 {
  return textureLoad(tex, coord, 0)[0];
}

fn avg(a: f32, b: f32) -> f32 {
  return a / 2.0 + b / 2.0;
}

fn absd(a: f32, b: f32) -> f32 {
  return abs(a - b);
}

fn max3(a: f32, b: f32, c: f32) -> f32 {
  return max(max(a, b), c);
}

fn min3(a: f32, b: f32, c: f32) -> f32 {
  return (min(min(a, b), c));
}

fn bordered(x_: i32, y_: i32, dim: vec2<i32>) -> vec2<i32> {
  var x = x_;
  var y = y_;
  if (x < 0) {
    x = -x;
  }
  if (y < 0) {
    y = -y;
  }
  if (x > dim[0] - 1) {
    x = dim[0] - 1 - (x - (dim[0] - 1));
  }
  if (y > dim[1] - 1) {
    y = dim[1] - 1 - (y - (dim[1] - 1));
  }
  return vec2<i32>(x, y);
}

fn yadif(cur: texture_2d<f32>, prev: texture_2d<f32>, next: texture_2d<f32>, fragUV: vec2<f32>) -> f32 {
  var coord = to_coord(cur, fragUV);
  var dim = textureDimensions(cur);
  var x = coord[0];
  var y = coord[1];

  if (y % 2 == 0) {
    return load(cur, coord);
  } else {
    var c = load(cur, bordered(x, y - 1, dim));
    var d = avg(load(cur, coord), load(next, coord));
    var e = load(cur, bordered(x, y + 1, dim));
    var tmp_diff0 = absd(load(cur, coord), load(next, coord)) / 2.0;
    var tmp_diff1 = avg(absd(load(prev, bordered(x, y - 1, dim)), c), absd(load(prev, bordered(x, y + 1, dim)), e));
    var tmp_diff2 = avg(absd(load(next, bordered(x, y - 1, dim)), c), absd(load(next, bordered(x, y + 1, dim)), e));
    var diff = max3(tmp_diff0, tmp_diff1, tmp_diff2);
    var spatial_pred = avg(c, e);
    var spatial_score =
      absd(load(cur, bordered(x - 1, y - 1, dim)), load(cur, bordered(x - 1, y + 1, dim)))
      + absd(c, e)
      + absd(load(cur, bordered(x + 1, y - 1, dim)), load(cur, bordered(x + 1, y + 1, dim)))
      - 1.0 / 255.0;

    {
      var score =
          absd(load(cur, bordered(x - 2, y - 1, dim)), load(cur, bordered(x - 0, y + 1, dim)))
        + absd(load(cur, bordered(x - 1, y - 1, dim)), load(cur, bordered(x + 1, y + 1, dim)))
        + absd(load(cur, bordered(x - 0, y - 1, dim)), load(cur, bordered(x + 2, y + 1, dim)));
      if (score < spatial_score) {
        spatial_score = score;
        spatial_pred = avg(load(cur, bordered(x - 1, y - 1, dim)), load(cur, bordered(x + 1, y + 1, dim)));

        var score = 
          absd(load(cur, bordered(x - 3, y - 1, dim)), load(cur, bordered(x + 1, y + 1, dim)))
        + absd(load(cur, bordered(x - 2, y - 1, dim)), load(cur, bordered(x + 2, y + 1, dim)))
        + absd(load(cur, bordered(x - 1, y - 1, dim)), load(cur, bordered(x + 3, y + 1, dim)));
        if (score < spatial_score) {
          spatial_score = score;
          spatial_pred = avg(load(cur, bordered(x - 2, y - 1, dim)), load(cur, bordered(x + 2, y + 1, dim)));
        }
      }
    }
    {
      var score =
          absd(load(cur, bordered(x - 0, y - 1, dim)), load(cur, bordered(x - 2, y + 1, dim)))
        + absd(load(cur, bordered(x + 1, y - 1, dim)), load(cur, bordered(x - 1, y + 1, dim)))
        + absd(load(cur, bordered(x + 2, y - 1, dim)), load(cur, bordered(x - 0, y + 1, dim)));
      if (score < spatial_score) {
        spatial_score = score;
        spatial_pred = avg(load(cur, bordered(x + 1, y - 1, dim)), load(cur, bordered(x - 1, y + 1, dim)));

        var score = 
          absd(load(cur, bordered(x + 1, y - 1, dim)), load(cur, bordered(x - 3, y + 1, dim)))
        + absd(load(cur, bordered(x + 2, y - 1, dim)), load(cur, bordered(x - 2, y + 1, dim)))
        + absd(load(cur, bordered(x + 3, y - 1, dim)), load(cur, bordered(x - 1, y + 1, dim)));
        if (score < spatial_score) {
          spatial_score = score;
          spatial_pred = avg(load(cur, bordered(x + 2, y - 1, dim)), load(cur, bordered(x - 2, y + 1, dim)));
        }
      }
    }

    var b = avg(load(cur, bordered(x, y - 2, dim)), load(next, bordered(x, y - 2, dim)));
    var f = avg(load(cur, bordered(x, y + 2, dim)), load(next, bordered(x, y + 2, dim)));
    var max_ = max3(d - e, d -c, min(b -c, f - e));
    var min_ = min3(d - e, d -c, max(b -c, f - e));
    diff = max3(diff, min_, -max_);

    spatial_pred = clamp(spatial_pred, d - diff, d + diff);

    return spatial_pred;
  }
}


[[stage(fragment)]]
fn main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
  var y = (yadif(currentY, prevY, nextY, fragUV) - 16.0 / 255.0);
  var u = (yadif(currentU, prevU, nextU, fragUV) - 128.0 / 255.0);
  var v = (yadif(currentV, prevV, nextV, fragUV) - 128.0 / 255.0);
  return vec4<f32>(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u, 1.0);
}
