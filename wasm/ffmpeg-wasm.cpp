#include <chrono>
#include <cstring>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/val.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace {
const size_t MAX_INPUT_BUFFER = 10 * 1024 * 1024;

std::uint8_t inputBuffer[MAX_INPUT_BUFFER];

size_t inputBufferReadIndex = 0;
size_t inputBufferWriteIndex = 0;

// for libav
AVFormatContext *formatContext = nullptr;
AVIOContext *avioContext = nullptr;
uint8_t *ibuf = nullptr;
size_t ibufSize = 32 * 1024;
size_t requireBufSize = 2 * 1024 * 1024;

AVStream *videoStream = nullptr;
AVStream *audioStream = nullptr;

const AVCodec *videoCodec = nullptr;
const AVCodec *audioCodec = nullptr;

AVCodecContext *videoCodecContext = nullptr;
AVCodecContext *audioCodecContext = nullptr;

AVFrame *frame;

int64_t initPts = -1;

} // namespace

// call callback
EM_JS(void, callOnCallback,
      (double timestamp, std::string type, uint8_t *buf, size_t size), {
        window.onCallback({
          type : type,
          timestamp : timestamp,
          buf : HEAPU8.subarray(buf, buf + size)
        });
      });

emscripten::val getNextInputBuffer(size_t nextSize) {
  size_t remainSize = inputBufferWriteIndex - inputBufferReadIndex;
  if (remainSize > 0) {
    memcpy(inputBuffer, &inputBuffer[inputBufferReadIndex], remainSize);
  }
  inputBufferReadIndex = 0;
  inputBufferWriteIndex = remainSize;
  auto retVal = emscripten::val(emscripten::typed_memory_view<uint8_t>(
      nextSize, &inputBuffer[inputBufferWriteIndex]));
  inputBufferWriteIndex += nextSize;
  return retVal;
}

void showVersionInfo() {
  printf("version: %s\nconfigure: %s\n", av_version_info(),
         avutil_configuration());
}

int read_packet(void *opaque, uint8_t *buf, int bufSize) {
  int inputBufferSize = inputBufferWriteIndex - inputBufferReadIndex;
  spdlog::debug("read_packet: inputBufferSize:{} bufSize:{} "
                "inputBufferReadIndex:{} inputBufferWriteIndex:{}",
                inputBufferSize, bufSize, inputBufferReadIndex,
                inputBufferWriteIndex);
  if (inputBufferSize >= bufSize) {
    memcpy(buf, &inputBuffer[inputBufferReadIndex], bufSize);
    inputBufferReadIndex += bufSize;
    return bufSize;
  }
  return AVERROR_EOF;
}

void setLogLevelDebug() { spdlog::set_level(spdlog::level::debug); }
void setLogLevelInfo() { spdlog::set_level(spdlog::level::info); }

void enqueueData(emscripten::val videoCallback, emscripten::val audioCallback) {
  //
  if (inputBufferWriteIndex - inputBufferReadIndex < requireBufSize) {
    return;
  }
  auto startTime = std::chrono::high_resolution_clock::now();
  spdlog::debug("enqueueData readIndex:{} writeIndex:{}", inputBufferReadIndex,
                inputBufferWriteIndex);

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

  // decode
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
    inputBufferReadIndex = 0;
  }

  // decode frames
  if (frame == nullptr) {
    frame = av_frame_alloc();
  }
  AVPacket packet = AVPacket();
  int videoCount = 0;
  int audioCount = 0;
  while ((inputBufferWriteIndex - inputBufferReadIndex) >= requireBufSize &&
         av_read_frame(formatContext, &packet) == 0) {
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
        std::vector<uint8_t> buf(bufferSize);
        av_image_copy_to_buffer(&buf[0], bufferSize, frame->data,
                                frame->linesize, (AVPixelFormat)frame->format,
                                frame->width, frame->height, 1);
        if (initPts < 0) {
          initPts = frame->pts;
        }
        double timestamp =
            (frame->pts - initPts) * av_q2d(videoStream->time_base) * 1000000;
        double duration =
            frame->pkt_duration * av_q2d(videoStream->time_base) * 1000000;
        videoCallback(timestamp, duration, frame->width, frame->height,
                      emscripten::typed_memory_view(bufferSize, &buf[0]));
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
        double timestamp =
            (frame->pts - initPts) * av_q2d(audioStream->time_base) * 1000000;
        double duration =
            frame->pkt_duration * av_q2d(audioStream->time_base) * 1000000;

        int bufSize = av_samples_get_buffer_size(
                          &frame->linesize[0], frame->channels,
                          frame->nb_samples, (AVSampleFormat)frame->format, 0) /
                      sizeof(float);
        std::vector<float> buf(bufSize);
        for (int i = 0; i < frame->channels; i++) {
          memcpy(&buf[frame->nb_samples * i], frame->buf[i]->data,
                 sizeof(float) * frame->nb_samples);
        }

        audioCallback(timestamp, duration, frame->sample_rate,
                      frame->nb_samples, frame->channels,
                      emscripten::typed_memory_view<float>(bufSize, &buf[0]));
      }
    }
    av_packet_unref(&packet);
  }
  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() /
      1000000.0;
  spdlog::debug("VideoFrameCount:{} AudioFrameCount:{} duration:{}", videoCount,
                audioCount, duration);
}

EMSCRIPTEN_BINDINGS(ffmpeg_wasm_module) {
  emscripten::function("showVersionInfo", &showVersionInfo);
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("enqueueData", &enqueueData);
  emscripten::function("setLogLevelDebug", &setLogLevelDebug);
  emscripten::function("setLogLevelInfo", &setLogLevelInfo);
}
