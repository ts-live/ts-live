import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { useAsync } from 'react-use'
import styles from '../styles/Home.module.css'

interface FFmpegWasmModule extends EmscriptenModule {
  showVersionInfo: () => void
  getNextInputBuffer: (size: number) => Uint8Array
  enqueueData: (
    size: number,
    videoCallback: (
      timestamp: number,
      duration: number,
      width: number,
      height: number,
      buf: Uint8Array
    ) => void,
    audioCallback: (
      timestamp: number,
      sampleRate: number,
      numberOfFrames: number,
      numberOfChannels: number,
      buf: Float32Array
    ) => void
  ) => void
}

declare function createFFmpegWasmModule (): Promise<FFmpegWasmModule>

const AudioTestPage: NextPage = () => {
  const splitterWasmModuleState = useAsync(async () => {
    await new Promise(resolve => {
      const script = document.createElement('script')
      script.onload = () => resolve(script)
      script.src = '/wasm/ffmpeg-wasm.js'
      document.body.appendChild(script)
    })
    const module = await createFFmpegWasmModule()
    return module
  })
  const videoRef = React.useRef<HTMLVideoElement>(null)

  async function onclick () {
    if (
      splitterWasmModuleState.loading ||
      splitterWasmModuleState.error ||
      !splitterWasmModuleState.value ||
      !videoRef.current
    ) {
      return
    }
    const wasm = splitterWasmModuleState.value
    wasm.showVersionInfo()

    let count = 0
    let totalSize = 0

    const videoTrack = new MediaStreamTrackGenerator({ kind: 'video' })
    const audioTrack = new MediaStreamTrackGenerator({ kind: 'audio' })
    const stream = new MediaStream()
    stream.addTrack(videoTrack)
    stream.addTrack(audioTrack)
    const videoWriter = videoTrack.writable.getWriter()
    const audioWriter = audioTrack.writable.getWriter()
    videoRef.current.onloadedmetadata = () => videoRef.current?.play()
    videoRef.current.srcObject = stream

    let videoFrameCount = 0
    let audioFrameCount = 0

    const startTime = performance.now()
    let maxTimestamp = 0

    const videoCallback = (
      timestamp: number,
      duratioon: number,
      width: number,
      height: number,
      buf: Uint8Array
    ) => {
      // console.log('callback! VideoFrame!', isKey, timestamp, buf)
      const chunk = new VideoFrame(buf, {
        codedWidth: width,
        codedHeight: height,
        format: 'I420',
        timestamp: timestamp,
        duration: duratioon
      })
      videoWriter.write(chunk)
      videoFrameCount++
      // console.debug(chunk)
      maxTimestamp = Math.max(maxTimestamp, timestamp)
    }
    const audioCallback = (
      timestamp: number,
      sampleRate: number,
      numberOfFrames: number,
      numberOfChannels: number,
      buf: Float32Array
    ) => {
      // console.debug(
      //   'callback! AudioData!',
      //   timestamp,
      //   sampleRate,
      //   numberOfFrames,
      //   numberOfChannels,
      //   buf
      // )
      const chunk = new AudioData({
        sampleRate: sampleRate,
        format: 'f32-planar',
        numberOfChannels: numberOfChannels,
        numberOfFrames: numberOfFrames,
        data: buf,
        timestamp: timestamp
      })
      audioWriter.write(chunk)
      audioFrameCount++
      // console.debug(chunk)
      maxTimestamp = Math.max(maxTimestamp, timestamp)
    }

    // const url = 'http://g1080:40772/api/channels/GR/16/services/23608/stream'
    // const url = 'http://g1080:8888/api/streams/live/3239123608/hls?mode=0'
    const url = '/data/hentatsu.m2ts'
    fetch(url, { mode: 'no-cors' }).then(response => {
      console.info('create pipeTo', response)
      return response.body?.pipeTo(
        new WritableStream<Uint8Array>(
          {
            async write (chunk) {
              count++
              // if (count > 5) return

              count++
              totalSize += chunk.length
              // if (count > 100) {
              //   console.log('stop data...')
              //   return
              // }
              try {
                const buffer = wasm.getNextInputBuffer(chunk.length)
                buffer.set(chunk)
                // console.debug('calling enqueueData', chunk.length)
                wasm.enqueueData(chunk.length, videoCallback, audioCallback)
              } catch (ex) {
                // if (typeof ex === 'number') {
                //   console.error(wasm.getExceptionMessage(ex))
                //   throw ex
                // }
                console.error('enqueueData exception', ex)
              }
              const currentTime = performance.now()
              const timeElapsed = (currentTime - startTime) / 1000.0
              const maxReadTime = maxTimestamp / 1000000.0
              const waitTime = (maxReadTime - timeElapsed - 0.5) * 1000
              // console.debug(
              //   'waitTime:',
              //   waitTime,
              //   currentTime,
              //   startTime,
              //   timeElapsed,
              //   maxTimestamp,
              //   maxReadTime
              // )
              if (waitTime > 0) {
                return new Promise(res => setTimeout(res, waitTime))
              } else {
                return
              }
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
          new ByteLengthQueuingStrategy({ highWaterMark: 1 })
        )
      )
    })
  }

  return (
    <div className={styles.container}>
      <button
        onClick={() => {
          onclick()
        }}
      >
        Click!
      </button>
      <video
        ref={videoRef}
        muted={false}
        autoPlay={true}
        controls={true}
      ></video>
    </div>
  )
}

export default AudioTestPage
