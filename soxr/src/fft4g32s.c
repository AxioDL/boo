/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

#include "filter.h"
#include "util32s.h"
#include "rdft_t.h"

static void * null(void) {return 0;}
static void nothing(void) {}
static void forward (int length, void * setup, float * H) {lsx_safe_rdft_f(length,  1, H); (void)setup;}
static void backward(int length, void * setup, float * H) {lsx_safe_rdft_f(length, -1, H); (void)setup;}
static int multiplier(void) {return 2;}
static int flags(void) {return RDFT_IS_SIMD;}

rdft_cb_table _soxr_rdft32s_cb = {
  null,
  null,
  nothing,
  forward,
  forward,
  backward,
  backward,
  ORDERED_CONVOLVE_SIMD,
  ORDERED_PARTIAL_CONVOLVE_SIMD,
  multiplier,
  nothing,
  SIMD_ALIGNED_MALLOC,
  SIMD_ALIGNED_CALLOC,
  SIMD_ALIGNED_FREE,
  flags,
};
