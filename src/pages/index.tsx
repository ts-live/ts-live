/** @jsxImportSource @emotion/react */
import { css } from '@emotion/react'
import { NextPage } from 'next'
import dynamic from 'next/dynamic'
import Script from 'next/script'
import { EventHandler, useCallback, useEffect, useRef, useState } from 'react'
import { useAsync, useKey, useLocalStorage } from 'react-use'
import {
  Box,
  Button,
  Checkbox,
  Divider,
  Drawer,
  FormControl,
  FormControlLabel,
  FormGroup,
  InputLabel,
  MenuItem,
  Select,
  Slider,
  Stack,
  TextField,
} from '@mui/material'
import { VolumeMute, VolumeUp } from '@mui/icons-material'
import { CartesianGrid, LineChart, XAxis, YAxis, Line, Legend } from 'recharts'
import Head from 'next/head'
import { WasmModule, StatsData } from '../lib/wasmmodule'
import dayjs from 'dayjs'

import { Program, Service } from 'mirakurun/api'
import { useRouter } from 'next/router'

const Caption = dynamic(() => import('../components/caption'), {
  ssr: false,
})

declare interface EpgRecordedFile {
  id: number
  filename: string
}

let initialized = false;

const Page: NextPage = () => {
  const router = useRouter()
  const { debug } = router.query

  const [debugLog, setDebugLog] = useState<boolean>(false)

  const [drawer, setDrawer] = useState<boolean>(true)
  const [touched, setTouched] = useState<boolean>(false)

  const [mirakurunServer, setMirakurunServer] = useLocalStorage<string>('mirakurunServer', '')
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
  const [epgRecordedFiles, setEpgRecordedFiles] = useState<Array<EpgRecordedFile>>()
  const [activeRecordedFileId, setActiveRecordedFileId] = useState<number>()
  const [playMode, setPlayMode] = useState<string>('live')
  const [dualMonoMode, setDualMonoMode] = useLocalStorage<number>('tsplayerDualMonoMode', 0)
  const [volume, setVolume] = useLocalStorage<number>('tsplayerVolume', 1.0)
  const [mute, setMute] = useLocalStorage<boolean>('tsplayerMute', false)

  const [stopFunc, setStopFunc] = useState(() => () => {})
  const [chartData, setChartData] = useState<Array<StatsData>>([
    {
      time: 0,
      VideoFrameQueueSize: 0,
      AudioFrameQueueSize: 0,
      InputBufferSize: 0,
      SDLQueuedAudioSize: 0,
    },
  ])
  const [showCharts, setShowCharts] = useState<boolean>(false)
  const [showCaption, setShowCaption] = useLocalStorage<boolean>('tsplayerShowCaption', false)

  const videoCanvasRef = useRef<HTMLCanvasElement>(null)
  const captionCanvasRef = useRef<HTMLCanvasElement>(null)

  const [wakeLock, setWakeLock] = useState<WakeLockSentinel>()
  const [wasmMod, setWasmMod] = useState<WasmModule | null>(null)

  useEffect(() => {
    let mounted = true;
    console.log("useEffect", initialized)
    if (initialized) {
      return
    }
    initialized = true;
    ;(async () => {
      console.log("async", wasmMod, initialized)

      const adapter = await (navigator as any).gpu.requestAdapter()
      const device = await adapter.requestDevice()
      const script = document.createElement('script')
      script.onload = () => {
        console.log("onload")
        ;(window as any)
          .createWasmModule({ preinitializedWebGPUDevice: device })
          .then((m: WasmModule) => {
            console.log('then', m)
            console.log("setWasmMod")
            setWasmMod(m)
          })
      }
      script.src = "/wasm/ts-live.js"
      document.head.appendChild(script)
      console.log("script element created")
    })();
}, [])

  useEffect(() => {
    if (!wasmMod) return
    if (dualMonoMode === undefined) return
    wasmMod.setDualMonoMode(dualMonoMode)
  }, [wasmMod, dualMonoMode])

  useEffect(() => {
    if (!wasmMod) return
    if (debugLog === undefined) return
    if (debugLog) {
      wasmMod.setLogLevelDebug()
    } else {
      wasmMod.setLogLevelInfo()
    }
  }, [wasmMod, debugLog])

  // const canvasProviderState = useAsync(async () => {
  //   const CanvasProvider = await import('aribb24.js').then(
  //     mod => mod.CanvasProvider
  //   )
  //   return CanvasProvider
  // })
  const statsCallback = useCallback(function statsCallbackFunc(statsDataList: StatsData[]) {
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
        response.json().then((retval: Array<Service>) => {
          const registeredIdMap: { [key: string]: boolean } = {}
          setTvServices(
            retval
              .map(v => {
                if (v.id in registeredIdMap) {
                  return null
                } else {
                  registeredIdMap[v.id] = true
                  return v
                }
              })
              .filter(v => v) as Array<Service>
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

  const findCurrentProgram = (programs: Array<Program>, activeService: Service) => {
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
    fetch(`${epgStationServer}/api/recorded?isHalfWidth=false&offset=0&limit=30`)
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
      console.log('mirakurunServer or activeService', mirakurunOk, mirakurunServer, activeService)
      return
    }
    if (!wasmMod) {
      console.log('WasmModule not loaded', wasmMod)
      return
    }
    const Module = wasmMod
    // 現在の再生中を止める（or 何もしない）
    stopFunc()

    // 視聴中のスリープを避ける
    if (!wakeLock) {
      navigator.wakeLock.request('screen').then(lock => setWakeLock(lock))
    }

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
        const url = `${mirakurunServer}/api/services/${activeService.id}/stream?decode=1`
        console.log('start fetch', url, Module)
        fetch(url, {
          signal: ac.signal,
          headers: { 'X-Mirakurun-Priority': '0' },
        })
          .then(async response => {
            if (!response.body) {
              console.error('response body is not supplied.')
              return
            }
            const sleep = (msec: number) => new Promise(resolve => setTimeout(resolve, msec))
            const reader = response.body.getReader()
            let ret = await reader.read()
            while (!ret.done) {
              if (ret.value) {
                try {
                  while (true) {
                    const buffer = Module.getNextInputBuffer(ret.value.length)
                    if (!buffer) {
                      await sleep(100)
                      continue
                    }
                    buffer.set(ret.value)
                    // console.debug('calling enqueueData', chunk.length)
                    Module.commitInputData(ret.value.length)
                    // console.debug('enqueData done.')
                    break
                  }
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
    }, 500)
  }, [
    touched,
    mirakurunOk,
    epgStationOk,
    activeService,
    activeRecordedFileId,
    playMode,
    wasmMod,
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

  useEffect(() => {
    if (!wasmMod) return
    if (volume === undefined) return
    wasmMod.setAudioGain(mute ? 0.0 : volume)
  }, [wasmMod, volume, mute])

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
          TS-Live! {currentProgram && currentProgram.name && `| ${currentProgram.name}`}
        </title>
      </Head>
      <Script
        src="https://www.googletagmanager.com/gtag/js?id=G-SR7L1XYNV0"
        strategy="afterInteractive"
      />
      <Script id="google-analytics" strategy="afterInteractive">
        {`
            window.dataLayer = window.dataLayer || [];
            function gtag(){window.dataLayer.push(arguments);}
            gtag('js', new Date());

            gtag('config', 'G-SR7L1XYNV0');
          `}
      </Script>
      <Drawer
        anchor="left"
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
            {'TS-Live!'} {debug ? 'Debug' : ''}
          </div>
          <div>{`version: ${process.env.VERSION}`}</div>
          <Divider
            css={css`
              margin: 10px 0px;
            `}
          ></Divider>
          <FormGroup>
            <TextField
              label="Mirakurun Server"
              placeholder="http://mirakurun:40772"
              css={css`
                width: 100%;
              `}
              onChange={ev => {
                setMirakurunServer(ev.target.value)
              }}
              value={mirakurunServer}
            ></TextField>
            <div
              css={css`
                margin-top: 16px;
              `}
            >
              {mirakurunOk ? `Mirakurun: OK (version: ${mirakurunVersion})` : 'Mirakurun: NG'}
            </div>
          </FormGroup>
          <FormGroup>
            <FormControl
              fullWidth
              css={css`
                margin-top: 24px;
                width: 100%;
              `}
            >
              <InputLabel id="services-label">Services</InputLabel>
              <Select
                css={css`
                  width: 100%;
                `}
                label="Services"
                labelId="services-label"
                defaultValue={
                  activeService ? activeService.id : tvServices.length > 0 ? tvServices[0].id : null
                }
                onChange={ev => {
                  if (ev.target.value !== null && typeof (ev.target.value === 'number')) {
                    const id = ev.target.value
                    const active = tvServices.find(v => v.id === id)
                    if (active) setActiveService(active)
                    setTouched(true)
                  }
                  setDrawer(false)
                }}
              >
                {getServicesOptions()}
              </Select>
            </FormControl>
          </FormGroup>
          <FormGroup>
            <FormControl
              fullWidth
              css={css`
                margin-top: 24px;
                width: 100%;
              `}
            >
              <Stack spacing={2} direction="row" sx={{ mb: 1 }} alignItems="center">
                <Button
                  size="small"
                  variant="outlined"
                  css={css`
                    padding: 3px 3px;
                    min-width: 32px;
                  `}
                  onClick={() => setMute(!mute)}
                >
                  {mute ? <VolumeMute /> : <VolumeUp />}
                </Button>
                <Slider
                  aria-label="Volume"
                  min={0}
                  max={2}
                  step={0.05}
                  value={mute ? 0 : volume}
                  disabled={mute}
                  valueLabelDisplay="auto"
                  onChange={(ev, val) => {
                    if (typeof val === 'number') setVolume(val)
                  }}
                />
              </Stack>
            </FormControl>
          </FormGroup>
          <FormControl
            fullWidth
            css={css`
              margin-top: 24px;
              width: 100%;
            `}
          >
            <InputLabel id="dualmonomode-label">音声 主/副</InputLabel>
            <Select
              css={css`
                width: 100%;
              `}
              label="音声 0主/副"
              labelId="dualmonomode-label"
              value={dualMonoMode}
              onChange={ev => {
                if (ev.target.value !== null && typeof ev.target.value === 'number') {
                  setDualMonoMode(ev.target.value)
                }
              }}
            >
              <MenuItem value={0}>主</MenuItem>
              <MenuItem value={1}>副</MenuItem>
            </Select>
          </FormControl>
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
              label="字幕を表示する"
            ></FormControlLabel>
          </FormGroup>
          {debug ? (
            <div>
              <Divider
                css={css`
                  margin: 10px 0px;
                `}
              ></Divider>
              <FormGroup>
                <TextField
                  label="EPGStation Server"
                  placeholder="http://epgstation:8888"
                  css={css`
                    width: 100%;
                  `}
                  onChange={ev => {
                    setEpgStationServer(ev.target.value)
                  }}
                  value={epgStationServer}
                ></TextField>
                <div>
                  {epgStationOk
                    ? `EPGStation: OK (version: ${epgStationVersion})`
                    : 'EPGStation: NG'}
                </div>
              </FormGroup>
              <FormGroup>
                <FormControl
                  fullWidth
                  css={css`
                    margin-top: 24px;
                    width: 100%;
                  `}
                >
                  <InputLabel id="program-files-label">録画ファイル</InputLabel>
                  <Select
                    css={css`
                      width: 100%;
                    `}
                    label="ProgramFiles"
                    labelId="program-files-label"
                    value={activeRecordedFileId !== undefined ? activeRecordedFileId : ''}
                    onChange={ev => {
                      if (ev.target.value !== null && typeof ev.target.value === 'number') {
                        setActiveRecordedFileId(ev.target.value)
                        setPlayMode('file')
                      }
                    }}
                  >
                    {getProgramFilesOptions()}
                  </Select>
                </FormControl>
              </FormGroup>
              <FormGroup>
                <FormControl
                  fullWidth
                  css={css`
                    margin-top: 24px;
                    width: 100%;
                  `}
                >
                  <InputLabel id="playmode-label">再生モード</InputLabel>
                  <Select
                    css={css`
                      width: 100%;
                    `}
                    label="再生モード"
                    labelId="playmode-label"
                    value={playMode}
                    onChange={ev => {
                      if (ev.target.value !== null && typeof ev.target.value === 'string') {
                        setPlayMode(ev.target.value)
                      }
                    }}
                  >
                    <MenuItem value="live">ライブ視聴</MenuItem>
                    {activeRecordedFileId !== undefined && (
                      <MenuItem value="file">ファイル再生</MenuItem>
                    )}
                  </Select>
                </FormControl>
              </FormGroup>
              <FormGroup>
                <FormControlLabel
                  control={
                    <Checkbox
                      checked={showCharts}
                      onChange={ev => {
                        setShowCharts(ev.target.checked)
                        if (ev.target.checked) {
                          wasmMod?.setStatsCallback(statsCallback)
                        } else {
                          wasmMod?.setStatsCallback(null)
                        }
                      }}
                    ></Checkbox>
                  }
                  label="統計グラフを表示する"
                ></FormControlLabel>
              </FormGroup>
              <FormGroup>
                <FormControlLabel
                  control={
                    <Checkbox
                      checked={debugLog}
                      onChange={ev => {
                        setDebugLog(ev.target.checked)
                      }}
                    ></Checkbox>
                  }
                  label="デバッグログを出力する"
                ></FormControlLabel>
              </FormGroup>
            </div>
          ) : (
            <></>
          )}
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
          id="video"
          ref={videoCanvasRef}
          tabIndex={-1}
          width={1920}
          height={1080}
          onClick={() => setDrawer(true)}
          onContextMenu={ev => ev.preventDefault()}
          // transformはWasmが書き換えるので要素のタグに直接書くこと
          style={{
            transform: 'translate(-50%, -50%)',
          }}
        ></canvas>
        <div hidden={!showCaption}>
          <Caption
            service={activeService}
            wasmModule={wasmMod!}
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
        {debug ? (
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
              <XAxis dataKey="time" />
              <YAxis />
              <Legend />
              <Line
                type="linear"
                dataKey="VideoFrameQueueSize"
                name="Video Queue Size"
                stroke="#8884d8"
                isAnimationActive={false}
                dot={false}
              />
            </LineChart>
            <LineChart width={550} height={250} data={showCharts ? chartData : []}>
              <CartesianGrid strokeDasharray={'3 3'} />
              <XAxis dataKey="time" />
              <YAxis />
              <Legend />
              <Line
                type="linear"
                dataKey="AudioFrameQueueSize"
                name="Audio Queue Size"
                stroke="#82ca9d"
                isAnimationActive={false}
                dot={false}
              />
            </LineChart>
            <LineChart width={550} height={250} data={showCharts ? chartData : []}>
              <CartesianGrid strokeDasharray={'3 3'} />
              <XAxis dataKey="time" />
              <YAxis />
              <Legend />
              <Line
                type="linear"
                dataKey="CaptionDataQueueSize"
                name="Caption Data Size"
                stroke="#9dca82"
                isAnimationActive={false}
                dot={false}
              />
            </LineChart>
            <LineChart width={550} height={250} data={showCharts ? chartData : []}>
              <CartesianGrid strokeDasharray={'3 3'} />
              <XAxis dataKey="time" />
              <YAxis />
              <Legend />
              <Line
                type="linear"
                dataKey="AudioWorkletBufferSize"
                name="AudioWorklet Buffer Size"
                stroke="#9d82ca"
                isAnimationActive={false}
                dot={false}
              />
            </LineChart>
            <LineChart width={550} height={250} data={showCharts ? chartData : []}>
              <CartesianGrid strokeDasharray={'3 3'} />
              <XAxis dataKey="time" />
              <YAxis />
              <Legend />
              <Line
                type="linear"
                dataKey="InputBufferSize"
                name="Input Buffer Size"
                stroke="#ca829d"
                isAnimationActive={false}
                dot={false}
              />
            </LineChart>
          </div>
        ) : (
          <></>
        )}
      </div>
    </Box>
  )
}

export default Page
