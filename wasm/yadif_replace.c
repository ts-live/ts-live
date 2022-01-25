#include <libavfilter/avfilter.h>
#include <libavfilter/formats.h>
#include <libavfilter/internal.h>
#include <libavfilter/video.h>
#include <libavfilter/yadif.h>

void halide_yadif(int w, int linesize, int h, int parity, int tff, uint8_t *out,
                  uint8_t *prev, uint8_t *cur, uint8_t *next);

typedef struct ThreadData {
  AVFrame *frame;
  int plane;
  int w, h;
  int parity;
  int tff;
} ThreadData;

#define MAX_ALIGN 8

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr,
                        int nb_jobs) {
  YADIFContext *s = ctx->priv;
  ThreadData *td = arg;
  int refs = s->cur->linesize[td->plane];
  int df = (s->csp->comp[td->plane].depth + 7) / 8;
  int pix_3 = 3 * df;
  int slice_start = (td->h * jobnr) / nb_jobs;
  int slice_end = (td->h * (jobnr + 1)) / nb_jobs;
  int y;
  int edge = 3 + MAX_ALIGN / df - 1;

  /* filtering reads 3 pixels to the left/right; to avoid invalid reads,
   * we need to call the c variant which avoids this for border pixels
   */
  for (y = slice_start; y < slice_end; y++) {
    if ((y ^ td->parity) & 1) {
      uint8_t *prev = &s->prev->data[td->plane][y * refs];
      uint8_t *cur = &s->cur->data[td->plane][y * refs];
      uint8_t *next = &s->next->data[td->plane][y * refs];
      uint8_t *dst =
          &td->frame->data[td->plane][y * td->frame->linesize[td->plane]];
      int mode = y == 1 || y + 2 == td->h ? 2 : s->mode;
      s->filter_line(dst + pix_3, prev + pix_3, cur + pix_3, next + pix_3,
                     td->w - edge, y + 1 < td->h ? refs : -refs,
                     y ? -refs : refs, td->parity ^ td->tff, mode);
      s->filter_edges(dst, prev, cur, next, td->w, y + 1 < td->h ? refs : -refs,
                      y ? -refs : refs, td->parity ^ td->tff, mode);
    } else {
      memcpy(&td->frame->data[td->plane][y * td->frame->linesize[td->plane]],
             &s->cur->data[td->plane][y * refs], td->w * df);
    }
  }
  return 0;
}

static void yadif_filter(AVFilterContext *ctx, AVFrame *dstpic, int parity,
                         int tff) {
  YADIFContext *yadif = ctx->priv;
  ThreadData td = {.frame = dstpic, .parity = parity, .tff = tff};
  int i;

  for (i = 0; i < yadif->csp->nb_components; i++) {
    int w = dstpic->width;
    int h = dstpic->height;

    if (i == 1 || i == 2) {
      w = AV_CEIL_RSHIFT(w, yadif->csp->log2_chroma_w);
      h = AV_CEIL_RSHIFT(h, yadif->csp->log2_chroma_h);
    }

    td.w = w;
    td.h = h;
    td.plane = i;

    halide_yadif(w, dstpic->linesize[i], h, parity, tff, dstpic->data[i],
                 yadif->prev->data[i], yadif->cur->data[i],
                 yadif->next->data[i]);

    // ff_filter_execute(ctx, filter_slice, &td, NULL,
    //                   FFMIN(h, ff_filter_get_nb_threads(ctx)));
  }

  emms_c();
}

void set_yadif_filter(AVFilterContext *ctx) {
  YADIFContext *yadif = ctx->priv;
  yadif->filter = yadif_filter;
}
