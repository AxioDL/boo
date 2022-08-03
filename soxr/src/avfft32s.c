/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

#include <math.h>
#include <libavcodec/avfft.h>
#include "util32s.h"
#include "rdft_t.h"

static void * forward_setup(int len) {return av_rdft_init((int)(log(len)/log(2)+.5),DFT_R2C);}
static void * backward_setup(int len) {return av_rdft_init((int)(log(len)/log(2)+.5),IDFT_C2R);}
static void rdft(int length, void * setup, void * H, void * scratch) {av_rdft_calc(setup, H); (void)length; (void)scratch;}
static int multiplier(void) {return 2;}
static void nothing2(int u1, void *u2, void *u3, void *u4) {(void)u1; (void)u2; (void)u3; (void)u4;}
static int flags(void) {return RDFT_IS_SIMD;}

rdft_cb_table _soxr_rdft32s_cb = {
  forward_setup,
  backward_setup,
  av_rdft_end,
  rdft,
  rdft,
  rdft,
  rdft,
  ORDERED_CONVOLVE_SIMD,
  ORDERED_PARTIAL_CONVOLVE_SIMD,
  multiplier,
  nothing2,
  SIMD_ALIGNED_MALLOC,
  SIMD_ALIGNED_CALLOC,
  SIMD_ALIGNED_FREE,
  flags,
};
