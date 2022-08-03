/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

#define SIMD_ALIGNED_FREE free
#define SIMD_ALIGNED_MALLOC malloc
#define PFFFT_SIMD_DISABLE
#define PFFFT_DOUBLE 0
#include "pffft-wrap.c"

#include "filter.h"
#include "rdft_t.h"

static void * setup(int len) {return pffft_new_setup(len, PFFFT_REAL);}
static void delete_setup(void * setup) {pffft_destroy_setup(setup);}
static void forward  (int length, void * setup, float * h, float * scratch) {pffft_transform        (setup, h, h, scratch, PFFFT_FORWARD); (void)length;}
static void oforward (int length, void * setup, float * h, float * scratch) {pffft_transform_ordered(setup, h, h, scratch, PFFFT_FORWARD); (void)length;}
static void backward (int length, void * setup, float * H, float * scratch) {pffft_transform        (setup, H, H, scratch, PFFFT_BACKWARD);(void)length;}
static void obackward(int length, void * setup, float * H, float * scratch) {pffft_transform_ordered(setup, H, H, scratch, PFFFT_BACKWARD);(void)length;}
static void convolve(int length, void * setup, float * H, float const * with) { pffft_zconvolve(setup, H, with, H);  (void)length;}
static int multiplier(void) {return 1;}
static int flags(void) {return RDFT_NEEDS_SCRATCH;}

rdft_cb_table _soxr_rdft32_cb = {
  setup,
  setup,
  delete_setup,
  forward,
  oforward,
  backward,
  obackward,
  convolve,
  _soxr_ordered_partial_convolve_f,
  multiplier,
  pffft_reorder_back,
  malloc,
  calloc,
  free,
  flags,
};
