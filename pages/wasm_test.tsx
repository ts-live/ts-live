import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { useAsync } from 'react-use'
import { AdtsFrame, AdtsFrameSplitter, AudioFrameDecoder } from '../lib/audio'
import { PesPacket, PesPacketizer } from '../lib/pes'
import { PayloadExtractor, TsPacketizer, TsPacketPmtFilter } from '../lib/ts'
import styles from '../styles/Home.module.css'

interface SplitterWasmModule extends EmscriptenModule {
  getNextInputBuffer: (size: number) => Uint8Array
  enqueueData: (size: number) => void
  getExceptionMessage: (ex: number) => string
}

declare function createSplitterWasmModule (): Promise<SplitterWasmModule>

const AudioTestPage: NextPage = () => {
  const splitterWasmModuleState = useAsync(async () => {
    await new Promise(resolve => {
      const script = document.createElement('script')
      script.onload = () => resolve(script)
      script.src = '/wasm/ts-splitter.js'
      document.body.appendChild(script)
    })
    return createSplitterWasmModule()
  })
  const audioRef = React.useRef<HTMLAudioElement>(null)

  async function onclick () {
    if (
      splitterWasmModuleState.loading ||
      splitterWasmModuleState.error ||
      !splitterWasmModuleState.value
    ) {
      return
    }
    const wasm = splitterWasmModuleState.value
    let count = 0
    fetch('/data/hentatsu.m2ts').then(response => {
      response.body?.pipeTo(
        new WritableStream({
          write (chunk) {
            count++
            if (count > 50) {
              return
            }
            const buffer = wasm.getNextInputBuffer(chunk.length)
            buffer.set(chunk)
            try {
              wasm.enqueueData(chunk.length)
            } catch (ex) {
              if (typeof ex === 'number') {
                console.error(wasm.getExceptionMessage(ex))
                throw ex
              }
            }
          }
        })
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
