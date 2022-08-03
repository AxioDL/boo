/* SoX Resampler Library      Copyright (c) 2007-13 robs@users.sourceforge.net
* Licence for this file: LGPL v2.1                  See LICENCE for details. */

typedef struct {
  void * (*input)(void *, void * samples, size_t   n);
  void (*process)(void *, size_t);
  void const * (*output)(void *, void * samples, size_t * n);
  void (*flush)(void *);
  void (*close)(void *);
  double (*delay)(void *);
  void (*sizes)(size_t * shared, size_t * channel);
  char const * (*create)(void * channel, void * shared, double io_ratio, void * q_spec, void * r_spec, double scale);
  void (*set_io_ratio)(void *, double io_ratio, size_t len);
  char const * (*id)(void);
} control_block_t;

#define resampler_input        p->control_block.input
#define resampler_process      p->control_block.process
#define resampler_output       p->control_block.output
#define resampler_flush        p->control_block.flush
#define resampler_close        p->control_block.close
#define resampler_delay        p->control_block.delay
#define resampler_sizes        p->control_block.sizes
#define resampler_create       p->control_block.create
#define resampler_set_io_ratio p->control_block.set_io_ratio
#define resampler_id           p->control_block.id
