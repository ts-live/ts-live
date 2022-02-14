#pragma once

extern "C" {
#include <libavutil/frame.h>
}

void initWebGpu();
void drawWebGpu(AVFrame *);
