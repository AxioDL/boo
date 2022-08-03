/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

#include <stdlib.h>
#include <math.h>
#include <libavcodec/avfft.h>
#include "filter.h"
#include "rdft_t.h"

static void * forward_setup(int len) {return av_rdft_init((int)(log(len)/log(2)+.5),DFT_R2C);}
static void * backward_setup(int len) {return av_rdft_init((int)(log(len)/log(2)+.5),IDFT_C2R);}
static void rdft(int length, void * setup, float * h) {av_rdft_calc(setup, h); (void)length;}
static int multiplier(void) {return 2;}
static void nothing(void) {}
static int flags(void) {return 0;}

rdft_cb_table _soxr_rdft32_cb = {
  forward_setup,
  backward_setup,
  av_rdft_end,
  rdft,
  rdft,
  rdft,
  rdft,
  _soxr_ordered_convolve_f,
  _soxr_ordered_partial_convolve_f,
  multiplier,
  nothing,
  malloc,
  calloc,
  free,
  flags,
};
