// from https://github.com/cwoffenden/hello-webgpu/blob/main/src/main.cpp

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
}

struct context {
  WGPUDevice device;
  WGPUSwapChain swapChain;
  WGPUQueue queue;
  WGPURenderPipeline pipeline;
  WGPUBuffer vertBuf;
  WGPUBuffer indxBuf;
  WGPUBuffer uRotBuf;
  WGPUBuffer frameY1440Buf, frameY1920Buf;
  WGPUTexture frameY1440Texture, frameY1920Texture;
  WGPUTextureView view1440, view1920;
  WGPUBindGroup bindGroup1440, bindGroup1920;
  WGPUSampler sampler;
  float rotDeg = 0.0f;
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

[[stage(fragment)]]
fn main([[location(0)]] fragUV : vec2<f32>) -> [[location(0)]] vec4<f32> {
  var y: vec4<f32> = textureSample(yTexture, mySampler, fragUV);
  return vec4<f32>(y[0], y[0], y[0], 1.0);
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

/**
 * Helper to create a buffer.
 *
 * \param[in] data pointer to the start of the raw data
 * \param[in] size number of bytes in \a data
 * \param[in] usage type of buffer
 */
static WGPUBuffer createBuffer(const void *data, size_t size,
                               WGPUBufferUsage usage) {
  WGPUBufferDescriptor desc = {};
  desc.usage = WGPUBufferUsage_CopyDst | usage;
  desc.size = size;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(ctx.device, &desc);
  wgpuQueueWriteBuffer(ctx.queue, buffer, 0, data, size);
  return buffer;
}

/**
 * Bare minimum pipeline to draw a triangle using the above shaders.
 */
static void createPipelineAndBuffers() {
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
  WGPUBindGroupLayoutEntry bglEntries[2] = {{}, {}};
  bglEntries[0].binding = 0;
  bglEntries[0].visibility = WGPUShaderStage_Fragment;
  bglEntries[0].sampler = samplerLayout;

  bglEntries[1].binding = 1;
  bglEntries[1].visibility = WGPUShaderStage_Fragment;
  bglEntries[1].texture = textureLayout;

  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  WGPUBindGroupLayout bindGroupLayout =
      wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);

  // pipeline layout (used by the render pipeline, released after its creation)
  WGPUPipelineLayoutDescriptor layoutDesc = {};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &bindGroupLayout;
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
  WGPUColorTargetState colorTargets[2] = {colorTarget, colorTarget};

  WGPUFragmentState fragment = {};
  fragment.module = fragMod;
  fragment.entryPoint = "main";
  fragment.targetCount = 1;
  fragment.targets = colorTargets;

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

  // create the buffers (x, y, r, g, b)
  float const vertData[] = {
      -0.8f, -0.8f, 0.0f, 0.0f, 1.0f, // BL
      0.8f,  -0.8f, 0.0f, 1.0f, 0.0f, // BR
      -0.0f, 0.8f,  1.0f, 0.0f, 0.0f, // top
  };
  uint16_t const indxData[] = {
      0, 1, 2, 3, 4, 5,
      0 // padding (better way of doing this?)
  };
  // ctx.vertBuf =
  //     createBuffer(vertData, sizeof(vertData), WGPUBufferUsage_Vertex);
  // ctx.indxBuf = createBuffer(indxData, sizeof(indxData),
  // WGPUBufferUsage_Index);

  // create the uniform bind group (note 'rotDeg' is copied here, not bound in
  // any way)
  ctx.uRotBuf =
      createBuffer(&ctx.rotDeg, sizeof(ctx.rotDeg), WGPUBufferUsage_Uniform);

  std::vector<uint8_t> tmpBuffer(1920 * 1080);

  ctx.frameY1440Buf = createBuffer(&tmpBuffer[0], sizeof(uint8_t) * 1440 * 1080,
                                   WGPUBufferUsage_Uniform);
  ctx.frameY1920Buf = createBuffer(&tmpBuffer[0], sizeof(uint8_t) * 1920 * 1080,
                                   WGPUBufferUsage_Uniform);

  WGPUExtent3D size1440 = {};
  size1440.width = 1440;
  size1440.height = 1080;
  size1440.depthOrArrayLayers = 1;
  WGPUExtent3D size1920 = {};
  size1920.width = 1920;
  size1920.height = 1080;
  size1920.depthOrArrayLayers = 1;

  WGPUTextureDescriptor textureDesc = {};
  textureDesc.dimension = WGPUTextureDimension_2D;
  textureDesc.format = WGPUTextureFormat_R8Unorm;
  textureDesc.size = size1440;
  textureDesc.usage =
      WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
  textureDesc.sampleCount = 1;
  textureDesc.mipLevelCount = 2;
  //   WGPUTextureUsage_None = 0x00000000,
  // WGPUTextureUsage_CopySrc = 0x00000001,
  // WGPUTextureUsage_CopyDst = 0x00000002,
  // WGPUTextureUsage_TextureBinding = 0x00000004,
  // WGPUTextureUsage_StorageBinding = 0x00000008,
  // WGPUTextureUsage_RenderAttachment = 0x00000010,
  // WGPUTextureUsage_Force32 = 0x7FFFFFFF

  ctx.frameY1440Texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  textureDesc.size = size1920;
  ctx.frameY1920Texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.magFilter = WGPUFilterMode_Linear;
  samplerDesc.minFilter = WGPUFilterMode_Linear;

  ctx.sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);

