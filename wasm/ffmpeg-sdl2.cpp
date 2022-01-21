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
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

struct context {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  SDL_AudioDeviceID dev;
  SDL_AudioSpec openedAudioSpec;
  size_t textureWidth;
  size_t textureHeight;
  int iteration;
  // std::chrono::system_clock::time_point fpsCounts[FPS_COUNT];
};

namespace {
const size_t WIDTH = 1920;
const size_t HEIGHT = 1080;

const size_t MAX_INPUT_BUFFER = 20 * 1024 * 1024;

std::uint8_t inputBuffer[MAX_INPUT_BUFFER];
std::mutex inputBufferMtx;
std::condition_variable waitCv;

size_t inputBufferReadIndex = 0;
size_t inputBufferWriteIndex = 0;

// for SDL
context ctx;

// for libav
std::deque<AVFrame *> videoFrameQueue, audioFrameQueue;
std::mutex videoFrameMtx, audioFrameMtx;
bool videoFrameFound = false;

int64_t initPts = -1;

bool reseted = false;

bool doDeinterlace = false;
} // namespace

// utility
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

// Interlace Setting
void setDeinterlace(bool deinterlace) { doDeinterlace = deinterlace; }

// Buffer control
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
  inputBufferWriteIndex += nextSize;
  waitCv.notify_all();
}

// reset
void reset() {
  reseted = true;
  {
    std::lock_guard<std::mutex> lock(inputBufferMtx);
    inputBufferReadIndex = 0;
    inputBufferWriteIndex = 0;
  }
  {
    std::lock_guard<std::mutex> lock(videoFrameMtx);
    while (videoFrameQueue.size() > 0) {
      auto frame = videoFrameQueue.front();
      videoFrameQueue.pop_front();
      av_frame_free(&frame);
    }
  }
  {
    std::lock_guard<std::mutex> lock(audioFrameMtx);
    while (audioFrameQueue.size() > 0) {
      auto frame = audioFrameQueue.front();
      audioFrameQueue.pop_front();
      av_frame_free(&frame);
    }
  }
  // SDL Audio
  SDL_ClearQueuedAudio(ctx.dev);
}

