export declare type ModuleType = {
  showVersionInfo: () => void
  setLogLevelDebug: () => void
  setLogLevelInfo: () => void
  getNextInputBuffer: (size: number) => Uint8Array
  enqueueData: (
    videoCallback: (
      timestamp: number,
      duration: number,
      width: number,
      height: number,
      buf: Uint8Array
    ) => void,
    audioCallback: (
      timestamp: number,
      duration: number,
      sampleRate: number,
      numberOfFrames: number,
      numberOfChannels: number,
      buf: Float32Array
    ) => void
  ) => void
}
declare const ffmpeg: () => Promise<ModuleType>
export default ffmpeg
