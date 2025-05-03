#include <condition_variable>
#include <cstring>
#include <deque>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/val.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#include "audio/audioworklet.hpp"
#include "decoder/decoder.hpp"
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

void mainloop(void *arg) {
  // mainloop
  decoderMainloop();
}

int main() {
  spdlog::info("Wasm main() started.");
  startTime = std::chrono::system_clock::now();

  // デコーダスレッド起動
  spdlog::info("initializing Decoder");
  initDecoder();

  // AudioWorklet起動
  spdlog::info("initializing audio worklet");
  startAudioWorklet();

  // WebGPU起動
  spdlog::info("initializing webgpu");
  initWebGpu();

  // //
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

EMSCRIPTEN_BINDINGS(ts_live_module) {
  emscripten::function("getExceptionMsg", &getExceptionMsg);
  emscripten::function("showVersionInfo", &showVersionInfo);
  emscripten::function("setCaptionCallback", &setCaptionCallback);
  emscripten::function("setStatsCallback", &setStatsCallback);
  emscripten::function("playFile", &playFile);
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("commitInputData", &commitInputData);
  emscripten::function("reset", &reset);
  emscripten::function("setLogLevelDebug", &setLogLevelDebug);
  emscripten::function("setLogLevelInfo", &setLogLevelInfo);
  emscripten::function("setBufferedAudioSamples", &setBufferedAudioSamples);
  emscripten::function("setAudioGain", &setAudioGain);
  emscripten::function("setDualMonoMode", &setDualMonoMode);
}
