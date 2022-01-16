#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <vector>

const size_t DATA_BUFFER_SIZE = 20 * 1024 * 1024;
const size_t DATA_RESERVED = 200;

class DataFeeder {
public:
  DataFeeder(bool dbg)
      : dataBuffer(DATA_BUFFER_SIZE), readIndex(0), writeIndex(0), debug(dbg) {}
  void feedData(uint8_t *ptr, size_t size) {
    if (debug) {
      spdlog::debug("DataFeeder feedData size:{} readIndex:{} writeIndex:{}",
                    size, readIndex, writeIndex);
    }
    if (writeIndex + size >= dataBuffer.size() - DATA_RESERVED) {
      size_t copySize = writeIndex - readIndex;
      spdlog::warn("DataFeeder cycling...");
      std::copy(dataBuffer.begin() + readIndex, dataBuffer.begin() + writeIndex,
                dataBuffer.begin());
      readIndex = 0;
      writeIndex = copySize;
    }
    std::copy(ptr, ptr + size, dataBuffer.begin() + writeIndex);
    writeIndex += size;
  }
  void updateTimestampMicroSeconds(int64_t us) {
    if (us >= 0)
      timestampMicroSeconds = us;
  }

  virtual void consumeData() = 0;

protected:
  std::vector<uint8_t> dataBuffer;
  size_t readIndex;
  size_t writeIndex;
  double timestampMicroSeconds;

private:
  bool debug;
};

class DebugDataFeeder : public DataFeeder {
public:
  DebugDataFeeder(bool dbg) : DataFeeder(dbg), debug(dbg) {}
  void consumeData() {
    // データを全部読んだことにする
    if (debug) {
      std::string str = fmt::format("{:02x}", dataBuffer[readIndex]);
      for (size_t idx = readIndex + 1; idx < writeIndex; idx++) {
        str += fmt::format(" {:02x}", dataBuffer[idx]);
      }
      spdlog::debug("DebugDataFeeder({}) read {} bytes. [{}]", debug,
                    writeIndex - readIndex, str);
    }
    readIndex = writeIndex;
  }

private:
  bool debug;
};

class CallbackDataFeeder : public DataFeeder {
public:
  CallbackDataFeeder(bool dbg)
      : DataFeeder(dbg),
        foundCallback([](double timestamp, uint8_t *ptr, size_t size) {}),
        debug(dbg) {}
  void
  setFoundCallback(std::function<void(double, uint8_t *, size_t)> callback) {
    foundCallback = callback;
  }
  void consumeData() {
    // データを全部コールバックに送る
    if (debug) {
      std::string str = fmt::format("{:02x}", dataBuffer[readIndex]);
      for (size_t idx = readIndex + 1; idx < writeIndex; idx++) {
        str += fmt::format(" {:02x}", dataBuffer[idx]);
      }
      spdlog::debug("DebugDataFeeder({}) read {} bytes. [{}]", debug,
                    writeIndex - readIndex, str);
    }
    foundCallback(timestampMicroSeconds, &dataBuffer[readIndex],
                  writeIndex - readIndex);
    readIndex = writeIndex;
  }

private:
  bool debug;
  std::function<void(double, uint8_t *, size_t)> foundCallback;
};