// decoder
void decoderThread() {
  AVFormatContext *formatContext = nullptr;
  AVIOContext *avioContext = nullptr;
  uint8_t *ibuf = nullptr;
  size_t ibufSize = 64 * 1024;
  size_t requireBufSize = 2 * 1024 * 1024;

  AVStream *videoStream = nullptr;
  AVStream *audioStream = nullptr;
  AVStream *captionStream = nullptr;

  const AVCodec *videoCodec = nullptr;
  const AVCodec *audioCodec = nullptr;

  AVCodecContext *videoCodecContext = nullptr;
  AVCodecContext *audioCodecContext = nullptr;

  AVFrame *frame = nullptr;
  AVFrame *filteredFrame = nullptr;

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
        spdlog::debug(
            "stream[{}]: codec_type:{} tag:{:x} codecName:{} video_delay:{} "
            "dim:{}x{}",
            i, formatContext->streams[i]->codecpar->codec_type,
            formatContext->streams[i]->codecpar->codec_tag,
            avcodec_get_name(formatContext->streams[i]->codecpar->codec_id),
            formatContext->streams[i]->codecpar->video_delay,
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
        if (formatContext->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_SUBTITLE &&
            captionStream == nullptr) {
          captionStream = formatContext->streams[i];
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
        spdlog::error("No supported decoder for Video ...");
        return;
      } else {
        spdlog::debug("Video Decoder created.");
      }
    }
    if (audioCodec == nullptr) {
      audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
      if (audioCodec == nullptr) {
        spdlog::error("No supported decoder for Audio ...");
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

  // FilterGraph作成
  const AVFilter *bufferSource = avfilter_get_by_name("buffer");
  const AVFilter *bufferSink = avfilter_get_by_name("buffersink");
  AVFilterInOut *inputs = avfilter_inout_alloc();
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterGraph *filterGraph = avfilter_graph_alloc();
  AVFilterContext *bufferSinkContext;
  AVFilterContext *bufferSourceContext;
  int ret;

  if (!outputs || !inputs || !filterGraph) {
    spdlog::error("Can't allocate avfilter in/out/filterGraph");
    return;
  }

  std::string bufferSourceArgs = fmt::format(
      "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
      videoCodecContext->width, videoCodecContext->height,
      videoCodecContext->pix_fmt, videoStream->time_base.num,
      videoStream->time_base.den, videoCodecContext->sample_aspect_ratio.num,
      videoCodecContext->sample_aspect_ratio.den);

  // Buffer video source
  ret =
      avfilter_graph_create_filter(&bufferSourceContext, bufferSource, "in",
                                   bufferSourceArgs.c_str(), NULL, filterGraph);
  if (ret < 0) {
    spdlog::error("Can't create buffer source");
    return;
  }

  ret = avfilter_graph_create_filter(&bufferSinkContext, bufferSink, "out",
                                     NULL, NULL, filterGraph);
  if (ret < 0) {
    spdlog::error("Can't create buffer sink");
    return;
  }

  enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};

  ret = av_opt_set_int_list(bufferSinkContext, "pix_fmts", pix_fmts,
                            AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) {
    spdlog::error("Can't set opt buffer sink");
    return;
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = bufferSourceContext;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = bufferSinkContext;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  std::string filtersDescription = fmt::format("yadif");
  // std::string filtersDescription = fmt::format("vflip");
  // std::string filtersDescription = fmt::format("bwdif");
  ret = avfilter_graph_parse_ptr(filterGraph, filtersDescription.c_str(),
                                 &inputs, &outputs, NULL);
  if (ret < 0) {
    spdlog::error("Can't parse filter Description");
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return;
  }

  ret = avfilter_graph_config(filterGraph, NULL);
  if (ret < 0) {
    spdlog::error("Can't configure filter Description");
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return;
  }
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  // decode phase
  while (!reseted) {
    // decode frames
    if (frame == nullptr) {
      frame = av_frame_alloc();
    }
    if (filteredFrame == nullptr) {
      filteredFrame = av_frame_alloc();
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
        if (initPts < 0) {
          initPts = frame->pts;
        }
        frame->time_base = videoStream->time_base;

        if (doDeinterlace) {
          // Filterに突っ込む前にPTSを保存する
          auto pts = frame->pts;
          // Filterに突っ込む
          int ret = av_buffersrc_add_frame(bufferSourceContext, frame);
          if (ret < 0) {
            spdlog::error("av_buffersrc_add_frame_flags error: {}",
                          av_err2str(ret));
          }

          while (true) {
            int ret = av_buffersink_get_frame(bufferSinkContext, filteredFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
              break;
            }
            if (ret < 0) {
              spdlog::error("av_buffersink_get_frame error: {}",
                            av_err2str(ret));
            }
            // PTSを復元
            filteredFrame->pts = pts;
            {
              std::lock_guard<std::mutex> lock(videoFrameMtx);
              videoFrameFound = true;
              videoFrameQueue.push_back(av_frame_clone(filteredFrame));
              av_frame_free(&filteredFrame);
            }
          }
        } else {
          std::lock_guard<std::mutex> lock(videoFrameMtx);
          videoFrameFound = true;
          videoFrameQueue.push_back(av_frame_clone(frame));
        }
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
        if (initPts < 0) {
          initPts = frame->pts;
        }
        frame->time_base = audioStream->time_base;
        if (videoFrameFound) {
          std::lock_guard<std::mutex> lock(audioFrameMtx);
          audioFrameQueue.push_back(av_frame_clone(frame));
        }
      }
    }
    if (packet.stream_index == captionStream->index) {
      char buffer[packet.size + 2];
      memcpy(buffer, packet.data, packet.size);
      buffer[packet.size + 1] = '\0';
      std::string str = fmt::format("{:02X}", packet.data[0]);
      for (int i = 1; i < packet.size; i++) {
        str += fmt::format(" {:02x}", packet.data[i]);
      }
      spdlog::debug("CaptionPacket received. size: {} data: [{}]", packet.size,
                    str);
    }
    av_packet_unref(&packet);
  }
  avcodec_close(videoCodecContext);
  avcodec_free_context(&videoCodecContext);
  avcodec_close(audioCodecContext);
  avcodec_free_context(&audioCodecContext);

  avio_context_free(&avioContext);
  avformat_free_context(formatContext);

  avfilter_graph_free(&filterGraph);
}

void mainloop(void *arg) {

  // mainloop
  // spdlog::info("mainloop! {}", videoFrameQueue.size());
  if (videoFrameQueue.size() > 0 && audioFrameQueue.size() > 0) {
    // std::lock_guard<std::mutex> lock(videoFrameMtx);
    // spdlog::info("found Current Frame {}x{} bufferSize:{}",
    // currentFrame->width,
    //              currentFrame->height, bufferSize);
    // 次のVideoFrameをまずは見る（popはまだしない）
    AVFrame *currentFrame = videoFrameQueue.front();
    spdlog::debug("VideoFrame@mainloop pts:{} time_base:{} AudioQueueSize:{}",
                  currentFrame->pts, av_q2d(currentFrame->time_base),
                  audioFrameQueue.size());

    // Textureとサイズ合わせ
    if (currentFrame->width != ctx.textureWidth ||
        currentFrame->height != ctx.textureHeight) {
      SDL_DestroyTexture(ctx.texture);
      ctx.texture =
          SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_IYUV,
                            SDL_TextureAccess::SDL_TEXTUREACCESS_STREAMING,
                            currentFrame->width, currentFrame->height);
      ctx.textureWidth = currentFrame->width;
      ctx.textureHeight = currentFrame->height;
    }

    // AudioFrameは完全に見るだけ
    AVFrame *audioFrame = audioFrameQueue.front();

    // VideoとAudioのPTSをクロックから時間に直す
    // TODO: クロック一回転したときの処理
    double videoPtsTime = currentFrame->pts * av_q2d(currentFrame->time_base);
    double audioPtsTime = audioFrame->pts * av_q2d(audioFrame->time_base);

    // SDL_GetQueuedAudioSize:
    // AudioQueueに溜まってる分（バイト数なので型サイズとチャンネル数で割る）
    // ctx.openedAudioSpec.samples: SDLの中のバッファサイズ
    // 本当は更に+でデバイスのバッファサイズがあるはず
    double queuedSize = static_cast<double>(SDL_GetQueuedAudioSize(ctx.dev)) /
                            (sizeof(float) * ctx.openedAudioSpec.channels) +
                        ctx.openedAudioSpec.samples;

    // 上記から推定される、現在再生している音声のPTS（時間）
    double estimatedAudioPlayTime =
        audioPtsTime - (double)queuedSize / ctx.openedAudioSpec.freq;

    // 1フレーム分くらいはズレてもいいからこれでいいか。フレーム真面目に考えると良くわからない。
    bool showFlag = estimatedAudioPlayTime > videoPtsTime;

    spdlog::debug("Time@mainloop VideoPTSTime: {} AudioPTSTime: {} "
                  "QueuedAudioSize: {} freq: {} EstimatedTime: {} showFlag: {}",
                  videoPtsTime, audioPtsTime, SDL_GetQueuedAudioSize(ctx.dev),
                  ctx.openedAudioSpec.freq, estimatedAudioPlayTime, showFlag);

    // リップシンク条件を満たしてたらVideoFrame再生
    if (showFlag) {
      {
        std::lock_guard<std::mutex> lock(videoFrameMtx);
        videoFrameQueue.pop_front();
      }
      int bufferSize = av_image_get_buffer_size(
          (AVPixelFormat)currentFrame->format, currentFrame->width,
          currentFrame->height, 1);
      uint8_t *buf;
      int pitch;
      SDL_LockTexture(ctx.texture, NULL, reinterpret_cast<void **>(&buf),
                      &pitch);
      av_image_copy_to_buffer(buf, bufferSize, currentFrame->data,
                              currentFrame->linesize,
                              (AVPixelFormat)currentFrame->format,
                              currentFrame->width, currentFrame->height, 1);
      SDL_UnlockTexture(ctx.texture);
      av_frame_free(&currentFrame);

      SDL_RenderCopy(ctx.renderer, ctx.texture, NULL, NULL);
      SDL_RenderPresent(ctx.renderer);
    }
  }

  // AudioFrameはVideoFrame処理でのPTS参照用に1個だけキューに残す
  while (audioFrameQueue.size() > 1) {
    AVFrame *frame = nullptr;
    {
      std::lock_guard<std::mutex> lock(audioFrameMtx);
      frame = audioFrameQueue.front();
      audioFrameQueue.pop_front();
    }
    spdlog::debug("AudioFrame@mainloop pts:{} time_base:{} nb_samples:{}",
                  frame->pts, av_q2d(frame->time_base), frame->nb_samples);

    auto &spec = ctx.openedAudioSpec;

    if (ctx.dev == 0 || spec.freq != frame->sample_rate ||
        spec.channels != frame->channels) {
      SDL_PauseAudioDevice(ctx.dev, 1);
      SDL_ClearQueuedAudio(ctx.dev);
      SDL_CloseAudioDevice(ctx.dev);

      // オーディオデバイス再オープン
      SDL_AudioSpec want;
      SDL_zero(want);
      want.freq = frame->sample_rate;
      want.format = AUDIO_F32; // TODO
      want.channels = frame->channels;
      want.samples = 4096;

      SDL_ClearError();
      spdlog::info("SDL_OpenAudioDevice dev:{} {}", ctx.dev, SDL_GetError());
      ctx.dev = SDL_OpenAudioDevice(NULL, 0, &want, &ctx.openedAudioSpec, 0);
      spdlog::info("SDL_OpenAudioDevice dev:{} {}", ctx.dev, SDL_GetError());
      spdlog::info("frame freq:{} channels:{}", frame->sample_rate,
                   frame->channels);
      spdlog::info("want: freq:{} format:{} channels:{}", want.freq,
                   want.format, want.channels);

      SDL_PauseAudioDevice(ctx.dev, 0);
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
          SDL_QueueAudio(ctx.dev, &buf[0],
                         sizeof(float) * frame->nb_samples * frame->channels);
      if (ret < 0) {
        spdlog::info("SDL_QueueAudio: {}: {}", ret, SDL_GetError());
      }
    } else {
      int ret = SDL_QueueAudio(ctx.dev, frame->buf[0]->data,
                               sizeof(float) * frame->nb_samples);
      if (ret < 0) {
        spdlog::info("SDL_QueueAudio: dev:{} {}: {} GetQueuedAudioSize:{}",
                     ctx.dev, ret, SDL_GetError(),
                     SDL_GetQueuedAudioSize(ctx.dev));
      }
    }
    av_frame_free(&frame);
  }
}

