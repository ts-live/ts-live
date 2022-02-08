/** @jsxImportSource @emotion/react */
import { css } from '@emotion/react'
import { NextPage } from 'next'
import dynamic from 'next/dynamic'
import Script from 'next/script'
import { EventHandler, useCallback, useEffect, useRef, useState } from 'react'
import { useAsync, useKey, useLocalStorage } from 'react-use'
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
  Slider,
  Stack,
  TextField
} from '@mui/material'
import VolumeDown from '@mui/icons-material/VolumeDown'
import VolumeUp from '@mui/icons-material/VolumeUp'
import { CartesianGrid, LineChart, XAxis, YAxis, Line, Legend } from 'recharts'
import Head from 'next/head'
import { WasmModule, StatsData } from '../lib/wasmmodule'
import dayjs from 'dayjs'

import { Program, Service } from 'mirakurun/api'

const Caption = dynamic(() => import('../components/caption'), { ssr: false })

declare interface EpgRecordedFile {
  id: number
  filename: string
}

const Page: NextPage = () => {
  const [drawer, setDrawer] = useState<boolean>(true)
  const [touched, setTouched] = useState<boolean>(false)

  const [mirakurunServer, setMirakurunServer] = useLocalStorage<string>(
    'mirakurunServer',
    location.hostname !== 'localhost' ? location.origin : undefined
  )
  const [mirakurunOk, setMirakurunOk] = useState<boolean>(false)
  const [mirakurunVersion, setMirakurunVersion] = useState<string>('unknown')
  const [tvServices, setTvServices] = useState<Array<Service>>([])
  const [activeService, setActiveService] = useLocalStorage<Service>(
    'tsplayerActiveService',
    undefined
  )
  const [programs, setPrograms] = useState<Array<Program>>([])
  const [currentProgram, setCurrentProgram] = useState<Program>()

  const [epgStationServer, setEpgStationServer] = useLocalStorage<string>(
    'tsplayerEpgStationServer',
    undefined
  )
  const [epgStationOk, setEpgStationOk] = useState<boolean>(false)
  const [epgStationVersion, setEpgStationVersion] = useState<string>('unknown')
  const [epgRecordedFiles, setEpgRecordedFiles] = useState<
    Array<EpgRecordedFile>
  >()
  const [activeRecordedFileId, setActiveRecordedFileId] = useState<number>()
  const [playMode, setPlayMode] = useState<string>('live')
  const [dualMonoMode, setDualMonoMode] = useLocalStorage<number>(
    'tsplayerDualMonoMode',
    0
  )
  const [volume, setVolume] = useState<number>(1.0)

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
  const [showCaption, setShowCaption] = useLocalStorage<boolean>(
    'tsplayerShowCaption',
    false
  )

  const videoCanvasRef = useRef<HTMLCanvasElement>(null)
  const captionCanvasRef = useRef<HTMLCanvasElement>(null)

  const wasmModuleState = useAsync(async () => {
    const adapter = await (navigator as any).gpu.requestAdapter()
    const device = await adapter.requestDevice()
    const mod = await new Promise<WasmModule>(resolve => {
      const script = document.createElement('script')
      script.onload = () => {
        ;(window as any)
          .createWasmModule({ preinitializedWebGPUDevice: device })
          .then((m: WasmModule) => {
            console.log('then', m)
            resolve(m)
          })
      }
      script.src = '/wasm/ts-live.js'
      document.head.appendChild(script)
      console.log('script element created')
    })
    console.log('mod', mod)
    // mod.setLogLevelDebug()
    return mod
  }, [])

  useEffect(() => {
    if (!wasmModuleState.value) return
    if (dualMonoMode === undefined) return
    wasmModuleState.value.setDualMonoMode(dualMonoMode)
  }, [wasmModuleState, dualMonoMode])

  // const canvasProviderState = useAsync(async () => {
  //   const CanvasProvider = await import('aribb24.js').then(
  //     mod => mod.CanvasProvider
  //   )
  //   return CanvasProvider
  // })
  const statsCallback = useCallback(function statsCallbackFunc (statsDataList) {
    setChartData(prev => {
      if (prev.length + statsDataList.length > 300) {
        const overLength = prev.length + statsDataList.length - 300
        prev.copyWithin(0, overLength)
        prev.length -= statsDataList.length + overLength
      }
      return prev.concat(statsDataList)
    })
  }, [])

  useEffect(() => {
    if (!mirakurunServer) return
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
    if (!mirakurunServer || !mirakurunOk) {
      return
    }
    fetch(`${mirakurunServer}/api/services?type=1`).then(response => {
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
    if (!mirakurunServer || !mirakurunOk) {
      return
    }
    fetch(`${mirakurunServer}/api/programs`).then(response => {
      if (response.ok && response.body !== null) {
        response.json().then((retVal: Array<Program>) => {
          setPrograms(retVal)
        })
      }
    })
  }, [mirakurunOk, mirakurunServer])

  const findCurrentProgram = (
    programs: Array<Program>,
    activeService: Service
  ) => {
    const currentTime = Date.now()
    const current = programs.find(v => {
      if (
        v.networkId === activeService.networkId &&
        v.serviceId === activeService.serviceId &&
        v.startAt <= currentTime &&
        currentTime < v.startAt + v.duration
      ) {
        return true
      } else {
        return false
      }
    })
    if (current !== undefined) {
      setCurrentProgram(current)
      setTimeout(() => {
        setPrograms(prev => [...prev.filter(p => p.id !== current.id)])
      }, current.startAt + current.duration - currentTime)
    }
  }

  useEffect(() => {
    if (!activeService) {
      return
    }
    findCurrentProgram(programs, activeService)
  }, [programs, activeService])

  useEffect(() => {
    if (!epgStationServer) return
    fetch(`${epgStationServer}/api/version`)
      .then(response => {
        if (response.ok && response.body !== null) {
          return response.json().then(({ version }) => {
            setEpgStationOk(true)
            setEpgStationVersion(version)
          })
        }
      })
      .catch(e => {
        console.log(e)
        setEpgStationOk(false)
      })
  }, [epgStationServer])

  useEffect(() => {
    if (!epgStationServer || !epgStationOk) return
    fetch(
      `${epgStationServer}/api/recorded?isHalfWidth=false&offset=0&limit=30`
    )
      .then(response => {
        if (response.ok && response.body !== null) {
          return response.json().then(ret => {
            const recordedFileList: Array<EpgRecordedFile> = []
            ret.records?.forEach((v: any) => {
              v.videoFiles.forEach((r: any) => {
                if (r.type === 'ts') {
                  recordedFileList.push({ filename: v.name, id: r.id })
                }
              })
            })
            setEpgRecordedFiles(recordedFileList)
          })
        }
      })
      .catch(e => {
        console.log(e)
        setEpgRecordedFiles([])
      })
  }, [epgStationServer, epgStationOk])

  useEffect(() => {
    if (!touched) {
      console.log('not touched')
      return
    }
    if (!mirakurunOk || !mirakurunServer || !activeService) {
      console.log(
        'mirakurunServer or activeService',
        mirakurunOk,
        mirakurunServer,
        activeService
      )
      return
    }
    if (
      wasmModuleState.loading ||
      wasmModuleState.error ||
      !wasmModuleState.value
    ) {
      console.log('WasmModule not loaded', wasmModuleState.error)
      return
    }
    const Module = wasmModuleState.value
    // 現在の再生中を止める（or 何もしない）
    stopFunc()

    // ARIB字幕パケットそのものを受け取るコールバック
    // Module.setCaptionCallback(captionData => {
    //   console.log('Caption Callback', captionData)
    // })
    if (showCharts) {
      // depsに入れると毎回リスタートするので入れない
      Module.setStatsCallback(statsCallback)
    } else {
      Module.setStatsCallback(null)
    }

    // 0.2秒遅らす
    setTimeout(() => {
      // 再生スタート
      if (playMode === 'live') {
        const ac = new AbortController()
        setStopFunc(() => () => {
          console.log('abort fetch')
          ac.abort()
          Module.reset()
          console.log('abort fetch done.')
        })
        const url = `${mirakurunServer}/api/services/${activeService.id}/stream`
        console.log('start fetch', url, Module)
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
      } else if (playMode === 'file') {
        setStopFunc(() => () => {
          Module.reset()
        })
        const url = `${epgStationServer}/api/videos/${activeRecordedFileId}`
        Module.playFile(url)
      }
    }, 200)
  }, [
    touched,
    mirakurunOk,
    epgStationOk,
    activeService,
    activeRecordedFileId,
    playMode,
    wasmModuleState
  ])

  useKey(
    'F2',
    () => {
      console.log('Hotkey s pressed!!!')
      if (!videoCanvasRef.current || !captionCanvasRef.current) return
      const video = videoCanvasRef.current
      const caption = captionCanvasRef.current
      const canvas = document.createElement('canvas')
      canvas.width = video.width
      canvas.height = video.height
      const ctx = canvas.getContext('2d')!
      ctx.drawImage(video, 0, 0)
      if (showCaption) {
        ctx.drawImage(caption, 0, 0)
      }
      const a = document.createElement('a')
      a.href = canvas.toDataURL('image/png')
      a.download = `${dayjs().format('YYYYMMDD-HHmmss_SSS')}.png`
      a.click()
    },
    {},
    [showCaption]
  )

  const getServicesOptions = useCallback(() => {
    return tvServices.map((service, idx) => {
      return (
        <MenuItem key={service.id} value={service.id}>
          {service.name}
        </MenuItem>
      )
    })
  }, [tvServices])

  const getProgramFilesOptions = useCallback(() => {
    return epgRecordedFiles?.map(prog => {
      return (
        <MenuItem key={prog.id} value={prog.id}>
          {prog.filename}
        </MenuItem>
      )
    })
  }, [epgRecordedFiles, activeRecordedFileId])

  const volumeChangeHandler = useCallback(
    (event: React.SyntheticEvent | Event, val: number | Array<number>) => {
      if (val !== null && typeof val === 'number') {
        setVolume(val)
        if (wasmModuleState.value !== undefined) {
          wasmModuleState.value.setAudioGain(val)
        }
      }
    },
    [wasmModuleState.value]
  )

  return (
    <Box
      css={css`
        display: flex;
        align-items: center;
        justify-content: center;
        height: 100vh;
        background: #1e1e1e;
      `}
    >
      <Head>
        <title>
          TS-Live!{' '}
          {currentProgram && currentProgram.name && `| ${currentProgram.name}`}
        </title>
        <meta
          httpEquiv='origin-trial'
          content='Amu7sW/oEH3ZqF6SQcPOYVpF9KYNHShFxN1GzM5DY0QW6NwGnbe2kE/YyeQdkSD+kZWhmRnUwQT85zvOA5WYfgAAAABJeyJvcmlnaW4iOiJodHRwOi8vbG9jYWxob3N0OjMwMDAiLCJmZWF0dXJlIjoiV2ViR1BVIiwiZXhwaXJ5IjoxNjUyODMxOTk5fQ=='
        ></meta>
      </Head>
      {/* <Script id='setupModule' strategy='lazyOnload'>
        {`
            var Module = {
              // canvas: (function () { return document.getElementById('video'); })(),
              // captionCanvas: (function () { return document.getElementById('caption'); })(),
            };

        `}
      </Script>
      <Script id='wasm' strategy='lazyOnload' src='/wasm/ts-live.js'></Script> */}
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
        <Box
          css={css`
            width: 320px;
            padding: 24px 24px;
          `}
        >
          <div
            css={css`
              font-weight: bold;
              font-size: 19px;
            `}
          >
            {'TS-Live!'}
          </div>
          <div
            css={css`
              margin-top: 28px;
            `}
          >
            <TextField
              label='Mirakurun Server'
              placeholder='http://mirakurun:40772'
              css={css`
                width: 100%;
              `}
              onChange={ev => {
                setMirakurunServer(ev.target.value)
              }}
              value={mirakurunServer}
            ></TextField>
          </div>
          <div
            css={css`
              margin-top: 16px;
            `}
          >
            Mirakurun:{' '}
            {mirakurunOk ? `OK (version: ${mirakurunVersion})` : 'NG'}
          </div>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <InputLabel id='services-label'>Services</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label='Services'
              labelId='services-label'
              defaultValue={
                activeService
                  ? activeService.id
                  : tvServices.length > 0
                  ? tvServices[0].id
                  : null
              }
              onChange={ev => {
                if (
                  ev.target.value !== null &&
                  typeof (ev.target.value === 'number')
                ) {
                  const id = ev.target.value
                  const active = tvServices.find(v => v.id === id)
                  if (active) setActiveService(active)
                }
                setDrawer(false)
              }}
            >
              {getServicesOptions()}
            </Select>
          </FormControl>
          <div
            css={css`
              margin-top: 16px;
            `}
          >
            Active Service: {activeService && activeService.name}
          </div>
          <div
            css={css`
              margin-top: 16px;
            `}
          >
            <FormGroup>
              <TextField
                label='EPGStation Server'
                placeholder='http://epgstation:8888'
                css={css`
                  width: 100%;
                `}
                onChange={ev => {
                  setEpgStationServer(ev.target.value)
                }}
                value={epgStationServer}
              ></TextField>
            </FormGroup>
          </div>
          <div
            css={css`
              margin-top: 16px;
            `}
          >
            EPGStation:{' '}
            {epgStationOk ? `OK (version: ${epgStationVersion})` : 'NG'}
          </div>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <InputLabel id='program-files-label'>録画ファイル</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label='ProgramFiles'
              labelId='program-files-label'
              defaultValue={
                activeRecordedFileId !== undefined ? activeRecordedFileId : ''
              }
              onChange={ev => {
                if (
                  ev.target.value !== null &&
                  typeof ev.target.value === 'number'
                ) {
                  setActiveRecordedFileId(ev.target.value)
                }
              }}
            >
              {getProgramFilesOptions()}
            </Select>
          </FormControl>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <Stack
              spacing={2}
              direction='row'
              sx={{ mb: 1 }}
              alignItems='center'
            >
              <VolumeDown />
              <Slider
                aria-label='Volume'
                min={0}
                max={2}
                step={0.1}
                value={volume}
                onChangeCommitted={volumeChangeHandler}
              />
              <VolumeUp />
            </Stack>
          </FormControl>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <InputLabel id='playmode-label'>再生モード</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label='再生モード'
              labelId='playmode-label'
              defaultValue={playMode}
              onChange={ev => {
                if (
                  ev.target.value !== null &&
                  typeof ev.target.value === 'string'
                ) {
                  setPlayMode(ev.target.value)
                }
              }}
            >
              <MenuItem value='live'>ライブ視聴</MenuItem>
              {activeRecordedFileId !== undefined && (
                <MenuItem value='file'>ファイル再生</MenuItem>
              )}
            </Select>
          </FormControl>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <InputLabel id='dualmonomode-label'>音声 主/副</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label='音声 0主/副'
              labelId='dualmonomode-label'
              value={dualMonoMode}
              onChange={ev => {
                if (
                  ev.target.value !== null &&
                  typeof ev.target.value === 'number'
                ) {
                  setDualMonoMode(ev.target.value)
                }
              }}
            >
              <MenuItem value={0}>主</MenuItem>
              <MenuItem value={1}>副</MenuItem>
            </Select>
          </FormControl>
          <div>
            <FormGroup>
              <FormControlLabel
                control={
                  <Checkbox
                    checked={showCaption}
                    onChange={ev => {
                      setShowCaption(ev.target.checked)
                    }}
                  ></Checkbox>
                }
                label='字幕を表示する'
              ></FormControlLabel>
            </FormGroup>
          </div>
          <div>
            <FormGroup>
              <FormControlLabel
                control={
                  <Checkbox
                    checked={showCharts}
                    onChange={ev => {
                      setShowCharts(ev.target.checked)
                      if (ev.target.checked) {
                        wasmModuleState.value?.setStatsCallback(statsCallback)
                      } else {
                        wasmModuleState.value?.setStatsCallback(null)
                      }
                    }}
                  ></Checkbox>
                }
                label='統計グラフを表示する'
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
            max-width: 100%;
            max-height: 100%;
            z-index: 1;
          `}
          id='video'
          ref={videoCanvasRef}
          tabIndex={-1}
          width={1920}
          height={1080}
          onClick={() => setDrawer(true)}
          onContextMenu={ev => ev.preventDefault()}
          // transformはWasmが書き換えるので要素のタグに直接書くこと
          style={{
            transform: 'translate(-50%, -50%)'
          }}
        ></canvas>
        <div hidden={!showCaption}>
          <Caption
            service={activeService}
            wasmModule={wasmModuleState.value}
            canvasRef={captionCanvasRef}
            width={1920}
            height={1080}
          ></Caption>
        </div>
        <div
          css={css`
            position: absolute;
            z-index: 99;
            width: 100%;
            height: 100%;
          `}
          onClick={() => setDrawer(true)}
        ></div>
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
            z-index: 5;
          `}
        >
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
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
              name='Video Frame Queue Size'
              stroke='#8884d8'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
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
              dataKey='VideoPacketQueueSize'
              name='Video Packet Queue Size'
              stroke='#d384d8'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
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
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='AudioPacketQueueSize'
              name='Audio Packet Queue Size'
              stroke='#6383ca'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='AudioWorkletBufferSize'
              name='AudioWorklet Buffer Size'
              stroke='#9d82ca'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='CaptionDataQueueSize'
              name='Caption Data Queue Size'
              stroke='#9dca82'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
            <CartesianGrid strokeDasharray={'3 3'} />
            <XAxis dataKey='time' />
            <YAxis />
            <Legend />
            <Line
              type='linear'
              dataKey='UpdateStreamInfoCount'
              name='UpdateStreamInfo Count'
              stroke='#82ca9d'
              isAnimationActive={false}
              dot={false}
            />
          </LineChart>
          <LineChart
            width={550}
            height={250}
            data={showCharts ? chartData : []}
          >
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
