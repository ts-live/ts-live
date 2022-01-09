#pragma once

#include "ts.hpp"
#include <algorithm>
#include <cstdint>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <vector>

struct PatData {
  unsigned tableId : 8;

  unsigned sectionLengthHi : 4;
  unsigned reserved1 : 2;
  unsigned fill0 : 1;
  unsigned sectionSyntaxIndicator : 1;

  unsigned sectionLengthLo : 8;
  unsigned transportStreamIdHi : 8;
  unsigned transportStreamIdLo : 8;

  unsigned sectionNumber : 8;
  unsigned currentNextIndicator : 1;
  unsigned versionNumber : 5;
  unsigned reserved2 : 2;

  unsigned lastSectionNumber : 8;
  struct PidTableEntry {
    unsigned programNumberHi : 8;
    unsigned programNumberLo : 8;

    unsigned programMapPidOrNetworkPidHi : 5;
    unsigned reserverd3 : 3;

    unsigned programMapOrNetworkPidLo : 8;

    unsigned int programNumber() {
      return (programNumberHi << 8) | programNumberLo;
    }
    unsigned int programMapPidOrNetworkPid() {
      return (programMapPidOrNetworkPidHi << 8) | programMapOrNetworkPidLo;
    }
  } pidTable[43]; // MAX

  unsigned int sectionLength() {
    return (sectionLengthHi << 8) | sectionLengthLo;
  }
  size_t getPidMapNum() {
    return (sectionLength() - 9) / sizeof(PidTableEntry);
  }

  std::string &&toString() {
    uint8_t *p = reinterpret_cast<uint8_t *>(this);
    std::string ret = fmt::format("Packet: [{:02x}", p[0]);
    for (int i = 1; i < TS_PACKET_SIZE; i++) {
      ret += fmt::format(" {:02x}", p[i]);
    }
    ret += "]";
    return std::move(ret);
  }
};

struct PmtTableEntry {
  unsigned char stream_type : 8;

  unsigned char elementaryPidHi : 5;
  unsigned char reserved : 3;

  unsigned char elementaryPidLo : 8;

  unsigned char esInfoLengthHi : 4;
  unsigned char reserved2 : 4;

  unsigned char esInfoLengthLo : 8;

  unsigned elementaryPid() { return (elementaryPidHi << 8) | elementaryPidLo; }
  unsigned esInfoLength() { return (esInfoLengthHi << 8) | esInfoLengthLo; }

  PmtTableEntry *getNextEntryPtr() {
    uint8_t *nextPtr = reinterpret_cast<uint8_t *>(this) +
                       sizeof(PmtTableEntry) + esInfoLength();
    return reinterpret_cast<PmtTableEntry *>(nextPtr);
  }
};

struct PmtPacketHeader {
  unsigned tableId : 8;

  unsigned sectionLengthHi : 4;
  unsigned reserved1 : 2;
  unsigned fill0 : 1;
  unsigned sectionSyntaxIndicator : 1;

  unsigned sectionLengthLo : 8;
  unsigned programNumberHi : 8;
  unsigned programNumberLo : 8;

  unsigned currentNextIndicator : 1;
  unsigned versionNumber : 5;
  unsigned reserved2 : 2;

  unsigned sectionNumber : 8;
  unsigned lastSectionNumber : 8;

  unsigned pcrPidHi : 5;
  unsigned reserved3 : 3;

  unsigned pcrPidLo : 8;

  unsigned programInfoLengthHi : 4;
  unsigned reserved4 : 4;

  unsigned programInfoLengthLo : 8;

  unsigned int sectionLength() {
    return (sectionLengthHi << 8) | sectionLengthLo;
  }
  unsigned int programNumber() {
    return (programNumberHi << 8) | programNumberLo;
  }
  unsigned int programInfoLength() {
    return (programInfoLengthHi << 8) | programInfoLengthLo;
  }

