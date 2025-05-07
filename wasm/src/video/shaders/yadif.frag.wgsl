R"(
struct Parameters {
  is_second: u32,
};

@group(0) @binding(0) var mySampler : sampler;
@group(0) @binding(1) var outputFrame :  texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(2) var currentY : texture_2d<f32>;
@group(0) @binding(3) var currentU : texture_2d<f32>;
@group(0) @binding(4) var currentV : texture_2d<f32>;
@group(0) @binding(5) var prevY : texture_2d<f32>;
@group(0) @binding(6) var prevU : texture_2d<f32>;
@group(0) @binding(7) var prevV : texture_2d<f32>;
@group(0) @binding(8) var nextY : texture_2d<f32>;
@group(0) @binding(9) var nextU : texture_2d<f32>;
@group(0) @binding(10) var nextV : texture_2d<f32>;
@group(0) @binding(11) var<uniform> parameters : Parameters;

fn to_coord(tex: texture_2d<f32>, fragUV: vec2<f32>) -> vec2<i32> {
  var dim = textureDimensions(tex);
  return vec2<i32>(
    i32(fragUV[0] * f32(dim[0])),
    i32(fragUV[1] * f32(dim[1]))
  );
}

fn bordered(x_: i32, y_: i32, dim: vec2<i32>) -> vec2<i32> {
  // ボーダーリピートで書き換える
  // ってか今は使ってないけど・・・
  return vec2<i32>(
    clamp(x_, 0, dim[0] - 1),
    clamp(y_, 0, dim[1] - 1)
  );

  // 以前書いてたミラーボーダー処理のコード
  // var x = x_;
  // var y = y_;
  // if (x < 0) {
  //   x = -x;
  // }
  // if (y < 0) {
  //   y = -y;
  // }
  // if (x > dim[0] - 1) {
  //   x = dim[0] - 1 - (x - (dim[0] - 1));
  // }
  // if (y > dim[1] - 1) {
  //   y = dim[1] - 1 - (y - (dim[1] - 1));
  // }
  // return vec2<i32>(x, y);
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

fn spatial_scorej(cur: texture_2d<f32>, j: i32, x: i32, y: i32) -> f32 {
  return absd(load(cur, x - 1 + j, y - 1), load(cur, x - 1 - j, y + 1))
  + absd(load(cur, x + j, y - 1), load(cur, x + j, y + 1))
  + absd(load(cur, x + 1 + j, y - 1), load(cur, x + 1 - j, y + 1))
  + select(0.0, -1.0 / 255.0, j == 0);
}

fn spatial_pred(cur: texture_2d<f32>, j: i32, x: i32, y: i32) -> f32 {
  return avg(load(cur, x + j, y - 1), load(cur, x - j, y + 1));
}

fn yadif(cur: texture_2d<f32>, prev: texture_2d<f32>, next: texture_2d<f32>, x: i32, y: i32, is_second: u32) -> f32 {
  if (y % 2 == select(1, 0, is_second != 0)) {
    return load(cur, x, y);
  }
  var sp_score_j0 = spatial_scorej(cur, 0, x, y);
  var sp_score_jm1 = spatial_scorej(cur, -1, x, y);
  var sp_score_jm2 = spatial_scorej(cur, -2, x, y);
  var sp_score_jp1 = spatial_scorej(cur, 1, x, y);
  var sp_score_jp2 = spatial_scorej(cur, 2, x, y);
  var sp_score_jm12 = select(sp_score_jm2, sp_score_jm1, sp_score_jm1 < sp_score_jm2);
  var sp_score_m = select(sp_score_jm12, sp_score_j0, sp_score_j0 < sp_score_jm1);
  var sp_score_jp12 = select(sp_score_jp2, sp_score_jp1, sp_score_jp1 < sp_score_jp2);
  var sp_score_p = select(sp_score_jp12, sp_score_j0, sp_score_j0 < sp_score_jp1);
  var sp_pred_j0 = spatial_pred(cur, 0, x, y);
  var sp_pred_jm1 = spatial_pred(cur, -2, x, y);
  var sp_pred_jm2 = spatial_pred(cur, -1, x, y);
  var sp_pred_jp1 = spatial_pred(cur, 1, x, y);
  var sp_pred_jp2 = spatial_pred(cur, 2, x, y);
  var sp_pred_jm12 = select(sp_pred_jm2, sp_pred_jm1, sp_score_jm1 < sp_score_jm2);
  var sp_pred_m = select(sp_pred_jm12, sp_pred_j0, sp_score_j0 < sp_score_jm1);
  var sp_pred_jp12 = select(sp_pred_jp2, sp_pred_jp1, sp_score_jp1 < sp_score_jp2);
  var sp_pred_p = select(sp_pred_jp12, sp_pred_j0, sp_score_j0 < sp_score_jp1);
  var sp_pred = select(sp_pred_p, sp_pred_m, sp_score_m < sp_score_p);

  var select2 = is_second != 0;
  var c = load(cur, x, y - 1);
  var e = load(cur, x, y + 1);
  var b = avg(
    select(load(cur, x, y - 2), load(prev, x, y - 2), select2),
    select(load(next, x, y - 2), load(cur, x, y - 2), select2)
  );
  var bmc = b - c;
  var f = avg(
    select(load(cur, x, y + 2), load(prev, x, y + 2), select2),
    select(load(next, x, y + 2), load(cur, x, y + 2), select2)
  );
  var fme = f - e;
  var d = avg(load(prev, x, y), load(next, x, y));
  var dme = d - e;
  var dmc = d - c;
  var diff0 = max3(
    absd(load(cur, x, y), load(next, x, y)) / 2.0,
    avg(absd(load(prev, x, y - 1), c), absd(load(prev, x, y + 1), e)),
    avg(absd(load(next, x, y - 1), c), absd(load(next, x, y + 1), e))
  );
  var diff_max = max3(dme, dmc, min(bmc, fme));
  var diff_min = min3(dme, dmc, max(bmc, fme));  
  var diff = max3(diff0, diff_min, -diff_max);

  return clamp(sp_pred, d - diff, d + diff);
}

fn yuv2rgba(y: f32, u: f32, v: f32) -> vec4<f32> {
  // clampからsaturateに直してもいいかも（あんまり変わらないだろうけど）
  return vec4<f32>(
    clamp(y + 1.5748 * v, 0.0, 1.0),
    clamp(y - 0.1873 * u - 0.4681 * v, 0.0, 1.0),
    clamp(y + 1.8556 * u, 0.0, 1.0),
    1.0);
}

@compute
@workgroup_size(16, 4, 1)
fn main(
  @builtin(global_invocation_id) coord3: vec3<u32>
) {
    var col = i32(coord3[0]);
    var row = i32(coord3[1]);
    var u = (yadif(currentU, prevU, nextU, col, row, parameters.is_second) - 128.0 / 255.0) * 128.0 / (128.0 - 16.0);
    var v = (yadif(currentV, prevV, nextV, col, row, parameters.is_second) - 128.0 / 255.0) * 128.0 / (128.0 - 16.0);
    var y00 = (yadif(currentY, prevY, nextY, 2 * col + 0, 2 * row + 0, parameters.is_second) - 16.0 / 255.0) * 255.0 / (235.0 - 16.0);
    var y01 = (yadif(currentY, prevY, nextY, 2 * col + 0, 2 * row + 1, parameters.is_second) - 16.0 / 255.0) * 255.0 / (235.0 - 16.0);
    var y10 = (yadif(currentY, prevY, nextY, 2 * col + 1, 2 * row + 0, parameters.is_second) - 16.0 / 255.0) * 255.0 / (235.0 - 16.0);
    var y11 = (yadif(currentY, prevY, nextY, 2 * col + 1, 2 * row + 1, parameters.is_second) - 16.0 / 255.0) * 255.0 / (235.0 - 16.0);
    var rgba00 = yuv2rgba(y00, u, v);
    var rgba01 = yuv2rgba(y01, u, v);
    var rgba10 = yuv2rgba(y10, u, v);
    var rgba11 = yuv2rgba(y11, u, v);
    textureStore(outputFrame, vec2<i32>(2 * col + 0, 2 * row + 0), rgba00);
    textureStore(outputFrame, vec2<i32>(2 * col + 0, 2 * row + 1), rgba01);
    textureStore(outputFrame, vec2<i32>(2 * col + 1, 2 * row + 0), rgba10);
    textureStore(outputFrame, vec2<i32>(2 * col + 1, 2 * row + 1), rgba11);
}
)"
