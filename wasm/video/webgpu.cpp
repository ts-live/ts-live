// from https://github.com/cwoffenden/hello-webgpu/blob/main/src/main.cpp

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
}

struct context {
  int textureWidth = 0;
  int textureHeight = 0;
  WGPUDevice device;
  WGPUSwapChain swapChain;
  WGPUQueue queue;
  WGPURenderPipeline pipeline;
  WGPUBindGroupLayout bindGroupLayout;
  WGPUTexture frameTextureY, frameTextureU, frameTextureV;
  WGPUTextureView frameViewY, frameViewU, frameViewV;
  WGPUBindGroup bindGroup;
  WGPUSampler sampler;
};

static context ctx;

/**
 * WGSL equivalent of \c triangle_vert_spirv.
 */
static char const triangle_vert_wgsl[] = R"(
[[group(0), binding(0)]] var mySampler : sampler;
[[group(0), binding(1)]] var myTexture : texture_2d<f32>;

struct VertexOutput {
  [[builtin(position)]] Position : vec4<f32>;
  [[location(0)]] fragUV : vec2<f32>;
};

[[stage(vertex)]]
fn main([[builtin(vertex_index)]] VertexIndex : u32) -> VertexOutput {
  var pos = array<vec2<f32>, 6>(
      vec2<f32>( 1.0,  1.0),
      vec2<f32>( 1.0, -1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>( 1.0,  1.0),
      vec2<f32>(-1.0, -1.0),
      vec2<f32>(-1.0,  1.0));

  var uv = array<vec2<f32>, 6>(
      vec2<f32>(1.0, 0.0),
      vec2<f32>(1.0, 1.0),
      vec2<f32>(0.0, 1.0),
      vec2<f32>(1.0, 0.0),
      vec2<f32>(0.0, 1.0),
      vec2<f32>(0.0, 0.0));

  var output : VertexOutput;
  output.Position = vec4<f32>(pos[VertexIndex], 0.0, 1.0);
  output.fragUV = uv[VertexIndex];
  return output;
}

[[stage(fragment)]]
fn frag_main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
  return textureSample(myTexture, mySampler, fragUV);
}
)";

/**
 * WGSL equivalent of \c triangle_frag_spirv.
 */
static char const triangle_frag_wgsl[] = R"(
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
)";

/**
 * Helper to create a shader from SPIR-V IR.
 *
 * \param[in] code shader source (output using the \c -V \c -x options in \c
 * glslangValidator) \param[in] size size of \a code in bytes \param[in] label
 * optional shader name
 */
/*static*/ WGPUShaderModule createShader(const uint32_t *code, uint32_t size,
                                         const char *label = nullptr) {
  WGPUShaderModuleSPIRVDescriptor spirv = {};
  spirv.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
  spirv.codeSize = size / sizeof(uint32_t);
  spirv.code = code;
  WGPUShaderModuleDescriptor desc = {};
  desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&spirv);
  desc.label = label;
  return wgpuDeviceCreateShaderModule(ctx.device, &desc);
}

/**
 * Helper to create a shader from WGSL source.
 *
 * \param[in] code WGSL shader source
 * \param[in] label optional shader name
 */
static WGPUShaderModule createShader(const char *const code,
                                     const char *label = nullptr) {
  WGPUShaderModuleWGSLDescriptor wgsl = {};
  wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  wgsl.source = code;
  WGPUShaderModuleDescriptor desc = {};
  desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&wgsl);
  desc.label = label;
  return wgpuDeviceCreateShaderModule(ctx.device, &desc);
}

static void createTextures(int width, int height) {
  std::vector<uint8_t> tmpBuffer(width * height);

  WGPUExtent3D size = {};
  size.width = width;
  size.height = height;
  size.depthOrArrayLayers = 1;

  WGPUExtent3D uvSize = {};
  uvSize.width = width / 2;
  uvSize.height = height / 2;
  uvSize.depthOrArrayLayers = 1;

  WGPUTextureDescriptor textureDesc = {};
  textureDesc.dimension = WGPUTextureDimension_2D;
  textureDesc.format = WGPUTextureFormat_R8Unorm;
  textureDesc.usage =
      WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
  textureDesc.sampleCount = 1;
  textureDesc.mipLevelCount = 1;

  textureDesc.size = size;
  ctx.frameTextureY = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  textureDesc.size = uvSize;
  ctx.frameTextureU = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.frameTextureV = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.magFilter = WGPUFilterMode_Linear;
  samplerDesc.minFilter = WGPUFilterMode_Linear;
  samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;

  ctx.sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.format = WGPUTextureFormat_R8Unorm;
  viewDesc.arrayLayerCount = 1;
  viewDesc.mipLevelCount = 1;
  viewDesc.aspect = WGPUTextureAspect_All;

  ctx.frameViewY = wgpuTextureCreateView(ctx.frameTextureY, &viewDesc);
  ctx.frameViewU = wgpuTextureCreateView(ctx.frameTextureU, &viewDesc);
  ctx.frameViewV = wgpuTextureCreateView(ctx.frameTextureV, &viewDesc);

  WGPUBindGroupEntry bgEntries[4] = {{}, {}, {}, {}};
  bgEntries[0].binding = 0;
  bgEntries[0].sampler = ctx.sampler;

  bgEntries[1].binding = 1;
  bgEntries[1].textureView = ctx.frameViewY;

  bgEntries[2].binding = 2;
  bgEntries[2].textureView = ctx.frameViewU;

  bgEntries[3].binding = 3;
  bgEntries[3].textureView = ctx.frameViewV;

  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = ctx.bindGroupLayout;
  bgDesc.entryCount = 4;
  bgDesc.entries = bgEntries;

  ctx.bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);

  ctx.textureWidth = width;
  ctx.textureHeight = height;
}

