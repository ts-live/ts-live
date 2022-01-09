#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/val.h>
#include <spdlog/spdlog.h>
#include <vector>

#include "patpmt.hpp"
#include "ts.hpp"

namespace {
const size_t MAX_INPUT_BUFFER = 1024 * 1024;
const size_t RESERVED_SIZE = 200;
std::uint8_t inputBuffer[MAX_INPUT_BUFFER];
size_t inputBufferReadIndex = 0;
size_t inputBufferWriteIndex = 0;
} // namespace

EM_JS(void, callOnPacket, (void *ptr),
      {
          // console.log("callback", HEAP8.subarray(ptr, ptr + 188));
      });

namespace {

TsPacket *getNextPacket() {
  while (inputBufferReadIndex < inputBufferWriteIndex &&
         inputBuffer[inputBufferReadIndex] != TS_PACKET_SYNC_BYTE) {
    inputBufferReadIndex++;
  }
  if (inputBuffer[inputBufferReadIndex] != TS_PACKET_SYNC_BYTE) {
    return nullptr;
  }
  if (inputBufferWriteIndex - inputBufferReadIndex < TS_PACKET_SIZE) {
    return nullptr;
  }
  TsPacket *ptr =
      reinterpret_cast<TsPacket *>(&inputBuffer[inputBufferReadIndex]);
  inputBufferReadIndex += TS_PACKET_SIZE;
  return ptr;
}

emscripten::val getNextInputBuffer(size_t nextSize) {
  if (inputBufferWriteIndex + nextSize >= MAX_INPUT_BUFFER - RESERVED_SIZE) {
    size_t remainReadSize = inputBufferWriteIndex - inputBufferReadIndex;
    memcpy(inputBuffer, &inputBuffer[inputBufferReadIndex], remainReadSize);
    inputBufferReadIndex = 0;
    inputBufferWriteIndex = remainReadSize;
  }
  return emscripten::val(emscripten::typed_memory_view(
      nextSize, &inputBuffer[inputBufferWriteIndex]));
}

PatPmtFinder patPmtFinder;

void enqueueData(size_t nextSize) {
  unsigned int count = 0;
  spdlog::set_level(spdlog::level::debug);

  assert(inputBufferWriteIndex + nextSize <= MAX_INPUT_BUFFER);
  inputBufferWriteIndex += nextSize;

  TsPacket *ptr;
  while ((ptr = getNextPacket()) != nullptr) {
    count++;
    TsPacket &packet = *ptr;
    auto pid = packet.header.pid();
    if (packet.header.syncByte != TS_PACKET_SYNC_BYTE) {
      auto sb = packet.header.syncByte;
      spdlog::error("SyncByte Error: {:#02x}", sb);
    }
    if (PatPmtFinder::isPatPid(pid)) {
      patPmtFinder.feedPatPacket(packet);
    } else if (patPmtFinder.isPmtPid(pid)) {
      patPmtFinder.feedPmtPacket(packet);
    } else if (patPmtFinder.isAudioPid(pid)) {
      // TODO: feed AudioPesStream
      spdlog::debug("Found Audio PES packet: {:#06x}", pid);
    } else if (patPmtFinder.isVideoPid(pid)) {
      // TODO: feed VideoPesStream
      spdlog::debug("Found Video PES packet: {:#06x}", pid);
    } else {
      // spdlog::debug("Unknown PID:{:#04x} count:[{}] {}", pid, count,
      //               packet.header.toString());
    }
  }
  // spdlog::debug("{} packets processed.", count);
}

std::string getExceptionMessage(uintptr_t ptr) {
  std::exception *exptr = reinterpret_cast<std::exception *>(ptr);
  return fmt::format("Exception: {}", exptr->what());
}

} // namespace

EMSCRIPTEN_BINDINGS(splitter_wasm_module) {
  emscripten::function("getExceptionMessage", &getExceptionMessage,
                       emscripten::allow_raw_pointers());
  emscripten::function("getNextInputBuffer", &getNextInputBuffer);
  emscripten::function("enqueueData", &enqueueData);
}
