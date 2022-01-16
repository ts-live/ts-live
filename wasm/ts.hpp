#pragma once

#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <string>

static const size_t TS_PACKET_SIZE = 188;
static const unsigned char TS_PACKET_SYNC_BYTE = 0x47;

// ビットフィールドの8bit内配置は最下位ビットから順に上へ向かう
struct TsPacketHeader {
  unsigned syncByte : 8;

  unsigned pidHi : 5;
  unsigned transportPriority : 1;
  unsigned payloadUnitStartIndicator : 1;
  unsigned transportErrorIndicator : 1;

  unsigned pidLo : 8;

  unsigned continuityCounter : 4;
  unsigned adaptationFieldControl : 2;
  unsigned transportScramblingControl : 2;

  unsigned int pid() { return (pidHi << 8) | pidLo; }

  std::string toString() {
    unsigned int sb = syncByte;
    unsigned int tei = transportErrorIndicator;
    unsigned int pusi = payloadUnitStartIndicator;
    unsigned int tp = transportPriority;
    unsigned int pidhi = pidHi;
    unsigned int pidlo = pidLo;
    unsigned int tsc = transportScramblingControl;
    unsigned int afc = adaptationFieldControl;
    unsigned int cc = continuityCounter;
    return fmt::format(
        "TsPacketHeader<{:02x} {:02x} {:02x} {:02x} sync_byte:{:#02x} "
        "transport_error_indicator:{:#01b} "
        "payload_unit_start_indicator:{:#01b} transport_priority:{:#01b} "
        "pid:{:#06x} transport_scrambling_control:{:#02b} "
        "adaptation_field_control:{:#02b} continuity_counter:{:#01x}>",
        reinterpret_cast<unsigned char *>(this)[0],
        reinterpret_cast<unsigned char *>(this)[1],
        reinterpret_cast<unsigned char *>(this)[2],
        reinterpret_cast<unsigned char *>(this)[3], sb, tei, pusi, tp, pid(),
        tsc, afc, cc);
  }
};

enum AdaptationFieldControl {
  AdaptationFieldError = 0x00,
  AdaptationFieldNone = 0x01,
  AdaptationFieldOnly = 0x02,
  AdaptationFieldBoth = 0x03,
};

struct TsPacketAdaptionFieldHeader {
  unsigned adaptionFieldLength : 8;

  unsigned fiveFlags : 5;
  unsigned elementaryStreamPriorityIndicator : 1;
  unsigned randomAccessIndicator : 1;
  unsigned discontinuityIndicator : 1;
};

struct TsPacket {
  TsPacketHeader header;
  unsigned char
      bodyWithAdaptationField[TS_PACKET_SIZE - sizeof(TsPacketHeader)];

  unsigned char *getBodyAddress() {
    unsigned int afc = this->header.adaptationFieldControl;
    if (afc == AdaptationFieldControl::AdaptationFieldOnly ||
        afc == AdaptationFieldControl::AdaptationFieldBoth) {
      TsPacketAdaptionFieldHeader *afHeader =
          (TsPacketAdaptionFieldHeader *)this->bodyWithAdaptationField;
      return &this->bodyWithAdaptationField[1 + afHeader->adaptionFieldLength];
    } else {
      return this->bodyWithAdaptationField;
    }
  }
  size_t getBodySize() {
    unsigned int afc = this->header.adaptationFieldControl;
    if (afc == AdaptationFieldControl::AdaptationFieldBoth) {
      TsPacketAdaptionFieldHeader *afHeader =
          (TsPacketAdaptionFieldHeader *)this->bodyWithAdaptationField;
      return TS_PACKET_SIZE - sizeof(TsPacketHeader) -
             afHeader->adaptionFieldLength - 1;
    } else if (afc == AdaptationFieldControl::AdaptationFieldOnly) {
      return 0;
    } else {
      return TS_PACKET_SIZE - sizeof(TsPacketHeader);
    }
  }
  std::string toString() {
    uint8_t *p = reinterpret_cast<uint8_t *>(this);
    std::string ret = std::string("") + fmt::format("Packet:[{:02x}", p[0]);
    for (int i = 1; i < TS_PACKET_SIZE; i++) {
      ret += fmt::format(" {:02x}", p[i]);
    }
    ret += "]";
    return ret;
  }
};

uint8_t *getPsiPayloadPointer(TsPacket &packet) {
  unsigned char *p = packet.getBodyAddress();
  return &p[1 + p[0]];
}
