import { write } from 'fs'
import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { AdtsFrame, AdtsFrameSplitter, AudioFrameDecoder } from '../lib/audio'
import { PesPacket, PesPacketizer } from '../lib/pes'
import { PayloadExtractor, TsPacketizer, TsPacketPmtFilter } from '../lib/ts'
import styles from '../styles/Home.module.css'

async function audio_test (audioRef: React.RefObject<HTMLAudioElement>) {
  if (!audioRef.current) {
    return
  }
  const generator = new MediaStreamTrackGenerator({ kind: 'audio' })
  const stream = new MediaStream()
  stream.addTrack(generator)
  audioRef.current.srcObject = stream
  audioRef.current.onloadedmetadata = () => audioRef.current?.play()

  const opt = { highWaterMark: 1 }

  fetch('/data/hentatsu.m2ts').then(response => {
    response.body
      ?.pipeThrough(new TransformStream(new TsPacketizer()))
      .pipeThrough(
        new TransformStream(
          new TsPacketPmtFilter(false, true, false),
          new CountQueuingStrategy(opt),
          new CountQueuingStrategy(opt)
        )
      )
      .pipeThrough(
        new TransformStream(
          new PayloadExtractor(),
          new CountQueuingStrategy(opt),
          new CountQueuingStrategy(opt)
        )
      )
      .pipeThrough(
        new TransformStream(
          new PesPacketizer(),
          new CountQueuingStrategy(opt),
          new CountQueuingStrategy(opt)
        )
      )
      .pipeThrough(
        new TransformStream(
          new AdtsFrameSplitter(),
          new CountQueuingStrategy(opt),
          new CountQueuingStrategy(opt)
        )
      )
      .pipeThrough(
        new TransformStream(
          new AudioFrameDecoder(),
          new CountQueuingStrategy(opt),
          new CountQueuingStrategy(opt)
        )
      )
      .pipeTo(generator.writable, { preventAbort: true })
    // .pipeTo(
    //   new WritableStream({
    //     write (chunk) {
    //       console.log('chunk:', chunk)
    //     },
    //     close () {},
    //     abort (e) {
    //       console.error(e)
    //     }
    //   })
    // )
  })
}

const AudioTestPage: NextPage = () => {
  const audioRef = React.useRef<HTMLAudioElement>(null)
  return (
    <div className={styles.container}>
      <button onClick={() => audio_test(audioRef)}>Click!</button>
      <audio
        ref={audioRef}
        muted={false}
        autoPlay={true}
        controls={true}
      ></audio>
    </div>
  )
}

export default AudioTestPage
