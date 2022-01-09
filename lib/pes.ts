export class PesPacket {
  payload: Uint8Array
  constructor (payload: Uint8Array) {
    this.payload = payload
  }
  startCodePrefix () {
    return (this.payload[0] << 16) | (this.payload[1] << 8) | this.payload[2]
  }
  streamId () {
    return this.payload[3]
  }
  PesPacketLength () {
    return (this.payload[4] << 8) | this.payload[5]
  }
  // '10'(2)
  PesScramblingControl () {
    return (this.payload[6] & 0b00110000) >>> 4
  }
  PesPriority () {
    return (this.payload[6] & 0b00001000) >>> 3
  }
  dataAlignmentIndicator () {
    return (this.payload[6] & 0b00000100) >>> 2
  }
  copyright () {
    return (this.payload[6] & 0b00000010) >>> 1
  }
  originalOrCopy () {
    return this.payload[6] & 0b00000001
  }
  PtsDtsFlags () {
    return (this.payload[7] & 0b11000000) >>> 6
  }
  EscrFlag () {
    return (this.payload[7] & 0b00100000) >>> 5
  }
  EsRateFlag () {
    return (this.payload[7] & 0b00010000) >>> 4
  }
  DsmTrickModeFlag () {
    return (this.payload[7] & 0b00001000) >>> 3
  }
  AdditionalCopyInfoFlag () {
    return (this.payload[7] & 0b00000100) >>> 2
  }
  PesCrcFlag () {
    return (this.payload[7] & 0b00000010) >>> 1
  }
  PesExtensionFlag () {
    return this.payload[7] & 0b00000001
  }
  PesHeaderDataLength () {
    return this.payload[8]
  }
  Pts () {
    if (this.PtsDtsFlags() === 0b10 || this.PtsDtsFlags() === 0b11) {
      let pts =
        (this.payload[9] & 0b00001110) * (1 << 29) +
        this.payload[10] * (1 << 22) +
        (this.payload[11] & 0b11111110) * (1 << 14) +
        this.payload[12] * (1 << 7) +
        ((this.payload[13] & 0b11111110) >>> 1)
      return pts
    } else {
      return null
    }
  }
  Dts () {
    if (this.PtsDtsFlags() === 0b11) {
      let dts = 0
      dts += (this.payload[14] & 0b00001110) >>> 1
      dts *= 2 ** 8
      dts += this.payload[15]
      dts *= 2 ** 7
      dts += (this.payload[16] & 0b11111110) >>> 1
      dts *= 2 ** 8
      dts += this.payload[17]
      dts *= 2 ** 7
      dts += (this.payload[18] & 0b11111110) >>> 1
      return dts
    } else {
      return null
    }
  }
  PtsDtsSize () {
    if (this.PtsDtsFlags() === 0b10) {
      return 5
    } else if (this.PtsDtsFlags() === 0b11) {
      return 5 * 2
    } else {
      return 0
    }
  }
  // reserved(2), ESCR_base[32..30](3), marker_bit(1), ESCR_base[29..15](15), marker_bit(1), ESCR_base[14..0](15), marker_bit(1), ESCR_ext(9), marker_bit(1)
  EscrBase () {
    if (this.EscrFlag() === 0b1) {
      const idx = 9 + this.PtsDtsSize()
      return (
        ((this.payload[idx] & 0b00111000) << 27) |
        ((this.payload[idx] & 0b00000011) << 28) |
        (this.payload[idx + 1] << 20) |
        ((this.payload[idx + 2] & 0b11111000) << 12) |
        ((this.payload[idx + 2] & 0b00000011) << 13) |
        (this.payload[idx + 3] << 5) |
        ((this.payload[idx + 4] & 0b11111000) >>> 3)
      )
    } else {
      return null
    }
  }
  EscrExtention () {
    if (this.EscrFlag() === 0b1) {
      const idx = 9 + this.PtsDtsSize()
      return (
        ((this.payload[idx + 4] & 0b00000011) << 7) |
        ((this.payload[idx + 5] & 0b11111110) >>> 1)
      )
    } else {
      return null
    }
  }
  EscrSize () {
    if (this.EscrFlag() === 0b1) {
      return 6
    } else {
      return 0
    }
  }
  EsRate () {
    if (this.EsRateFlag() === 0b1) {
      const idx = 9 + this.PtsDtsSize() + this.EscrSize()
      // marker_bit(1), ES_rate(22), marker_bit(1)
      return (
        ((this.payload[idx] & 0b01111111) << 15) |
        (this.payload[idx + 1] << 7) |
        (this.payload[idx + 2] >>> 1)
      )
    } else {
      return null
    }
  }
  EsRateSize () {
    if (this.EsRateFlag() === 0b1) {
      return 3
    } else {
      return 0
    }
  }
  // Dsm_XXX...
  DsmTrickModeSize () {
    if (this.DsmTrickModeFlag() === 0b1) {
      return 1
    } else {
      return 0
    }
  }
  AdditionalCopyInfo () {
    if (this.AdditionalCopyInfoFlag() === 0b1) {
      const idx =
        9 +
        this.PtsDtsSize() +
        this.EscrSize() +
        this.EsRateSize() +
        this.DsmTrickModeSize()
      return this.payload[idx] & 0b01111111
    } else {
      return null
    }
  }
  AdditionalCopyInfoSize () {
    if (this.AdditionalCopyInfoFlag() === 0b1) {
      return 1
    } else {
      return 0
    }
  }
  // previous_PES_packet_CRC
  PesCrcSize () {
    if (this.PesCrcFlag() === 0b1) {
      return 2
    } else {
      return 0
    }
  }
  // PES_extension....
  PesPrivateDataFlag () {
    if (this.PesExtensionFlag() === 0b1) {
      const idx =
        9 +
        this.PtsDtsSize() +
        this.EscrSize() +
        this.EsRateSize() +
        this.DsmTrickModeSize() +
        this.AdditionalCopyInfoSize() +
        this.PesCrcSize()
      return (this.payload[idx] & 0b10000000) >>> 7
    } else {
      return null
    }
  }
  PackHeaderFieldFlag () {
    if (this.PesExtensionFlag() === 0b1) {
      const idx =
        9 +
        this.PtsDtsSize() +
        this.EscrSize() +
        this.EsRateSize() +
        this.DsmTrickModeSize() +
        this.AdditionalCopyInfoSize() +
        this.PesCrcSize()
      return (this.payload[idx] & 0b01000000) >>> 6
    } else {
      return null
    }
  }
  PesExtensionSize () {
    if (this.PesExtensionFlag() === 0b1) {
      let extSize = 1
      if (this.PesPrivateDataFlag() === 0b1) {
        extSize += 16
      }
      if (this.PackHeaderFieldFlag() === 0b1) {
        // TODO...
      }
      return 0
    } else {
      return 0
    }
  }
  PesPacketData () {
    return this.payload.subarray(9 + this.PesHeaderDataLength())
  }
  toString () {
    return `PES<start_code_prefix:${this.startCodePrefix()} stream_id:${this.streamId()} PES_packet_length:${this.PesPacketLength()} PES_scrambling_control:${this.PesScramblingControl()} PES_priority:${this.PesPriority()} data_alignment_indicator:${this.dataAlignmentIndicator()} copyright:${this.copyright()} original_or_copy:${this.originalOrCopy()} PTS_DTS_flags:${this.PtsDtsFlags().toString(
      2
    )} ESCR__flag:${this.EscrFlag()} ES_rate_flag:${this.EsRateFlag()} DSM_trick_mode_flag:${this.DsmTrickModeFlag()} additional_copy_info_flag:${this.AdditionalCopyInfoFlag()} PES_CRC_flag:${this.PesCrcFlag()} PES_extension_flag:${this.PesExtensionFlag()} PES_header_data_length:${this.PesHeaderDataLength()}>`
  }
  toStringPacket () {
    return this.PesPacketData().reduce(
      (p, b) => p + ' ' + b.toString(16).padStart(2, '0'),
      'PES Packet:'
    )
  }
}

export class PesPacketizer implements Transformer {
  constructor () {
    // nop?
  }
  transform (
    payload: Uint8Array,
    controller: TransformStreamDefaultController
  ) {
    controller.enqueue(new PesPacket(payload))
  }
}

class PesDumper implements UnderlyingSink {
  write (p: PesPacket) {
    console.log(p.toString(), p.toStringPacket())
  }
  close () {}
  abort (e: unknown) {
    console.error('PesDumper abort', e)
  }
}
