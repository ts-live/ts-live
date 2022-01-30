#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/val.h>
#include <fmt/format.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#include "audio/audioworklet.hpp"
#include "video/webgpu.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace {
const size_t DEFAULT_WIDTH = 1920;
const size_t DEFAULT_HEIGHT = 1080;

const size_t MAX_INPUT_BUFFER = 20 * 1024 * 1024;
const size_t PROBE_SIZE = 1024 * 1024;

std::uint8_t inputBuffer[MAX_INPUT_BUFFER];
std::mutex inputBufferMtx;
std::condition_variable waitCv;

size_t inputBufferReadIndex = 0;
size_t inputBufferWriteIndex = 0;

std::chrono::system_clock::time_point startTime;

// for libav
std::deque<AVFrame *> videoFrameQueue, audioFrameQueue;
std::deque<std::vector<uint8_t>> captionDataQueue;
std::mutex videoFrameMtx, audioFrameMtx, captionDataMtx;
bool videoFrameFound = false;

AVStream *videoStream = nullptr;
AVStream *audioStream = nullptr;
AVStream *captionStream = nullptr;

int64_t initPts = -1;

std::string playFileUrl;
std::thread downloaderThread;

bool resetedDecoder = false;
bool resetedDownloader = false;

bool doDeinterlace = false;

std::vector<emscripten::val> statsBuffer;

emscripten::val captionCallback = emscripten::val::null();
emscripten::val statsCallback = emscripten::val::null();
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

// Callback register
void setCaptionCallback(emscripten::val callback) {
  captionCallback = callback;
}

void setStatsCallback(emscripten::val callback) {
  //
  statsCallback = callback;
}

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

const size_t donwloadRangeSize = 2 * 1024 * 1024;
size_t downloadCount = 0;

void downloadNextRange() {
  emscripten_fetch_attr_t attr;
  emscripten_fetch_attr_init(&attr);
  strcpy(attr.requestMethod, "GET");
  attr.attributes =
      EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
  std::string range = fmt::format("bytes={}-{}", downloadCount,
                                  downloadCount + donwloadRangeSize - 1);
  const char *headers[] = {"Range", range.c_str(), NULL};
  attr.requestHeaders = headers;

  spdlog::debug("request {} Range: {}", playFileUrl, range);
  emscripten_fetch_t *fetch = emscripten_fetch(&attr, playFileUrl.c_str());
  if (fetch->status == 206) {
    spdlog::debug("fetch success size: {}", fetch->numBytes);
    {
      std::lock_guard<std::mutex> lock(inputBufferMtx);
      if (inputBufferWriteIndex + fetch->numBytes >= MAX_INPUT_BUFFER) {
        size_t remainSize = inputBufferWriteIndex - inputBufferReadIndex;
        memcpy(&inputBuffer[0], &inputBuffer[inputBufferReadIndex], remainSize);
        inputBufferReadIndex = 0;
        inputBufferWriteIndex = remainSize;
      }
      memcpy(&inputBuffer[inputBufferWriteIndex], &fetch->data[0],
             fetch->numBytes);
      inputBufferWriteIndex += fetch->numBytes;
      downloadCount += fetch->numBytes;
      waitCv.notify_all();
    }
  } else {
    spdlog::error("fetch failed URL: {} status code: {}", playFileUrl,
                  fetch->status);
  }
  emscripten_fetch_close(fetch);
}

