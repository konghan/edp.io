/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __LOGGER_H__
#define __LOGGER_H__

#ifdef __cplusplus
extern "C"{
#endif

#define LOGGER_MAX_BUF	    128

enum{
    LOGGER_FATAL = 0,
    LOGGER_ERROR,
    LOGGER_WARN,
    LOGGER_INFO,
    LOGGER_DEBUG,
    LOGGER_TRACE,
    LOGGER_UNKOWN,
};


int logger_print(int level, char *fmt, ...);

#define log_fatal(__fmt,...)	\
    logger_print(LOGGER_FATAL,__fmt,##__VA_ARGS__)

#define log_error(__fmt,...)	\
    logger_print(LOGGER_ERROR,__fmt,##__VA_ARGS__)

#define log_warn(__fmt,...)	\
    logger_print(LOGGER_WARN,__fmt,##__VA_ARGS__)

#define log_info(__fmt,...)	\
    logger_print(LOGGER_INFO,__fmt,##__VA_ARGS__)

#define log_debug(__fmt,...)	\
    logger_print(LOGGER_DEBUG,__fmt,##__VA_ARGS__)

#define log_trace(__fmt,...)	\
    logger_print(LOGGER_TRACE,__fmt,##__VA_ARGS__)

int logger_init();
int logger_fini();

#ifdef __cplusplus
}
#endif

#endif // __LOGGER_H__


