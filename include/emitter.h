/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __EMITTER_H__
#define __EMITTER_H__

#include "edp.h"

#ifdef __cplusplus
extern "C" {
#endif

struct edp_emit
typedef struct edp_emit *emit_t;

typedef int (*emit_handler)(emit_t em, edp_event_t *ev);

int emit_dispatch(emit_t em, edp_event_t *ev, edp_event_cb cb, void *data);

int emit_add_handler(emit_t em, int type, emit_handler handler);
int emit_rmv_handler(emit_t em, int type);

int emit_create(void *data, emit_t *em);
int emit_destroy(emit_t em);

void *emit_get(emit_t em);
void *emit_set(emit_t em, void *data);

int emit_init();
int emit_fini();

#ifdef __cplusplus
}
#endif

#endif // __EMITTER_H__

