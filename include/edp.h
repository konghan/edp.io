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

#ifndef __EDP_H__
#define __EDP_H__

#include "edp_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EDP_EVENT_TYPE_MAX	8

enum edp_event_priority{
    EDP_EVENT_PRIORITY_CRIT = 0,
    EDP_EVENT_PRIORITY_EMRG,
    EDP_EVENT_PRIORITY_HIGH,
    EDP_EVENT_PRIORITY_NORM,
    EDP_EVENT_PRIORITY_IDLE,
};

struct edp_event;
typedef void (*edp_event_cb)(struct edp_event *ev, void *data, int errcode);
typedef void (*edp_event_handler)(void *edm, struct edp_event *ev);

typedef struct edp_event{
    struct list_head	ev_node;    // for scheduler

    short		ev_type;
    short		ev_priority;
    short		ev_cpuid;
    short		ev_reserved;

    edp_event_cb	ev_cb;
    void		*ev_data;

    void		*ev_edm;
    edp_event_handler	ev_handler;
}edp_event_t;

static inline void edp_event_init(edp_event_t *ev, short type, short priority){
    ev->ev_type = type;
    ev->ev_priority = priority;
    ev->ev_cpuid = -1;
}

static inline void edp_event_done(edp_event_t *ev, int errcode){
    if(ev != NULL)
        ev->ev_cb(ev, ev->ev_data, errcode);
}

// used internally
int __edp_dispatch(edp_event_t *ev);

int edp_init(int thread_num);
int edp_loop();
int edp_fini();

#ifdef __cplusplus
}
#endif

#endif // __EDP_H__

