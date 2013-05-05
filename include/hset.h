/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __HSET_H__
#define __HSET_H__

#include "edp_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HSET_LOCK_NUM	    16

typedef struct hset_entry{
    uint32_t		hse_hash;
    struct list_head	hse_node;
}hset_entry_t;

struct hset_struct;
typedef struct hset_struct *hset_t;

int hset_create(uint32_t size, hset_t *hs);
int hset_destroy(hset_t hs);

int hset_add(hset_t hs, hset_entry_t *hse);
int hset_del(hset_t hs, uint32_t hash);

int hset_get(hset_t hs, uint32_t hash, hset_entry_t **hse);

int hset_init();
int hset_fini();

#ifdef __cplusplus
}
#endif

#endif // __HSET_H__

