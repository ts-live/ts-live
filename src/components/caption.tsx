/** @jsxImportSource @emotion/react */
import { css } from '@emotion/react'
import React, {
  RefObject,
  useCallback,
  useEffect,
  useRef,
  useState
} from 'react'
import { WasmModule } from '../lib/wasmmodule'
import { CanvasProvider } from 'aribb24.js'
import { Service } from 'mirakurun/api'

type Props = {
  //
  wasmModule: WasmModule | undefined
  canvasRef: RefObject<HTMLCanvasElement>
  width?: number
  height?: number
  service: Service | undefined
}

const Caption: React.FC<Props> = ({
  canvasRef,
  wasmModule,
  width,
  height,
  service
}) => {
  // const canvasRef = useRef<HTMLCanvasElement>(null)
  // const [currentSubtitle, setCurrentSubtitle] = useState<number>()

  const captionCallback = useCallback(
    (pts: number, ptsTime: number, captionData: Uint8Array) => {
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
      // const font = setting.font || SUBTITLE_DEFAULT_FONT
      // const font = `"Rounded M+ 1m for ARIB"`
      const timer = setTimeout(() => {
        const result = provider.render({
          canvas,
          useStroke: true,
          keepAspectRatio: true,
          // normalFont: font,
          // gaijiFont: font,
          drcsReplacement: true
        })
        if (estimate.endTime === Number.POSITIVE_INFINITY) return
        setTimeout(() => {
          // console.log('end timeout', now, currentSubtitle)
          // if (currentSubtitle !== now) return
          context.clearRect(0, 0, canvas.width, canvas.height)
          // setCurrentSubtitle(undefined)
        }, (estimate.endTime - estimate.startTime) * 1000)
      }, estimate.startTime * 1000)
    },
    []
  )

  useEffect(() => {
    if (!wasmModule) return
    wasmModule.setCaptionCallback(captionCallback)
  }, [wasmModule])

  useEffect(() => {
    if (!service) return
    if (!canvasRef.current) return
    const canvas = canvasRef.current
    const context = canvas.getContext('2d')
    if (!context) return
    context.clearRect(0, 0, canvas.width, canvas.height)
  }, [service])

  return (
    <canvas
      css={css`
        position: absolute;
        top: 50%;
        left: 50%;
        max-width: 100%;
        max-height: 100%;
        transform: translate(-50%, -50%);
        z-index: 2;
      `}
      ref={canvasRef}
      width={width || 1920}
      height={height || 1080}
    ></canvas>
  )
}

export default Caption
