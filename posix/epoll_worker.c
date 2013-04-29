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

#include "worker.h"
#include "edp.h"

#include "logger.h"
#include "mcache.h"
#include "hset.h"
#include "atomic.h"

#include <sys/epoll.h>
#include <unistd.h>

#define EPOLL_MAX_EVENTS	    32

// worker thread's data
typedef struct worker{
    int			wk_init;
    
    int			wk_epoll;
    spi_thread_t	wk_thread;

    atomic_t		wk_fds;

    uint64_t		wk_events;  // processed events
}worker_t;
   
// event used by worker
typedef struct worker_event{
    hset_entry_t	we_ent;

    int			we_fd;
    struct worker	*we_worker;

    worker_event_cb	we_cb;
    void		*we_data;
}worker_event_t;

// epoll worker control data
typedef struct worker_data{
    int			wd_init;

    int			wd_num;	    // workers number
    spi_spinlock_t	wd_lock;

    int			wd_round;   // round-robin
    
    mcache_t		wd_evcache;
    hset_t		wd_fds;

    struct worker	wd_workers[];
}worker_data_t;

static worker_data_t	*__worker_data = NULL;

static inline worker_data_t *worker_get(){
    return __worker_data;
}

// select light load worker thread
static worker_t *worker_lightload(){
    worker_data_t   *wd = worker_get();
    int		    idx;

    // FIXME: determine witch one is light load
    spi_spin_lock(&wd->wd_lock);
    idx = wd->wd_round++ % wd->wd_num;
    spi_spin_lock(&wd->wd_lock);

    return &(wd->wd_workers[idx]);
}

int watch_add(int fd,  worker_event_cb cb, void *data){
    worker_data_t	*wd = worker_get();
    struct worker	*wk = worker_lightload();
    worker_event_t	*we;
    struct epoll_event  ev;
    int			ret;

    we = mcache_alloc(wd->wd_evcache);
    if(we == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }

    we->we_fd = fd;
    we->we_cb = cb;
    we->we_data = data;
    we->we_ent.hse_hash = (uint32_t)fd;
    we->we_worker = wk;

    spi_spin_lock(&wd->wd_lock);
    ret = hset_add(wd->wd_fds, &we->we_ent);
    spi_spin_unlock(&wd->wd_lock);
    if(ret != 0){
	log_warn("fd:%d already exist!\n", fd);
	mcache_free(wd->wd_evcache, we);
	return -EEXIST;
    }

    ev.events   = EPOLLIN | EPOLLOUT;
    ev.data.ptr = we;

    ret = epoll_ctl(wk->wk_epoll, EPOLL_CTL_ADD, fd, &ev);
    if(ret != 0){
	log_warn("epoll add watch fd:%d fail!\n", fd);
	
	spi_spin_lock(&wd->wd_lock);
	hset_del(wd->wd_fds, (uint32_t)fd);
	spi_spin_unlock(&wd->wd_lock);

	mcache_free(wd->wd_evcache, we);
	return ret;
    }
    atomic_inc(&wk->wk_fds);

    return 0;
}

int watch_del(int fd){
    worker_data_t	*wd = worker_get();
    worker_event_t	*we;
    hset_entry_t	*hen;
    int			ret;

    spi_spin_lock(&wd->wd_lock);
    ret = hset_get(wd->wd_fds, (uint32_t)fd, &hen);
    if(ret != 0){
	log_warn("fd:%d not in watch!\n", fd);
	spi_spin_unlock(&wd->wd_lock);
	return -ENOENT;
    }
    hset_del(wd->wd_fds, (uint32_t)fd);
    spi_spin_unlock(&wd->wd_lock);

    we = container_of(hen, worker_event_t, we_ent);

    ret = epoll_ctl(we->we_worker->wk_epoll, EPOLL_CTL_DEL, fd, NULL);
    if(ret != 0){
	log_warn("fd:%d remove from watch fail!\n", fd);
	return -ENOENT;
    }

    atomic_dec(&wk->wk_fds);
    mcache_free(wd->wd_evcache, we);

    return 0;
}

