const TS_PACKET_SIZE = 188
const TS_SYNC_BYTE = 0x47

export class TsPacket {
  packet: Uint8Array
  constructor (packetUint8Array: Uint8Array) {
    this.packet = packetUint8Array
  }
  transportErrorIndicator () {
    return (0x80 & this.packet[1]) >>> 7
  }
  payloadUnitStartIndicator () {
    return (0x40 & this.packet[1]) >>> 6
  }
  transportPriority () {
    return (0x20 & this.packet[1]) >>> 5
  }
  pid () {
    return ((0x1f & this.packet[1]) << 8) | this.packet[2]
  }
  transportScrambleControl () {
    return (0xc0 & this.packet[3]) >>> 6
  }
  adaptationFieldControl () {
    return (0x30 & this.packet[3]) >>> 4
  }
  continuityCounter () {
    return 0x0f & this.packet[3]
  }
  adaptionFieltControlMap (afc: number) {
    switch (afc) {
      case 0x01:
        return 'NONE'
      case 0x02:
        return 'ONLY'
      case 0x03:
        return 'BOTH'
      default:
        return 'UNKNOWN'
    }
  }
  toString () {
    const afc = this.adaptionFieltControlMap(this.adaptationFieldControl())
    return `TS_PACKET<PID:${this.pid()}:0x${this.pid()
      .toString(16)
      .padStart(
        2,
        '0'
      )} TransportErrorIndicator:${this.transportErrorIndicator()} PayloadUnitStartIndicator:${this.payloadUnitStartIndicator()} TransportPriority:${this.transportPriority()} TransportScramblingControl:${this.transportScrambleControl()} AdaptionFieldControl:${this.adaptationFieldControl()}(${afc}) ContinuityCounter:${this.continuityCounter()}>`
  }
  toStringPacket () {
    return this.packet.reduce(
      (prev, byte) => prev + ' ' + byte.toString(16).padStart(2, '0'),
      ''
    )
  }
  isPat () {
    // header: packet[0:3]
    // pointer: packet[4]
    // table_id: packet[5]
    // section_length: packet[6] & 0x0f << 8 | packet[7]
    // transport_stream_id: packet[8:9]
    // reserverd, version_number, current_next_indicator: packet[10]
    // section_number: packet[11]
    // last_section_number: packet[12]
    // [program_number, pid] = packet[13:14] ~
    return (
      this.pid() === 0 &&
      this.transportErrorIndicator() === 0 &&
      this.adaptationFieldControl() !== 0x00
    )
  }
  getPatTableId () {
    return this.packet[5]
  }
  // PID => ProgramNumberになってることに注意！
  getPatMap () {
    const packet = this.packet
    const sectionLength = (((packet[6] & 0x0f) << 8) | packet[7]) - 5 - 4
    const map: { [pid: number]: number } = {}
    for (let i = 13; i < 13 + sectionLength; i += 4) {
      const programNumber = (packet[i] << 8) | packet[i + 1]
      const pid = ((packet[i + 2] & 0x1f) << 8) | packet[i + 3]
      map[pid] = programNumber
    }
    // const crc32 =
    //   (packet[13 + sectionNum * 4] << 24) |
    //   (packet[13 + sectionNum + 1] << 16) |
    //   (packet[13 + sectionNum + 2] << 8) |
    //   packet[13 + sectionNum + 3]
    // const crc32_calc = this.crc32(packet.subarray(8, 8 + sectionLength - 4))
    // console.log(crc32.toString(16), crc32_calc.toString(16))
    return map
  }
}

