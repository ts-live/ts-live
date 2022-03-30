class AudioFeederProcessor extends AudioWorkletProcessor {
  bufferedSamples = 0
  currentBufferReadSize = 0
  processCallCount = 0
  buffers0 = new Array(0)
  buffers1 = new Array(0)
  started = false
  constructor (...args) {
    super(...args)
    this.port.onmessage = e => {
      if (e.data.type === 'feed') {
        this.buffers0.push(e.data.buffer0)
        this.buffers1.push(e.data.buffer1)
        this.bufferedSamples += e.data.buffer0.length
        this.port.postMessage(this.bufferedSamples)
      }
    }
  }
  process (inputs, outputs, parameters) {
    const output = outputs[0]

    if (
      this.buffers0.length == 0 ||
      (!this.started && this.bufferedSamples < 48000 / 10)
    ) {
      output[0].fill(0)
      output[1].fill(0)
      return true
    }
    this.started = true

    const buffer0 = this.buffers0[0]
    const buffer1 = this.buffers1[0]

    if (buffer0.length - this.currentBufferReadSize > output[0].length) {
      output[0].set(
        buffer0.subarray(
          this.currentBufferReadSize,
          this.currentBufferReadSize + output[0].length
        )
      )
      output[1].set(
        buffer1.subarray(
          this.currentBufferReadSize,
          this.currentBufferReadSize + output[0].length
        )
      )
      this.currentBufferReadSize += output[0].length
      this.bufferedSamples -= output[0].length
    } else {
      const copySize = buffer0.length - this.currentBufferReadSize
      output[0].set(
        buffer0.subarray(
          this.currentBufferReadSize,
          this.currentBufferReadSize + copySize
        )
      )
      output[1].set(
        buffer1.subarray(
          this.currentBufferReadSize,
          this.currentBufferReadSize + copySize
        )
      )
      this.bufferedSamples -= copySize
      this.currentBufferReadSize = 0
      if (output[0].length > copySize) {
        if (this.buffers0.length > 1) {
          output[0].set(
            buffers0[1].subarray(0, output[0].length - copySize),
            copySize
          )
          output[1].set(
            buffers1[1].subarray(0, output[0].length - copySize),
            copySize
          )
          this.bufferedSamples -= output[0].length - copySize
          this.currentBufferReadSize = copySize
          this.buffers0.shift()
          this.buffers1.shift()
        } else {
          output[0].fill(0, copySize)
          output[1].fill(0, copySize)
          this.buffers0.shift()
          this.buffers1.shift()
        }
      } else {
        this.buffers0.shift()
        this.buffers1.shift()
      }
    }
    if (this.processCallCount++ % 20 == 0) {
      this.port.postMessage(this.bufferedSamples)
    }
    return true
  }
}

registerProcessor('audio-feeder-processor', AudioFeederProcessor)
