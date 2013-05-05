/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __MCACHE_H__
#define __MCACHE_H__

#include "edp_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCACHE_FLAGS_NOWAIT	0X00000000
#define MCACHE_FLAGS_WAIT	0x00000001

#define MCACHE_FLAGS_CORE	0X00010000

//struct mem_pool;
struct mem_cache;

//typedef struct mem_pool* mpool_t;
typedef struct mem_cache* mcache_t;

//int mpool_create(void *start, uint64_t size, mpool_t *mp);
//int mpool_destroy(mpool_t mp);

int mcache_create(size_t size, size_t align, int flags, mcache_t *mc);
int mcache_destroy(mcache_t mc);

void *mcache_alloc(mcache_t mc);
void mcache_free(mcache_t mc, void *ptr);

void *mheap_alloc(size_t size);
void mheap_free(void *ptr);

int mcache_init(void *start, uint64_t size);
int mcache_fini();

#ifdef __cplusplus
}
#endif

#endif // __MCACHE_H__


