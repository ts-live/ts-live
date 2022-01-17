import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { useAsync } from 'react-use'
import { buffer } from 'stream/consumers'
import styles from '../styles/Home.module.css'

const AudioTestPage: NextPage = () => {
  const videoRef = React.useRef<HTMLVideoElement>(null)

  async function onclick () {
    if (!videoRef.current) {
      return
    }

    const videoTrack = new MediaStreamTrackGenerator({ kind: 'video' })
    const audioTrack = new MediaStreamTrackGenerator({ kind: 'audio' })
    const stream = new MediaStream([videoTrack, audioTrack])
    const videoWriter = videoTrack.writable.getWriter()
    const audioWriter = audioTrack.writable.getWriter()
    videoRef.current.onloadstart = () => {
      if (!videoRef.current) return
      videoRef.current.width = 1920
      videoRef.current.height = 1080
      videoRef.current.play()
    }
    videoRef.current.srcObject = stream

    let startVideoTimestamp: number | null = null
    let startAudioTimestamp: number | null = null
    let lastAudioTimestamp: number | null = null
    let startTime: number | null = null

    // const url = 'http://g1080:40772/api/channels/GR/16/services/23608/stream'
    const url = 'http://g1080:40772/api/channels/GR/27/services/1024/stream'
    // const url = '/data/hentatsu.m2ts'
    // const url = 'http://g1080:8888/api/streams/live/3239123608/mpegts?mode=0'
    // const url = 'http://g1080:8888/api/streams/live/3239123608/hls?mode=0'
    // const url = '/data/hentatsu.m2ts'
    // const url = '/data/rohan-therun.m2ts'
    // const url = '/data/nhk-news7.m2ts'
    const worker = new Worker(new URL('../lib/ffmpeg-worker', import.meta.url))
    worker.addEventListener('message', ({ data }) => {
      if (!lastAudioTimestamp && data.type === 'audio') {
        lastAudioTimestamp = data.chunk.timestamp
        if (startVideoTimestamp !== null) {
          startAudioTimestamp = lastAudioTimestamp
        }
      }
      if (!startVideoTimestamp && data.type === 'video') {
        startVideoTimestamp = data.chunk.timestamp
        startAudioTimestamp = lastAudioTimestamp
        startTime = performance.now()
      }
      if (
        startVideoTimestamp === null ||
        startAudioTimestamp === null ||
        startTime === null
      ) {
        return
      }
      const startTimestamp =
        data.type === 'video' ? startVideoTimestamp : startAudioTimestamp
      const timestampDiff = (data.chunk.timestamp - startTimestamp) / 1000
      const timeDiff = performance.now() - startTime
      const waitDuration = timestampDiff - timeDiff
      const writeFunc = () => {
        if (data.type === 'audio') {
          const chunk = new AudioData({ ...data.chunk, data: data.buf })
          audioWriter.write(chunk)
          chunk.close()
        } else if (data.type === 'video') {
          const chunk = new VideoFrame(data.buf, {
            ...data.chunk,
            displayWidth: 1920,
            displayHeight: 1080
          })
          videoWriter.write(chunk)
          chunk.close()
        }
      }
      console.log(
        data.type,
        data.chunk.timestamp,
        performance.now(),
        timestampDiff,
        timeDiff,
        timeDiff - timestampDiff,
        waitDuration
      )
      if (waitDuration > 0) {
        setTimeout(writeFunc, waitDuration)
      } else {
        writeFunc()
      }
    })
    console.log('worker:', worker)
    worker.postMessage({ url: url })
    console.log('postMessage done.')
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
