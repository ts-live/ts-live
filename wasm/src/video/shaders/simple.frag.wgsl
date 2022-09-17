R"(
@group(0) @binding(0) var mySampler: sampler;
@group(0) @binding(1) var myTexture: texture_2d<f32>;

@fragment
fn main(@location(0) fragUV : vec2<f32>) -> @location(0) vec4<f32> {
  return textureSampleLevel(myTexture, mySampler, fragUV, 0.0);
}
)"
