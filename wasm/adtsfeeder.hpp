#pragma once

#include <functional>

#include "datafeeder.hpp"

struct AdtsHeader {
  unsigned char syncwordhi8 : 8;

  unsigned char protectionAbsent : 1;
  unsigned char layer : 2;
  unsigned char id : 1;
  unsigned char syncwordlo4 : 4;

  unsigned char channelConfigurationHi1 : 1;
  unsigned char pribateBit : 1;
  unsigned char samplingFrequencyIndex : 4;
  unsigned char profile : 2;

  unsigned char aacFrameLengthHi2 : 2;
  unsigned char copyrightIdentificationStart : 1;
  unsigned char copyrightIdentificationBit : 1;
  unsigned char home : 1;
  unsigned char originalCopy : 1;
  unsigned char channelConfigurationLo2 : 2;

  unsigned char aacFramelengthMid8 : 8;

  unsigned char adtsBufferFullnessHi5 : 5;
  unsigned char aacFrameLengthLo3 : 3;

  unsigned char numberOfRawDataBlocksInFrame : 2;
  unsigned char adtsBufferFullnessLo6 : 6;

  unsigned int aacFrameLength() {
    return ((unsigned int)aacFrameLengthHi2 << 11) |
           ((unsigned int)aacFramelengthMid8 << 3) | aacFrameLengthLo3;
  }

  std::string toString() {
    unsigned char swh = syncwordhi8;
    unsigned char swl = syncwordlo4;
    unsigned char sfi = samplingFrequencyIndex;
    unsigned char cc = (channelConfigurationHi1 << 2) | channelConfigurationLo2;

    return fmt::format(
        "ADTS Header<syncword:{:#02x}{:01x} sampling_frequency_index:{:#01x} "
        "channel_configuration:{} aac_frame_length:{}>",
        swh, swl, sfi, cc, aacFrameLength());
  }
};

class AdtsFeeder : public DataFeeder {
public:
  AdtsFeeder(bool dbg)
      : DataFeeder(dbg),
        foundCallback([](double timestamp, uint8_t *ptr, size_t size) {}),
        debug(dbg) {}

  void
  setFoundCallback(std::function<void(double, uint8_t *, size_t)> callback) {
    foundCallback = callback;
  }

  void consumeData() {
    AdtsHeader *pHeader = nullptr;
    // if (debug) {
    //   std::string str = fmt::format("{:02x}", dataBuffer[readIndex]);
    //   for (size_t idx = readIndex + 1; idx < writeIndex; idx++) {
    //     str += fmt::format(" {:02x}", dataBuffer[idx]);
    //   }
    //   spdlog::debug("AdtsFeeder consumeData read:{} write:{} [{}]",
    //   readIndex,
    //                 writeIndex, str);
    // }

    while (readIndex < writeIndex) {
      auto oldReadIndex = readIndex;
      // ヘッダを探す
      while (readIndex + sizeof(AdtsHeader) <= writeIndex) {
        AdtsHeader *p = reinterpret_cast<AdtsHeader *>(&dataBuffer[readIndex]);
        if (p->syncwordhi8 == 0xff && p->syncwordlo4 == 0x0f && p->id == 0x01 &&
            p->layer == 0x00 && p->aacFrameLength() > 0) {
          // 見つかった
          pHeader = p;
          if (readIndex - oldReadIndex > 0) {
            std::string str = fmt::format("{:02x}", dataBuffer[oldReadIndex]);
            for (size_t idx = oldReadIndex + 1; idx < readIndex; idx++) {
              str += fmt::format(" {:02x}", dataBuffer[idx]);
            }
            spdlog::warn("AdtsFeeder skipped {} Bytes header:{} [{}]",
                         readIndex - oldReadIndex, pHeader->toString(), str);
          }
          break;
        } else {
          readIndex++;
        }
      }
      if (pHeader == nullptr) {
        // 見つからないからもっとデータもらう
        return;
      }

      if (readIndex + pHeader->aacFrameLength() <= writeIndex) {
        // Frame全体がバッファに入ってる
        // TSパケット規格と違ってフレーム長はヘッダも含んでる
        std::string str = fmt::format("{:02x}", dataBuffer[readIndex]);
        for (size_t idx = readIndex + 1;
             idx < readIndex + pHeader->aacFrameLength(); idx++) {
          str += fmt::format(" {:02x}", dataBuffer[idx]);
        }
        std::string str2 = fmt::format(
            "{:02x}", dataBuffer[readIndex + pHeader->aacFrameLength()]);
        for (int i = 1; i < 10; i++) {
          str2 += fmt::format(
              " {:02x}", dataBuffer[readIndex + pHeader->aacFrameLength() + i]);
        }
        if (debug) {
          spdlog::debug(
              "Found ADTS Frame Header:{} Frame:[{}] nextFrame:[{}...]",
              pHeader->toString(), str, str2);
        }
        foundCallback(timestampMicroSeconds, &dataBuffer[readIndex],
                      pHeader->aacFrameLength());
        readIndex += pHeader->aacFrameLength();
        pHeader = nullptr;
      } else {
        // まだバッファに入ってない
        // spdlog::debug("AdtsFeeder frame incomleted.");
        return;
      }
    }
  }

private:
  bool debug;
  std::function<void(double, uint8_t *, size_t)> foundCallback;
};
