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

#ifndef __EDPNET_H__
#define __EDPNET_H__

#include "edp_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct edpnet_ipv4_addr{
    uint32_t	eia_ip;
    short	eia_port;
};

struct edpnet_ipv6_addr{
};

enum edpnet_addr_type{
    kEDPNET_ADDR_TYPE_IPV4 = 0,
    kEDPNET_ADDR_TYPE_IPV6,
};

typedef struct edpnet_addr{
    int	    ea_type;
    union{
	struct edpnet_ipv4_addr	ea_v4;
	struct edpnet_ipv6_addr	ea_v6;
    };
}edpnet_addr_t;

struct edpnet_sock;
typedef struct edpnet_sock *edpnet_sock_t;

enum edpnet_error_code{
    kEDPNET_ERR_TIMEOUT = 1024,
    kEDPNET_ERR_CLOSE,
};

enum edpnet_sock_event{
//    kEDPNET_SOCK_EVENT_CONNECT = 0,
//    kEDPNET_SOCK_EVENT_DATA,
    kEDPNET_SOCK_EVENT_END,
    kEDPNET_SOCK_EVENT_TIMEOUT,
//    kEDPNET_SOCK_EVENT_DRAIN,
//    kEDPNET_SOCK_EVENT_ERROR, in close
//    kEDPNET_SOCK_EVENT_CLOSE,
};

typedef struct edpnet_sock_cbs{
    void (*sock_connect)(edpnet_sock_t sock, int errcode);
    void (*data_ready)(edpnet_sock_t sock, size_t size);
    void (*data_drain)(edpnet_sock_t sock);
    void (*sock_error)(edpnet_sock_t sock, int errcode);
    void (*sock_close)(edpnet_sock_t sock, int errcode);
}edpnet_sock_cbs_t;

typedef void (*edpnet_rwcb)(edpnet_sock_t sock, edpnet_ioctx_t *ioctx, int errcode);

enum edpnet_iocontext_type{
    kEDPNET_IOCTX_TYPE_IOVEC = 0,
    kEDPNET_IOCTX_TYPE_IODATA,
};

struct sglist{
    uint32_t	sgl_size;
    void	*sgl_data;
};

typedef struct edpnet_iocontext{
    struct list_head	ec_node;    // link to owner
    uint32_t		ec_type;    // IOVEC or IODATA
    edpnet_rwcb		ec_iocb;
    edpnet_sock_t	ec_sock;
    uint32_t		ec_read;

    union{
	struct{
	    uint32_t	    ec_ionr;
	    struct  sglist  *ec_iov;
	};
	struct{
	    uint32_t	ec_size;
	    void	*ec_data;
	};
    };

}edpnet_ioctx_t;


int edpnet_sock_create(edpnet_sock_t *sock, edpnet_sock_cbs_t *cbs);
int edpnet_sock_destroy(edpnet_sock_t sock);

int edpnet_sock_connect(edpnet_sock_t sock, edpnet_addr_t *addr);
int edpnet_sock_close(edpnet_sock_t sock);

int edpnet_sock_write(edpnet_sock_t sock, edpnet_ioctx_t *ioctx, edpnet_rwcb cb);
int edpnet_sock_read(edpnet_sock_t sock, edpnet_ioctx_t *ioctx, edpnet_rwcb cb);

// serv 
struct edpnet_serv;
typedef struct edpnet_serv *edpnet_serv_t;

//enum edpnet_serv_event{
//    kEDPNET_SERV_EVENT_LISTENING = 0,
//    kEDPNET_SERV_EVENT_CONNECTION,
//    kEDPNET_SERV_EVENT_CLOSE,
//    kEDPNET_SERV_EVENT_ERROR,
//};
typedef struct edpnet_serv_cbs{
    int (*listening)(edpnet_serv_t serv, int errcode);
    int (*connected)(edpnet_serv_t serv, edpnet_sock_t sock, int errcode);
    int (*close)(edpnet_serv_t serv, int errcode);
//    int (*error)(edpnet_serv_t *svr, int errcode);
}edpnet_serv_cbs_t;

int edpnet_serv_create(edpnet_serv_t *serv, edpnet_serv_cbs_t *cbs);
int edpnet_serv_destroy(edpnet_serv_t serv);

int edpnet_serv_listen(edpnet_serv_t serv, edpnet_addr_t *addr);
int edpnet_serv_close(edpnet_serv_t serv);

int edpnet_init();
int edpnet_fini();

#ifdef __cplusplus
}
#endif

#endif // __EDPNET_H__

