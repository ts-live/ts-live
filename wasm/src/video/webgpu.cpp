// from https://github.com/cwoffenden/hello-webgpu/blob/main/src/main.cpp

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <fstream>
#include <functional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <webgpu/webgpu_cpp.h>

extern "C" {
#include <libavutil/frame.h>
}

struct WebGPUContext {
  int textureWidth = 0;
  int textureHeight = 0;
  WGPUDevice device;
  WGPUSwapChain swapChain;
  WGPUQueue queue;
  WGPUComputePipeline yadifPipeline;
  WGPURenderPipeline pipeline;
  WGPUBindGroupLayout yadifBindGroupLayout, bindGroupLayout;
  WGPUTexture curTextureY, curTextureU, curTextureV;
  WGPUTextureView curViewY, curViewU, curViewV;
  WGPUTexture prevTextureY, prevTextureU, prevTextureV;
  WGPUTextureView prevViewY, prevViewU, prevViewV;
  WGPUTexture nextTextureY, nextTextureU, nextTextureV;
  WGPUTextureView nextViewY, nextViewU, nextViewV;
  WGPUTexture frameTexture;
  WGPUTextureView frameView;
  WGPUBindGroup yadifBindGroup, bindGroup;
  WGPUSampler sampler;
};

static WebGPUContext ctx;

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
  wgsl.code = code;
  WGPUShaderModuleDescriptor desc = {};
  desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&wgsl);
  desc.label = label;
  return wgpuDeviceCreateShaderModule(ctx.device, &desc);
}

static void releaseTextures() {
  wgpuTextureViewRelease(ctx.curViewY);
  wgpuTextureViewRelease(ctx.curViewU);
  wgpuTextureViewRelease(ctx.curViewV);
  wgpuTextureRelease(ctx.curTextureY);
  wgpuTextureRelease(ctx.curTextureU);
  wgpuTextureRelease(ctx.curTextureV);

  wgpuTextureViewRelease(ctx.prevViewY);
  wgpuTextureViewRelease(ctx.prevViewU);
  wgpuTextureViewRelease(ctx.prevViewV);
  wgpuTextureRelease(ctx.prevTextureY);
  wgpuTextureRelease(ctx.prevTextureU);
  wgpuTextureRelease(ctx.prevTextureV);

  wgpuTextureViewRelease(ctx.nextViewY);
  wgpuTextureViewRelease(ctx.nextViewU);
  wgpuTextureViewRelease(ctx.nextViewV);
  wgpuTextureRelease(ctx.nextTextureY);
  wgpuTextureRelease(ctx.nextTextureU);
  wgpuTextureRelease(ctx.nextTextureV);

  wgpuTextureViewRelease(ctx.frameView);
  wgpuTextureRelease(ctx.frameTexture);
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
  textureDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
                      WGPUTextureUsage_TextureBinding;
  textureDesc.sampleCount = 1;
  textureDesc.mipLevelCount = 1;

  textureDesc.size = size;
  ctx.curTextureY = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.prevTextureY = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.nextTextureY = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  textureDesc.size = uvSize;
  ctx.curTextureU = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.curTextureV = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.prevTextureU = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.prevTextureV = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.nextTextureU = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
  ctx.nextTextureV = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

  textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
  textureDesc.usage = WGPUTextureUsage_CopyDst |
                      WGPUTextureUsage_TextureBinding |
                      WGPUTextureUsage_StorageBinding;
  textureDesc.size = size;
  ctx.frameTexture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

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

  ctx.curViewY = wgpuTextureCreateView(ctx.curTextureY, &viewDesc);
  ctx.curViewU = wgpuTextureCreateView(ctx.curTextureU, &viewDesc);
  ctx.curViewV = wgpuTextureCreateView(ctx.curTextureV, &viewDesc);
  ctx.prevViewY = wgpuTextureCreateView(ctx.prevTextureY, &viewDesc);
  ctx.prevViewU = wgpuTextureCreateView(ctx.prevTextureU, &viewDesc);
  ctx.prevViewV = wgpuTextureCreateView(ctx.prevTextureV, &viewDesc);
  ctx.nextViewY = wgpuTextureCreateView(ctx.nextTextureY, &viewDesc);
  ctx.nextViewU = wgpuTextureCreateView(ctx.nextTextureU, &viewDesc);
  ctx.nextViewV = wgpuTextureCreateView(ctx.nextTextureV, &viewDesc);

  viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
  ctx.frameView = wgpuTextureCreateView(ctx.frameTexture, &viewDesc);

  WGPUBindGroupEntry bgEntries[] = {
      {.binding = 0, .sampler = ctx.sampler},
      {.binding = 1, .textureView = ctx.frameView},
      {.binding = 2, .textureView = ctx.curViewY},
      {.binding = 3, .textureView = ctx.curViewU},
      {.binding = 4, .textureView = ctx.curViewV},
      {.binding = 5, .textureView = ctx.prevViewY},
      {.binding = 6, .textureView = ctx.prevViewU},
      {.binding = 7, .textureView = ctx.prevViewV},
      {.binding = 8, .textureView = ctx.nextViewY},
      {.binding = 9, .textureView = ctx.nextViewU},
      {.binding = 10, .textureView = ctx.nextViewV},
  };
  WGPUBindGroupDescriptor bgDesc = {};
  bgDesc.layout = ctx.yadifBindGroupLayout;
  bgDesc.entryCount = sizeof(bgEntries) / sizeof(bgEntries[0]);
  bgDesc.entries = bgEntries;

  ctx.yadifBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);

  bgDesc.entryCount = 2;
  bgDesc.layout = ctx.bindGroupLayout;
  ctx.bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);

  ctx.textureWidth = width;
  ctx.textureHeight = height;
}

