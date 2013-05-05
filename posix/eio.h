/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __EIO_H__
#define __EIO_H__

#include "edp_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*eio_event_cb)(uint32_t events, void *data);
 
int eio_addfd(int fd, eio_event_cb cb, void *data);
int eio_delfd(int fd);

int eio_init(int thread_num);
int eio_fini();

#ifdef __cplusplus
}
#endif

#endif // __EIO_H__