  WGPUBindGroupEntry bgEntries1440[2] = {{}, {}};
  bgEntries1440[0].binding = 0;
  bgEntries1440[0].sampler = ctx.sampler;

  WGPUTextureViewDescriptor viewDesc = {};
  viewDesc.dimension = WGPUTextureViewDimension_2D;
  viewDesc.format = WGPUTextureFormat_R8Unorm;
  viewDesc.arrayLayerCount = 1;
  viewDesc.mipLevelCount = 1;
  viewDesc.aspect = WGPUTextureAspect_All;

  ctx.view1440 = wgpuTextureCreateView(ctx.frameY1440Texture, &viewDesc);
  ctx.view1920 = wgpuTextureCreateView(ctx.frameY1920Texture, &viewDesc);

  bgEntries1440[1].binding = 1;
  bgEntries1440[1].textureView = ctx.view1440;
  bgEntries1440[1].offset = 0;
  bgEntries1440[1].size = sizeof(ctx.frameY1440Buf);

  WGPUBindGroupEntry bgEntries1920[2] = {{}, {}};
  bgEntries1920[0].binding = 0;
  bgEntries1920[0].sampler = ctx.sampler;

  bgEntries1920[1].binding = 1;
  bgEntries1920[1].textureView = ctx.view1920;
  bgEntries1920[1].offset = 0;
  bgEntries1920[1].size = sizeof(ctx.frameY1920Buf);

  WGPUBindGroupDescriptor bgDesc1440 = {};
  bgDesc1440.layout = bindGroupLayout;
  bgDesc1440.entryCount = 2;
  bgDesc1440.entries = bgEntries1440;

  WGPUBindGroupDescriptor bgDesc1920 = {};
  bgDesc1920.layout = bindGroupLayout;
  bgDesc1920.entryCount = 2;
  bgDesc1920.entries = bgEntries1920;

  ctx.bindGroup1440 = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc1440);
  ctx.bindGroup1920 = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc1920);

  // last bit of clean-up
  wgpuBindGroupLayoutRelease(bindGroupLayout);
}

void initWebGpu() {
  ctx.device = emscripten_webgpu_get_device();

  // create queue
  ctx.queue = wgpuDeviceGetQueue(ctx.device);

  // pipeline/buffer
  createPipelineAndBuffers();

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
  WGPUTextureView backBufView =
      wgpuSwapChainGetCurrentTextureView(ctx.swapChain); // create textureView

  WGPURenderPassColorAttachment colorDesc = {};
  colorDesc.view = backBufView;
  colorDesc.loadOp = WGPULoadOp_Clear;
  colorDesc.storeOp = WGPUStoreOp_Store;
  colorDesc.clearColor.r = 0.3f;
  colorDesc.clearColor.g = 0.3f;
  colorDesc.clearColor.b = 0.3f;
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

  // typedef struct WGPUImageCopyBuffer {
  //   WGPUChainedStruct const *nextInChain;
  //   WGPUTextureDataLayout layout;
  //   WGPUBuffer buffer;
  // } WGPUImageCopyBuffer;
  WGPUTextureDataLayout copyBufferLayout = {};
  copyBufferLayout.rowsPerImage = frame->height;
  copyBufferLayout.bytesPerRow = frame->width;
  copyBufferLayout.offset = 0;
  WGPUImageCopyBuffer copyBuffer = {};
  copyBuffer.layout = copyBufferLayout;
  copyBuffer.buffer =
      frame->width == 1440 ? ctx.frameY1440Buf : ctx.frameY1920Buf;

  WGPUOrigin3D origin = {};

  WGPUImageCopyTexture copyTexture = {};
  copyTexture.origin = origin;
  copyTexture.aspect = WGPUTextureAspect::WGPUTextureAspect_All;
  copyTexture.mipLevel = 0;
  copyTexture.texture =
      frame->width == 1440 ? ctx.frameY1440Texture : ctx.frameY1920Texture;

  // wgpuQueueWriteBuffer(
  //     ctx.queue, frame->width == 1440 ? ctx.frameY1440Buf :
  //     ctx.frameY1920Buf, 0, frame->data[0], frame->height *
  //     frame->linesize[0]);
  // wgpuCommandEncoderCopyBufferToTexture(encoder, &copyBuffer, &copyTexture,
  //                                       &copySize);

  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[0],
                        frame->height * frame->linesize[0], &copyBufferLayout,
                        &copySize);

  WGPURenderPassEncoder pass =
      wgpuCommandEncoderBeginRenderPass(encoder, &renderPass); // create pass
  wgpuRenderPassEncoderSetPipeline(pass, ctx.pipeline);
  wgpuRenderPassEncoderSetBindGroup(
      pass, 0, frame->width == 1440 ? ctx.bindGroup1440 : ctx.bindGroup1920, 0,
      0);
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
