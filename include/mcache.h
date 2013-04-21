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


