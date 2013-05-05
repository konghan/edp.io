/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __EDBLK_H__
#define __EDBLK_H__

#include "edap_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct edblk;
typedef struct edblk edblk_t;

typedef struct edblk_callback{
}edblk_callback_t;

int edblk_create(edblk_t **blk);
int edblk_destroy(edblk_t *blk);

int edblk_setcallback(edblk_t *blk, edblk_callback_t *cb);

int edblk_open(edblk_t *blk, const char *path, int flags);
int edblk_close(edblk_t *blk);

int edblk_stat(edblk_t *blk, void *attr);

int edblk_read(edblk_t *blk, edio_context_t *ioctx);
int edblk_write(edblk_t *blk, edio_context_t *ioctx);


#ifdef __cplusplus
}
#endif

#endif // __EDBLK_H__


