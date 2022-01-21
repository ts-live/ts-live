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
  FormControlLabel,
  FormGroup,
  MenuItem,
  Select,
  TextField
} from '@mui/material'

declare interface WasmModule extends EmscriptenModule {
  getExceptionMsg(ex: number): string
  setLogLevelDebug(): void
  setLogLevelInfo(): void
  showVersionInfo(): void
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
    <Box>
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
        css={css`
          width: 550px;
        `}
        anchor='left'
        open={drawer}
        onClose={() => {
          if (mirakurunOk) {
            setTouched(true)
            setDrawer(false)
          }
        }}
      >
        <div>web-ts-player</div>
        <div
          css={css`
            margin: 10px auto;
          `}
        >
          <TextField
            label='mirakurun server'
            placeholder='http://mirakurun:40772'
            onChange={ev => {
              setMirakurunServer(ev.target.value)
            }}
            value={mirakurunServer}
          ></TextField>
        </div>
        <div>{mirakurunOk ? 'OK: ' + mirakurunVersion : 'NG'}</div>
        <div
          css={css`
            margin: 10px auto;
            width: 100%;
          `}
        >
          <Select
            css={css`
              width: 100%;
            `}
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
        </div>
        <div>{activeService}</div>
        <div>
          <FormGroup>
            <FormControlLabel
              control={
                <Checkbox
                  checked={doDeinterlace}
                  onChange={ev => setDoDeinterlace(ev.target.checked)}
                ></Checkbox>
              }
              label='インターレース解除'
            ></FormControlLabel>
          </FormGroup>
        </div>
      </Drawer>

      <canvas
        id='video'
        tabIndex={-1}
        width={1920}
        height={1080}
        onClick={() => setDrawer(true)}
        onContextMenu={ev => ev.preventDefault()}
      ></canvas>
    </Box>
  )
}

export default Page
