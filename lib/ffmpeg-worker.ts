export default null

import ffmpeg from './ffmpeg'

const worker = (self as unknown) as Worker
worker.addEventListener('message', async ({ data }: MessageEvent) => {
  console.log('worker called: ', data)
  const wasm = await ffmpeg()
  wasm.setLogLevelInfo()
  // wasm.setLogLevelDebug()
  console.log('wasm module loaded', wasm)
  const url = data.url
  let videoFrameCount = 0
  let audioFrameCount = 0

  const startTime = performance.now()
  let maxTimestamp = 0
  let videoArrived = false
  let audioArrived = false
  const videoCallback = (
    timestamp: number,
    duratioon: number,
    width: number,
    height: number,
    buf: Uint8Array
  ) => {
    videoArrived = true
    // console.log('callback! VideoFrame!', isKey, timestamp, buf)
    const chunk = {
      codedWidth: width,
      codedHeight: height,
      format: 'I420',
      timestamp: timestamp,
      duration: duratioon
    }
    const copyBuffer = new Uint8Array(buf)
    if (videoArrived && audioArrived) {
      worker.postMessage(
        { type: 'video', chunk: chunk, buf: copyBuffer.buffer },
        [copyBuffer.buffer]
      )
    }
    videoFrameCount++
    maxTimestamp = Math.max(maxTimestamp, timestamp)
  }
  const audioCallback = (
    timestamp: number,
    duration: number,
    sampleRate: number,
    numberOfFrames: number,
    numberOfChannels: number,
    buf: Float32Array
  ) => {
    audioArrived = true
    // console.debug(
    //   'callback! AudioData!',
    //   timestamp,
    //   sampleRate,
    //   numberOfFrames,
    //   numberOfChannels,
    //   buf
    // )
    const chunk = {
      sampleRate: sampleRate,
      format: 'f32-planar',
      numberOfChannels: numberOfChannels,
      numberOfFrames: numberOfFrames,
      timestamp: timestamp,
      duration: duration
    }
    audioFrameCount++
    const copyBuffer = new Float32Array(buf)
    if (videoArrived && audioArrived) {
      worker.postMessage(
        { type: 'audio', chunk: chunk, buf: copyBuffer.buffer },
        [copyBuffer.buffer]
      )
    }
    // console.debug(chunk)
    maxTimestamp = Math.max(maxTimestamp, timestamp)
  }

  let count = 0
  let totalSize = 0
  let prevPromise = new Promise<void>(res => res())
  fetch(url, {
    // mode: 'no-cors',
    // headers: { Accept: 'video/MP2T', 'Accept-Encoding': 'chunked' }
    headers: { 'X-Mirakurun-Priority': '0' }
  }).then(response => {
    console.info(
      'create pipeTo',
      response,
      response.headers,
      response.ok,
      response.body,
      response.status,
      'statusText',
      response.statusText,
      'type',
      response.type,
      'url',
      response.url
    )
    return response.body?.pipeTo(
      new WritableStream<Uint8Array>(
        {
          async write (chunk) {
            count++
            totalSize += chunk.length
            // if (count > 100) {
            //   console.log('stop data...')
            //   return
            // }
            try {
              // await prevPromise
              const buffer = wasm.getNextInputBuffer(chunk.length)
              buffer.set(chunk)
              // console.debug('calling enqueueData', chunk.length)
              wasm.enqueueData(videoCallback, audioCallback)
              // console.debug('enqueData done.')
            } catch (ex) {
              // if (typeof ex === 'number') {
              //   console.error(wasm.getExceptionMessage(ex))
              //   throw ex
              // }
              console.error('enqueueData exception', ex)
            }
            const currentTime = performance.now()
            const timeElapsed = (currentTime - startTime) / 1000.0
            const maxTimestampTime = maxTimestamp / 1000000.0
            const waitTime = (maxTimestampTime - timeElapsed - 0.3) * 1000
            // console.debug(
            //   'waitTime:',
            //   waitTime,
            //   currentTime,
            //   startTime,
            //   timeElapsed,
            //   maxTimestamp,
            //   maxTimestampTime
            // )
            if (waitTime > 0) {
              prevPromise = new Promise<void>(res => setTimeout(res, waitTime))
            } else {
              prevPromise = new Promise<void>(res => res())
            }
            return prevPromise
          },
          close () {
            console.log(
              'WritableStream close count:',
              count,
              'totalSize:',
              totalSize,
              'videoFrame',
              videoFrameCount,
              'audioData',
              audioFrameCount
            )
          },
          abort (e) {
            console.log('WritableStream abort', e)
          }
        },
        new CountQueuingStrategy({ highWaterMark: 1 })
      )
    )
  })
})
