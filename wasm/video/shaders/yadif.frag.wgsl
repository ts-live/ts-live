[[group(0), binding(0)]] var mySampler : sampler;
[[group(0), binding(1)]] var outputFrame :  texture_storage_2d<rgba8unorm, write>;
[[group(0), binding(2)]] var currentY : texture_2d<f32>;
[[group(0), binding(3)]] var currentU : texture_2d<f32>;
[[group(0), binding(4)]] var currentV : texture_2d<f32>;
[[group(0), binding(5)]] var prevY : texture_2d<f32>;
[[group(0), binding(6)]] var prevU : texture_2d<f32>;
[[group(0), binding(7)]] var prevV : texture_2d<f32>;
[[group(0), binding(8)]] var nextY : texture_2d<f32>;
[[group(0), binding(9)]] var nextU : texture_2d<f32>;
[[group(0), binding(10)]] var nextV : texture_2d<f32>;

fn to_coord(tex: texture_2d<f32>, fragUV: vec2<f32>) -> vec2<i32> {
  var dim = textureDimensions(tex);
  return vec2<i32>(
    i32(fragUV[0] * f32(dim[0])),
    i32(fragUV[1] * f32(dim[1]))
  );
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

fn load(tex: texture_2d<f32>, x: i32, y: i32) -> f32 {

  // https://www.w3.org/TR/WGSL/#textureload
  // If an out of bounds access occurs, the built-in function returns one of:
  // - The data for some texel within bounds of the texture
  // - A vector (0,0,0,0) or (0,0,0,1) of the appropriate type for non-depth textures
  // - 0.0 for depth textures
  // とあるので、実装によって結果が違うかも・・・
  return textureLoad(tex, vec2<i32>(x, y), 0)[0];

  // return textureLoad(tex, bordered(x, y, textureDimensions(tex)), 0)[0];
}

fn avg(a: f32, b: f32) -> f32 {
  return (a + b) * 0.5;
}

fn absd(a: f32, b: f32) -> f32 {
  return max(a, b) - min(a, b);
}

fn max3(a: f32, b: f32, c: f32) -> f32 {
  return max(max(a, b), c);
}

fn min3(a: f32, b: f32, c: f32) -> f32 {
  return (min(min(a, b), c));
}

fn yadif(cur: texture_2d<f32>, prev: texture_2d<f32>, next: texture_2d<f32>, x: i32, y: i32) -> f32 {
  if (y % 2 == 0) {
    return load(cur, x, y);
  } else {
    var c = load(cur, x, y - 1);
    var d = avg(load(cur, x, y), load(next, x, y));
    var e = load(cur, x, y + 1);
    var tmp_diff0 = absd(load(cur, x, y), load(next, x, y)) / 2.0;
    var tmp_diff1 = avg(absd(load(prev, x, y - 1), c), absd(load(prev, x, y + 1), e));
    var tmp_diff2 = avg(absd(load(next, x, y - 1), c), absd(load(next, x, y + 1), e));
    var diff = max3(tmp_diff0, tmp_diff1, tmp_diff2);

    var b = avg(load(cur, x, y - 2), load(next, x, y - 2));
    var f = avg(load(cur, x, y + 2), load(next, x, y + 2));
    var max_ = max3(d - e, d -c, min(b -c, f - e));
    var min_ = min3(d - e, d -c, max(b -c, f - e));
    diff = max3(diff, min_, -max_);


    // var spatial_pred_0 = avg(c, e);
    var score_0 =
      absd(load(cur, x - 1, y - 1), load(cur, x - 1, y + 1))
      + absd(c, e)
      + absd(load(cur, x + 1, y - 1), load(cur, x + 1, y + 1))
      - 1.0 / 255.0;

    var score_1 =
        absd(load(cur, x - 2, y - 1), load(cur, x - 0, y + 1))
      + absd(load(cur, x - 1, y - 1), load(cur, x + 1, y + 1))
      + absd(load(cur, x - 0, y - 1), load(cur, x + 2, y + 1));
    // var spatial_pred_1 = avg(load(cur, x - 1, y - 1), load(cur, x + 1, y + 1));

    var score_2 =
        absd(load(cur, x - 0, y - 1), load(cur, x - 2, y + 1))
      + absd(load(cur, x + 1, y - 1), load(cur, x - 1, y + 1))
      + absd(load(cur, x + 2, y - 1), load(cur, x - 0, y + 1));
    // var spatial_pred_2 = avg(load(cur, x + 1, y - 1), load(cur, x - 1, y + 1));

    if (score_1 < score_0 && score_1 < score_2) {
      var score_11 = 
        absd(load(cur, x - 3, y - 1), load(cur, x + 1, y + 1))
      + absd(load(cur, x - 2, y - 1), load(cur, x + 2, y + 1))
      + absd(load(cur, x - 1, y - 1), load(cur, x + 3, y + 1));
      // var spatial_pred_11 = avg(load(cur, x - 2, y - 1), load(cur, x + 2, y + 1));
      if (score_11 < score_1) {
        // spatial_pred = spatial_pred_11;
        return clamp(avg(load(cur, x - 2, y - 1), load(cur, x + 2, y + 1)), d - diff, d + diff);
      } else {
        // spatial_pred = spatial_pred_1;
        return clamp(avg(load(cur, x - 1, y - 1), load(cur, x + 1, y + 1)), d - diff, d + diff);
      }
    } else {
      if (score_2 < score_0 && score_2 < score_1) {
        var score_21 = 
          absd(load(cur, x + 1, y - 1), load(cur, x - 3, y + 1))
        + absd(load(cur, x + 2, y - 1), load(cur, x - 2, y + 1))
        + absd(load(cur, x + 3, y - 1), load(cur, x - 1, y + 1));
        // var spatial_pred_21 = avg(load(cur, x + 2, y - 1), load(cur, x - 2, y + 1));

        if (score_21 < score_2) {
          // spatial_pred = spatial_pred_21;
          return clamp(avg(load(cur, x + 2, y - 1), load(cur, x - 2, y + 1)), d - diff, d + diff);
        } else {
          // spatial_pred = spatial_pred_2;
          return clamp(avg(load(cur, x + 1, y - 1), load(cur, x - 1, y + 1)), d - diff, d + diff);
        }
      } else {
        return clamp(avg(c, e), d - diff, d + diff);
      }
    }

    // return clamp(spatial_pred, d - diff, d + diff);
  }
}

fn yuv2rgba(y: f32, u: f32, v: f32) -> vec4<f32> {
  return vec4<f32>(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u, 1.0);
}

[[stage(compute), workgroup_size(32, 8, 1)]]
fn main(
  [[builtin(global_invocation_id)]] coord3: vec3<u32>
) {
  var col = i32(coord3[0]);
  var row = i32(coord3[1]);
  var u = yadif(currentU, prevU, nextU, col, row) - 128.0 / 255.0;
  var v = yadif(currentV, prevV, nextV, col, row) - 128.0 / 255.0;
  var y00 = yadif(currentY, prevY, nextY, 2 * col + 0, 2 * row + 0) - 16.0 / 255.0;
  var y01 = yadif(currentY, prevY, nextY, 2 * col + 0, 2 * row + 1) - 16.0 / 255.0;
  var y10 = yadif(currentY, prevY, nextY, 2 * col + 1, 2 * row + 0) - 16.0 / 255.0;
  var y11 = yadif(currentY, prevY, nextY, 2 * col + 1, 2 * row + 1) - 16.0 / 255.0;
  var rgba00 = yuv2rgba(y00, u, v);
  var rgba01 = yuv2rgba(y01, u, v);
  var rgba10 = yuv2rgba(y10, u, v);
  var rgba11 = yuv2rgba(y11, u, v);
  textureStore(outputFrame, vec2<i32>(2 * col + 0, 2 * row + 0), rgba00);
  textureStore(outputFrame, vec2<i32>(2 * col + 0, 2 * row + 1), rgba01);
  textureStore(outputFrame, vec2<i32>(2 * col + 1, 2 * row + 0), rgba10);
  textureStore(outputFrame, vec2<i32>(2 * col + 1, 2 * row + 1), rgba11);
}
