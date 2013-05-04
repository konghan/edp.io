/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the accompanying
 */

#include "emitter.h"

#include "atomic.h"
#include "logger.h"
#include "mcache.h"

#define	EDPU_INSTANCE_MAGIC	0xedafedafedaf0000

typedef struct edap_edpu{
    uint64_t		eu_magic;
    int			eu_init;

    spi_spinlock_t	eu_lock;
    atomic64_t		eu_pendings;  
//    struct list_head	eu_events;  // events belong to this edpu
    struct list_head	eu_node;    // link to edpu master

    edpu_watch		eu_watch[EDPU_EVENT_TYPE_MAX];

    void		*eu_data;   // owner's data
}edpu_t;

typedef struct edpu_data{
    int			ed_init;
    spi_spinlock_t	ed_lock;
    struct list_head	ed_edpus;
}edpu_data_t;

static edpu_data_t	__edpu_data = {};

static int edpu_check(edpu_t *eu){
    return eu->eu_magic == EDPU_INSTANCE_MAGIC;
}

static int edpu_default_watch(edpu_t *eu, edap_event_t *ev){
    return -1;
}

static void edpu_event_handler(void *edpu, struct edap_event *ev){
    edpu_t	*eu = (edpu_t *)edpu;
    int		errcode;

    ASSERT((edpu != NULL) && edpu_check(eu));
    ASSERT(ev != NULL);

    if((ev->ev_type < 0) || (ev->ev_type >= EDPU_EVENT_TYPE_MAX)){
	log_warn("event type overflow!\n");
	edap_event_done(ev, ERANGE);
	return ;
    }

    if(eu->eu_watch[ev->ev_type] == edpu_default_watch){
	log_warn("no watch for this event!\n");
	edap_event_done(ev, ENOENT);
	return ;
    }

    errcode = (eu->eu_watch[ev->ev_type])(eu, ev);

//    spi_spin_lock(&eu->eu_lock);
//    list_del(&ev->ev_edpu);
//    spi_spin_unlock(&eu->eu_lock);
    atomic64_dec(&eu->eu_pendings);

    edap_event_done(ev, errcode);
}

int edpu_dispatch(edpu_t *eu, edap_event_t *ev, edap_event_cb cb, void *data){

    ASSERT(eu != NULL);
    ASSERT(ev != NULL);

    if((ev->ev_type < 0) || (ev->ev_type >= EDPU_EVENT_TYPE_MAX)){
	log_warn("event type overflow!\n");
	return -ERANGE;
    }

    if(eu->eu_watch[ev->ev_type] == edpu_default_watch){
	log_warn("no watch for this event!\n");
	return -ENOENT;
    }

    if(eu->eu_init == 0){
	log_warn("edpu not initalized!\n");
	return -EINVAL;
    }

    ev->ev_cb	= cb;
    ev->ev_data	= data;
    ev->ev_handler = edpu_event_handler;
    ev->ev_edpu	= eu;

//    spi_spin_lock(&eu->eu_lock);
//    list_add(&ev->ev_edpu, &eu->eu_events);
//    spi_spin_unlock(&eu->eu_lock);
    atomic64_inc(&eu->eu_pendings);

    return edap_dispatch(ev);
}

int edpu_add_watch(edpu_t *eu, int type, edpu_watch watch){
    ASSERT(eu != NULL);

    if((type < 0) || (type >= EDPU_EVENT_TYPE_MAX)){
	log_warn("event type overflow!\n");
	return -ERANGE;
    }

    eu->eu_watch[type] = watch;
    return 0;
}

int edpu_rmv_watch(edpu_t *eu, int type){
    ASSERT(eu != NULL);

    if((type < 0) || (type >= EDPU_EVENT_TYPE_MAX)){
	log_warn("event type overflow!\n");
	return -ERANGE;
    }

    eu->eu_watch[type] = edpu_default_watch;
    return 0;
}

int edpu_create(void *data, edpu_t **eu){
    edpu_t  *e;
    int	    i;

    ASSERT(eu != NULL);

    e = (edpu_t *)spi_malloc(sizeof(*e));
    if(e == NULL){
	log_warn("not enough memory!\n");
	return -ENOMEM;
    }
    memset(e, 0, sizeof(*e));

    e->eu_magic = EDPU_INSTANCE_MAGIC;
    spi_spin_init(&e->eu_lock);
    atomic64_reset(&e->eu_pendings);
//    INIT_LIST_HEAD(&e->eu_events);
    INIT_LIST_HEAD(&e->eu_node);

    for(i = 0; i < EDPU_EVENT_TYPE_MAX; i++){
	e->eu_watch[i] = edpu_default_watch;
    }

    e->eu_data	= data;
    e->eu_init	= 1;

    spi_spin_lock(&__edpu_data.ed_lock);
    list_add(&e->eu_node, &__edpu_data.ed_edpus);
    spi_spin_unlock(&__edpu_data.ed_lock);

    *eu = e;

    return 0;
}

int edpu_destroy(edpu_t *eu){
    ASSERT(eu != NULL);

    if(eu->eu_pendings != 0){
	log_warn("edpu still have pending events!\n");
	return -EINVAL;
    }

    eu->eu_init = 0;

    spi_spin_lock(&__edpu_data.ed_lock);
    list_del(&eu->eu_node);
    spi_spin_unlock(&__edpu_data.ed_lock);

    spi_spin_fini(&eu->eu_lock);

    spi_free(eu);

    return 0;
}

void *edpu_get(edpu_t *eu){
    ASSERT(eu != NULL);

    return eu->eu_data;
}

void *edpu_set(edpu_t *eu, void *data){
    void    *old;

    ASSERT(eu != NULL);

    spi_spin_lock(&eu->eu_lock);
    old = eu->eu_data;
    eu->eu_data = data;
    spi_spin_unlock(&eu->eu_lock);

    return old;
}

int edpu_init(){
    edpu_data_t	*ed = &__edpu_data;

    if(ed->ed_init){
	return 0;
    }

    ed->ed_init = 1;
    spi_spin_init(&ed->ed_lock);
    INIT_LIST_HEAD(&ed->ed_edpus);

    return 0;
}

int edpu_fini(){
    edpu_data_t	*ed = &__edpu_data;

    if(ed->ed_init == 0){
	return 0;
    }

    spi_spin_lock(&ed->ed_lock);
    if(!list_empty(&ed->ed_edpus)){
	spi_spin_unlock(&ed->ed_lock);
	return -EINVAL;
    }
    ed->ed_init = 0;
    spi_spin_unlock(&ed->ed_lock);
    
    spi_spin_fini(&ed->ed_lock);
    return 0;
}