  std::string &&toString() {
    unsigned int tid = tableId;
    unsigned int ssi = sectionSyntaxIndicator;
    unsigned int vn = versionNumber;
    unsigned int cni = currentNextIndicator;
    return std::move(fmt::format(
        "PMT<tableId:{:#04x} section_syntax_indicator:{:#03b} "
        "section_ength:{} program_number:{:#06x} version_number:{:#07b} "
        "current_next_indicator:{:#03b} program_info_length:{}>",
        tid, ssi, sectionLength(), programNumber(), vn, cni,
        programInfoLength()));
  }

  // headerの外のアドレスを返すことに注意
  uint8_t *getDescriptorAddress() {
    return reinterpret_cast<uint8_t *>(this) + sizeof(PmtPacketHeader);
  }
  uint8_t *getMapAddress() {
    return reinterpret_cast<uint8_t *>(this) + sizeof(PmtPacketHeader) +
           programInfoLength();
  }

  uint8_t *getCrc32Address() {
    return reinterpret_cast<uint8_t *>(this) + sizeof(PmtPacketHeader) +
           sectionLength() - 9 - programInfoLength();
  }

  PmtTableEntry *getNextTableEntry(PmtTableEntry *ptr) {
    if (ptr == nullptr) {
      return reinterpret_cast<PmtTableEntry *>(getMapAddress());
    } else {
      PmtTableEntry *nextPtr = ptr->getNextEntryPtr();
      if (reinterpret_cast<uint8_t *>(nextPtr) >= getCrc32Address()) {
        return nullptr;
      } else {
        return nextPtr;
      }
    }
  }
};

size_t getPmtPayloadSize(TsPacket &packet) {
  unsigned char *p = packet.getBodyAddress();
  PmtPacketHeader *header = reinterpret_cast<PmtPacketHeader *>(&p[1 + p[0]]);
  // tableId(8) + sectionSyntaxIndicator(1) + fill0(1) + reserver1(2) +
  // sectionLength(12) = 24bit
  return header->sectionLength() + 3;
}

class PatPmtFinder {
private:
  std::vector<unsigned int> programMapPids;
  std::pair<unsigned int, std::vector<uint8_t>> unreadPmtPayload;
  size_t unreadPmtPayloadReadSize = 0;

  size_t currentAudioStreamIndex = 0;
  size_t currentVideoStreamIndex = 0;

public:
  std::vector<unsigned int> audioPesPids;
  std::vector<unsigned int> videoPesPids;

  bool isAudioPid(unsigned int pid) {
    if (currentAudioStreamIndex < audioPesPids.size() &&
        audioPesPids[currentAudioStreamIndex] == pid) {
      return true;
    } else {
      return false;
    }
  }
  bool isVideoPid(unsigned int pid) {
    if (currentVideoStreamIndex < videoPesPids.size() &&
        videoPesPids[currentVideoStreamIndex] == pid) {
      return true;
    } else {
      return false;
    }
  }

