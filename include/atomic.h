/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include "edp_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t	    atomic_t;
typedef int32_t	    atom32_t;

static inline atomic_t atomic_add(atomic_t *atm, atomic_t val){
    return __sync_add_and_fetch(atm, val);
}

static inline atomic_t atomic_sub(atomic_t *atm, atomic_t val){
    return __sync_sub_and_fetch(atm, val);
}

static inline atomic_t atomic_inc(atomic_t *atm){
    return __sync_add_and_fetch(atm, 1);
}

static inline atomic_t atomic_dec(atomic_t *atm){
    return __sync_sub_and_fetch(atm, 1);
}

static inline atomic_t atomic_reset(atomic_t *atm){
    return __sync_fetch_and_and(atm, 0);
}

#ifdef __cplusplus
}
#endif

#endif // __ATOMIC_H__