static void createPipeline() {
  std::string vertWgsl =
#include "shaders/simple.vert.wgsl"
      ;
  std::string fragWgsl =
#include "shaders/simple.frag.wgsl"
      ;
  std::string yadifWgsl =
#include "shaders/yadif.frag.wgsl"
      ;

  WGPUShaderModule vertMod = createShader(vertWgsl.c_str());
  WGPUShaderModule fragMod = createShader(fragWgsl.c_str());
  WGPUShaderModule yadifMod = createShader(yadifWgsl.c_str());

  WGPUSamplerBindingLayout samplerLayout = {};
  samplerLayout.type = WGPUSamplerBindingType_Filtering;

  WGPUTextureBindingLayout textureLayout = {};
  textureLayout.sampleType = WGPUTextureSampleType_Float;
  textureLayout.multisampled = false;
  textureLayout.viewDimension = WGPUTextureViewDimension_2D;

  WGPUStorageTextureBindingLayout storageTextureLayout = {};
  storageTextureLayout.format = WGPUTextureFormat_RGBA8Unorm;
  storageTextureLayout.access = WGPUStorageTextureAccess_WriteOnly;
  storageTextureLayout.viewDimension = WGPUTextureViewDimension_2D;

  WGPUBindGroupLayoutEntry bglEntries[] = {
      {.binding = 0,
       .visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute,
       .sampler = samplerLayout},
      {.binding = 1,
       .visibility = WGPUShaderStage_Compute,
       .storageTexture = storageTextureLayout},
      {.binding = 2,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 3,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 4,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 5,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 6,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 7,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 8,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 9,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
      {.binding = 10,
       .visibility = WGPUShaderStage_Compute,
       .texture = textureLayout},
  };

  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = sizeof(bglEntries) / sizeof(bglEntries[0]);
  bglDesc.entries = bglEntries;
  ctx.yadifBindGroupLayout =
      wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);

  bglEntries[1] = {
      .binding = 1,
      .visibility = WGPUShaderStage_Fragment,
      .texture = textureLayout,
  };

  bglDesc.entryCount = 2;
  ctx.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);

  WGPUPipelineLayoutDescriptor layoutDesc = {};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = &ctx.yadifBindGroupLayout;
  WGPUPipelineLayout yadifPipelineLayout =
      wgpuDeviceCreatePipelineLayout(ctx.device, &layoutDesc);

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

  WGPUProgrammableStageDescriptor compStageDesc = {
      .module = yadifMod,
      .entryPoint = "main",
  };
  WGPUComputePipelineDescriptor compDesc = {.layout = yadifPipelineLayout,
                                            .compute = compStageDesc};

  ctx.yadifPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &compDesc);

  // partial clean-up (just move to the end, no?)
  wgpuPipelineLayoutRelease(yadifPipelineLayout);
  wgpuPipelineLayoutRelease(pipelineLayout);

  wgpuShaderModuleRelease(fragMod);
  wgpuShaderModuleRelease(vertMod);
}

void initWebGpu() {
  ctx.device = emscripten_webgpu_get_device();

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
  createTextures(1920, 1080);
}

