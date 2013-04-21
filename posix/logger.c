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

#include "logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

typedef struct logger{
    int		    log_init;
    int		    log_sock;

    struct sockaddr_in log_serv;
}logger_t;

struct logger_ltos {
    int	    ll_level;
    char    *ll_string;
};

static struct logger_ltos __log_ltos[] = {
    {LOGGER_FATAL,  "FATAL"},
    {LOGGER_ERROR,  "ERROR"},
    {LOGGER_WARN,   " WORN"},
    {LOGGER_INFO,   " INFO"},
    {LOGGER_DEBUG,  "DEBUG"},
    {LOGGER_TRACE,  "TRACE"},
    {LOGGER_UNKOWN, "UNKOWN"},
};
static logger_t	    __log_data = {};

static char *log_ltos(int level){
    if((level > LOGGER_TRACE)||(level < 0)){
	return (__log_ltos[LOGGER_UNKOWN]).ll_string;
    }

    return (__log_ltos[level]).ll_string;
}

int logger_print(int level, char *fmt, ...){
    logger_t	*log = &__log_data;
    char	buf[LOGGER_MAX_BUF];
    int		size = 0;
    va_list	args;

    if(!log->log_init){
	return -1;
    }

    size = snprintf(buf, LOGGER_MAX_BUF, "%s:", log_ltos(level));

    va_start(args, fmt);
    size += vsnprintf(buf+size, LOGGER_MAX_BUF-size, fmt, args);
    va_end(args);

    return send(log->log_sock, buf, size, 0);
}

int logger_init(){
    logger_t	*log = &__log_data;

    log->log_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(log->log_sock < 0){
	return -1;
    }

    log->log_serv.sin_family = AF_INET;
    log->log_serv.sin_addr.s_addr = inet_addr("127.0.0.1");
    log->log_serv.sin_port = htons(4040);

    if(connect(log->log_sock, (struct sockaddr *)&log->log_serv,
		sizeof(struct sockaddr_in)) < 0){
	close(log->log_sock);
	return -1;
    }

    log->log_init = 1;

    return 0;
}

int logger_fini(){
    logger_t	*log = &__log_data;

    if(log->log_init){
	log->log_init = 0;
	close(log->log_sock);
    }

    return 0;
}