int main() {
  spdlog::info("Wasm main() started.");

  // SDL初期化
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

  // Disable Keyboard handling
  // https://github.com/emscripten-core/emscripten/issues/3621
  SDL_EventState(SDL_TEXTINPUT, SDL_DISABLE);
  SDL_EventState(SDL_KEYDOWN, SDL_DISABLE);
  SDL_EventState(SDL_KEYUP, SDL_DISABLE);

  ctx.dev = 0;

  ctx.window = SDL_CreateWindow("video", SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT,
                                SDL_WINDOW_SHOWN);

  ctx.renderer = SDL_CreateRenderer(ctx.window, -1, 0);
  ctx.texture = SDL_CreateTexture(
      ctx.renderer, SDL_PIXELFORMAT_IYUV,
      SDL_TextureAccess::SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
  ctx.iteration = 0;
  ctx.textureWidth = WIDTH;
  ctx.textureHeight = HEIGHT;

  auto now = std::chrono::system_clock::now();

  // デコーダスレッド起動
  spdlog::info("Starting decoder thread.");
  std::thread th([]() {
    while (true) {
      reseted = false;
      decoderThread();
    }
  });

  // fps指定するとrAFループじゃなくタイマーになるので裏周りしても再生が続く。fps<=0だとrAFが使われるらしい。
  const int fps = 60;
  const int simulate_infinite_loop = 1;
  spdlog::info("Starting main loop.");
  emscripten_set_main_loop_arg(mainloop, NULL, fps, simulate_infinite_loop);

  // SDL_DestroyRenderer(ctx.renderer);
  // SDL_DestroyWindow(ctx.window);
  // SDL_Quit();

  return EXIT_SUCCESS;
}

EMSCRIPTEN_BINDINGS(ffmpeg_sdl2_module) {
  emscripten::function("getExceptionMsg", &getExceptionMsg);
  emscripten::function("showVersionInfo", &showVersionInfo);
  emscripten::function("setDeinterlace", &setDeinterlace);
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("commitInputData", &commitInputData);
  emscripten::function("reset", &reset);
  emscripten::function("setLogLevelDebug", &setLogLevelDebug);
  emscripten::function("setLogLevelInfo", &setLogLevelInfo);
}
