#pragma once

#include "datafeeder.hpp"
#include "ts.hpp"

struct Pts {
  unsigned char markerBit1 : 1;
  unsigned char pts32_30 : 3;
  unsigned char fill0010 : 4;

  unsigned char pts29_22 : 8;

  unsigned char markerBit2 : 1;
  unsigned char pts21_15 : 7;

  unsigned char pts14_7 : 8;

  unsigned char markerBit3 : 1;
  unsigned char pts6_0 : 7;
  int64_t pts() {
    return ((uint64_t)pts32_30 << 30) | ((uint64_t)pts29_22 << 22) |
           ((uint64_t)pts21_15 << 15) | ((uint64_t)pts14_7 << 7) | pts6_0;
  }
};

struct AudioVideoPesPacketHeader {
  unsigned char packetStartCodePrefixHi : 8;
  unsigned char packetStartCodePrefixMid : 8;
  unsigned char packetStartCodePrefixLo : 8;
  unsigned char streamId : 8;
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
    return pesPacketLength() - 3 - pesHeaderDataLength;
  }

  bool checkStartCodePrefix() {
    return packetStartCodePrefixHi == 0x00 &&
           packetStartCodePrefixMid == 0x00 && packetStartCodePrefixLo == 0x01;
  }

  int64_t getPts() {
    if (ptsDtsFlags == 0x02 || ptsDtsFlags == 0x03) {
      Pts *p = reinterpret_cast<Pts *>(reinterpret_cast<uint8_t *>(this) +
                                       sizeof(AudioVideoPesPacketHeader));
      return p->pts();
    } else {
      unsigned char flag = ptsDtsFlags;
      spdlog::debug("PTS:-1? PTS_DTS_flags: {} header:{}", flag, toString());
      return -1L;
    }
  }

  // TODO: initPts の考慮
  double getTimestampMicroSeconds() {
    uint64_t pts = getPts();
    return pts * 1000000.0 / 90000.0;
  }

  std::string toString() {
    unsigned char pscph = packetStartCodePrefixHi;
    unsigned char pscpm = packetStartCodePrefixMid;
    unsigned char pscpl = packetStartCodePrefixLo;
    unsigned char sid = streamId;
    unsigned char hdl = pesHeaderDataLength;
    std::string str = fmt::format(
        "PES Header<packetStartCode:{:#04x}{:02x}{:02x} stream_id:{:#04x} "
        "PES_packet_length:{} PES_header_data_length:{}",
        pscph, pscpm, pscpl, sid, pesPacketLength(), hdl);
    return str;
  }
};

class PesFeeder {
public:
  PesFeeder(DataFeeder &feeder, bool dbg)
      : dataFeeder(feeder), pHeader(nullptr), readSize(0),
        previousContinuityCounter(-1), debug(dbg) {}

