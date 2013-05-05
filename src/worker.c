/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "worker.h"
#include "edp.h"

#include "atomic.h"
#include "logger.h"
#include "mcache.h"

#define HIGH_NORM_RATIO	    5

enum worker_status{
    kWORKER_STATUS_ZERO = 0,
    kWORKER_STATUS_INIT,
    kWORKER_STATUS_RUNNING,
    kWORKER_STATUS_STOP,
};

typedef struct worker{
    spi_thread_t	wk_thread;  // thread handle
    int			wk_status;  // enum worker_status

    __spi_convar_t	wk_convar;

    spi_spinlock_t	wk_crit_lock;
    struct list_head	wk_crit_events;
    atomic_t		wk_crit_pending;
    atomic_t		wk_crit_handled;

    spi_spinlock_t	wk_emrg_lock;
    struct list_head	wk_emrg_events;
    atomic_t		wk_emrg_pending;
    atomic_t		wk_emrg_handled;

    spi_spinlock_t	wk_high_lock;
    struct list_head	wk_high_events;
    atomic_t		wk_high_pending;
    atomic_t		wk_high_handled;

    spi_spinlock_t	wk_norm_lock;
    struct list_head	wk_norm_events;
    atomic_t		wk_norm_pending;
    atomic_t		wk_norm_handled;

    spi_spinlock_t	wk_idle_lock;
    struct list_head	wk_idle_events;
    atomic_t		wk_idle_pending;
    atomic_t		wk_idle_handled;

    struct list_head	wk_events;
    atomic_t		wk_event_pending;

}worker_t;

typedef struct worker_data{
    int			wd_init;
    int			wd_thread_num;
    int			wd_round;
    worker_t		*wd_threads;
}worker_data_t;

static worker_data_t  __worker_data = {};

static inline worker_data_t *get_data(){
    return &__worker_data;
}

static inline void worker_do_event(edp_event_t *ev){
    ASSERT((ev != NULL) && (ev->ev_handler != NULL));

    ev->ev_handler(ev->ev_emit, ev);
}

static int worker_init_tls(worker_t *wkr){
    ASSERT(wkr != NULL);

    __spi_convar_init(&wkr->wk_convar);

    spi_spin_init(&wkr->wk_crit_lock);
    INIT_LIST_HEAD(&wkr->wk_crit_events);

    spi_spin_init(&wkr->wk_emrg_lock);
    INIT_LIST_HEAD(&wkr->wk_emrg_events);

    spi_spin_init(&wkr->wk_high_lock);
    INIT_LIST_HEAD(&wkr->wk_high_events);

    spi_spin_init(&wkr->wk_norm_lock);
    INIT_LIST_HEAD(&wkr->wk_norm_events);

    spi_spin_init(&wkr->wk_idle_lock);
    INIT_LIST_HEAD(&wkr->wk_idle_events);

    wkr->wk_status = kWORKER_STATUS_INIT;
    
    return 0;
}

static int worker_fini_tls(worker_t *wkr){
    ASSERT(wkr!= NULL);

    if(wkr->wk_status != kWORKER_STATUS_STOP){
	return -1;
    }

    spi_spin_fini(&wkr->wk_idle_lock);

    spi_spin_fini(&wkr->wk_norm_lock);

    spi_spin_fini(&wkr->wk_high_lock);

    spi_spin_fini(&wkr->wk_emrg_lock);

    spi_spin_fini(&wkr->wk_crit_lock);

    __spi_convar_fini(&wkr->wk_convar);

    wkr->wk_status = kWORKER_STATUS_ZERO;

    return 0;
}

