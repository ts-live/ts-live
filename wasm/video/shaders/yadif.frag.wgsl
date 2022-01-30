[[group(0), binding(0)]] var mySampler : sampler;
[[group(0), binding(1)]] var currentY : texture_2d<f32>;
[[group(0), binding(2)]] var currentU : texture_2d<f32>;
[[group(0), binding(3)]] var currentV : texture_2d<f32>;
[[group(0), binding(4)]] var prevY : texture_2d<f32>;
[[group(0), binding(5)]] var prevU : texture_2d<f32>;
[[group(0), binding(6)]] var prevV : texture_2d<f32>;

fn pixel_Y(fragUV: vec2<f32>) -> vec2<i32> {
  return vec2<i32>(
    i32(fragUV[0] * f32(textureDimensions(currentY)[0])),
    i32(fragUV[1] * f32(textureDimensions(currentY)[1]))
  );
}

[[stage(fragment)]]
fn main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
  var y = (textureSample(currentY, mySampler, fragUV)[0] - 16.0 / 255.0);
  var u = (textureSample(currentU, mySampler, fragUV)[0] - 128.0 / 255.0);
  var v = (textureSample(currentV, mySampler, fragUV)[0] - 128.0 / 255.0);
  var texel = pixel_Y(fragUV);
  if (texel[1] % 2 == 0) {
    return vec4<f32>(1.0, 0.0,0.0, 1.0);
  } else {
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
  }
}


// [[stage(fragment)]]
// // ITU-R BT.709 TODO: get color space from stream.
// fn main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
//   var y = (textureSample(currentY, mySampler, fragUV)[0] - 16.0 / 255.0);
//   var u = (textureSample(currentU, mySampler, fragUV)[0] - 128.0 / 255.0);
//   var v = (textureSample(currentV, mySampler, fragUV)[0] - 128.0 / 255.0);
//   return vec4<f32>(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u, 1.0);
// }
