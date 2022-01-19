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
      if (!response.body) {
        console.error('response body is not supplied.')
        return
      }
      const reader = response.body.getReader()
      reader.read().then(function processData ({ done, value }): Promise<void> {
        if (done) {
          console.log('Stream completed?')
          return new Promise(res => res)
        }
        if (!value) {
          console.warn('value is undefined.')
          return reader.read().then(processData)
        }
        try {
          // await prevPromise
          const buffer = Module.getNextInputBuffer(value.length)
          buffer.set(value)
          // console.debug('calling enqueueData', chunk.length)
          Module.commitInputData(value.length)
          // console.debug('enqueData done.')
        } catch (ex) {
          if (typeof ex === 'number') {
            console.error(Module.getExceptionMsg(ex))
            throw ex
          }
        }

        return reader.read().then(processData)
      })
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
