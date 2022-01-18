import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import Script from 'next/script'
import React from 'react'
import { useAsync } from 'react-use'
import styles from '../styles/Home.module.css'

declare interface WasmModule {
  getExceptionMsg(ex: number): string
  showVersionInfo(): void
  getNextInputBuffer(size: number): Uint8Array
  commitInputData(size: number): void
}
declare var Module: WasmModule

const Page: NextPage = () => {
  function onClickFunc () {
    const url = 'http://g1080:40772/api/channels/GR/27/services/1024/stream'
    let startTime = performance.now()
    let maxTimestamp = 0
    fetch(url, {
      // mode: 'no-cors',
      // headers: { Accept: 'video/MP2T', 'Accept-Encoding': 'chunked' }
      headers: { 'X-Mirakurun-Priority': '0' }
    }).then(response => {
      return response.body?.pipeTo(
        new WritableStream<Uint8Array>({
          async write (chunk) {
            try {
              // await prevPromise
              const buffer = Module.getNextInputBuffer(chunk.length)
              buffer.set(chunk)
              // console.debug('calling enqueueData', chunk.length)
              Module.commitInputData(chunk.length)
              // console.debug('enqueData done.')
            } catch (ex) {
              if (typeof ex === 'number') {
                console.error(Module.getExceptionMsg(ex))
                throw ex
              }
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
              return new Promise<void>(res => setTimeout(res, waitTime))
            } else {
              return new Promise<void>(res => res())
            }
          },
          close () {
            console.log('WritableStream close')
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
      <Script id='setupModule' strategy='lazyOnload'>
        {`
        console.log('setupModule!');
            var Module = {
              canvas: (function () { return document.getElementById('video'); })(),
            };
            console.log(Module);
        `}
      </Script>
      <Script
        id='wasm'
        strategy='lazyOnload'
        src='/wasm/ffmpeg-sdl2.js'
      ></Script>
      <button
        onClick={() => {
          onClickFunc()
        }}
      >
        Click!
      </button>
      <canvas
        id='video'
        width={1920}
        height={1080}
        onContextMenu={event => event.preventDefault()}
      ></canvas>
    </div>
  )
}

export default Page
