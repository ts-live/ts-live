import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import { encode } from 'punycode'
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
  const audioRef = React.useRef<HTMLAudioElement>(null)

  async function onclick () {
    if (!audioRef.current) {
      return
    }
    let count = 0
    // const audioGenerator = new MediaStreamTrackGenerator({ kind: 'audio' })
    // const stream = new MediaStream()
    // const writer = audioGenerator.writable.getWriter()
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

    // stream.addTrack(audioGenerator)
    // audioRef.current.srcObject = stream
    let callbackCount = 0
    let totalCallbackSize = 0
    // ;(window as any).onAudioFrame = (timestamp: number, buf: Uint8Array) => {
    //   callbackCount++
    //   totalCallbackSize += buf.length
    //   // console.log(callbackCount, timestamp, buf)
    //   const chunk = new EncodedAudioChunk({
    //     type: 'key',
    //     timestamp: timestamp,
    //     data: buf
    //   })
    //   try {
    //     if (audioDecoder.state == 'closed') {
    //       console.warn('audioDecoder closed reset it...')
    //       audioDecoder = new AudioDecoder(audioDecoderInit)
    //       audioDecoder.configure(config)
    //     }
    //     audioDecoder.decode(chunk)
    //     if (callbackCount % 10000 == 0) {
    //       console.log(`decode called ${callbackCount}`, audioDecoder)
    //     }
    //   } catch (e) {
    //     console.error('decoder exception', e)
    //   }
    // }
    let totalSize = 0
    let remain = new Uint8Array()
    fetch('/data/hentatsu.aac').then(response => {
      response.body?.pipeTo(
        new WritableStream({
          async write (inputChunk) {
            count++
            totalSize += inputChunk.length
            // if (count > 100) {
            //   console.log('stop data...')
            //   return
            // }
            let chunk = inputChunk
            if (remain.length > 0) {
              chunk = new Uint8Array(remain.length + inputChunk.length)
              chunk.set(remain)
              chunk.set(inputChunk, remain.length)
              remain = new Uint8Array()
            }
            const headerFinder = (e: number, i: number, a: Uint8Array) => {
              return i + 1 < a.length && a[i] == 0xff && a[i + 1] == 0xf8
            }
            while (true) {
              const idx = chunk.findIndex(headerFinder)
              if (idx < 0) {
                return
              }
              if (idx + 7 >= chunk.length) {
                remain = chunk.subarray(idx)
                return
              }
              const idx2 = chunk.subarray(idx + 7).findIndex(headerFinder)
              if (idx2 < 0) {
                remain = chunk.subarray(idx)
                return
              }
              const encodedChunk = new EncodedAudioChunk({
                type: 'key',
                timestamp: count * 1000,
                data: chunk.subarray(idx, idx2)
              })
              audioDecoder.decode(encodedChunk)
              console.log(audioDecoder)
              chunk = chunk.subarray(idx2)
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
