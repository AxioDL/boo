#ifndef __NOUVEAU_LIBDRM_PRIVATE_H__
#define __NOUVEAU_LIBDRM_PRIVATE_H__

#include "libdrm_atomics.h"
#include "nouveau_drm.h"

#include "nouveau.h"

#include <switch.h>

#define BO_MAP_NUM_BUCKETS 31

struct nouveau_client_bo_map_entry {
	struct nouveau_client_bo_map_entry *next;
	struct nouveau_client_bo_map_entry **prev_next;
	struct drm_nouveau_gem_pushbuf_bo *kref;
	struct nouveau_pushbuf *push;
	uint32_t bo_handle;
};

struct nouveau_client_bo_map {
	struct nouveau_client_bo_map_entry *buckets[BO_MAP_NUM_BUCKETS+1];
};

struct nouveau_client_priv {
	struct nouveau_client base;
	struct nouveau_client_bo_map bomap;
};

static inline struct nouveau_client_priv *
nouveau_client(struct nouveau_client *client)
{
	return (struct nouveau_client_priv *)client;
}

void
cli_map_free(struct nouveau_client *);

struct drm_nouveau_gem_pushbuf_bo *
cli_kref_get(struct nouveau_client *, struct nouveau_bo *bo);

struct nouveau_pushbuf *
cli_push_get(struct nouveau_client *, struct nouveau_bo *bo);

void
cli_kref_set(struct nouveau_client *, struct nouveau_bo *bo,
             struct drm_nouveau_gem_pushbuf_bo *kref,
             struct nouveau_pushbuf *push);

struct nouveau_bo_priv {
	struct nouveau_bo base;
	struct nouveau_list head;
	atomic_t refcnt;
	void* map_addr;
	uint32_t name;
	uint32_t access;
	NvBuffer buffer;
	NvFence fence;
};

static inline struct nouveau_bo_priv *
nouveau_bo(struct nouveau_bo *bo)
{
	return (struct nouveau_bo_priv *)bo;
}

struct nouveau_device_priv {
	struct nouveau_device base;
	int close;
	struct nouveau_list bo_list;
	uint32_t *client;
	int nr_client;
	bool have_bo_usage;
	int gart_limit_percent, vram_limit_percent;
	uint64_t allocspace_offset;
	Mutex lock;
	NvGpu gpu;
};

static inline struct nouveau_device_priv *
nouveau_device(struct nouveau_device *dev)
{
	return (struct nouveau_device_priv *)dev;
}

#endif
