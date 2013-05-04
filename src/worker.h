/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __WORKER_H__
#define __WORKER_H__

#ifdef __cplusplus
extern "C" {
#endif

int worker_init(int thread_num);
int worker_fini();

#ifdef __cplusplus
}
#endif

#endif // __WORKER_H__


