/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __EDPNET_H__
#define __EDPNET_H__

#include "edp_sys.h"
#include "ioctx.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * edpnet address structs & interface
 */
struct edpnet_ipv4_addr{
    uint32_t	eia_ip;
    short	eia_port;
};

struct edpnet_ipv6_addr{
};

enum edpnet_addr_type{
    kEDPNET_ADDR_TYPE_IPV4 = 1234,
    kEDPNET_ADDR_TYPE_IPV6,
};

typedef struct edpnet_addr{
    int	    ea_type;
    union{
	struct edpnet_ipv4_addr	ea_v4;
	struct edpnet_ipv6_addr	ea_v6;
    };
}edpnet_addr_t;

// convert ipv4 or ipv6 address from text form to binary form
int edpnet_pton(int type, const char *src, void *dst);
// convert ipv4 or ipv6 address form binary form to text form
const char *edpnet_ntop(int type, const void *src, char *dst, int len);

/*
 * edpnet sock structs & interface
 */
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
    void (*sock_connect)(edpnet_sock_t sock, void *data);
    void (*data_ready)(edpnet_sock_t sock, void *data);
    void (*data_drain)(edpnet_sock_t sock, void *data);
    void (*sock_error)(edpnet_sock_t sock, void *data);
    void (*sock_close)(edpnet_sock_t sock, void *data);
}edpnet_sock_cbs_t;

enum edpnet_iocontext_type{
    kEDPNET_IOCTX_TYPE_IOVEC = 1111,
    kEDPNET_IOCTX_TYPE_IODATA,
};

#if 0
struct edpnet_ioctx;
typedef void (*edpnet_rwcb)(edpnet_sock_t sock, struct edpnet_ioctx *ioctx, int errcode);

typedef struct edpnet_ioctx{
    struct list_head	ec_node;    // link to owner
    uint32_t		ec_type;    // IOVEC or IODATA
    edpnet_rwcb		ec_iocb;
    edpnet_sock_t	ec_sock;
    uint32_t		ec_read;

    union{
	struct{
	    uint32_t	    ec_ionr;
	    struct iovec    *ec_iov;
	};
	struct{
	    uint32_t	ec_size;
	    void	*ec_data;
	};
    };

}edpnet_ioctx_t;

static void edpnet_ioctx_init(edpnet_ioctx_t *ioc, uint32_t type){
    memset(ioc, 0, sizeof(*ioc));
    INIT_LIST_HEAD(&ioc->ec_node);
    ioc->ec_type = type;
}
#endif

int edpnet_sock_create(edpnet_sock_t *sock, edpnet_sock_cbs_t *cbs, void *data);
int edpnet_sock_destroy(edpnet_sock_t sock);

int edpnet_sock_set(edpnet_sock_t sock, edpnet_sock_cbs_t *cbs, void *data);

int edpnet_sock_connect(edpnet_sock_t sock, edpnet_addr_t *addr);
int edpnet_sock_close(edpnet_sock_t sock);

int edpnet_sock_write(edpnet_sock_t sock, ioctx_t *ioctx, edpnet_writecb cb);
int edpnet_sock_read(edpnet_sock_t sock, ioctx_t *ioctx);

/*
 * serv - structs & interfaces
 */
struct edpnet_serv;
typedef struct edpnet_serv *edpnet_serv_t;

//enum edpnet_serv_event{
//    kEDPNET_SERV_EVENT_LISTENING = 0,
//    kEDPNET_SERV_EVENT_CONNECTION,
//    kEDPNET_SERV_EVENT_CLOSE,
//    kEDPNET_SERV_EVENT_ERROR,
//};
typedef struct edpnet_serv_cbs{
//    int (*listening)(edpnet_serv_t serv, int errcode);
    int (*connected)(edpnet_serv_t serv, edpnet_sock_t sock, void *data);
    int (*close)(edpnet_serv_t serv, void *data);
//    int (*error)(edpnet_serv_t *svr, int errcode);
}edpnet_serv_cbs_t;

int edpnet_serv_create(edpnet_serv_t *serv, edpnet_serv_cbs_t *cbs, void *data);
int edpnet_serv_destroy(edpnet_serv_t serv);

int edpnet_serv_listen(edpnet_serv_t serv, edpnet_addr_t *addr);
//int edpnet_serv_close(edpnet_serv_t serv);

/*
 * edpnet interfaces
 */
int edpnet_init();
int edpnet_fini();

#ifdef __cplusplus
}
#endif

#endif // __EDPNET_H__

