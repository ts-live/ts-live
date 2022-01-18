#include <SDL.h>
#include <SDL_audio.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/val.h>
#include <fmt/format.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace {
const size_t MAX_INPUT_BUFFER = 20 * 1024 * 1024;

std::uint8_t inputBuffer[MAX_INPUT_BUFFER];
std::mutex inputBufferMtx;
std::condition_variable waitCv;

size_t inputBufferReadIndex = 0;
size_t inputBufferWriteIndex = 0;

// for libav
AVFormatContext *formatContext = nullptr;
AVIOContext *avioContext = nullptr;
uint8_t *ibuf = nullptr;
size_t ibufSize = 64 * 1024;
size_t requireBufSize = 2 * 1024 * 1024;

AVStream *videoStream = nullptr;
AVStream *audioStream = nullptr;

const AVCodec *videoCodec = nullptr;
const AVCodec *audioCodec = nullptr;

AVCodecContext *videoCodecContext = nullptr;
AVCodecContext *audioCodecContext = nullptr;

AVFrame *frame = nullptr;
std::deque<AVFrame *> videoFrameQueue, audioFrameQueue;
std::mutex videoFrameMtx, audioFrameMtx;
bool videoFrameFound = false;

int64_t initPts = -1;

} // namespace

emscripten::val getNextInputBuffer(size_t nextSize) {
  std::unique_lock<std::mutex> lock(inputBufferMtx);
  waitCv.wait(lock, [&] {
    return MAX_INPUT_BUFFER - inputBufferWriteIndex + inputBufferReadIndex >
           nextSize;
  });
  if (inputBufferWriteIndex + nextSize >= MAX_INPUT_BUFFER) {
    size_t remainSize = inputBufferWriteIndex - inputBufferReadIndex;
    memcpy(&inputBuffer[0], &inputBuffer[inputBufferReadIndex], remainSize);
    inputBufferReadIndex = 0;
    inputBufferWriteIndex = remainSize;
  }
  auto retVal = emscripten::val(emscripten::typed_memory_view<uint8_t>(
      nextSize, &inputBuffer[inputBufferWriteIndex]));
  waitCv.notify_all();
  return retVal;
}

std::string getExceptionMsg(intptr_t ptr) {
  auto e = reinterpret_cast<std::exception *>(ptr);
  return std::string(e->what());
}

void showVersionInfo() {
  printf("version: %s\nconfigure: %s\n", av_version_info(),
         avutil_configuration());
}

void setLogLevelDebug() { spdlog::set_level(spdlog::level::debug); }
void setLogLevelInfo() { spdlog::set_level(spdlog::level::info); }

const size_t WIDTH = 1920;
const size_t HEIGHT = 1080;

struct context {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  SDL_AudioDeviceID dev;
  SDL_AudioSpec openedAudioSpec;
  int iteration;
  // std::chrono::system_clock::time_point fpsCounts[FPS_COUNT];
};

int read_packet(void *opaque, uint8_t *buf, int bufSize) {
  std::unique_lock<std::mutex> lock(inputBufferMtx);
  waitCv.wait(lock, [&] {
    return inputBufferWriteIndex - inputBufferReadIndex >= bufSize;
  });
  memcpy(buf, &inputBuffer[inputBufferReadIndex], bufSize);
  inputBufferReadIndex += bufSize;
  waitCv.notify_all();
  return bufSize;
}

void commitInputData(size_t nextSize) {
  std::lock_guard<std::mutex> lock(inputBufferMtx);
  // spdlog::info("commit Input Data! size:{}", nextSize);
  inputBufferWriteIndex += nextSize;
  waitCv.notify_all();
}

void audioCallback(void *userData, uint8_t *stream, int len) {
  memset(stream, 0, len);
}

