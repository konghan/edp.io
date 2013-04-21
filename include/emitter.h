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

#ifndef __EMITTER_H__
#define __EMITTER_H__

#include "edp.h"

#ifdef __cplusplus
extern "C" {
#endif

struct emitter
typedef struct emitter *emitter_t;

typedef int (*emitter_watch)(emitter_t em, edp_event_t *ev);

int emitter_dispatch(emitter_t em, edp_event_t *ev, edp_event_cb cb, void *data);

int emitter_add_watch(emitter_t em, int type, emitter_watch watch);
int emitter_rmv_watch(emitter_t em, int type);

int emitter_create(void *data, emitter_t *em);
int emitter_destroy(emitter_t em);

void *emitter_get(emitter_t em);
void *emitter_set(emitter_t em, void *data);

int emitter_init();
int emitter_fini();

#ifdef __cplusplus
}
#endif

#endif // __EMITTER_H__

