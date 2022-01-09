#pragma once

#include "ts.hpp"

struct AudioVideoPesPacketHeader {
  unsigned char packetStartCodePrefixHi : 8;
  unsigned char packetStartCodePrefixMid : 8;
  unsigned char streamId : 8;
  unsigned char packetStartCodePrefixLo : 8;
  unsigned char pesPacketLengthHi : 8;
  unsigned char pesPacketLengthLo : 8;

  unsigned char originalOrCopy : 1;
  unsigned char copyright : 1;
  unsigned char dataAlignmentIndicator : 1;
  unsigned char pesPriority : 1;
  unsigned char pesScramblingControl : 2;
  unsigned char fill10 : 2;

  unsigned char pesExtentionFlag : 1;
  unsigned char pesCrcFlag : 1;
  unsigned char additionalCopyInfoFlag : 1;
  unsigned char dsmTrickModeFlag : 1;
  unsigned char esRateFlag : 1;
  unsigned char escrFlag : 1;
  unsigned char ptsDtsFlags : 2;

  unsigned char pesHeaderDataLength : 8;

  unsigned int pesPacketLength() {
    return ((unsigned int)pesPacketLengthHi << 8) | pesPacketLengthLo;
  }
  unsigned int pesPacketBodyLength() {
    return pesPacketLength() - 9 - pesHeaderDataLength;
  }
};

class DataFeeder {
public:
  virtual void feedData(uint8_t *ptr, size_t size);
};

class PesFeeder {
public:
  PesFeeder(DataFeeder &feeder)
      : dataFeeder(feeder), pHeader(nullptr), readSize(0) {}

  void feedPesPacket(TsPacket &packet) {
    // PES パケットを食ってDataFeederにデータを送り続ける
    if (pHeader == nullptr) {
      // 最初のパケット
      pHeader = reinterpret_cast<AudioVideoPesPacketHeader *>(
          packet.getBodyAddress());
      uint8_t *p = reinterpret_cast<uint8_t *>(pHeader) +
                   sizeof(AudioVideoPesPacketHeader) +
                   pHeader->pesHeaderDataLength;
      size_t bodySize =
          packet.getBodySize() - sizeof(AudioVideoPesPacketHeader);
      if (bodySize < pHeader->pesPacketBodyLength()) {
        // まだ途中
        dataFeeder.feedData(p, bodySize);
        readSize = bodySize;
      } else {
        // 全部このパケットに入ってる
        dataFeeder.feedData(p, pHeader->pesPacketBodyLength());
        pHeader = nullptr;
        readSize = 0;
      }
    } else {
      // 継続パケット
      size_t bodySize = packet.getBodySize();
      if (readSize + bodySize < pHeader->pesPacketBodyLength()) {
        // まだ途中
        dataFeeder.feedData(packet.getBodyAddress(), bodySize);
        readSize += bodySize;
      } else {
        // 終わった
        dataFeeder.feedData(packet.getBodyAddress(),
                            pHeader->pesPacketBodyLength() - readSize);
        pHeader = nullptr;
        readSize = 0;
      }
    }
  }

private:
  DataFeeder &dataFeeder;
  AudioVideoPesPacketHeader *pHeader;
  size_t readSize;
};