/**
 * Bare minimum pipeline to draw a triangle using the above shaders.
 */
static void createPipeline() {
  // compile shaders
  // NOTE: these are now the WGSL shaders (tested with Dawn and Chrome Canary)
  WGPUShaderModule vertMod = createShader(triangle_vert_wgsl);
  WGPUShaderModule fragMod = createShader(triangle_frag_wgsl);

  WGPUSamplerBindingLayout samplerLayout = {};
  samplerLayout.type = WGPUSamplerBindingType_Filtering;

  WGPUTextureBindingLayout textureLayout = {};
  textureLayout.sampleType = WGPUTextureSampleType_Float;
  textureLayout.multisampled = false;
  textureLayout.viewDimension = WGPUTextureViewDimension_2D;

  // bind group layout (used by both the pipeline layout and uniform bind group,
  // released at the end of this function)
  WGPUBindGroupLayoutEntry bglEntries[4] = {{}, {}, {}, {}};
  bglEntries[0].binding = 0;
  bglEntries[0].visibility = WGPUShaderStage_Fragment;
  bglEntries[0].sampler = samplerLayout;

  bglEntries[1].binding = 1;
  bglEntries[1].visibility = WGPUShaderStage_Fragment;
  bglEntries[1].texture = textureLayout;

  bglEntries[2].binding = 2;
  bglEntries[2].visibility = WGPUShaderStage_Fragment;
  bglEntries[2].texture = textureLayout;

  bglEntries[3].binding = 3;
  bglEntries[3].visibility = WGPUShaderStage_Fragment;
  bglEntries[3].texture = textureLayout;

  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 4;
  bglDesc.entries = bglEntries;
  ctx.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);

  // pipeline layout (used by the render pipeline, released after its creation)
  WGPUPipelineLayoutDescriptor layoutDesc = {};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &ctx.bindGroupLayout;
  WGPUPipelineLayout pipelineLayout =
      wgpuDeviceCreatePipelineLayout(ctx.device, &layoutDesc);

  // Fragment state
  WGPUBlendState blend = {};
  blend.color.operation = WGPUBlendOperation_Add;
  blend.color.srcFactor = WGPUBlendFactor_One;
  blend.color.dstFactor = WGPUBlendFactor_One;
  blend.alpha.operation = WGPUBlendOperation_Add;
  blend.alpha.srcFactor = WGPUBlendFactor_One;
  blend.alpha.dstFactor = WGPUBlendFactor_One;

  WGPUColorTargetState colorTarget = {};
  colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
  colorTarget.blend = &blend;
  colorTarget.writeMask = WGPUColorWriteMask_All;
  // WGPUColorTargetState colorTargets[2] = {colorTarget, colorTarget};

  WGPUFragmentState fragment = {};
  fragment.module = fragMod;
  fragment.entryPoint = "main";
  fragment.targetCount = 1;
  fragment.targets = &colorTarget;

  WGPURenderPipelineDescriptor desc = {};
  desc.fragment = &fragment;

  // Other state
  desc.layout = pipelineLayout;
  desc.depthStencil = nullptr;

  desc.vertex.module = vertMod;
  desc.vertex.entryPoint = "main";
  // desc.vertex.bufferCount = 0; // 1;
  // desc.vertex.buffers = &vertexBufferLayout;

  desc.multisample.count = 1;
  desc.multisample.mask = 0xFFFFFFFF;
  desc.multisample.alphaToCoverageEnabled = false;

  desc.primitive.frontFace = WGPUFrontFace_CCW;
  desc.primitive.cullMode = WGPUCullMode_None;
  desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

  ctx.pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);

  // partial clean-up (just move to the end, no?)
  wgpuPipelineLayoutRelease(pipelineLayout);

  wgpuShaderModuleRelease(fragMod);
  wgpuShaderModuleRelease(vertMod);
}