  // PAT finder
  static bool isPatPid(unsigned int pid) { return pid == 0x00; }
  void feedPatPacket(TsPacket &packet) {
    // PAT
    uint8_t *ptr = packet.getBodyAddress();
    unsigned char pointerField = ptr[0];
    PatData &pat = *reinterpret_cast<PatData *>(&ptr[1 + pointerField]);
    size_t num = pat.getPidMapNum();
    for (int i = 0; i < num; i++) {
      if (pat.pidTable[i].programNumber() != 0x0000) {
        unsigned int foundPid = pat.pidTable[i].programMapPidOrNetworkPid();
        auto it =
            std::find(programMapPids.begin(), programMapPids.end(), foundPid);
        if (it == programMapPids.end()) {
          programMapPids.push_back(foundPid);
          spdlog::debug(
              "ProgramMapTable PIDs update add PID:{:#06x} programNo:{:#06x}",
              foundPid, pat.pidTable[i].programNumber());
        }
      }
    }
  }
  // PMT finder
  bool isPmtPid(unsigned int pid) {
    auto it = std::find(programMapPids.begin(), programMapPids.end(), pid);
    return (it != programMapPids.end());
  }
  void feedPmtPacket(TsPacket &packet) {
    unsigned int pid = packet.header.pid();
    TsPacket *ptr = &packet;

    bool findCompleted = false;

    if (unreadPmtPayload.first != pid) {
      // 途中で違うPIDが来たのでリセット
      unreadPmtPayloadReadSize = 0;
    }
    if (unreadPmtPayloadReadSize == 0) {
      size_t payloadSize = getPmtPayloadSize(packet);
      unreadPmtPayload.second.resize(payloadSize);
      uint8_t *psiPayloadPointer = getPsiPayloadPointer(packet);
      size_t readSize =
          std::min(payloadSize,
                   TS_PACKET_SIZE -
                       (psiPayloadPointer - reinterpret_cast<uint8_t *>(ptr)));
      memcpy(&unreadPmtPayload.second[0], psiPayloadPointer, readSize);
      unreadPmtPayloadReadSize = readSize;
      unreadPmtPayload.first = pid;
      if (readSize == payloadSize) {
        findCompleted = true;
      }
    } else {
      size_t remainSize =
          unreadPmtPayload.second.size() - unreadPmtPayloadReadSize;
      size_t packetSize = TS_PACKET_SIZE - (packet.getBodyAddress() -
                                            reinterpret_cast<uint8_t *>(ptr));
      if (packetSize < remainSize) {
        // 中間パケット
        memcpy(&unreadPmtPayload.second[unreadPmtPayloadReadSize],
               packet.getBodyAddress(), packetSize);
        unreadPmtPayloadReadSize += packetSize;
      } else {
        // 最終パケット
        memcpy(&unreadPmtPayload.second[unreadPmtPayloadReadSize],
               packet.getBodyAddress(), remainSize);
        findCompleted = true;
      }
    }
    if (findCompleted) {
      PmtPacketHeader &header =
          *reinterpret_cast<PmtPacketHeader *>(&unreadPmtPayload.second[0]);
      PmtTableEntry *pentry = header.getNextTableEntry(nullptr);
      // spdlog::debug("PMT findCompleted: {}", header.toString());
      while (pentry != nullptr) {
        // Audio Stream
        // 0x0F: ISO/IEC 13818-7 Audio with ADTS transport syntax
        if (pentry->stream_type == 0x0f) {
          auto it = std::find(audioPesPids.begin(), audioPesPids.end(),
                              pentry->elementaryPid());
          if (it == audioPesPids.end()) {
            unsigned int st = pentry->stream_type;
            unsigned int elementaryPid = pentry->elementaryPid();
            spdlog::debug(
                "Audio Stream PMT Entry: stream_type:{:#04x} byte:[{:#04x}] "
                "elementaryPid:{:#06x} esInfoLength:{} sizeof:{}",
                st, reinterpret_cast<uint8_t *>(pentry)[0], elementaryPid,
                pentry->esInfoLength(), sizeof(PmtTableEntry));

            audioPesPids.push_back(pentry->elementaryPid());
          }
        }

        // Video Stream
        // 0x02: ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2
        // constrained parameter video stream
        if (pentry->stream_type == 0x02) {
          auto it = std::find(videoPesPids.begin(), videoPesPids.end(),
                              pentry->elementaryPid());
          if (it == videoPesPids.end()) {
            unsigned int st = pentry->stream_type;
            unsigned int elementaryPid = pentry->elementaryPid();
            spdlog::debug(
                "Video Stream PMT Entry: stream_type:{:#04x} byte:[{:#04x}] "
                "elementaryPid:{:#06x} esInfoLength:{} sizeof:{}",
                st, reinterpret_cast<uint8_t *>(pentry)[0], elementaryPid,
                pentry->esInfoLength(), sizeof(PmtTableEntry));

            videoPesPids.push_back(pentry->elementaryPid());
          }
        }

        pentry = header.getNextTableEntry(pentry);
      }
      // クリア
      unreadPmtPayload.first = 0x0000;
      unreadPmtPayload.second.resize(0);
      unreadPmtPayloadReadSize = 0;
    }
  }
};
