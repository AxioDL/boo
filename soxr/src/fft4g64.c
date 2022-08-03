/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

#include <stdlib.h>
#include "filter.h"
#include "fft4g.c"
#include "soxr-config.h"

#if WITH_CR64
#include "rdft_t.h"
static void * null(int u1) {(void)u1; return 0;}
static void nothing(void *u1) {(void)u1;}
static void nothing2(int u1, void *u2, void *u3, void *u4) {(void)u1; (void)u2; (void)u3; (void)u4;}
static void forward (int length, void * setup, void * H, void * scratch) {lsx_safe_rdft(length,  1, H); (void)setup; (void)scratch;}
static void backward(int length, void * setup, void * H, void * scratch) {lsx_safe_rdft(length, -1, H); (void)setup; (void)scratch;}
static int multiplier(void) {return 2;}
static int flags(void) {return 0;}

rdft_cb_table _soxr_rdft64_cb = {
  null,
  null,
  nothing,
  forward,
  forward,
  backward,
  backward,
  _soxr_ordered_convolve,
  _soxr_ordered_partial_convolve,
  multiplier,
  nothing2,
  malloc,
  calloc,
  free,
  flags,
};
#endif