void initWebGpu() {
  ctx.device = emscripten_webgpu_get_device();

  // create queue
  ctx.queue = wgpuDeviceGetQueue(ctx.device);

  // pipeline/buffer
  createPipeline();

  // create swapchain?
  WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
  canvasDesc.selector = "video";

  WGPUSurfaceDescriptor surfaceDesc = {};
  surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&canvasDesc);

  WGPUSurface surface = wgpuInstanceCreateSurface(nullptr, &surfaceDesc);

  WGPUSwapChainDescriptor swapDesc = {};
  swapDesc.usage = WGPUTextureUsage_RenderAttachment;
  swapDesc.format = WGPUTextureFormat_BGRA8Unorm;
  swapDesc.width = 1920;
  swapDesc.height = 1080;
  swapDesc.presentMode = WGPUPresentMode_Fifo;

  ctx.swapChain = wgpuDeviceCreateSwapChain(ctx.device, surface, &swapDesc);
}

void drawWebGpu(AVFrame *frame) {
  if (frame->width != ctx.textureWidth || frame->height != ctx.textureHeight) {
    createTextures(frame->width, frame->height);
  }

  WGPUTextureView backBufView =
      wgpuSwapChainGetCurrentTextureView(ctx.swapChain); // create textureView

  WGPURenderPassColorAttachment colorDesc = {};
  colorDesc.view = backBufView;
  colorDesc.loadOp = WGPULoadOp_Clear;
  colorDesc.storeOp = WGPUStoreOp_Store;
  colorDesc.clearColor.r = 0.0f;
  colorDesc.clearColor.g = 0.0f;
  colorDesc.clearColor.b = 0.0f;
  colorDesc.clearColor.a = 1.0f;

  WGPURenderPassDescriptor renderPass = {};
  renderPass.colorAttachmentCount = 1;
  renderPass.colorAttachments = &colorDesc;

  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(ctx.device, nullptr); // create encoder

  // update the rotation
  // ctx.rotDeg += 0.1f;
  // wgpuQueueWriteBuffer(ctx.queue, ctx.uRotBuf, 0, &ctx.rotDeg,
  //                      sizeof(ctx.rotDeg));

  // draw the triangle (comment these five lines to simply clear the screen)
  if (frame->width != 1440 && frame->width != 1920) {
    // TODO: error message
    return;
  }

  WGPUExtent3D copySize = {};
  copySize.width = frame->width;
  copySize.height = frame->height;
  copySize.depthOrArrayLayers = 1;

  WGPUExtent3D copySizeuv = {};
  copySizeuv.width = frame->width / 2;
  copySizeuv.height = frame->height / 2;
  copySizeuv.depthOrArrayLayers = 1;

  WGPUTextureDataLayout textureDataLayout = {};
  textureDataLayout.rowsPerImage = frame->height;
  textureDataLayout.bytesPerRow = frame->width;
  textureDataLayout.offset = 0;

  WGPUTextureDataLayout textureDataLayoutUv = {};
  textureDataLayoutUv.rowsPerImage = frame->height / 2;
  textureDataLayoutUv.bytesPerRow = frame->width / 2;
  textureDataLayoutUv.offset = 0;

  WGPUOrigin3D origin = {};

  WGPUImageCopyTexture copyTexture = {};
  copyTexture.origin = origin;
  copyTexture.aspect = WGPUTextureAspect::WGPUTextureAspect_All;
  copyTexture.mipLevel = 0;

  // wgpuQueueWriteBuffer(
  //     ctx.queue, frame->width == 1440 ? ctx.frameY1440Buf :
  //     ctx.frameY1920Buf, 0, frame->data[0], frame->height *
  //     frame->linesize[0]);
  // wgpuCommandEncoderCopyBufferToTexture(encoder, &copyBuffer, &copyTexture,
  //                                       &copySize);

  copyTexture.texture = ctx.frameTextureY;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[0],
                        frame->height * frame->linesize[0], &textureDataLayout,
                        &copySize);

  copyTexture.texture = ctx.frameTextureU;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[1],
                        frame->height * frame->linesize[1],
                        &textureDataLayoutUv, &copySizeuv);

  copyTexture.texture = ctx.frameTextureV;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[2],
                        frame->height * frame->linesize[2],
                        &textureDataLayoutUv, &copySizeuv);

  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &renderPass); // create pass
  wgpuRenderPassEncoderSetPipeline(pass, ctx.pipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, ctx.bindGroup, 0, 0);
  // wgpuRenderPassEncoderSetVertexBuffer(pass, 0, ctx.vertBuf, 0,
  //                                      WGPU_WHOLE_SIZE);
  // wgpuRenderPassEncoderSetIndexBuffer(pass, ctx.indxBuf,
  // WGPUIndexFormat_Uint16,
  //                                     0, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

  wgpuRenderPassEncoderEndPass(pass);
  wgpuRenderPassEncoderRelease(pass); // release pass
  WGPUCommandBuffer commands =
      wgpuCommandEncoderFinish(encoder, nullptr); // create commands
  wgpuCommandEncoderRelease(encoder);             // release encoder

  wgpuQueueSubmit(ctx.queue, 1, &commands);
  wgpuCommandBufferRelease(commands);  // release commands
  wgpuTextureViewRelease(backBufView); // release textureView
}