  void feedPesPacket(TsPacket &packet) {
    if (debug) {
      std::string headerStr = std::string(pHeader ? "NOT NULL" : "NULL");
      spdlog::debug("feedPesPacket: pHeader:{} packet:{} {}", headerStr,
                    packet.header.toString(), packet.toString());
    }
    if (packet.header.transportErrorIndicator != 0x00) {
      spdlog::warn("PesFeeder transport_error_indicator. {}",
                   packet.header.toString());
    }
    if (previousContinuityCounter >= 0) {
      if (((previousContinuityCounter + 1) & 0x0f) !=
          packet.header.continuityCounter) {
        unsigned char cc = packet.header.continuityCounter;
        if (debug) {
          spdlog::warn(
              "ContinuityCounter Error found! prev:{:#01x} now:{:#01x}",
              previousContinuityCounter, cc);
        }
      }
    }
    previousContinuityCounter = packet.header.continuityCounter;
    // PES パケットを食ってDataFeederにデータを送り続ける
    if (pHeader == nullptr) {
      // 最初のパケット
      if (packet.header.payloadUnitStartIndicator != 0x01) {
        // payload_unit_start_indicatorが立ってなければ途中のパケットなので捨てる
        if (debug) {
          spdlog::warn("PES skip non unit start packet {}",
                       packet.header.toString());
        }
        return;
      }
      pHeader = reinterpret_cast<AudioVideoPesPacketHeader *>(
          packet.getBodyAddress());
      if (debug) {
        spdlog::debug("PES first packet: PID: {:#06x} PES Header: {} Packet "
                      "header: {} packet: {}",
                      packet.header.pid(), pHeader->toString(),
                      packet.header.toString(), packet.toString());
      }

      if (!pHeader->checkStartCodePrefix()) {
        // なんかしらんけどPrefixがおかしい
        spdlog::warn("checkStartCodePrefix error packet: {}",
                     packet.toString());
        pHeader = nullptr;
        readSize = 0;
        return;
      }
      uint8_t *p = reinterpret_cast<uint8_t *>(pHeader) +
                   sizeof(AudioVideoPesPacketHeader) +
                   pHeader->pesHeaderDataLength;
      if (pHeader->pesPacketLength() == 0) {
        // PESPacketLength==0:
        // 全部ぶん投げろ。次の処理がストリーム処理してくれる。・・・まじか。
        size_t size = reinterpret_cast<uint8_t *>(&packet) + TS_PACKET_SIZE - p;
        dataFeeder.feedData(p, size);
        dataFeeder.updateTimestampMicroSeconds(
            pHeader->getTimestampMicroSeconds());
        dataFeeder.consumeData();
        if (debug) {
          spdlog::debug("PES_packet_length == 0");
        }
        pHeader = nullptr;
        readSize = 0;
        return;
      }
      size_t bodySize =
          packet.getBodySize() - sizeof(AudioVideoPesPacketHeader);
      if (bodySize < pHeader->pesPacketBodyLength()) {
        // まだ途中
        if (debug) {
          spdlog::debug("PES first packet size: {}", bodySize);
        }
        dataFeeder.feedData(p, bodySize);
        readSize = bodySize;
      } else {
        // 全部このパケットに入ってる
        if (debug) {
          spdlog::debug("PES entire packet in TS packet");
        }
        dataFeeder.feedData(p, pHeader->pesPacketBodyLength());
        dataFeeder.updateTimestampMicroSeconds(
            pHeader->getTimestampMicroSeconds());
        dataFeeder.consumeData();
        pHeader = nullptr;
        readSize = 0;
      }
    } else {
      // 継続パケット
      if (packet.header.payloadUnitStartIndicator != 0x00) {
        // payload_unit_start_indicatorが1なので最初から
        spdlog::warn("payload_unit_start_indicator == 1 => restart PES: {}",
                     pHeader->toString());
        pHeader = nullptr;
        readSize = 0;
        feedPesPacket(packet);
        return;
      }
      size_t bodySize = packet.getBodySize();
      if (readSize + bodySize < pHeader->pesPacketBodyLength()) {
        // まだ途中
        if (debug) {
          spdlog::debug("PES intermidiate packet: size: {}", bodySize);
        }
        dataFeeder.feedData(packet.getBodyAddress(), bodySize);
        readSize += bodySize;
      } else {
        // 終わった
        if (debug) {
          spdlog::debug("PES Packets completed. bodySize: {} size:{} "
                        "readSize:{} PESPacketBodyLength:{} Packet:{}",
                        bodySize, pHeader->pesPacketBodyLength() - readSize,
                        readSize, pHeader->pesPacketBodyLength(),
                        packet.toString());
        }
        dataFeeder.feedData(packet.getBodyAddress(),
                            pHeader->pesPacketBodyLength() - readSize);
        dataFeeder.updateTimestampMicroSeconds(
            pHeader->getTimestampMicroSeconds());
        dataFeeder.consumeData();
        pHeader = nullptr;
        readSize = 0;
        if (debug) {
          spdlog::debug("PES Packets completed process done.");
        }
      }
    }
  }

private:
  DataFeeder &dataFeeder;
  AudioVideoPesPacketHeader *pHeader;
  size_t readSize;
  bool debug;
  int8_t previousContinuityCounter;
};
