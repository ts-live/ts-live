// from https://github.com/cwoffenden/hello-webgpu/blob/main/src/main.cpp

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>

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

static std::string slurp(const char *filename) {
  std::ifstream in;
  in.open(filename, std::ifstream::in | std::ifstream::binary);
  std::stringstream sstr;
  sstr << in.rdbuf();
  in.close();
  return sstr.str();
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

static void releaseTextures() {
  wgpuTextureViewRelease(ctx.frameViewY);
  wgpuTextureViewRelease(ctx.frameViewU);
  wgpuTextureViewRelease(ctx.frameViewV);
  wgpuTextureRelease(ctx.frameTextureY);
  wgpuTextureRelease(ctx.frameTextureU);
  wgpuTextureRelease(ctx.frameTextureV);
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

static void createPipeline() {
  std::string vertWgsl = slurp("/shaders/simple.vert.wgsl");
  std::string fragWgsl = slurp("/shaders/yuv2rgba.frag.wgsl");

  WGPUShaderModule vertMod = createShader(vertWgsl.c_str());
  WGPUShaderModule fragMod = createShader(fragWgsl.c_str());

  WGPUSamplerBindingLayout samplerLayout = {};
  samplerLayout.type = WGPUSamplerBindingType_Filtering;

  WGPUTextureBindingLayout textureLayout = {};
  textureLayout.sampleType = WGPUTextureSampleType_Float;
  textureLayout.multisampled = false;
  textureLayout.viewDimension = WGPUTextureViewDimension_2D;

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

  WGPUPipelineLayoutDescriptor layoutDesc = {};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &ctx.bindGroupLayout;
  WGPUPipelineLayout pipelineLayout =
      wgpuDeviceCreatePipelineLayout(ctx.device, &layoutDesc);

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

  WGPUFragmentState fragment = {};
  fragment.module = fragMod;
  fragment.entryPoint = "main";
  fragment.targetCount = 1;
  fragment.targets = &colorTarget;

  WGPURenderPipelineDescriptor desc = {};
  desc.fragment = &fragment;

  desc.layout = pipelineLayout;
  desc.depthStencil = nullptr;

  desc.vertex.module = vertMod;
  desc.vertex.entryPoint = "main";

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

  // dummy texture.
  createTextures(16, 16);
}

void drawWebGpu(AVFrame *frame) {
  if (frame->width != ctx.textureWidth || frame->height != ctx.textureHeight) {
    releaseTextures();
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
