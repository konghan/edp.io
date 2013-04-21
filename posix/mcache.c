/*
 * copyright (c) 2013, Konghan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: 
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied.
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

