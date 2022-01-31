export declare interface StatsData {
  time: number
  VideoFrameQueueSize: number
  AudioFrameQueueSize: number
  SDLQueuedAudioSize: number
  InputBufferSize: number
}

export declare interface WasmModule extends EmscriptenModule {
  getExceptionMsg(ex: number): string
  setLogLevelDebug(): void
  setLogLevelInfo(): void
  showVersionInfo(): void
  setCaptionCallback(
    callback: (pts: number, ptsTime: number, captionData: Uint8Array) => void
  ): void
  setStatsCallback(
    callback: ((statsDataList: Array<StatsData>) => void) | null
  ): void
  playFile(url: string): void
  getNextInputBuffer(size: number): Uint8Array
  commitInputData(size: number): void
  reset(): void
}
export declare var Module: WasmModule
