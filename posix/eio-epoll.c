/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "eio.h"

#include "edp.h"

#include "logger.h"
#include "mcache.h"
#include "hset.h"
#include "atomic.h"

#include <sys/epoll.h>

#define EPOLL_MAX_EVENTS	    32

// eio worker thread's local data
typedef struct eio_worker{
    int			iwk_init;

    __spi_convar_t	iwk_convar;
    
    int			iwk_epoll;  // epoll fd
    spi_thread_t	iwk_thread; // thread handle

    atomic_t		iwk_fds;    // fds watch by epoll

    uint64_t		iwk_events; // have processed io events
}eio_worker_t;
   
// event used by eio
typedef struct eio_event{
    hset_entry_t	ioe_ent;    // link to owner

    int			ioe_fd;	    // fd which generate events
    struct eio_worker	*ioe_worker;

    eio_event_cb	ioe_cb;
    void		*ioe_data;
}eio_event_t;

// eio epoll control data
typedef struct eio_data{
    int			iod_init;

    int			iod_num;    // workers number
    spi_spinlock_t	iod_lock;

    int			iod_round;  // round-robin
    
    mcache_t		iod_evcache;
    hset_t		iod_fds;

    struct eio_worker	iod_workers[];
}eio_data_t;

//
static eio_data_t	*__eio_data = NULL;

static inline eio_data_t *get_data(){
    return __eio_data;
}

// select light load worker thread
static eio_worker_t *worker_lightload(){
    eio_data_t	*iod = get_data();
    int		idx;

    // FIXME: determine witch one is light load
    spi_spin_lock(&iod->iod_lock);
    idx = iod->iod_round++ % iod->iod_num;
    spi_spin_unlock(&iod->iod_lock);

    return &(iod->iod_workers[idx]);
}

// add fd to epoll-handle
int eio_addfd(int fd,  eio_event_cb cb, void *data){
    eio_data_t		*iod = get_data();
    struct eio_worker	*iwk = worker_lightload();
    eio_event_t		*ioe;
    struct epoll_event  ev;
    int			ret;

    log_info("---------eio addfd -------\n");
    // construct ioe for epoll-wait callbacks
    ioe = mcache_alloc(iod->iod_evcache);
    if(ioe == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }

    ioe->ioe_fd = fd;
    ioe->ioe_cb = cb;
    ioe->ioe_data = data;
    ioe->ioe_ent.hse_hash = (uint32_t)fd;
    ioe->ioe_worker = iwk;

    log_info("add fd & data to hset\n");
    spi_spin_lock(&iod->iod_lock);
    ret = hset_add(iod->iod_fds, &ioe->ioe_ent);
    spi_spin_unlock(&iod->iod_lock);
    if(ret != 0){
	log_warn("fd:%d already exist!\n", fd);
	mcache_free(iod->iod_evcache, ioe);
	return -EEXIST;
    }

    ev.events   = EPOLLIN | EPOLLOUT;
    ev.data.ptr = ioe;

    log_info("add fd to epoll\n");

    log_info("call epoll ctl\n");
    ret = epoll_ctl(iwk->iwk_epoll, EPOLL_CTL_ADD, fd, &ev);
    if(ret != 0){
	log_warn("epoll add watch fd:%d fail!\n", fd);
	
	spi_spin_lock(&iod->iod_lock);
	hset_del(iod->iod_fds, (uint32_t)fd);
	spi_spin_unlock(&iod->iod_lock);

	mcache_free(iod->iod_evcache, ioe);
	return ret;
    }
    atomic_inc(&iwk->iwk_fds);

    return 0;
}

// delete fd frome epoll-handle
int eio_delfd(int fd){
    eio_data_t	    *iod = get_data();
    eio_event_t	    *ioe;
    hset_entry_t    *hen;
    int		    ret;

    spi_spin_lock(&iod->iod_lock);
    ret = hset_get(iod->iod_fds, (uint32_t)fd, &hen);
    if(ret != 0){
	log_warn("fd:%d not in watch!\n", fd);
	spi_spin_unlock(&iod->iod_lock);
	return -ENOENT;
    }
    hset_del(iod->iod_fds, (uint32_t)fd);
    spi_spin_unlock(&iod->iod_lock);

    ioe = container_of(hen, eio_event_t, ioe_ent);

    ret = epoll_ctl(ioe->ioe_worker->iwk_epoll, EPOLL_CTL_DEL, fd, NULL);
    if(ret != 0){
	log_warn("fd:%d remove from watch fail!\n", fd);
	return -ENOENT;
    }

    atomic_dec(&ioe->ioe_worker->iwk_fds);
    mcache_free(iod->iod_evcache, ioe);

    return 0;
}

static int eio_init_tls(eio_worker_t *iwk){
    ASSERT(iwk != NULL);

    iwk->iwk_epoll = epoll_create(EPOLL_MAX_EVENTS);
    if(iwk->iwk_epoll < 0){
	log_warn("epoll create fail:\n", errno);
	return -errno;
    }

    iwk->iwk_init = 1;

    return 0;
}

