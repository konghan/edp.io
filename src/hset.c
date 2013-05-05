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

#include "hset.h"

#include "atomic.h"
#include "mcache.h"
#include "logger.h"

struct hset_struct{
    int			hs_init;
    struct list_head	hs_node;

    spi_spinlock_t	hs_locks[HSET_LOCK_NUM];

    atomic_t		hs_items;
    uint32_t		hs_size;
    struct list_head	hs_ents[];
};

typedef struct hset_data{
    int			hsd_init;

    spi_spinlock_t	hsd_lock;
    struct list_head	hsd_sets;
}hset_data_t;

static hset_data_t	__hset_data = {};

int hset_create(uint32_t size, hset_t *hs){
    hset_data_t		*hsd = &__hset_data;
    struct hset_struct	*h;
    uint32_t		memsize;
    int			i;

    ASSERT((size != 0) && (hs != NULL));

    memsize = sizeof(*h) + size*sizeof(struct list_head);
    h = mheap_alloc(memsize);
    if(h == NULL){
	log_warn("can't alloc enough memory!\n");
	return -ENOMEM;
    }
    memset(h, 0, memsize);

    INIT_LIST_HEAD(&h->hs_node);
    spi_spin_lock(&hsd->hsd_lock);
    list_add(&hsd->hsd_sets, &h->hs_node);
    spi_spin_unlock(&hsd->hsd_lock);
    
    for(i = 0; i < HSET_LOCK_NUM; i++){
	spi_spin_init(&(h->hs_locks[i]));
    }
    atomic_reset(&h->hs_items);

    h->hs_size = size;
    for(i = 0; i < size; i++){
	INIT_LIST_HEAD(&(h->hs_ents[i]));
    }

    h->hs_init = 1;
    *hs = h;

    return 0;
}

int hset_destroy(hset_t hs){
    hset_data_t		*hsd = &__hset_data;
    struct hset_struct  *h = hs;
    int			i;

    ASSERT((h != NULL) && (h->hs_init != 0));

    if(h->hs_items != 0){
	log_warn("hset still have items!\n");
	return -EINVAL;
    }

    h->hs_init = 0;

    for(i = 0; i < HSET_LOCK_NUM; i++){
	spi_spin_fini(&(h->hs_locks[i]));
    }

    spi_spin_lock(&hsd->hsd_lock);
    list_del(&h->hs_node);
    spi_spin_unlock(&hsd->hsd_lock);

    mheap_free(h);

    return 0;
}

int hset_add(hset_t hs, hset_entry_t *hse){
    struct hset_struct	*h = hs;
    spi_spinlock_t	*lock;
    struct list_head	*lh;
    struct hset_entry	*pos;
    int			exist = 0;

    ASSERT((h != NULL) && (hse != NULL));

    lock = &(h->hs_locks[hse->hse_hash % HSET_LOCK_NUM]);

    spi_spin_lock(lock);
    lh = &(h->hs_ents[hse->hse_hash % h->hs_size]);
    list_for_each_entry(pos, lh, hse_node){
	if(pos->hse_hash == hse->hse_hash){
	    exist = 1;
	    break;
	}
    }
    if(!exist){
	INIT_LIST_HEAD(&hse->hse_node);
	list_add_tail(lh, &hse->hse_node);
    }
    spi_spin_unlock(lock);

    return (exist == 0) ? 0 : -1;
}

int hset_del(hset_t hs, uint32_t hash){
    struct hset_struct	*h = hs;
    spi_spinlock_t	*lock;
    struct list_head	*lh;
    struct hset_entry	*pos;
    int			exist = 0;

    ASSERT(h != NULL);

    lock = &(h->hs_locks[hash % HSET_LOCK_NUM]);

    spi_spin_lock(lock);
    lh = &(h->hs_ents[hash % h->hs_size]);
    list_for_each_entry(pos, lh, hse_node){
	if(pos->hse_hash == hash){
	    exist = 1;
	    list_del(&pos->hse_node);
	    break;
	}
    }
    spi_spin_unlock(lock);

    return (exist == 1) ? 0 : -1;
}

int hset_get(hset_t hs, uint32_t hash, hset_entry_t **hse){
    struct hset_struct	*h = hs;
    spi_spinlock_t	*lock;
    struct list_head	*lh;
    struct hset_entry	*pos;
    int			exist = 0;

    ASSERT(h != NULL);

    lock = &(h->hs_locks[hash % HSET_LOCK_NUM]);

    spi_spin_lock(lock);
    lh = &(h->hs_ents[hash % h->hs_size]);
    list_for_each_entry(pos, lh, hse_node){
	if(pos->hse_hash == hash){
	    *hse = pos;
	    exist = 1;
	    break;
	}
    }
    spi_spin_unlock(lock);

    return (exist == 1) ? 0 : -1;
}

int hset_init(){
    hset_data_t   *hsd = &__hset_data;

    spi_spin_init(&hsd->hsd_lock);
    INIT_LIST_HEAD(&hsd->hsd_sets);

    hsd->hsd_init = 1;

    return 0;
}

int hset_fini(){
    hset_data_t   *hsd = &__hset_data;

    if(!hsd->hsd_init){
	return 0;
    }

    spi_spin_lock(&hsd->hsd_lock);
    if(!list_empty(&hsd->hsd_sets)){
	spi_spin_unlock(&hsd->hsd_lock);
	log_warn("htable not empty!\n");
	return -1;
    }
    hsd->hsd_init = 0;
    spi_spin_unlock(&hsd->hsd_lock);

    spi_spin_fini(&hsd->hsd_lock);

    return 0;
}

