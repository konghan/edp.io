/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "emitter.h"

#include "atomic.h"
#include "logger.h"
#include "mcache.h"


#define	EMIT_INSTANCE_MAGIC	    0xedafedafedaf0000

struct edp_emit{
    uint64_t		ee_magic;
    int			ee_init;

    spi_spinlock_t	ee_lock;
    atomic_t		ee_pendings;  
//    struct list_head	ee_events;  // events belong to this edpu
    struct list_head	ee_node;    // link to emit master

    emit_handler	ee_handler[EMIT_EVENT_TYPE_MAX];

    void		*ee_data;   // owner's data
};

typedef struct emit_data{
    int			ed_init;
    spi_spinlock_t	ed_lock;
    struct list_head	ed_emits;
}emit_data_t;

static emit_data_t	__emit_data = {};

/*
 * implemetations
 */
static inline emit_data_t *get_data(){
    return &__emit_data;
}

static int emit_check(emit_t em){
    struct edp_emit *ee = em;

    ASSERT(ee != NULL);
    
    return ee->ee_magic == EMIT_INSTANCE_MAGIC;
}

static int emit_default_handler(emit_t em, edp_event_t *ev){
    ASSERT(ev != NULL);
    
    log_warn("default watch event, type:%d\n", ev->ev_type);
    return -ENOENT;
}

static void emit_event_handler(void *emit, struct edp_event *ev){
    struct edp_emit *ee = (struct edp_emit *)edpu;
    int		    errcode;

    ASSERT((emit != NULL) && emit_check(ee));
    ASSERT(ev != NULL);

    if((ev->ev_type < 0) || (ev->ev_type >= EMIT_EVENT_TYPE_MAX)){
	log_warn("event type overflow:%d!\n", ev->ev_type);
	edp_event_done(ev, -ERANGE);
	return ;
    }

    if(ee->ee_handler[ev->ev_type] == emit_default_watch){
	log_warn("no handler for this event:%d!\n", ev->ev_type);
	edp_event_done(ev, -ENOENT);
	return ;
    }

    errcode = (ee->ee_handler[ev->ev_type])(ee, ev);

//    spi_spin_lock(&eu->ee_lock);
//    list_del(&ev->ev_edpu);
//    spi_spin_unlock(&eu->ee_lock);
    atomic_dec(&eu->ee_pendings);

    edp_event_done(ev, errcode);
}

int emit_dispatch(emit_t em, edp_event_t *ev, edp_event_cb cb, void *data){
    struct edp_emit  *ee = em;

    ASSERT(ee != NULL);
    ASSERT(ev != NULL);

    if((ev->ev_type < 0) || (ev->ev_type >= emit_EVENT_TYPE_MAX)){
	log_warn("event type overflow:%d!\n", ev->ev_type);
	return -ERANGE;
    }

    if(ee->ee_handler[ev->ev_type] == emit_default_watch){
	log_warn("no handler for this event:%d!\n", ev->ev_type);
	return -ENOENT;
    }

    if(ee->ee_init == 0){
	log_warn("emit not initalized!\n");
	return -EINVAL;
    }

    ev->ev_cb	= cb;
    ev->ev_data	= data;
    ev->ev_handler = emit_event_handler;
    ev->ev_emit	= ee;

//    spi_spin_lock(&eu->ee_lock);
//    list_add(&ev->ev_edpu, &eu->ee_events);
//    spi_spin_unlock(&eu->ee_lock);
    atomic_inc(&eu->ee_pendings);

    return edp_dispatch(ev);
}

int emit_add_handler(emit_t em, int type, emit_handler handler){
    struct edp_emit *ee = em;

    ASSERT(ee != NULL);

    if((type < 0) || (type >= EMIT_EVENT_TYPE_MAX)){
	log_warn("event type overflow:%d!\n", type);
	return -ERANGE;
    }

    eu->ee_handler[type] = handler;
    return 0;
}

int emit_rmv_watch(emit_t em, int type){
    struct edp_emit *ee = em;

    ASSERT(ee != NULL);

    if((type < 0) || (type >= EMIT_EVENT_TYPE_MAX)){
	log_warn("event type overflow:%d!\n", type);
	return -ERANGE;
    }

    eu->ee_handler[type] = emit_default_handler;
    return 0;
}

int emit_create(void *data, emit_t *em){
    struct edp_emit  *ee;
    int	    i;

    ASSERT(em != NULL);

    ee = (struct edp_emit *)mheap_alloc(sizeof(*ee));
    if(ee == NULL){
	log_warn("not enough memory!\n");
	return -ENOMEM;
    }
    memset(ee, 0, sizeof(*ee));

    ee->ee_magic = emit_INSTANCE_MAGIC;
    spi_spin_init(&ee->ee_lock);
    atomic_reset(&ee->ee_pendings);
//    INIT_LIST_HEAD(&ee->ee_events);
    INIT_LIST_HEAD(&ee->ee_node);

    for(i = 0; i < EMIT_EVENT_TYPE_MAX; i++){
	ee->ee_handler[i] = emit_default_handler;
    }

    ee->ee_data	= data;
    ee->ee_init	= 1;

    spi_spin_lock(&__emit_data.ed_lock);
    list_add(&ee->ee_node, &__emit_data.ed_emits);
    spi_spin_unlock(&__emit_data.ed_lock);

    *em = ee;

    return 0;
}

int emit_destroy(emit_t em){
    struct edp_emit *ee = em;

    ASSERT(ee != NULL);

    if(ee->ee_pendings != 0){
	log_warn("edpu still have pending events!\n");
	return -EINVAL;
    }

    ee->ee_init = 0;

    spi_spin_lock(&__emit_data.ed_lock);
    list_del(&ee->ee_node);
    spi_spin_unlock(&__emit_data.ed_lock);

    spi_spin_fini(&ee->ee_lock);

    mheap_free(ee);

    return 0;
}

void *emit_get(emit_t em){
    struct edp_emit *ee = em;

    ASSERT(ee != NULL);

    return ee->ee_data;
}

void *emit_set(emit_t em, void *data){
    struct edp_emit *ee = em;
    void	    *old;

    ASSERT(ee != NULL);

    spi_spin_lock(&ee->ee_lock);
    old = ee->ee_data;
    ee->ee_data = data;
    spi_spin_unlock(&ee->ee_lock);

    return old;
}

int emit_init(){
    emit_data_t	*ed = &__emit_data;

    if(ed->ed_init){
	return 0;
    }

    ed->ed_init = 1;
    spi_spin_init(&ed->ed_lock);
    INIT_LIST_HEAD(&ed->ed_emits);

    return 0;
}

int emit_fini(){
    emit_data_t	*ed = &__emit_data;

    if(ed->ed_init == 0){
	return 0;
    }

    spi_spin_lock(&ed->ed_lock);
    if(!list_empty(&ed->ed_emits)){
	spi_spin_unlock(&ed->ed_lock);
	return -EINVAL;
    }
    ed->ed_init = 0;
    spi_spin_unlock(&ed->ed_lock);
    
    spi_spin_fini(&ed->ed_lock);
    return 0;
}

