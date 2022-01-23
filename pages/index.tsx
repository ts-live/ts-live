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

declare interface WasmModule extends EmscriptenModule {
  getExceptionMsg(ex: number): string
  setLogLevelDebug(): void
  setLogLevelInfo(): void
  showVersionInfo(): void
  setCaptionCallback(callback: (captionData: Uint8Array) => void): void
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
    Module.setCaptionCallback(captionData => {
      // console.log('Caption Callback', captionData)
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
        </Box>
      </Drawer>

      <canvas
        id='video'
        tabIndex={-1}
        width={1920}
        height={1080}
        css={css`
          max-width: 100%;
          max-height: 100%;
          aspect-ratio: 16 / 9;
        `}
        onClick={() => setDrawer(true)}
        onContextMenu={ev => ev.preventDefault()}
      ></canvas>
    </Box>
  )
}

export default Page