static int eio_fini_tls(eio_worker_t *iwk){
    ASSERT(iwk != NULL);

    if(iwk->iwk_init == 0){
	return 0;
    }

    iwk->iwk_init = 0;
    close(iwk->iwk_epoll);
    return 0;
}

static void *eio_worker_routine(void *data){
    eio_worker_t	*iwk = (eio_worker_t *)data;
    struct epoll_event	*events, *ev;
    eio_event_t		*ioe;
    int			msz;
    int			evcnt;
    int			i;
    int			ret = -1;

    ASSERT(iwk != NULL);

    ret = eio_init_tls(iwk);
    if(ret != 0){
	log_warn("init worker fail:%d\n", ret);
	return (void *)-1;
    }

    msz = sizeof(*events) * EPOLL_MAX_EVENTS;
    events = mheap_alloc(msz);
    if(events == NULL){
	log_warn("alloc memory for epoll wait fail!\n");
	return (void *)(-ENOMEM);
    }
    memset(events, 0, msz);

    log_info("eio worker routine running\n");

    // yes, I'm working
    __spi_convar_signal(&iwk->iwk_convar);

    while(iwk->iwk_init){
	evcnt = epoll_wait(iwk->iwk_epoll, events, EPOLL_MAX_EVENTS, -1);
	if(evcnt < 0){
	    log_warn("epoll wait fail:%d\n", errno);
	    ret = -errno;
	    break;
	}

	for(i = 0; i < evcnt; i++){
	    ev = &(events[i]);
	    ioe = (eio_event_t *)ev->data.ptr;
	    ASSERT(ioe != NULL);

	    // call fd bind callback function
	    ioe->ioe_cb(ev->events, ioe->ioe_data);
	    iwk->iwk_events ++;
	}
    }

    eio_fini_tls(iwk);

    return NULL;
}

int eio_init(int thread_num){
    eio_data_t	    *iod;
    eio_worker_t    *iwk;
    size_t	    msz;
    int		    i;
    int		    ret = -1;

    msz = sizeof(*iod) + sizeof(*iwk)*thread_num;

    iod = mheap_alloc(msz);
    if(iod == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }
    memset(iod, 0, msz);

    ret = mcache_create(sizeof(eio_event_t), sizeof(void *), 0, &iod->iod_evcache);
    if(ret != 0){
	log_warn("create mcache fail:%d\n", ret);
	goto exit_mcache;
    }

    spi_spin_init(&iod->iod_lock);

    ret = hset_create(EPOLL_MAX_EVENTS, &iod->iod_fds);
    if(ret != 0){
	log_warn("create hset fail:%d\n", ret);
	goto exit_hset;
    }

    for(i = 0; i < thread_num; i++){
	iwk = &(iod->iod_workers[i]);
	
	ret = __spi_convar_init(&iwk->iwk_convar);
	if(ret != 0){
	    log_warn("create thread convar fail:%d\n", ret);
	    goto exit_thread;
	}

	ret = spi_thread_create(&iwk->iwk_thread, eio_worker_routine, iwk);
	if(ret != 0){
	    log_warn("create thread fail:%d\n", ret);
	    __spi_convar_fini(&iwk->iwk_convar);
	    goto exit_thread;
	}

	ret = __spi_convar_timedwait(&iwk->iwk_convar, 1000);
	if(ret != 0){
	    log_warn("thread not run:\n", ret);
	    spi_thread_destroy(iwk->iwk_thread);
	    __spi_convar_fini(&iwk->iwk_convar);
	    goto exit_thread;
	}
    }

    iod->iod_num = thread_num;
    iod->iod_init = 1;
    __eio_data = iod;

    return 0;

exit_thread:
    for(; i >= 0; i--){
	iwk = &(iod->iod_workers[i]);
	spi_thread_destroy(iwk->iwk_thread);
	__spi_convar_fini(&iwk->iwk_convar);
    }

    hset_destroy(iod->iod_fds);

exit_hset:
    spi_spin_fini(&iod->iod_lock);
    mcache_destroy(iod->iod_evcache);

exit_mcache:
    mheap_free(iod);

    return ret;
}

int eio_fini(){
    eio_data_t	    *iod = get_data();
    eio_worker_t    *iwk;
    int		    i;;

    if((iod == NULL) || (iod->iod_init == 0)){
	return 0;
    }

    __eio_data = NULL;
    iod->iod_init = 0;

    for(i = 0; i < iod->iod_num; i++){
	iwk = &(iod->iod_workers[i]);
	spi_thread_destroy(iwk->iwk_thread);
	__spi_convar_fini(&iwk->iwk_convar);
    }

    hset_destroy(iod->iod_fds);
    spi_spin_fini(&iod->iod_lock);
    mcache_destroy(iod->iod_evcache);
    mheap_free(iod);

    return 0;
}