static int worker_init(worker_t *wk){
    ASSERT(wk != NULL);

    wk->wk_epoll = epoll_create(EPOLL_MAX_EVENTS);
    if(wk->wk_epoll < 0){
	log_warn("epoll create fail:\n", errno);
	return -errno;
    }

    wk->wk_init = 1;

    return 0;
}

static int worker_fini(worker_t *wk){
    ASSERT(wk != NULL);

    if(wk->wk_init == 0){
	return 0;
    }

    wk->wk_init = 0;
    close(wk->wk_epoll);
    return 0;
}

static void *worker_routine(void *data){
    worker_t		*wk = (worker_t *)data;
    struct epoll_event	*events, *ev;
    worker_event_t	*we;
    int			msz;
    int			evcnt;
    int			i;
    int			ret = -1;

    ASSERT(wk != NULL);

    ret = worker_init(wk);
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

    while(wk->wk_init){
	evcnt = epoll_wait(wk->wk_epoll, events, EPOLL_MAX_EVENTS, -1);
	if(evcnt < 0){
	    log_warn("epoll wait fail:%d\n", errno);
	    ret = -errno;
	    break;
	}

	for(i = 0; i < evcnt; i++){
	    ev = &(events[i]);
	    we = (worker_event_t *)ev->data.ptr;
	    ASSERT(we != NULL);

	    we->we_cb(ev->events, we->we_data);
	    wk->wk_events ++;
	}
    }

    worker_fini(wk);

    return NULL;
}

int worker_start(int thread_num){
    worker_data_t   *wd;
    worker_t	    *wk;
    size_t	    msz;
    int		    i;
    int		    ret = -1;

    msz = sizeof(*wd) + sizeof(worker_t)*thread_num;

    wd = mheap_alloc(msz);
    if(wd == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }
    memset(wd, 0, msz);

    ret = mcache_create(sizeof(worker_event_t), sizeof(int), 0, &wd->wd_evcache);
    if(ret != 0){
	log_warn("create mcache fail:%d\n", ret);
	goto exit_mcache;
    }

    spi_spin_init(&wd->wd_lock);

    ret = hset_create(EPOLL_MAX_EVENTS, &wd->wd_fds);
    if(ret != 0){
	log_warn("create hset fail:%d\n", ret);
	goto exit_hset;
    }

    for(i = 0; i < thread_num; i++){
	wk = &(wd->wd_workers[i]);
	ret = spi_thread_create(&wk->wk_thread, worker_routine, wk);
	if(ret != 0){
	    log_warn("create thread fail:%d\n", ret);
	    goto exit_thread;
	}
    }

    wd->wd_init = 1;
    __worker_data = wd;

    return 0;

exit_thread:
    for(; i >= 0; i--){
	wk = &(wd->wd_workers[i]);
	spi_thread_destroy(wk->wk_thread);
    }

    hset_destroy(wd->wd_fds);

exit_hset:
    spi_spin_fini(&wd->wd_lock);
    mcache_destroy(wd->wd_evcache);

exit_mcache:
    mheap_free(wd);

    return ret;
}

int worker_loop(){
    
    log_info("loop event ...\n");

    while(1){
	sleep(10);
    }
    return 0;
}

int worker_stop(){
    worker_data_t   *wd = worker_get();
    worker_t	    *wk;
    int		    i;;

    if((wd == NULL) || (wd->wd_init == 0)){
	return 0;
    }

    __worker_data = NULL;
    wd->wd_init = 0;

    for(i = 0; i < wd->wd_num; i++){
	wk = &(wd->wd_workers[i]);
	spi_thread_destroy(wk->wk_thread);
    }

    hset_destroy(wd->wd_fds);
    spi_spin_fini(&wd->wd_lock);
    mcache_destroy(wd->wd_evcache);
    mheap_free(wd);

    return 0;
}