void decoderThread(context *ctx) {
  // probe phase
  {
    // probe
    if (ibuf == nullptr) {
      ibuf = static_cast<uint8_t *>(av_malloc(ibufSize));
    }
    if (avioContext == nullptr) {
      avioContext = avio_alloc_context(ibuf, ibufSize, 0, 0, &read_packet,
                                       nullptr, nullptr);
    }
    if (formatContext == nullptr) {
      formatContext = avformat_alloc_context();
      formatContext->pb = avioContext;
      spdlog::debug("calling avformat_open_input");

      if (avformat_open_input(&formatContext, nullptr, nullptr, nullptr) != 0) {
        spdlog::error("avformat_open_input error");
        return;
      }
      spdlog::debug("open success");
      formatContext->probesize = requireBufSize;
    }

    if (videoStream == nullptr || audioStream == nullptr) {
      if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        spdlog::error("avformat_find_stream_info error");
        return;
      }
      spdlog::debug("avformat_find_stream_info success");
      spdlog::debug("nb_streams:{}", formatContext->nb_streams);

      // find video stream
      for (int i = 0; i < (int)formatContext->nb_streams; ++i) {
        spdlog::debug("stream[{}]: codec_type:{} dim:{}x{}", i,
                      formatContext->streams[i]->codecpar->codec_type,
                      formatContext->streams[i]->codecpar->width,
                      formatContext->streams[i]->codecpar->height);
        if (formatContext->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO &&
            videoStream == nullptr) {
          videoStream = formatContext->streams[i];
        }
        if (formatContext->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_AUDIO &&
            audioStream == nullptr) {
          audioStream = formatContext->streams[i];
        }
      }
      if (videoStream == nullptr) {
        spdlog::error("No video stream ...");
        return;
      }
      if (audioStream == nullptr) {
        spdlog::error("No audio stream ...");
        return;
      }
      spdlog::info("Found video stream index:{} codec:{}={} dim:{}x{}",
                   videoStream->index, videoStream->codecpar->codec_id,
                   avcodec_get_name(videoStream->codecpar->codec_id),
                   videoStream->codecpar->width, videoStream->codecpar->height);

      spdlog::info("Found audio stream index:{} codecID:{}={} channels:{}",
                   audioStream->index, audioStream->codecpar->codec_id,
                   avcodec_get_name(audioStream->codecpar->codec_id),
                   audioStream->codecpar->channels);
    }

    // find decoder
    if (videoCodec == nullptr) {
      videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
      if (videoCodec == nullptr) {
        spdlog::error("No supported decoder ...");
        return;
      } else {
        spdlog::debug("Video Decoder created.");
      }
    }
    if (audioCodec == nullptr) {
      audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
      if (audioCodec == nullptr) {
        spdlog::error("No supported decoder ...");
        return;
      } else {
        spdlog::debug("Audio Decoder created.");
      }
    }

    // Codec Context
    if (videoCodecContext == nullptr) {
      videoCodecContext = avcodec_alloc_context3(videoCodec);
      if (videoCodecContext == nullptr) {
        spdlog::error("avcodec_alloc_context3 for video failed");
        return;
      } else {
        spdlog::debug("avcodec_alloc_context3 for video success.");
      }
      // open codec
      if (avcodec_parameters_to_context(videoCodecContext,
                                        videoStream->codecpar) < 0) {
        spdlog::error("avcodec_parameters_to_context failed");
        return;
      }
      if (avcodec_open2(videoCodecContext, videoCodec, nullptr) != 0) {
        spdlog::error("avcodec_open2 failed");
        return;
      }
      spdlog::debug("avcodec for video open success.");
    }
    if (audioCodecContext == nullptr) {
      audioCodecContext = avcodec_alloc_context3(audioCodec);
      if (audioCodecContext == nullptr) {
        spdlog::error("avcodec_alloc_context3 for audio failed");
        return;
      } else {
        spdlog::debug("avcodec_alloc_context3 for audio success.");
      }
      // open codec
      if (avcodec_parameters_to_context(audioCodecContext,
                                        audioStream->codecpar) < 0) {
        spdlog::error("avcodec_parameters_to_context failed");
        return;
      }

      if (avcodec_open2(audioCodecContext, audioCodec, nullptr) != 0) {
        spdlog::error("avcodec_open2 failed");
        return;
      }
      spdlog::debug("avcodec for audio open success.");

      // 巻き戻す
      // inputBufferReadIndex = 0;
    }
  }

  // decode phase
  while (true) {
    // decode frames
    if (frame == nullptr) {
      frame = av_frame_alloc();
    }
    AVPacket packet = AVPacket();
    int videoCount = 0;
    int audioCount = 0;
    int ret = av_read_frame(formatContext, &packet);
    if (ret != 0) {
      spdlog::info("av_read_frame: {} {}", ret, av_err2str(ret));
      continue;
    }
    if (packet.stream_index == videoStream->index) {
      int ret = avcodec_send_packet(videoCodecContext, &packet);
      if (ret != 0) {
        spdlog::error("avcodec_send_packet(video) failed: {} {}", ret,
                      av_err2str(ret));
        // return;
      }
      while (avcodec_receive_frame(videoCodecContext, frame) == 0) {
        videoCount++;
        const AVPixFmtDescriptor *desc =
            av_pix_fmt_desc_get((AVPixelFormat)(frame->format));
        int bufferSize = av_image_get_buffer_size(
            (AVPixelFormat)frame->format, frame->width, frame->height, 1);
        spdlog::debug("VideoFrame: {}x{}x{} pixfmt:{} key:{} interlace:{} "
                      "tff:{} codecContext->field_order:{} pts:{} "
                      "stream.timebase:{} bufferSize:{}",
                      frame->width, frame->height, frame->channels,
                      frame->format, frame->key_frame, frame->interlaced_frame,
                      frame->top_field_first, videoCodecContext->field_order,
                      frame->pts, av_q2d(videoStream->time_base), bufferSize);
        if (desc == nullptr) {
          spdlog::debug("desc is NULL");
        } else {
          spdlog::debug(
              "desc name:{} nb_components:{} comp[0].plane:{} .offet:{} "
              "comp[1].plane:{} .offset:{} comp[2].plane:{} .offset:{}",
              desc->name, desc->nb_components, desc->comp[0].plane,
              desc->comp[0].offset, desc->comp[1].plane, desc->comp[1].offset,
              desc->comp[2].plane, desc->comp[2].offset);
        }
        spdlog::debug(
            "buf[0]size:{} buf[1].size:{} buf[2].size:{} buffer_size:{}",
            frame->buf[0]->size, frame->buf[1]->size, frame->buf[2]->size,
            bufferSize);
        // std::vector<uint8_t> buf(bufferSize);
        // av_image_copy_to_buffer(&buf[0], bufferSize, frame->data,
        //                         frame->linesize,
        //                         (AVPixelFormat)frame->format, frame->width,
        //                         frame->height, 1);
        if (initPts < 0) {
          initPts = frame->pts;
        }
        {
          std::lock_guard<std::mutex> lock(videoFrameMtx);
          videoFrameFound = true;
          videoFrameQueue.push_back(av_frame_clone(frame));
        }
        // double timestamp =
        //     (frame->pts - initPts) * av_q2d(videoStream->time_base) *
        //     1000000;
        // double duration =
        //     frame->pkt_duration * av_q2d(videoStream->time_base) * 1000000;
        // videoCallback(timestamp, duration, frame->width, frame->height,
        //               emscripten::typed_memory_view(bufferSize, &buf[0]));
      }
    }
    if (packet.stream_index == audioStream->index) {
      int ret = avcodec_send_packet(audioCodecContext, &packet);
      if (ret != 0) {
        spdlog::error("avcodec_send_packet(audio) failed: {} {}", ret,
                      av_err2str(ret));
        // return;
      }
      while (avcodec_receive_frame(audioCodecContext, frame) == 0) {
        audioCount++;
        spdlog::debug("AudioFrame: format:{} pts:{} frame timebase:{} stream "
                      "timebase:{} buf[0].size:{} buf[1].size:{} nb_samples:{}",
                      frame->format, frame->pts, av_q2d(frame->time_base),
                      av_q2d(audioStream->time_base), frame->buf[0]->size,
                      frame->buf[1]->size, frame->nb_samples);
        // callOnCallback(frame->pts, "audio", frame->data[0], )
        if (initPts < 0) {
          initPts = frame->pts;
        }
        if (videoFrameFound) {
          std::lock_guard<std::mutex> lock(audioFrameMtx);
          audioFrameQueue.push_back(av_frame_clone(frame));
        }

        // Audioはとりあえず何もしない。あとでOpenAL対応する=>SDLでも良さそう
        // double timestamp =
        //     (frame->pts - initPts) * av_q2d(audioStream->time_base) *
        //     1000000;
        // double duration =
        //     frame->pkt_duration * av_q2d(audioStream->time_base) * 1000000;

        // int bufSize =
        //     av_samples_get_buffer_size(&frame->linesize[0],
        //     frame->channels,
        //                                frame->nb_samples,
        //                                (AVSampleFormat)frame->format, 0) /
        //     sizeof(float);
        // std::vector<float> buf(bufSize);
        // for (int i = 0; i < frame->channels; i++) {
        //   memcpy(&buf[frame->nb_samples * i], frame->buf[i]->data,
        //          sizeof(float) * frame->nb_samples);
        // }

        // audioCallback(timestamp, duration, frame->sample_rate,
        //               frame->nb_samples, frame->channels,
        //               emscripten::typed_memory_view<float>(bufSize,
        //               &buf[0]));
      }
    }
    av_packet_unref(&packet);
  }
}

