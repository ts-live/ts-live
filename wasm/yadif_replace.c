#include <libavfilter/avfilter.h>
#include <libavfilter/formats.h>
#include <libavfilter/internal.h>
#include <libavfilter/video.h>
#include <libavfilter/yadif.h>

void halide_yadif(int w, int linesize, int h, int parity, int tff, uint8_t *out,
                  uint8_t *prev, uint8_t *cur, uint8_t *next);

static void yadif_filter(AVFilterContext *ctx, AVFrame *dstpic, int parity,
                         int tff) {
  YADIFContext *yadif = ctx->priv;
  int i;

  for (i = 0; i < yadif->csp->nb_components; i++) {
    int w = dstpic->width;
    int h = dstpic->height;

    if (i == 1 || i == 2) {
      w = AV_CEIL_RSHIFT(w, yadif->csp->log2_chroma_w);
      h = AV_CEIL_RSHIFT(h, yadif->csp->log2_chroma_h);
    }

    halide_yadif(w, dstpic->linesize[i], h, parity, tff, dstpic->data[i],
                 yadif->prev->data[i], yadif->cur->data[i],
                 yadif->next->data[i]);
  }

  emms_c();
}

void set_yadif_filter(AVFilterContext *ctx) {
  YADIFContext *yadif = ctx->priv;
  yadif->filter = yadif_filter;
}
