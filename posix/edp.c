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

#include "edp.h"
#include "worker.h"

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

    ret = worker_start(thread_num);
    if(ret != 0){
	log_warn("start worker fail:%d\n", ret);
	goto exit_worker;
    }

    log_info("edp.io have initialized\n");

    return 0;

exit_worker:
    hset_fini();

exit_hset:
    mcache_fini();

exit_mcache:
    logger_fini();

    return ret;
}

int edp_loop(){
    return worker_loop();
}

int edp_fini(){

    worker_stop();

    hset_fini();

    mcache_fini();

    logger_fini();

    return 0;
}