export class TsPacketizer implements Transformer<Uint8Array, TsPacket> {
  unreadBuffer: Uint8Array
  constructor () {
    this.unreadBuffer = new Uint8Array()
  }
  transform (
    chunk: Uint8Array,
    controller: TransformStreamDefaultController<TsPacket>
  ) {
    let idx = 0
    if (this.unreadBuffer.length > 0) {
      const packet = new Uint8Array(TS_PACKET_SIZE)
      packet.set(this.unreadBuffer, 0)
      idx = TS_PACKET_SIZE - this.unreadBuffer.length
      packet.set(chunk.subarray(0, idx), this.unreadBuffer.length)
      controller.enqueue(new TsPacket(packet))
    }
    while (idx < chunk.length) {
      idx = chunk.indexOf(TS_SYNC_BYTE, idx)
      if (idx < 0) {
        this.unreadBuffer = new Uint8Array()
        return
      }
      if (idx + TS_PACKET_SIZE > chunk.length) {
        idx = chunk.indexOf(TS_SYNC_BYTE, idx)
        if (idx < 0) {
          this.unreadBuffer = new Uint8Array()
        } else {
          this.unreadBuffer = chunk.subarray(idx)
        }
        return
      } else {
        const packet = chunk.subarray(idx, idx + TS_PACKET_SIZE)
        controller.enqueue(new TsPacket(packet))
        idx += TS_PACKET_SIZE
      }
    }
  }
}

export class TsPacketPmtFilter implements Transformer<TsPacket, TsPacket> {
  // PMT(PayloadUnitStartIndex:0)
  // header: packet[0:3]
  // pointer_field: packet[4]
  // table_id: packet[5]
  // section_syntax_indictator, '0', reserver, section_length(12): packet[6:7]
  // program_number: packet[8:9]
  // reserverd(2), version_number(5), current_next_indicator(1): packet[10]
  // section_number: packet[11]
  // last_section_number: packet[12]
  // reserved(3), PCR_PID(13): packet[13:14]
  // reserved(4), program_info_length(12): packet[15:16]
  // [description: tag(8), len(8), data]: packet[17:]... バイト数=program_info_length
  // []

  pmtMap: { [pid: number]: number }
  pmtBuffer: Uint8Array
  pmtLength: number
  pmtReadLength: number
  programPidMap: { [pid: number]: [number, number] }
  enqueueVideoPackets: boolean
  enqueueAudioPackets: boolean
  debug: boolean
  videoPids: Array<number>
  audioPids: Array<number>

