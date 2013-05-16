/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __EDP_SYS_H__
#define __EDP_SYS_H__

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C"{
#endif

#define ASSERT	    assert

// condition variable, used internally
typedef struct __spi_convar_data{
    pthread_mutex_t	cv_mutex;
    pthread_cond_t	cv_convar;
    int			cv_count;
}__spi_convar_t;

static inline int __spi_convar_init(__spi_convar_t *cv){
    int	    ret;
    ret = pthread_mutex_init(&cv->cv_mutex, NULL);
    if(ret != 0){
	return ret;
    }

    ret = pthread_cond_init(&cv->cv_convar, NULL);
    if(ret != 0){
	pthread_mutex_destroy(&cv->cv_mutex);
	return ret;
    }

    cv->cv_count = 0;

    return 0;
}

static inline int __spi_convar_fini(__spi_convar_t *cv){
    pthread_cond_destroy(&cv->cv_convar);
    pthread_mutex_destroy(&cv->cv_mutex);
    return 0;
}

static inline int __spi_convar_signal(__spi_convar_t *cv){
    pthread_mutex_lock(&cv->cv_mutex);
    if(cv->cv_count != 0){
	pthread_mutex_unlock(&cv->cv_mutex);
	return 0;
    }else{
	cv->cv_count = 1;
    }
    pthread_cond_signal(&cv->cv_convar);
    pthread_mutex_unlock(&cv->cv_mutex);
    return 0;
}

static inline int __spi_convar_wait(__spi_convar_t *cv){
    pthread_mutex_lock(&cv->cv_mutex);
    if(cv->cv_count == 0){
	pthread_cond_wait(&cv->cv_convar, &cv->cv_mutex);
    }
    cv->cv_count = 0;
    pthread_mutex_unlock(&cv->cv_mutex);
    return 0;
}

static inline int __spi_convar_timedwait(__spi_convar_t *cv, uint32_t ms){
    struct timespec	ts;
    struct timeval	tv;
    int			ret;

    gettimeofday(&tv, NULL);

    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = (tv.tv_usec + ms) * 1000;

    pthread_mutex_lock(&cv->cv_mutex);
    if(cv->cv_count == 0){
	ret = pthread_cond_timedwait(&cv->cv_convar, &cv->cv_mutex, &ts);
	if(ret != 0){
	    pthread_mutex_unlock(&cv->cv_mutex);
	    return -ETIMEDOUT;
	}
    }
    cv->cv_count = 0;
    pthread_mutex_unlock(&cv->cv_mutex);
    return 0;
}

// OS independ interface

typedef pthread_t		spi_thread_t;
static inline int spi_thread_create(spi_thread_t *thrd,
	void *(*thread_routine)(void *), void *data){
    return pthread_create(thrd, NULL, thread_routine, data);
}

static inline int spi_thread_destroy(spi_thread_t thrd){
    return pthread_cancel(thrd);
}


typedef pthread_spinlock_t	spi_spinlock_t;
static inline int spi_spin_init(spi_spinlock_t *lock){
    return pthread_spin_init(lock, 1);
}

static inline int spi_spin_fini(spi_spinlock_t *lock){
    return pthread_spin_destroy(lock);
}

static inline int spi_spin_lock(spi_spinlock_t *lock){
    return pthread_spin_lock(lock);
}

static inline int spi_spin_unlock(spi_spinlock_t *lock){
    return pthread_spin_unlock(lock);
}

static inline int spi_spin_trylock(spi_spinlock_t *lock){
    return pthread_spin_trylock(lock);
}

// use by edp_loop internally.
static inline void __spi_sleep(int second){
    sleep(second);
}

#ifdef __cplusplus
}
#endif

#endif // __EDP_SYS_H__