static void (
    *initDeviceCallback)(); // キャプチャするとコンパイルできなかったのでグローバル変数化・・・

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
  colorDesc.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  colorDesc.clearValue.r = 0.0f;
  colorDesc.clearValue.g = 0.0f;
  colorDesc.clearValue.b = 0.0f;
  colorDesc.clearValue.a = 1.0f;

  WGPUComputePassDescriptor compPassDesc = {};

  WGPURenderPassDescriptor renderPassDesc = {};
  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &colorDesc;

  WGPUCommandEncoder encoder =
      wgpuDeviceCreateCommandEncoder(ctx.device, nullptr); // create encoder

  WGPUExtent3D copySize = {
      .width = static_cast<uint32_t>(frame->width),
      .height = static_cast<uint32_t>(frame->height),
      .depthOrArrayLayers = 1,
  };

  WGPUExtent3D copySizeuv = {
      .width = static_cast<uint32_t>(frame->width / 2),
      .height = static_cast<uint32_t>(frame->height / 2),
      .depthOrArrayLayers = 1,
  };

  WGPUTextureDataLayout textureDataLayout = {
      .offset = 0,
      .bytesPerRow = static_cast<uint32_t>(frame->width),
      .rowsPerImage = static_cast<uint32_t>(frame->height),
  };

  WGPUTextureDataLayout textureDataLayoutUv = {
      .offset = 0,
      .bytesPerRow = static_cast<uint32_t>(frame->width / 2),
      .rowsPerImage = static_cast<uint32_t>(frame->height / 2),
  };

  WGPUOrigin3D origin = {};

  WGPUImageCopyTexture copyTexture = {
      .mipLevel = 0,
      .origin = origin,
      .aspect = WGPUTextureAspect::WGPUTextureAspect_All,
  };

  copyTexture.texture = ctx.nextTextureY;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[0],
                        frame->height * frame->linesize[0], &textureDataLayout,
                        &copySize);

  copyTexture.texture = ctx.nextTextureU;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[1],
                        frame->height * frame->linesize[1],
                        &textureDataLayoutUv, &copySizeuv);

  copyTexture.texture = ctx.nextTextureV;
  wgpuQueueWriteTexture(ctx.queue, &copyTexture, frame->data[2],
                        frame->height * frame->linesize[2],
                        &textureDataLayoutUv, &copySizeuv);

  WGPUComputePassEncoder compPass =
      wgpuCommandEncoderBeginComputePass(encoder, &compPassDesc);
  wgpuComputePassEncoderSetPipeline(compPass, ctx.yadifPipeline);
  wgpuComputePassEncoderSetBindGroup(compPass, 0, ctx.yadifBindGroup, 0, 0);
  wgpuComputePassEncoderDispatchWorkgroups(compPass, ctx.textureWidth / 16 / 2,
                                           ctx.textureHeight / 4 / 2, 1);
  wgpuComputePassEncoderEnd(compPass);
  wgpuComputePassEncoderRelease(compPass);

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
      encoder, &renderPassDesc); // create pass
  wgpuRenderPassEncoderSetPipeline(pass, ctx.pipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, ctx.bindGroup, 0, 0);
  wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass); // release pass

  // current => prev
  WGPUImageCopyTexture copySrc = {
      .mipLevel = 0,
      .origin = origin,
      .aspect = WGPUTextureAspect::WGPUTextureAspect_All,
  };
  WGPUImageCopyTexture copyDst = {
      .mipLevel = 0,
      .origin = origin,
      .aspect = WGPUTextureAspect::WGPUTextureAspect_All,
  };

  copySrc.texture = ctx.curTextureY;
  copyDst.texture = ctx.prevTextureY;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySize);
  copySrc.texture = ctx.curTextureU;
  copyDst.texture = ctx.prevTextureU;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySizeuv);
  copySrc.texture = ctx.curTextureV;
  copyDst.texture = ctx.prevTextureV;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySizeuv);

  copySrc.texture = ctx.nextTextureY;
  copyDst.texture = ctx.curTextureY;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySize);
  copySrc.texture = ctx.nextTextureU;
  copyDst.texture = ctx.curTextureU;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySizeuv);
  copySrc.texture = ctx.nextTextureV;
  copyDst.texture = ctx.curTextureV;
  wgpuCommandEncoderCopyTextureToTexture(encoder, &copySrc, &copyDst,
                                         &copySizeuv);

  WGPUCommandBuffer commands =
      wgpuCommandEncoderFinish(encoder, nullptr); // create commands
  wgpuCommandEncoderRelease(encoder);             // release encoder

  wgpuQueueSubmit(ctx.queue, 1, &commands);
  wgpuCommandBufferRelease(commands);  // release commands
  wgpuTextureViewRelease(backBufView); // release textureView
}
