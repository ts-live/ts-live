import { PesPacket } from './pes'

const CLOCK_HZ = 90000

export class AdtsFrame {
  syncword: string
  aacFrameLength: number
  frameData: Uint8Array
  timestamp: number
  constructor (buffer: Uint8Array, timestamp: number) {
    this.timestamp = timestamp / CLOCK_HZ
    this.syncword = ((buffer[0] << 4) | ((buffer[1] & 0xf0) >> 4))
      .toString(16)
      .padStart(3, '0')
    this.aacFrameLength =
      ((buffer[3] & 0b00000011) << 11) |
      (buffer[4] << 3) |
      ((buffer[5] & 0b11100000) >>> 5)
    this.frameData = buffer.subarray(0, this.aacFrameLength)
  }
}

export class AdtsFrameSplitter implements Transformer<PesPacket, AdtsFrame> {
  unreadBuffers: Array<Uint8Array>
  unreadLength: number
  unreadHeaderFragment: AdtsFrame | null
  constructor () {
    this.unreadBuffers = []
    this.unreadHeaderFragment = null
    this.unreadLength = 0
  }
  transform (
    packet: PesPacket,
    controller: TransformStreamDefaultController<AdtsFrame>
  ) {
    if (packet.startCodePrefix() !== 0x001) {
      console.error('PesStartCodePrefix Error', packet)
    }
    const pts = packet.Pts()
    if (pts === null) {
      return
    }
    let payload = packet.PesPacketData()
    if (this.unreadHeaderFragment === null && this.unreadBuffers.length === 0) {
      if (payload.length === 1 && payload[0] === 0xff) {
        this.unreadHeaderFragment = null
        this.unreadBuffers = [payload]
        this.unreadLength = payload.length
        return
      }
      const idx = payload.findIndex(
        (v, i, a) => i + 1 < a.length && a[i] === 0xff && a[i + 1] === 0xf8
      )
      if (idx < 0) {
        console.debug('reject payload1', payload)
        return
      }
      payload = payload.subarray(idx)
      if (payload.length < 7) {
        this.unreadHeaderFragment = null
        this.unreadBuffers = [payload]
        this.unreadLength += payload.length
        return
      } else {
        this.unreadHeaderFragment = new AdtsFrame(payload.subarray(0, 7), pts)
      }
    } else if (
      this.unreadHeaderFragment === null &&
      this.unreadBuffers.length === 1
    ) {
      const unread = this.unreadBuffers[0]
      if (unread.length >= 7) {
        console.error('BUG?: unread.length >= 7', unread.length)
        return
      }
      const headerBuffer = new Uint8Array(7)
      headerBuffer.set(unread)
      headerBuffer.set(payload.subarray(0, 7 - unread.length), unread.length)
      if (headerBuffer[0] === 0xff && headerBuffer[1] === 0xf8) {
        this.unreadHeaderFragment = new AdtsFrame(headerBuffer, pts)
      } else {
        const idx = payload.findIndex(
          (v, i, a) => i + 1 < a.length && a[i] === 0xff && a[i + 1] === 0xf8
        )
        if (idx < 0) {
          console.debug('reject payload2', payload)
          this.unreadHeaderFragment = null
          this.unreadBuffers = []
          this.unreadLength = 0
          return
        } else {
          payload = payload.subarray(idx)
        }
      }
    }
    if (!this.unreadHeaderFragment) {
      return
    }

    const frameLength = this.unreadHeaderFragment.aacFrameLength
    if (this.unreadLength + payload.length < frameLength) {
      this.unreadBuffers.push(payload)
      this.unreadLength += payload.length
      return
    }

    const buffer = new Uint8Array(frameLength)
    let writeLength = 0
    this.unreadBuffers.forEach((v, i, a) => {
      try {
        buffer.set(v, writeLength)
        writeLength += v.length
      } catch (e) {
        console.error(e, buffer, v, i, a, writeLength)
        throw e
      }
    })
    buffer.set(payload.subarray(0, frameLength - writeLength), writeLength)
    const frame = new AdtsFrame(buffer, pts)
    // console.log('enqueue frame', frame)
    controller.enqueue(frame)

    let remainBuffer = payload.subarray(frameLength - writeLength)
    if (remainBuffer.length === 1 && remainBuffer[0] === 0xff) {
      this.unreadHeaderFragment = null
      this.unreadBuffers = [remainBuffer]
      this.unreadLength = remainBuffer.length
      return
    }

    this.unreadHeaderFragment = null
    this.unreadBuffers = []
    this.unreadLength = 0

    while (remainBuffer.length > 0) {
      if (remainBuffer.length < 7) {
        this.unreadHeaderFragment = null
        this.unreadBuffers = [remainBuffer]
        this.unreadLength = remainBuffer.length
        break
      }
      const idx = remainBuffer.findIndex(
        (v, i, a) => i + 1 < a.length && a[i] === 0xff && a[i + 1] == 0xf8
      )
      if (idx < 0) {
        console.debug('reject remainBuffer', remainBuffer)
        return
      } else {
        remainBuffer = remainBuffer.subarray(idx)
      }
      const remainFrameHeader = new AdtsFrame(remainBuffer.subarray(0, 7), pts)
      const nextLength = remainFrameHeader.aacFrameLength
      if (remainBuffer.length < nextLength) {
        this.unreadHeaderFragment = remainFrameHeader
        this.unreadBuffers = [remainBuffer]
        this.unreadLength = remainBuffer.length
        break
      } else {
        const nextFrame = new AdtsFrame(
          remainBuffer.subarray(0, nextLength),
          pts
        )
        // console.log('enqueue nextFrame', nextFrame)
        controller.enqueue(nextFrame)
        remainBuffer = remainBuffer.subarray(nextLength)
      }
    }
  }
}

export class AudioFrameDecoder implements Transformer<AdtsFrame, AudioData> {
  config = {
    codec: 'mp4a.40.2',
    sampleRate: 48000,
    numberOfChannels: 2
  }
  decoder: AudioDecoder | null
  constructor () {
    // nop?
    this.decoder = null
  }
  async transform (
    frame: AdtsFrame,
    controller: TransformStreamDefaultController<any>
  ) {
    if (!this.decoder) {
      this.decoder = new AudioDecoder({
        output (data) {
          console.log(data)
          controller.enqueue(data)
        },
        error (e) {
          console.error('Decode Error', e)
        }
      })
      await this.decoder.configure(this.config)
    }
    if (!this.decoder) {
      return
    }
    const data = frame.frameData
    if (data[0] !== 0xff || data[1] !== 0xf8) {
      console.error('sync word mismatch!', frame)
    }
    const chunk = new EncodedAudioChunk({
      type: 'key',
      timestamp: frame.timestamp,
      data: data
    })
    this.decoder.decode(chunk)
  }
}
