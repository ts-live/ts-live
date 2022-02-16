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
      if (Module.myAudio.recognizer) {
        if (!Module.myAudio.audioBuffer) Module.myAudio.audioBuffer = new Object();
        const bufKey = `buf_${buffer0.length}`;
        if (!Module.myAudio.audioBuffer[bufKey]) {
           Module.myAudio.audioBuffer[bufKey] = Module.myAudio.ctx.createBuffer(1, buffer0.length, Module.myAudio.ctx.sampleRate);
        }
        Module.myAudio.audioBuffer[bufKey].getChannelData(0).set(buffer0);
        Module.myAudio.recognizer.acceptWaveform(Module.myAudio.audioBuffer[bufKey]);
      }
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
      const audioContext = new AudioContext();
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

      const model = await Vosk.createModel('model.tar.gz');
      console.log('model load ok', model);
      const recognizer = new model.KaldiRecognizer(48000);
      Module.myAudio.recognizer = recognizer;
      console.log('recognizer create ok', recognizer);
      recognizer.on("result", (message) => {
          console.log(`Result: ${message.result.text}`);
      });
      // recognizer.on("partialresult", (message) => {
      //     console.log(`Partial result: ${message.result.partial}`);
      // });
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