void mainloop(void *arg) {
  context *ctx = static_cast<context *>(arg);

  // mainloop
  // spdlog::info("mainloop! {}", videoFrameQueue.size());
  if (videoFrameQueue.size() > 0) {
    // std::lock_guard<std::mutex> lock(videoFrameMtx);
    // spdlog::info("found Current Frame {}x{} bufferSize:{}",
    // currentFrame->width,
    //              currentFrame->height, bufferSize);
    uint8_t *buf;
    int pitch;
    AVFrame *currentFrame = nullptr;
    {
      std::lock_guard<std::mutex> lock(videoFrameMtx);
      currentFrame = videoFrameQueue.front();
      videoFrameQueue.pop_front();
    }
    int bufferSize =
        av_image_get_buffer_size((AVPixelFormat)currentFrame->format,
                                 currentFrame->width, currentFrame->height, 1);
    SDL_LockTexture(ctx->texture, NULL, reinterpret_cast<void **>(&buf),
                    &pitch);
    av_image_copy_to_buffer(buf, bufferSize, currentFrame->data,
                            currentFrame->linesize,
                            (AVPixelFormat)currentFrame->format,
                            currentFrame->width, currentFrame->height, 1);
    SDL_UnlockTexture(ctx->texture);
    av_frame_free(&currentFrame);

    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
    SDL_RenderPresent(ctx->renderer);
  }
  while (audioFrameQueue.size() > 0) {
    AVFrame *frame = nullptr;
    {
      std::lock_guard<std::mutex> lock(audioFrameMtx);
      frame = audioFrameQueue.front();
      audioFrameQueue.pop_front();
    }

    auto &spec = ctx->openedAudioSpec;

    if (ctx->dev == 0 || spec.freq != frame->sample_rate ||
        spec.channels != frame->channels) {
      SDL_PauseAudioDevice(ctx->dev, 1);
      SDL_ClearQueuedAudio(ctx->dev);
      SDL_CloseAudioDevice(ctx->dev);

      // オーディオデバイス再オープン
      SDL_AudioSpec want;
      SDL_zero(want);
      want.freq = frame->sample_rate;
      want.format = AUDIO_F32; // TODO
      want.channels = frame->channels;
      want.samples = 4096;

      ctx->dev = SDL_OpenAudioDevice(NULL, 0, &want, &ctx->openedAudioSpec, 0);
      spdlog::info("SDL_OpenAudioDevice dev:{} {}", ctx->dev, SDL_GetError());
      spdlog::info("frame freq:{} channels:{}", frame->sample_rate,
                   frame->channels);
      spdlog::info("want: freq:{} format:{} channels:{}", want.freq,
                   want.format, want.channels);

      SDL_PauseAudioDevice(ctx->dev, 0);
    }

    if (frame->channels > 1) {
      // 詰め直し
      std::vector<float> buf(frame->channels * frame->nb_samples);
      for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->channels; ch++) {
          buf[frame->channels * i + ch] =
              reinterpret_cast<float *>(frame->buf[ch]->data)[i];
        }
      }
      int ret =
          SDL_QueueAudio(ctx->dev, &buf[0],
                         sizeof(float) * frame->nb_samples * frame->channels);
      if (ret < 0) {
        spdlog::info("SDL_QueueAudio: {}: {}", ret, SDL_GetError());
      }
    } else {
      int ret = SDL_QueueAudio(ctx->dev, frame->buf[0]->data,
                               sizeof(float) * frame->nb_samples);
      if (ret < 0) {
        spdlog::info("SDL_QueueAudio: dev:{} {}: {} GetQueuedAudioSize:{}",
                     ctx->dev, ret, SDL_GetError(),
                     SDL_GetQueuedAudioSize(ctx->dev));
      }
    }
    av_frame_free(&frame);
  }
}

