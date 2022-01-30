[[group(0), binding(0)]] var mySampler : sampler;
[[group(0), binding(1)]] var yTexture : texture_2d<f32>;
[[group(0), binding(2)]] var uTexture : texture_2d<f32>;
[[group(0), binding(3)]] var vTexture : texture_2d<f32>;

[[stage(fragment)]]
// ITU-R BT.709 TODO: get color space from stream.
fn main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
  var y = (textureSample(yTexture, mySampler, fragUV)[0] - 16.0 / 255.0);
  var u = (textureSample(uTexture, mySampler, fragUV)[0] - 128.0 / 255.0);
  var v = (textureSample(vTexture, mySampler, fragUV)[0] - 128.0 / 255.0);
  return vec4<f32>(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u, 1.0);
}
