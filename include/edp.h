/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
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
    kEDP_EVENT_PRIORITY_CRIT = 0,
    kEDP_EVENT_PRIORITY_EMRG,
    kEDP_EVENT_PRIORITY_HIGH,
    kEDP_EVENT_PRIORITY_NORM,
    kEDP_EVENT_PRIORITY_IDLE,
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

    void		*ev_emit;
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

