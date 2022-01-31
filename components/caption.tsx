/** @jsxImportSource @emotion/react */
import { Interpolation, Theme } from '@emotion/react'
import React, { useCallback, useEffect, useRef, useState } from 'react'
import { Module } from '../lib/wasmmodule'
import { CanvasProvider } from 'aribb24.js'

type Props = {
  //
  css?: Interpolation<Theme>
  width?: number
  height?: number
}

const Caption: React.FC<Props> = ({ css, width, height }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [currentSubtitle, setCurrentSubtitle] = useState<Uint8Array>()

  useEffect(() => {
    console.log('in useEffect', Module === undefined)
  }, [canvasRef])

  const captionCallback = useCallback(
    (pts: number, ptsTime: number, captionData: Uint8Array) => {
      const start = performance.now()
      const data = captionData.slice()
      const canvas = canvasRef.current
      if (!canvas) return
      const context = canvas.getContext('2d')
      if (!context) return

      // if (!aribSubtitleData) {
      //   context.clearRect(0, 0, canvas.width, canvas.height)
      //   setDisplayingAribSubtitleData(null)
      //   return
      // }

      const provider = new CanvasProvider(data, ptsTime)
      const estimate = provider.render()
      if (!estimate) return
      console.log('estimate', estimate, pts, ptsTime, data)
      const ctx = canvas.getContext('2d')
      if (!ctx) return
      // const font = setting.font || SUBTITLE_DEFAULT_FONT
      const font = `"Rounded M+ 1m for ARIB"`
      const timer = setTimeout(() => {
        const result = provider.render({
          canvas,
          useStroke: true,
          keepAspectRatio: true,
          normalFont: font,
          gaijiFont: font,
          drcsReplacement: true
        })
        const end = performance.now()
        console.log('result', result, end - start)
        setCurrentSubtitle(data)
        if (estimate.endTime === Number.POSITIVE_INFINITY) return
        setTimeout(() => {
          if (currentSubtitle !== data) return
          ctx.clearRect(0, 0, canvas.width, canvas.height)
          setCurrentSubtitle(undefined)
        }, (estimate.endTime - estimate.startTime) * 1000)
      }, estimate.startTime * 1000)
    },
    []
  )

  useEffect(() => {
    console.log('Module in caption component', Module)
    // Module.setCaptionCallback(captionCallback)
  }, [])
  return (
    <canvas
      css={css}
      ref={canvasRef}
      width={width || 1920}
      height={height || 1080}
    ></canvas>
  )
}

export default Caption