static void *worker_routine(void *data){
    worker_t		*wkr = (worker_t *)data;
    edp_event_t		*evt;
    struct list_head	events;
    struct list_head	*pos, *tmp;
    int			ratio = 0, high = 0;

    ASSERT(wkr != NULL);

    INIT_LIST_HEAD(&events);
    worker_init_tls(wkr);

    wkr->wk_status = kWORKER_STATUS_RUNNING;

    log_info("worker initailized!\n");

    while(wkr->wk_status != kWORKER_STATUS_STOP){
	__spi_convar_wait(&wkr->wk_convar);

criti_event:
	spi_spin_lock(&wkr->wk_crit_lock);
	if(!list_empty(&wkr->wk_crit_events)){
	    list_splice_init(&wkr->wk_crit_events, &events);
	}
	atomic_reset(&wkr->wk_crit_pending);
	spi_spin_unlock(&wkr->wk_crit_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edp_event, ev_node);
	    if(evt->ev_type != kEDP_EVENT_PRIORITY_CRIT)
		break;
	    list_del(pos);
	    worker_do_event(evt);
	    atomic_inc(&wkr->wk_crit_handled);
	}

emerg_event:
	spi_spin_lock(&wkr->wk_emrg_lock);
	if(!list_empty(&wkr->wk_emrg_events)){
	    list_splice_init(&wkr->wk_emrg_events, &events);
	}
	atomic_reset(&wkr->wk_emrg_pending);
	spi_spin_unlock(&wkr->wk_emrg_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edp_event, ev_node);
	    if(evt->ev_type != kEDP_EVENT_PRIORITY_EMRG)
		break;
	    list_del(pos);
	    worker_do_event(evt);
	    atomic_inc(&wkr->wk_emrg_handled);

	    if(wkr->wk_crit_pending)
		goto criti_event;
	}
	
high_event:
	spi_spin_lock(&wkr->wk_high_lock);
	if(!list_empty(&wkr->wk_high_events)){
	    list_splice_init(&wkr->wk_high_events, &events);
	}
	ratio = atomic_reset(&wkr->wk_high_pending);
	spi_spin_unlock(&wkr->wk_high_lock);

	// tell norm part: ratio is useful
	high = (ratio != 0) ? 1 : 0;

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edp_event, ev_node);
	    if(evt->ev_type != kEDP_EVENT_PRIORITY_HIGH)
		break;
	    list_del(pos);
	    worker_do_event(evt);
	    atomic_inc(&wkr->wk_high_handled);

	    if(wkr->wk_crit_pending)
		goto criti_event;
	    if(wkr->wk_emrg_pending)
		goto emerg_event;
	}
	
norm_event:
	spi_spin_lock(&wkr->wk_norm_lock);
	if(!list_empty(&wkr->wk_norm_events)){
	    list_splice_tail_init(&wkr->wk_norm_events, &events);
	}
	atomic_reset(&wkr->wk_norm_pending);
	spi_spin_unlock(&wkr->wk_norm_lock);

	ratio /= HIGH_NORM_RATIO;
	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edp_event, ev_node);
	    list_del(pos);
	    worker_do_event(evt);
	    atomic_inc(&wkr->wk_norm_handled);

	    if(wkr->wk_crit_pending)
		goto criti_event;
	    if(wkr->wk_emrg_pending)
		goto emerg_event;

	    if(wkr->wk_high_pending){
		if(high == 0){
		    goto high_event;
		}else if(ratio == 0){
		    goto high_event;
		}
	    }
	    ratio--;
	}
	
idle_event:
	spi_spin_lock(&wkr->wk_idle_lock);
	if(!list_empty(&wkr->wk_idle_events)){
	    list_splice_tail_init(&wkr->wk_idle_events, &events);
	}
	atomic_reset(&wkr->wk_idle_pending);
	spi_spin_unlock(&wkr->wk_idle_lock);

	list_for_each_safe(pos, tmp, &events){
	    evt = list_entry(pos, struct edp_event, ev_node);
	    list_del(pos);
	    worker_do_event(evt);
	    atomic_inc(&wkr->wk_idle_handled);

	    if(wkr->wk_crit_pending)
		goto criti_event;
	    if(wkr->wk_emrg_pending)
		goto emerg_event;
	    if(wkr->wk_high_pending)
		goto high_event;
	    if(wkr->wk_norm_pending)
		goto norm_event;
	}
    }

    log_info("worker loop break:%d\n", wkr->wk_status);

    ASSERT(wkr->wk_status == kWORKER_STATUS_STOP);

    if(wkr->wk_crit_pending)
	goto criti_event;
    if(wkr->wk_emrg_pending)
	goto emerg_event;
    if(wkr->wk_high_pending)
	goto high_event;
    if(wkr->wk_norm_pending)
	goto norm_event;
    if(wkr->wk_idle_pending)
	goto idle_event;

    worker_fini_tls(wkr);

    return NULL;
}

