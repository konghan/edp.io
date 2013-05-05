/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
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

