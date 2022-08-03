/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

typedef struct {
  void * (* forward_setup)(int);
  void * (* backward_setup)(int);
  void (* delete_setup)(void *);
  void (* forward)(int, void *, void *, void *);
  void (* oforward)(int, void *, void *, void *);
  void (* backward)(int, void *, void *, void *);
  void (* obackward)(int, void *, void *, void *);
  void (* convolve)(int, void *, void *, void const *);
  void (* convolve_portion)(int, void *, void const *);
  int (* multiplier)(void);
  void (* reorder_back)(int, void *, void *, void *);
  void * (* malloc)(size_t);
  void * (* calloc)(size_t, size_t);
  void (* free)(void *);
  int (* flags)(void);
} rdft_cb_table;

#define rdft_forward_setup    RDFT_CB->forward_setup
#define rdft_backward_setup   RDFT_CB->backward_setup
#define rdft_delete_setup     RDFT_CB->delete_setup
#define rdft_forward          RDFT_CB->forward
#define rdft_oforward         RDFT_CB->oforward
#define rdft_backward         RDFT_CB->backward
#define rdft_obackward        RDFT_CB->obackward
#define rdft_convolve         RDFT_CB->convolve
#define rdft_convolve_portion RDFT_CB->convolve_portion
#define rdft_multiplier       RDFT_CB->multiplier
#define rdft_reorder_back     RDFT_CB->reorder_back
#define rdft_malloc           RDFT_CB->malloc
#define rdft_calloc           RDFT_CB->calloc
#define rdft_free             RDFT_CB->free
#define rdft_flags            RDFT_CB->flags

/* Flag templates: */
#define RDFT_IS_SIMD       1
#define RDFT_NEEDS_SCRATCH 2