int __edp_dispatch(edp_event_t *ev){
    worker_data_t	*wd = get_data();
    worker_t		*wkr;
    spi_spinlock_t	*lock;
    struct list_head	*lh;
    atomic_t		*pendings;
    int			cpuid;

    ASSERT(ev != NULL);

    // FIXME:should base on cpu load
    if((ev->ev_cpuid >= 0) && (ev->ev_cpuid < wd->wd_round)){
	cpuid = ev->ev_cpuid;
    }else{
	cpuid = wd->wd_round;
	ev->ev_cpuid = cpuid;
	wd->wd_round++;
	wd->wd_round = wd->wd_round % wd->wd_thread_num;
    }

    wkr = &(wd->wd_threads[cpuid]);
    switch(ev->ev_priority){
	case kEDP_EVENT_PRIORITY_CRIT:
	    lock = &wkr->wk_crit_lock;
	    lh   = &wkr->wk_crit_events;
	    pendings = &wkr->wk_crit_pending;
	    break;

	case kEDP_EVENT_PRIORITY_EMRG:
	    lock = &wkr->wk_emrg_lock;
	    lh   = &wkr->wk_emrg_events;
	    pendings = &wkr->wk_emrg_pending;
	    break;

	case kEDP_EVENT_PRIORITY_HIGH:
	    lock = &wkr->wk_high_lock;
	    lh   = &wkr->wk_high_events;
	    pendings = &wkr->wk_high_pending;
	    break;

	case kEDP_EVENT_PRIORITY_NORM:
	    lock = &wkr->wk_norm_lock;
	    lh   = &wkr->wk_norm_events;
	    pendings = &wkr->wk_norm_pending;
	    break;

	case kEDP_EVENT_PRIORITY_IDLE:
	    lock = &wkr->wk_idle_lock;
	    lh   = &wkr->wk_idle_events;
	    pendings = &wkr->wk_idle_pending;
	    break;

	default:
	    log_warn("priority out of range!\n");
	    return -ERANGE;
    }

    spi_spin_lock(lock);
    list_add_tail(&ev->ev_node, lh);
    atomic_inc(pendings);
    spi_spin_unlock(lock);

    __spi_convar_signal(&wkr->wk_convar);

    return 0;
}

int worker_init(int thread){
    worker_data_t   *wd = get_data();
    worker_t	    *wkr;
    int		    i, ret = -1;

    if(wd->wd_init){
	return -1;
    }

    wd->wd_threads = (worker_t *)mheap_alloc(sizeof(*wkr) * thread);
    if(wd->wd_threads == NULL){
	return -ENOMEM;
    }
    memset(wd->wd_threads, 0, sizeof(*wkr) * thread);

    for(i = 0; i < thread; i++){
	wkr = &(wd->wd_threads[i]);
        ret = spi_thread_create(&wkr->wk_thread, worker_routine, wkr);
	if(ret != 0){
	    break;
	}
    }

    if(i != thread){
	for(; i>= 0; i--){
	    wkr = &(wd->wd_threads[i]);
	    spi_thread_destroy(wkr->wk_thread);
	}
	mheap_free(wd->wd_threads);
    }else{
	wd->wd_init = 1;
	wd->wd_thread_num = thread;
    }

    return ret;
}

int worker_fini(){
    worker_data_t   *wd = get_data();
    worker_t	    *wkr;
    int		    i;

    if(wd->wd_init){
	wd->wd_init = 0;
	for(i = wd->wd_thread_num; i >= 0; i--){
	    wkr = &(wd->wd_threads[i]);
	    wkr->wk_status = kWORKER_STATUS_STOP; // let it stop
//	    spi_thread_destroy(&wkr->wk_thread);
	    //FIXME:join it
	}
	mheap_free(wd->wd_threads);
    }

    return 0;
}


