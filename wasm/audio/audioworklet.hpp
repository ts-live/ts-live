#pragma once

void feedAudioData(float *buffer0, float *buffer1, int samples);
void startAudioWorklet();
void setBufferedAudioSamples(int samples);

extern int bufferedAudioSamples;
