/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "edp.h"
#include "worker.h"
#include "emitter.h"
#include "edpnet.h"

#include "logger.h"
#include "mcache.h"
#include "hset.h"

int edp_init(int thread_num){
    int	    ret = -1;

    ret = logger_init();
    if(ret != 0){
	return ret;
    }

    // FIXME:pre-alloc memory for your application
    ret = mcache_init(NULL, 0);
    if(ret != 0){
	log_warn("mcache init fail:%d\n", ret);
	goto exit_mcache;
    }

    ret = hset_init();
    if(ret != 0){
	log_warn("hset init fail:%d\n", ret);
	goto exit_hset;
    }

    ret = worker_init(thread_num);
    if(ret != 0){
	log_warn("init worker fail:%d\n", ret);
	goto exit_worker;
    }

    ret = emit_init();
    if(ret != 0){
	log_warn("init emitter fail:%d\n", ret);
	goto exit_emit;
    }

    ret = edpnet_init();
    if(ret != 0){
	log_warn("init edpnet fail:%d\n", ret);
	goto exit_net;
    }

    log_info("edp have been initialized!\n");

    return 0;

exit_net:
    emit_fini();

exit_emit:
    worker_fini();

exit_worker:
    hset_fini();

exit_hset:
    mcache_fini();

exit_mcache:
    logger_fini();

    return ret;
}

int edp_loop(){

    while(1){
	__spi_sleep(10);
    }

    return 0;
}

int edp_fini(){

    edpnet_fini();

    emit_fini();

    worker_fini();

    hset_fini();

    mcache_fini();

    logger_fini();

    return 0;
}

