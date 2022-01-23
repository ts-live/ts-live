/** @jsxImportSource @emotion/react */
import { css } from '@emotion/react'
import { NextPage } from 'next'
import Script from 'next/script'
import { useCallback, useEffect, useState } from 'react'
import { useLocalStorage } from 'react-use'
import {
  Box,
  Checkbox,
  Drawer,
  FormControl,
  FormControlLabel,
  FormGroup,
  InputLabel,
  MenuItem,
  Select,
  TextField
} from '@mui/material'
import {
  CartesianGrid,
  LineChart,
  XAxis,
  YAxis,
  Tooltip,
  Line,
  Legend
} from 'recharts'

declare interface WasmModule extends EmscriptenModule {
  getExceptionMsg(ex: number): string
  setLogLevelDebug(): void
  setLogLevelInfo(): void
  showVersionInfo(): void
  setCaptionCallback(callback: (captionData: Uint8Array) => void): void
  setStatsCallback(callback: (statsDataList: Array<StatsData>) => void): void
  setDeinterlace(deinterlace: boolean): void
  getNextInputBuffer(size: number): Uint8Array
  commitInputData(size: number): void
  reset(): void
}
declare var Module: WasmModule

declare interface TvService {
  id: number
  name: string
  serviceId: number
  networkId: number
  hasLogoData: boolean
}

declare interface StatsData {
  time: number
  VideoFrameQueueSize: number
  AudioFrameQueueSize: number
  SDLQueuedAudioSize: number
  InputBufferSize: number
}

