#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#include "../util/util.hpp"

int bufferedAudioSamples = 0;

void setBufferedAudioSamples(int samples) {
  // set buffereredAudioSamples
  bufferedAudioSamples = samples;
}

void feedAudioData(float *buffer0, float *buffer1, int samples) {
  // clang-format off
  EM_ASM({
    if (Module && Module['myAudio'] && Module['myAudio']['ctx'] && Module['myAudio']['ctx'].state === 'suspended') {
      Module['myAudio']['ctx'].resume()
    }
    if (Module && Module['myAudio'] && Module['myAudio']['node']) {
      const buffer0 = HEAPF32.slice($0>>2, ($0>>2) + $2);
      const buffer1 = HEAPF32.slice($1>>2, ($1>>2) + $2);
      Module['myAudio']['node'].port.postMessage({
        type: 'feed',
        buffer0: buffer0,
        buffer1: buffer1
      }, [buffer0.buffer, buffer1.buffer]);
    }
  }, buffer0, buffer1, samples);
  // clang-format on
}

void startAudioWorklet() {
  std::string scriptSource = slurp("/processor.js");

  // clang-format off
  EM_ASM({
    (async function(){
      const audioContext = new AudioContext({sampleRate: 48000});
      await audioContext.audioWorklet.addModule(`data:text/javascript,${encodeURI(UTF8ToString($0))}`);
      const audioNode = new AudioWorkletNode(
          audioContext, 'audio-feeder-processor',
          {numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2]});
      const gainNode = audioContext.createGain();
      audioNode.connect(gainNode);
      gainNode.connect(audioContext.destination);
      console.log('AudioSetup OK');
      Module['myAudio'] = {ctx: audioContext, node: audioNode, gain: gainNode};
      audioContext.resume();
      audioNode.port.onmessage = e => {Module.setBufferedAudioSamples(e.data)};
      console.log('latency', Module['myAudio']['ctx'].baseLatency);
      if (Module.myAudio.gainValue === undefined) {
        Module.myAudio.gainValue = 1.0;
      }
      Module.myAudio.gain.gain.setValueAtTime(Module.myAudio.gainValue,
                                                Module.myAudio.ctx.currentTime);
    })();
  }, scriptSource.c_str());
  // clang-format on
}

void setAudioGain(double val) {
  // clang-format off
  EM_ASM(
      {
        if (Module.myAudio && Module.myAudio.gain)
          Module.myAudio.gain.gain.setValueAtTime($0,
                                                Module.myAudio.ctx.currentTime);
      },
      val);
  // clang-format on
}
