/*
 * Copyright @ konghan, All rights reserved.
 */

#include "edap.h"

#include "atomic.h"
#include "logger.h"
#include "slab.h"
#include "edpu.h"

#define EDAP_HIGH_NORM_RATIO	    5

typedef struct edap_worker{
    spi_thread_t	ew_thread;
    int			ew_status;  // 0 = stop; 1 = run

    __spi_mutex_t	ew_mutex;

    spi_spinlock_t	ew_criti_lock;
    struct list_head	ew_criti_events;
    atomic64_t		ew_criti_pending;
    atomic64_t		ew_criti_handled;

    spi_spinlock_t	ew_emerg_lock;
    struct list_head	ew_emerg_events;
    atomic64_t		ew_emerg_pending;
    atomic64_t		ew_emerg_handled;

    spi_spinlock_t	ew_high_lock;
    struct list_head	ew_high_events;
    atomic64_t		ew_high_pending;
    atomic64_t		ew_high_handled;

    spi_spinlock_t	ew_norm_lock;
    struct list_head	ew_norm_events;
    atomic64_t		ew_norm_pending;
    atomic64_t		ew_norm_handled;

    spi_spinlock_t	ew_idle_lock;
    struct list_head	ew_idle_events;
    atomic64_t		ew_idle_pending;
    atomic64_t		ew_idle_handled;

    struct list_head	ew_events;
    atomic64_t		ew_event_pending;

}edap_worker_t;

typedef struct edap_data{
    int			ed_init;
    int			ed_thread_num;
    int			ed_round;
    edap_worker_t	*ed_threads;
}edap_data_t;

static edap_data_t  __edap_data = {};

static void edap_do_event(edap_event_t *ev){
    ASSERT((ev != NULL) && (ev->ev_handler != NULL));

    ev->ev_handler(ev->ev_edpu, ev);
}

static int edap_worker_init(edap_worker_t *ew){
    ASSERT(ew != NULL);

    __spi_mutex_init(&ew->ew_mutex);

    spi_spin_init(&ew->ew_criti_lock);
    INIT_LIST_HEAD(&ew->ew_criti_events);

    spi_spin_init(&ew->ew_emerg_lock);
    INIT_LIST_HEAD(&ew->ew_emerg_events);

    spi_spin_init(&ew->ew_high_lock);
    INIT_LIST_HEAD(&ew->ew_high_events);

    spi_spin_init(&ew->ew_norm_lock);
    INIT_LIST_HEAD(&ew->ew_norm_events);

    spi_spin_init(&ew->ew_idle_lock);
    INIT_LIST_HEAD(&ew->ew_idle_events);

    ew->ew_status = 1; // running
    
    return 0;
}

static int edap_worker_fini(edap_worker_t *ew){
    ASSERT(ew!= NULL);

    if(!ew->ew_status){
	return -1;
    }

    spi_spin_fini(&ew->ew_idle_lock);

    spi_spin_fini(&ew->ew_norm_lock);

    spi_spin_fini(&ew->ew_high_lock);

    spi_spin_fini(&ew->ew_emerg_lock);

    spi_spin_fini(&ew->ew_criti_lock);

    __spi_mutex_fini(&ew->ew_mutex);

    ew->ew_status = 0;

    return 0;
}

static void *edap_worker_routine(void *data){
    edap_worker_t	*ew = (edap_worker_t *)data;
    edap_event_t	*evt;
    struct list_head	events;
    struct list_head	*pos, *tmp;
    int			ratio = 0;

    INIT_LIST_HEAD(&events);

    edap_worker_init(ew);

    while(ew->ew_status){
	
	__spi_mutex_lock(&ew->ew_mutex);

criti_event:
	spi_spin_lock(&ew->ew_criti_lock);
	if(!list_empty(&ew->ew_criti_events)){
	    list_move(&ew->ew_criti_events, &events);
	}
	atomic64_reset(&ew->ew_criti_pending);
	spi_spin_unlock(&ew->ew_criti_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, edap_event_t, ev_node);
	    list_del(pos);
	    edap_do_event(evt);
	    atomic64_inc(&ew->ew_criti_handled);
	}

emerg_event:
	spi_spin_lock(&ew->ew_emerg_lock);
	if(!list_empty(&ew->ew_emerg_events)){
	    list_move(&ew->ew_emerg_events, &events);
	}
	atomic64_reset(&ew->ew_emerg_pending);
	spi_spin_unlock(&ew->ew_emerg_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edap_event, ev_node);
	    list_del(pos);
	    edap_do_event(evt);
	    atomic64_inc(&ew->ew_emerg_handled);

	    if(ew->ew_criti_pending)
		goto criti_event;
	}
	
high_event:
	spi_spin_lock(&ew->ew_high_lock);
	if(!list_empty(&ew->ew_high_events)){
	    list_move(&ew->ew_high_events, &events);
	}
	ratio = atomic64_reset(&ew->ew_high_pending);
	spi_spin_unlock(&ew->ew_high_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, edap_event_t, ev_node);
	    list_del(pos);
	    edap_do_event(evt);
	    atomic64_inc(&ew->ew_high_handled);

	    if(ew->ew_criti_pending)
		goto criti_event;
	    if(ew->ew_emerg_pending)
		goto emerg_event;
	}
	
