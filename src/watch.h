/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __WATCH_H__
#define __WATCH_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*watch_event_cb)(uint32_t events, void *data);
 
int watch_add(int fd, watch_event_cb cb, void *data);
int watch_del(int fd);

int watch_start(int thread_num);
int watch_loop();
int watch_stop();

#ifdef __cplusplus
}
#endif

#endif // __WATCH_H__


