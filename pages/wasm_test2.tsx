import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import React from 'react'
import { useAsync } from 'react-use'
import styles from '../styles/Home.module.css'

interface SplitterWasmModule extends EmscriptenModule {
  setup: () => void
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
    const module = await createSplitterWasmModule()
    module.setup()
    return module
  })
  const audioRef = React.useRef<HTMLAudioElement>(null)

  async function onclick () {
    if (
      splitterWasmModuleState.loading ||
      splitterWasmModuleState.error ||
      !splitterWasmModuleState.value ||
      !audioRef.current
    ) {
      return
    }
    const wasm = splitterWasmModuleState.value
    let count = 0
    // const audioGenerator = new MediaStreamTrackGenerator({ kind: 'audio' })
    // const stream = new MediaStream()
    // const writer = audioGenerator.writable.getWriter()
    // stream.addTrack(audioGenerator)
    // audioRef.current.srcObject = stream
    let callbackCount = 0
    let totalCallbackSize = 0
    let startTimestamp: number | null = null
    const chunkBuffer = new Array<EncodedAudioChunk>()
    ;(window as any).onAudioFrame = (timestamp: number, buf: Uint8Array) => {
      callbackCount++
      totalCallbackSize += buf.length
      if (startTimestamp == null) {
        startTimestamp = timestamp
      }
      console.log((timestamp - startTimestamp) / 1000000, buf.length)
      // console.log(callbackCount, timestamp, buf)
      const chunk = new EncodedAudioChunk({
        type: 'key',
        timestamp: timestamp - startTimestamp,
        data: buf
      })
      chunkBuffer.push(chunk)
    }
    let totalSize = 0
    fetch('/data/hentatsu.m2ts').then(response => {
      response.body?.pipeTo(
        new WritableStream({
          async write (chunk) {
            count++
            totalSize += chunk.length
            // if (count > 100) {
            //   console.log('stop data...')
            //   return
            // }
            try {
              const buffer = wasm.getNextInputBuffer(chunk.length)
              buffer.set(chunk)
              wasm.enqueueData(chunk.length)
            } catch (ex) {
              if (typeof ex === 'number') {
                console.error(wasm.getExceptionMessage(ex))
                throw ex
              }
            }
          },
          close () {
            console.log(
              'WritableStream close count:',
              count,
              'totalSize:',
              totalSize,
              'totalCallbackSize:',
              totalCallbackSize
            )
            const audioDecoderInit = {
              async output (output: AudioData) {
                console.log('AudioDecoder ouptut', output)
                // await writer.write(output)
              },
              error (e: unknown) {
                console.error(
                  'AudioDecoder Error',
                  e,
                  audioDecoder,
                  'count',
                  count,
                  'callbackCount',
                  callbackCount
                )
              }
            }
            let audioDecoder = new AudioDecoder(audioDecoderInit)
            const config = {
              codec: 'mp4a.40.2',
              sampleRate: 48000,
              numberOfChannels: 2
            }
            audioDecoder.configure(config)

            const startTime = performance.now() * 1000
            const intervalId = setInterval(() => {
              const current = performance.now() * 1000 - startTime
              while (
                chunkBuffer.length > 0 &&
                chunkBuffer[0].timestamp < current + 500000
              ) {
                const chunk = chunkBuffer.shift()!
                audioDecoder.decode(chunk)
                console.log(audioDecoder, chunk)
              }
              if (chunkBuffer.length == 0) {
                clearInterval(intervalId)
              }
            }, 100)
          },
          abort (e) {
            console.log('WritableStream abort', e)
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