  constructor (
    enqueueVideoPackets: boolean,
    enqueueAudioPackets: boolean,
    debug: boolean
  ) {
    this.pmtMap = {}
    this.pmtBuffer = new Uint8Array()
    this.pmtLength = 0
    this.pmtReadLength = 0
    this.programPidMap = {}
    this.enqueueVideoPackets = enqueueVideoPackets
    this.enqueueAudioPackets = enqueueAudioPackets
    this.debug = debug
    this.videoPids = []
    this.audioPids = []
  }
  transform (
    packet: TsPacket,
    controller: TransformStreamDefaultController<TsPacket>
  ) {
    if (packet.isPat()) {
      this.pmtMap = packet.getPatMap()
      if (this.debug) {
        console.log(
          'PAT pmtMap:',
          this.pmtMap,
          ' Packet:',
          packet.toString(),
          packet.toStringPacket()
        )
      }
    } else if (this.pmtMap[packet.pid()] !== undefined) {
      // console.log('PMT packet:', packet.toString(), packet.toStringPacket())
      const programNumber = this.pmtMap[packet.pid()]
      if (packet.payloadUnitStartIndicator() === 1) {
        this.pmtLength = (packet.packet[6] & (0x0f << 8)) | packet.packet[7]
        this.pmtBuffer = new Uint8Array(this.pmtLength)
        this.pmtBuffer.set(packet.packet.subarray(8, 8 + this.pmtLength))
        this.pmtReadLength = TS_PACKET_SIZE - 8
      } else {
        if (this.pmtLength > 0) {
          this.pmtBuffer.set(
            packet.packet.subarray(
              4,
              Math.min(TS_PACKET_SIZE, this.pmtLength - this.pmtReadLength + 4)
            ),
            this.pmtReadLength
          )
          this.pmtReadLength += TS_PACKET_SIZE - 4
        }
      }
      if (this.pmtLength > 0 && this.pmtReadLength >= this.pmtLength) {
        const buf = this.pmtBuffer
        // bufに全部入ったので読んでいく。program_numberから入ってるはず
        // programNumber = buf[0:1]
        // reserved(2), version_number(5), current_next_indicator(1) = buf[2]
        // section_number = buf[3]
        // last_section_number = buf[4]
        // reserved(3), PCR_PID(13) = buf[5:6]
        // reserver(4), program_info_length(12) = buf[7:8]
        const programInfoLength = ((buf[7] & 0x0f) << 8) | buf[8]
        this.programPidMap = {}
        for (let idx = 9 + programInfoLength; idx < buf.length - 4; ) {
          const streamType = buf[idx]
          // reserved(3), elementary_PID(13) = buf[idx+1:idx+2]
          const elementaryPid = ((buf[idx + 1] & 0x1f) << 8) | buf[idx + 2]
          // reserved(4), infoLength(12)
          const infoLength = ((buf[idx + 3] & 0x0f) << 8) | buf[idx + 4]
          idx += 5 + infoLength
          this.programPidMap[elementaryPid] = [streamType, programNumber]
          if (
            streamType === 0x02 &&
            this.videoPids.length > 0 &&
            !this.videoPids.includes(elementaryPid)
          ) {
            this.videoPids.push(elementaryPid)
          }
          if (streamType === 0x0f && !this.audioPids.includes(elementaryPid)) {
            this.audioPids.push(elementaryPid)
          }
        }
        if (this.debug) {
          console.log(
            'PMT update!',
            programNumber,
            packet.pid(),
            this.programPidMap
          )
        }
        this.pmtLength = 0
      }
    } else if (
      this.enqueueVideoPackets &&
      this.videoPids.includes(packet.pid())
    ) {
      if (this.debug) {
        console.log(
          'Enqueue Video Packet',
          packet.toString(),
          packet.toStringPacket()
        )
      }
      controller.enqueue(packet)
    } else if (
      this.enqueueAudioPackets &&
      this.audioPids.includes(packet.pid())
    ) {
      if (this.debug) {
        console.log(
          'Enqueue Audio Packet',
          packet.toString(),
          packet.toStringPacket()
        )
      }
      controller.enqueue(packet)
    }
  }
}

export class PayloadExtractor implements Transformer<TsPacket, Uint8Array> {
  // 最後のペイロードユニットを捨てちゃうけど仕方ないか・・・
  payloads: Array<Uint8Array>
  previousCounter: number
  constructor () {
    this.payloads = []
    this.previousCounter = -1
  }
  transform (
    packet: TsPacket,
    controller: TransformStreamDefaultController<Uint8Array>
  ) {
    if (packet.transportErrorIndicator() === 0b1) {
      console.debug('ErrorIndicator detected', packet)
      this.payloads = []
      return
    }
    if (
      this.previousCounter >= 0 &&
      (this.previousCounter + 1) % 16 !== packet.continuityCounter()
    ) {
      console.debug('ContinuityCounter Error detected', packet)
      this.payloads = []
      return
    }
    let packet_start = 4
    if (
      packet.adaptationFieldControl() === 0x02 ||
      packet.adaptationFieldControl() === 0x03
    ) {
      packet_start += 1 + packet.packet[4]
    }
    if (packet.payloadUnitStartIndicator() === 1) {
      if (this.payloads.length > 0) {
        const totalLength = this.payloads.reduce((p, a) => p + a.length, 0)
        const buffer = new Uint8Array(totalLength)
        let idx = 0
        this.payloads.forEach((v, i) => {
          buffer.set(v, idx)
          idx += v.length
        })
        controller.enqueue(buffer)
      }
      // Start Packet
      this.payloads = [packet.packet.subarray(packet_start)]
    } else if (this.payloads.length > 0) {
      // startじゃないのにデータが入ってない：途中から始まってるので捨てる
      this.payloads.push(packet.packet.subarray(packet_start))
    }
  }
}