int main() {
  spdlog::info("main()");
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  spdlog::info("SDL_Init done.");

  context ctx;
  ctx.dev = 0;

  ctx.window = SDL_CreateWindow("video", SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT,
                                SDL_WINDOW_SHOWN);
  spdlog::info("SDL_CreateWindow done.");
  ctx.renderer = SDL_CreateRenderer(ctx.window, -1, 0);
  spdlog::info("SDL_CreateRenderer done.");
  ctx.texture = SDL_CreateTexture(
      ctx.renderer, SDL_PIXELFORMAT_IYUV,
      SDL_TextureAccess::SDL_TEXTUREACCESS_STREAMING, 1440, HEIGHT);
  spdlog::info("SDL_CreateTexture done.");
  ctx.iteration = 0;

  auto now = std::chrono::system_clock::now();

  const int simulate_infinite_loop = 1;
  const int fps = -1;
  std::thread th([&]() {
    spdlog::info("I am thread!");
    decoderThread(&ctx);
  });
  emscripten_set_main_loop_arg(mainloop, &ctx, fps, simulate_infinite_loop);
  spdlog::info("emscripten_set_main_loop_arg done.");

  // SDL_DestroyRenderer(ctx.renderer);
  // SDL_DestroyWindow(ctx.window);
  // SDL_Quit();

  return EXIT_SUCCESS;
}

EMSCRIPTEN_BINDINGS(ffmpeg_sdl2_module) {
  emscripten::function("getExceptionMsg", &getExceptionMsg);
  emscripten::function("showVersionInfo", &showVersionInfo);
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("commitInputData", &commitInputData);
  emscripten::function("setLogLevelDebug", &setLogLevelDebug);
  emscripten::function("setLogLevelInfo", &setLogLevelInfo);
}
