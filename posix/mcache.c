/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "mcache.h"
#include "atomic.h"

struct mem_cache{
    size_t	mc_size;
    size_t	mc_align;
    int		mc_flags;

    atomic_t	mc_allocs;
    atomic_t	mc_frees;
};

static void *align_alloc(size_t size, size_t align){
    void *ptr;

    if(posix_memalign(&ptr, align, size)){
	return NULL;
    }

    return ptr;
}

static void align_free(void *ptr){
    free(ptr);
}

int mcache_create(size_t size, size_t align, int flags, mcache_t *mc){
    struct mem_cache	*m;

    m = mheap_alloc(sizeof(*m));
    if(m == NULL){
	return -ENOMEM;
    }
    memset(m, 0, sizeof(*m));

    m->mc_size	= size;
    m->mc_align	= align;
    m->mc_flags	= flags;

    *mc = m;

    return 0;
}

int mcache_destroy(mcache_t mc){
    struct mem_cache *m = mc;

    ASSERT(m != NULL);

    if(m->mc_allocs != m->mc_frees){
	return -EINVAL;
    }

    mheap_free(m);

    return 0;
}

void *mcache_alloc(mcache_t mc){
    struct mem_cache *m = mc;
    void    *ptr;

    ASSERT(m != NULL);

    ptr = align_alloc(m->mc_size, m->mc_align);
    if(ptr != NULL){
	atomic_inc(&m->mc_allocs);
    }

    return ptr;
}

void mcache_free(mcache_t mc, void *ptr){
    struct mem_cache *m = mc;

    ASSERT(m != NULL);

    atomic_inc(&m->mc_frees);
    align_free(ptr);
}

void *mheap_alloc(size_t size){
    return malloc(size);
}

void mheap_free(void *ptr){
    return free(ptr);
}

int mcache_init(void *start, uint64_t size){
    return 0;
}

int mcache_fini(){
    return 0;
}