void downloaderThraedFunc() {
  resetedDownloader = false;
  while (!resetedDownloader) {
    size_t remainSize;
    {
      std::lock_guard<std::mutex> lock(inputBufferMtx);
      remainSize = inputBufferWriteIndex - inputBufferReadIndex;
    }
    if (remainSize < donwloadRangeSize / 2) {
      downloadNextRange();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

// reset
void reset() {
  resetedDecoder = true;
  resetedDownloader = true;
  downloadCount = 0;
  playFileUrl = std::string("");

  spdlog::info("downloaderThread joinable: {}", downloaderThread.joinable());
  if (downloaderThread.joinable()) {
    spdlog::info("join to downloader thread");
    downloaderThread.join();
    spdlog::info("done.");
  }
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
  videoStream = nullptr;
  audioStream = nullptr;
  captionStream = nullptr;
}

void playFile(std::string url) {
  spdlog::info("playFile: {}", url);
  playFileUrl = url;
  downloaderThread = std::thread([]() { downloaderThraedFunc(); });
}

// decoder
void decoderThread() {
  AVFormatContext *formatContext = nullptr;
  AVIOContext *avioContext = nullptr;
  uint8_t *ibuf = nullptr;
  size_t ibufSize = 64 * 1024;
  size_t requireBufSize = 2 * 1024 * 1024;

  const AVCodec *videoCodec = nullptr;
  const AVCodec *audioCodec = nullptr;

  AVCodecContext *videoCodecContext = nullptr;
  AVCodecContext *audioCodecContext = nullptr;

  AVFrame *frame = nullptr;

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
      formatContext->probesize = PROBE_SIZE;
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
            formatContext->streams[i]->codecpar->codec_id ==
                AV_CODEC_ID_ARIB_CAPTION &&
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
      spdlog::info("Found video stream index:{} codec:{}={} dim:{}x{} "
                   "colorspace:{}={} colorrange:{}={} delay:{}",
                   videoStream->index, videoStream->codecpar->codec_id,
                   avcodec_get_name(videoStream->codecpar->codec_id),
                   videoStream->codecpar->width, videoStream->codecpar->height,
                   videoStream->codecpar->color_space,
                   av_color_space_name(videoStream->codecpar->color_space),
                   videoStream->codecpar->color_range,
                   av_color_range_name(videoStream->codecpar->color_range),
                   videoStream->codecpar->video_delay);
      spdlog::info("Found audio stream index:{} codecID:{}={} channels:{} "
                   "sample_rate:{}",
                   audioStream->index, audioStream->codecpar->codec_id,
                   avcodec_get_name(audioStream->codecpar->codec_id),
                   audioStream->codecpar->channels,
                   audioStream->codecpar->sample_rate);
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

  // decode phase
  while (!resetedDecoder) {
    if (videoFrameQueue.size() > 180) {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      continue;
    }
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
        if (initPts < 0) {
          initPts = frame->pts;
        }
        frame->time_base = videoStream->time_base;

        {
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
      if (!captionCallback.isNull()) {
        std::vector<uint8_t> buffer(packet.size);
        memcpy(&buffer[0], packet.data, packet.size);
        {
          std::lock_guard<std::mutex> lock(captionDataMtx);
          captionDataQueue.push_back(std::move(buffer));
        }
      }
    }
    av_packet_unref(&packet);
  }
  avcodec_close(videoCodecContext);
  avcodec_free_context(&videoCodecContext);
  avcodec_close(audioCodecContext);
  avcodec_free_context(&audioCodecContext);

  avio_context_free(&avioContext);
  avformat_free_context(formatContext);
}

void mainloop(void *arg) {
  // mainloop
  // spdlog::info("mainloop! {}", videoFrameQueue.size());

  if (videoStream && audioStream && !statsCallback.isNull()) {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - startTime);
    auto data = emscripten::val::object();
    data.set("time", duration.count() / 1000.0);
    data.set("VideoFrameQueueSize", videoFrameQueue.size());
    data.set("AudioFrameQueueSize", audioFrameQueue.size());
    data.set("AudioWorkletBufferSize", bufferedAudioSamples);
    data.set("InputBufferSize", inputBufferWriteIndex - inputBufferReadIndex);
    statsBuffer.push_back(std::move(data));
    if (statsBuffer.size() >= 6) {
      auto statsArray = emscripten::val::array();
      for (int i = 0; i < statsBuffer.size(); i++) {
        statsArray.set(i, statsBuffer[i]);
      }
      statsBuffer.clear();
      statsCallback(statsArray);
    }
  }

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

    // WindowSize確認＆リサイズ
    // TODO:
    // if (ww != videoStream->codecpar->width ||
    //     wh != videoStream->codecpar->height) {
    //   set_style(videoStream->codecpar->width);
    // }

    // AudioFrameは完全に見るだけ
    AVFrame *audioFrame = audioFrameQueue.front();

    // VideoとAudioのPTSをクロックから時間に直す
    // TODO: クロック一回転したときの処理
    double videoPtsTime = currentFrame->pts * av_q2d(currentFrame->time_base);
    double audioPtsTime = audioFrame->pts * av_q2d(audioFrame->time_base);

    // 上記から推定される、現在再生している音声のPTS（時間）
    // double estimatedAudioPlayTime =
    //     audioPtsTime - (double)queuedSize / ctx.openedAudioSpec.freq;
    double estimatedAudioPlayTime =
        audioPtsTime -
        (double)bufferedAudioSamples / audioStream->codecpar->sample_rate;

    // 1フレーム分くらいはズレてもいいからこれでいいか。フレーム真面目に考えると良くわからない。
    bool showFlag = estimatedAudioPlayTime > videoPtsTime;

    // リップシンク条件を満たしてたらVideoFrame再生
    if (showFlag) {
      {
        std::lock_guard<std::mutex> lock(videoFrameMtx);
        videoFrameQueue.pop_front();
      }
      double timestamp =
          currentFrame->pts * av_q2d(currentFrame->time_base) * 1000000;

      // // clang-format off
      // EM_ASM({
      //   if (Module.canvas.width != $0 || Module.canvas.height != $1) {
      //     Module.canvas.width = $0;
      //     Module.canvas.height = $1;
      //     const scaleX = 1920 / $0;
      //     const transform = Module.canvas.style.transform;
      //     if (transform.indexOf('scaleX') < 0) {
      //       const newTransform = `${transform} scaleX(${1920 / $0})`;
      //       Module.canvas.style.transform = newTransform;
      //     } else {
      //       const re = new RegExp('scaleX.([-0-9.]+).');
      //       const newTransform = transform.replace(/scaleX.[-0-9.]+./,
      //       `scaleX(${1920 / $0})`); Module.canvas.style.transform =
      //       newTransform;
      //     }
      //   }
      // }, currentFrame->width, currentFrame->height);
      // EM_ASM({
      //   const videoFrame = new VideoFrame(HEAPU8, {
      //     format: "I420",
      //     layout: [
      //       {
      //         offset: $1,
      //         stride: $2
      //       },
      //       {
      //         offset: $3,
      //         stride: $4
      //       },
      //       {
      //         offset: $5,
      //         stride: $6
      //       }
      //     ],
      //     codedWidth: Module.canvas.width,
      //     codedHeight: Module.canvas.height,
      //     timestamp: $0
      //   });
      //   Module.canvasCtx.drawImage(videoFrame, 0, 0);
      //   videoFrame.close();
      // }, timestamp,
      //   currentFrame->data[0], currentFrame->linesize[0],
      //   currentFrame->data[1], currentFrame->linesize[1],
      //   currentFrame->data[2], currentFrame->linesize[2]);
      // // clang-format on
      drawWebGpu(currentFrame);

      av_frame_free(&currentFrame);
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

    if (frame->channels == 2) {
      feedAudioData(reinterpret_cast<float *>(frame->buf[0]->data),
                    reinterpret_cast<float *>(frame->buf[1]->data),
                    frame->nb_samples);
    } else {
      feedAudioData(reinterpret_cast<float *>(frame->buf[0]->data),
                    reinterpret_cast<float *>(frame->buf[0]->data),
                    frame->nb_samples);
    }

    av_frame_free(&frame);
  }
  if (!captionCallback.isNull()) {
    while (captionDataQueue.size() > 0) {
      auto buffer = captionDataQueue.front();
      {
        std::lock_guard<std::mutex> lock(captionDataMtx);
        captionDataQueue.pop_front();
      }
      auto data = emscripten::val(
          emscripten::typed_memory_view<uint8_t>(buffer.size(), &buffer[0]));
      captionCallback(data);
    }
  }
}

int main() {
  spdlog::info("Wasm main() started.");
  startTime = std::chrono::system_clock::now();

  auto now = std::chrono::system_clock::now();

  // デコーダスレッド起動
  spdlog::info("Starting decoder thread.");
  std::thread th([]() {
    while (true) {
      resetedDecoder = false;
      decoderThread();
    }
  });

  // WebGPU起動
  initWebGpu();

  // AudioWorklet起動
  startAudioWorklet();

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
  emscripten::function("setCaptionCallback", &setCaptionCallback);
  emscripten::function("setStatsCallback", &setStatsCallback);
  emscripten::function("playFile", &playFile);
  emscripten::function("setDeinterlace", &setDeinterlace);
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("commitInputData", &commitInputData);
  emscripten::function("reset", &reset);
  emscripten::function("setLogLevelDebug", &setLogLevelDebug);
  emscripten::function("setLogLevelInfo", &setLogLevelInfo);
  emscripten::function("setBufferedAudioSamples", &setBufferedAudioSamples);
}
