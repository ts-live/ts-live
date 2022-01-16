import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { useAsync } from 'react-use'
import styles from '../styles/Home.module.css'
import { Player } from 'zlplayer'

interface SplitterWasmModule extends EmscriptenModule {
  setup: () => void
  getNextInputBuffer: (size: number) => Uint8Array
  enqueueData: (size: number) => void
  getExceptionMessage: (ex: number) => string
}

declare function createSplitterWasmModule (): Promise<SplitterWasmModule>

const AudioTestPage: NextPage = () => {
  const videoRef = React.useRef<HTMLVideoElement>(null)

  async function onclick () {
    if (!videoRef.current) {
      return
    }
    let totalSize = 0
    const zlplayer = new (window as any).zlplayer.Player()
    zlplayer.attachMedia(videoRef.current)
    zlplayer
      .load('/data/hentatsu_h264.m2ts')
      .then(() => videoRef.current?.play())
  }

  return (
    <div>
      <Head>
        <script src='https://unpkg.com/zlplayer'></script>
      </Head>
      <div className={styles.container}>
        <button
          onClick={() => {
            onclick()
          }}
        >
          Click!
        </button>
        <video ref={videoRef} muted={false} controls={true}></video>
      </div>
    </div>
  )
}

export default AudioTestPage