norm_event:
	spi_spin_lock(&ew->ew_norm_lock);
	if(!list_empty(&ew->ew_norm_events)){
	    list_move(&ew->ew_norm_events, &events);
	}
	atomic64_reset(&ew->ew_norm_pending);
	spi_spin_unlock(&ew->ew_norm_lock);

	ratio /= EDAP_HIGH_NORM_RATIO;
	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, edap_event_t, ev_node);
	    list_del(pos);
	    edap_do_event(evt);
	    atomic64_inc(&ew->ew_norm_handled);

	    if(ew->ew_criti_pending)
		goto criti_event;
	    if(ew->ew_emerg_pending)
		goto emerg_event;

	    if((ratio == 0) && (ew->ew_high_pending)){
		goto high_event;
	    }
	    ratio--;
	}
	
idle_event:
	spi_spin_lock(&ew->ew_idle_lock);
	if(!list_empty(&ew->ew_idle_events)){
	    list_move_tail(&ew->ew_idle_events, &events);
	}
	atomic64_reset(&ew->ew_idle_pending);
	spi_spin_unlock(&ew->ew_idle_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, edap_event_t, ev_node);
	    list_del(pos);
	    edap_do_event(evt);
	    atomic64_inc(&ew->ew_idle_handled);

	    if(ew->ew_criti_pending)
		goto criti_event;
	    if(ew->ew_emerg_pending)
		goto emerg_event;
	    if(ew->ew_high_pending)
		goto high_event;
	    if(ew->ew_norm_pending)
		goto norm_event;
	}
    }

    if(ew->ew_criti_pending)
	goto criti_event;
    if(ew->ew_emerg_pending)
	goto emerg_event;
    if(ew->ew_high_pending)
	goto high_event;
    if(ew->ew_norm_pending)
	goto norm_event;
    if(ew->ew_idle_pending)
	goto idle_event;

    edap_worker_fini(ew);

    return NULL;
}

int edap_dispatch(edap_event_t *ev){
    edap_data_t		*ed = &__edap_data;
    edap_worker_t	*ew;
    spi_spinlock_t	*lock;
    struct list_head	*lh;
    atomic64_t		*pendings;
    int			cpuid;

    ASSERT(ev != NULL);

    //FIXME:should base on cpu load
    if((ev->ev_cpuid >= 0) && (ev->ev_cpuid < ed->ed_round)){
	cpuid = ev->ev_cpuid;
    }else{
	cpuid = ed->ed_round;
	ev->ev_cpuid = cpuid;
	ed->ed_round++;
	ed->ed_round = ed->ed_round % ed->ed_thread_num;
    }

    ew = &(ed->ed_threads[cpuid]);
    switch(ev->ev_priority){
	case EDAP_EVENT_PRIORITY_CRITI:
	    lock = &ew->ew_criti_lock;
	    lh   = &ew->ew_criti_events;
	    pendings = &ew->ew_criti_pending;
	    break;

	case EDAP_EVENT_PRIORITY_EMERG:
	    lock = &ew->ew_emerg_lock;
	    lh   = &ew->ew_emerg_events;
	    pendings = &ew->ew_emerg_pending;
	    break;

	case EDAP_EVENT_PRIORITY_HIGH:
	    lock = &ew->ew_high_lock;
	    lh   = &ew->ew_high_events;
	    pendings = &ew->ew_high_pending;
	    break;

	case EDAP_EVENT_PRIORITY_NORM:
	    lock = &ew->ew_norm_lock;
	    lh   = &ew->ew_norm_events;
	    pendings = &ew->ew_norm_pending;
	    break;

	case EDAP_EVENT_PRIORITY_IDLE:
	    lock = &ew->ew_idle_lock;
	    lh   = &ew->ew_idle_events;
	    pendings = &ew->ew_idle_pending;
	    break;

	default:
	    log_warn("priority out of range!\n");
	    return -ERANGE;
    }

    spi_spin_lock(lock);
    list_add(&ev->ev_node, lh);
    atomic64_inc(pendings);
    spi_spin_unlock(lock);

    __spi_mutex_unlock(&ew->ew_mutex);

    return 0;
}

int edap_init(int thread){
    edap_data_t	    *ed = &__edap_data;
    edap_worker_t   *et;
    int		    i, ret = -1;

    logger_init();
    slab_init();

    if(ed->ed_init){
	return -1;
    }

    ed->ed_threads = (edap_worker_t *)spi_malloc(sizeof(edap_worker_t) * thread);
    if(ed->ed_threads == NULL){
	return -ENOMEM;
    }
    memset(ed->ed_threads, 0, sizeof(edap_worker_t) * thread);

    for(i = 0; i < thread; i++){
	et = &(ed->ed_threads[i]);
        ret = spi_thread_create(&et->ew_thread, edap_worker_routine, et);
	if(ret != 0){
	    break;
	}
    }

    if(i != thread){
	for(; i>= 0; i--){
	    et = &(ed->ed_threads[i]);
	    spi_thread_destroy(et->ew_thread);
	}
	spi_free(ed->ed_threads);
    }else{
	ed->ed_init = 1;
	ed->ed_thread_num = thread;
    }

    edpu_init();

    return ret;
}

int edap_fini(){
    edap_data_t	    *ed = &__edap_data;
    edap_worker_t   *et;
    int		    i;

    edpu_fini();

    if(ed->ed_init){
	ed->ed_init = 0;
	for(i = ed->ed_thread_num; i >= 0; i--){
	    et = &(ed->ed_threads[i]);
	    et->ew_status = 0; // let it stop
//	    spi_thread_destroy(&et->ew_thread);
	    //FIXME:join it
	}
	spi_free(ed->ed_threads);
    }

    slab_fini();
    logger_fini();

    return 0;
}


