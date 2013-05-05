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

#ifndef __EDP_SYS_H__
#define __EDP_SYS_H__

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>

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
    while(! cv->cv_count){
	pthread_cond_wait(&cv->cv_convar, &cv->cv_mutex);
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

#ifdef __cplusplus
}
#endif

#endif // __EDP_SYS_H__