const Page: NextPage = () => {
  const [drawer, setDrawer] = useState<boolean>(true)
  const [touched, setTouched] = useState<boolean>(false)
  const [mirakurunServer, setMirakurunServer] = useLocalStorage<string>(
    'mirakurunServer',
    undefined
  )
  const [mirakurunOk, setMirakurunOk] = useState<boolean>(false)
  const [mirakurunVersion, setMirakurunVersion] = useState<string>('unknown')
  const [tvServices, setTvServices] = useState<Array<TvService>>([])
  const [activeService, setActiveService] = useLocalStorage<number | undefined>(
    'mirakurunActiveService',
    undefined
  )
  const [doDeinterlace, setDoDeinterlace] = useLocalStorage<boolean>(
    'tsplayerDoDeinterlace',
    false
  )
  const [stopFunc, setStopFunc] = useState(() => () => {})
  const [chartData, setChartData] = useState<Array<StatsData>>([
    {
      time: 0,
      VideoFrameQueueSize: 0,
      AudioFrameQueueSize: 0,
      InputBufferSize: 0,
      SDLQueuedAudioSize: 0
    }
  ])
  const [showCharts, setShowCharts] = useState<boolean>(false)

  useEffect(() => {
    fetch(`${mirakurunServer}/api/version`)
      .then(response => {
        if (response.ok && response.body !== null) {
          return response.json().then(({ current }) => {
            setMirakurunOk(true)
            setMirakurunVersion(current)
          })
        }
      })
      .catch(e => {
        console.log(e)
        setMirakurunOk(false)
      })
  }, [mirakurunServer])

  useEffect(() => {
    if (!mirakurunOk) {
      return
    }
    fetch(`${mirakurunServer}/api/services`).then(response => {
      if (response.ok && response.body !== null) {
        response.json().then((retval: Array<any>) => {
          const registeredIdMap: { [key: string]: boolean } = {}
          setTvServices(
            retval
              .map(v => {
                if (v.id in registeredIdMap) {
                  return null
                } else {
                  registeredIdMap[v.id as string] = true
                  return {
                    id: v.id,
                    serviceId: v.serviceId,
                    networkId: v.networkId,
                    name: v.name,
                    hasLogoData: v.hasLogoData
                  }
                }
              })
              .filter(v => v) as Array<TvService>
          )
        })
      }
    })
  }, [mirakurunOk, mirakurunServer])

  useEffect(() => {
    if (doDeinterlace === undefined) {
      setDoDeinterlace(false)
      return
    }
    if ((window as any).Module !== undefined) {
      Module.setDeinterlace(doDeinterlace)
    }
  }, [doDeinterlace])

  useEffect(() => {
    if (!touched) {
      // first gestureまでは再生しない
      return
    }
    if (!mirakurunOk || !mirakurunServer || !activeService) {
      return
    }
    // 現在の再生中を止める（or 何もしない）
    stopFunc()

    // ARIB字幕パケットそのものを受け取るコールバック
    // Module.setCaptionCallback(captionData => {
    //   console.log('Caption Callback', captionData)
    // })
    Module.setStatsCallback(statsDataList => {
      setChartData(prev => [
        ...(prev.length >= 300 ? prev.slice(statsDataList.length) : prev),
        ...statsDataList
      ])
    })
    Module.setDeinterlace(doDeinterlace || false)

    // 再生スタート
    const ac = new AbortController()
    setStopFunc(() => () => {
      console.log('abort fetch')
      ac.abort()
      Module.reset()
    })
    const url = `${mirakurunServer}/api/services/${activeService}/stream`
    fetch(url, {
      signal: ac.signal,
      headers: { 'X-Mirakurun-Priority': '0' }
    })
      .then(async response => {
        if (!response.body) {
          console.error('response body is not supplied.')
          return
        }
        const reader = response.body.getReader()
        let ret = await reader.read()
        while (!ret.done) {
          if (ret.value) {
            try {
              const buffer = Module.getNextInputBuffer(ret.value.length)
              buffer.set(ret.value)
              // console.debug('calling enqueueData', chunk.length)
              Module.commitInputData(ret.value.length)
              // console.debug('enqueData done.')
            } catch (ex) {
              if (typeof ex === 'number') {
                console.error(Module.getExceptionMsg(ex))
                throw ex
              }
            }
          }
          ret = await reader.read()
        }
      })
      .catch(ex => {
        console.log('fetch aborted ex:', ex)
      })
  }, [touched, mirakurunOk, mirakurunServer, activeService])

  const getServicesOptions = useCallback(() => {
    return tvServices.map((service, idx) => {
      return (
        <MenuItem key={service.id} value={service.id}>
          {service.name}
        </MenuItem>
      )
    })
  }, [tvServices])

  return (
    <Box css={css`
      display: flex;
      align-items: center;
      justify-content: center;
      height: 100vh;
      background: #1E1E1E;
    `}>
      <Script id='setupModule' strategy='lazyOnload'>
        {`
            var Module = {
              canvas: (function () { return document.getElementById('video'); })(),
              doNotCaptureKeyboard: true,
              onRuntimeInitialized: function(){Module.setLogLevelInfo();}
            };
        `}
      </Script>
      <Script
        id='wasm'
        strategy='lazyOnload'
        src='/wasm/ffmpeg-sdl2.js'
      ></Script>
      <Drawer
        anchor='left'
        open={drawer}
        onClose={() => {
          if (mirakurunOk) {
            setTouched(true)
            setDrawer(false)
          }
        }}
      >
        <Box css={css`
            width: 320px;
            padding: 20px 24px;
          `}>
          <div css={css`
            font-weight: bold;
            font-size: 19px;
          `}>Web-TS-Player</div>
          <div css={css`margin-top: 28px;`}>
            <TextField
              label='Mirakurun Server'
              placeholder='http://mirakurun:40772'
              css={css`width: 100%;`}
              onChange={ev => {
                setMirakurunServer(ev.target.value)
              }}
              value={mirakurunServer}
            ></TextField>
          </div>
          <div css={css`margin-top: 16px;`}>Mirakurun: {mirakurunOk ? `OK (version: ${mirakurunVersion})` : 'NG'}</div>
          <FormControl fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}>
            <InputLabel id="demo-simple-select-label">Services</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label='Services'
              labelId="demo-simple-select-label"
              defaultValue={
                activeService
                  ? activeService
                  : tvServices.length > 0
                  ? tvServices[0].id
                  : null
              }
              onChange={ev => {
                if (
                  ev.target.value !== null &&
                  typeof ev.target.value === 'number'
                ) {
                  setActiveService(ev.target.value)
                }
                setDrawer(false)
              }}
            >
              {getServicesOptions()}
            </Select>
          </FormControl>
          <div css={css`margin-top: 16px;`}>Active Service: {activeService}</div>
          <div css={css`margin-top: 16px;`}>
            <FormGroup>
              <FormControlLabel
                control={
                  <Checkbox
                    checked={doDeinterlace}
                    onChange={ev => setDoDeinterlace(ev.target.checked)}
                  ></Checkbox>
                }
                label='インターレースを解除する'
            ></FormControlLabel>
          </FormGroup>
          </div>
          <div>
            <FormGroup>
              <FormControlLabel
                control={
                  <Checkbox
                    checked={showCharts}
                    onChange={ev => setShowCharts(ev.target.checked)}
                  ></Checkbox>
                }
                label='統計グラフ表示'
              ></FormControlLabel>
            </FormGroup>
          </div>
        </Box>
      </Drawer>
      <div
        css={css`
          position: relative;
          width: 100%;
          height: 100%;
        `}
      >
        <canvas
          css={css`
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            max-width: 100%;
            max-height: 100%;
          `}
          id='video'
          tabIndex={-1}
          width={1920}
          height={1080}
          onClick={() => setDrawer(true)}
          onContextMenu={ev => ev.preventDefault()}
        ></canvas>
        <div
          css={css`
            display: ${showCharts ? 'flex' : 'none'};
            align-content: flex-start;
            flex-direction: column;
            flex-wrap: wrap;
            position: absolute;
            left: 0px;
            top: 0px;
            width: 100%;
            height: 100%;
            padding: 28px 12px;
            pointer-events: none;
          `}
        >
          <LineChart
            width={550}
            height={250}
            data={chartData}
            css={css`
              position: absolute;
              left: 0px;
              top: 0px;
            `}
          >
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='VideoFrameQueueSize'
              name='Video Queue Size'
              stroke='#8884d8'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart width={550} height={250} data={chartData}>
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='AudioFrameQueueSize'
              name='Audio Queue Size'
              stroke='#82ca9d'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart width={550} height={250} data={chartData}>
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='SDLQueuedAudioSize'
              name='SDL Audio Queue Size'
              stroke='#9d82ca'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart width={550} height={250} data={chartData}>
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='InputBufferSize'
              name='Input Buffer Size'
              stroke='#ca829d'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
        </div>
      </div>
    </Box>
  )
}

export default Page
