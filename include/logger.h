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


