/*
 * Copyright @ konghan, All rights reserved.
 */

#ifndef __EDNET_H__
#define __EDNET_H__

#include "edap_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ednet_ipv4_addr{
    uint32_t	eia_ip;
    short	eia_port;
};

struct ednet_ipv6_addr{
};

enum ednet_addr_type{
    kEDNET_ADDR_TYPE_IPV4 = 0,
    kEDNET_ADDR_TYPE_IPV6,
};

typedef struct ednet_addr{
    int	    ea_type;
    union{
	struct ednet_ipv4_addr	ea_v4;
	struct ednet_ipv6_addr	ea_v6;
    };
}ednet_addr_t;

struct ednet_socket;
typedef struct ednet_socket *ednet_socket_t;

enum ednet_error_code{
    EDNET_ERR_TIMEOUT = 1024,
    EDNET_ERR_CLOSE,
};

enum ednet_socket_event{
//    EDNET_SOCKET_EVENT_CONNECT = 0,
//    EDNET_SOCKET_EVENT_DATA,
    kEDNET_SOCKET_EVENT_END,
    kEDNET_SOCKET_EVENT_TIMEOUT,
//    EDNET_SOCKET_EVENT_DRAIN,
//    EDNET_SOCKET_EVENT_ERROR, in close
//    EDNET_SOCKET_EVENT_CLOSE,
};

typedef struct ednet_socket_callback{
    void (*socket_connect)(ednet_socket_t sock, int errcode);
    void (*data_ready)(ednet_socket_t sock, size_t size);
    void (*data_drain)(ednet_socket_t sock);
    void (*socket_close)(ednet_socket_t sock, int errcode);
}ednet_socket_callback_t;

typedef void (*ednet_rwcb)(ednet_socket_t sock, ednet_ioctx_t *ioctx, int errcode);

enum ednet_iocontext_type{
    kEDNET_IOCTX_TYPE_IOVEC = 0,
    kEDNET_IOCTX_TYPE_IODATA,
};

struct sglist{
    uint32_t	sgl_size;
    void	*sgl_data;
};

typedef struct ednet_iocontext{
    struct list_head	ec_node;    // link to owner
    uint32_t		ec_type;
    ednet_rwcb		ec_iocb;
    ednet_socket_t	ec_sock;

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

}ednet_ioctx_t;


int ednet_socket_create(ednet_socket_t *sock);
int ednet_socket_destroy(ednet_socket_t sock);

int ednet_socket_setcallback(ednet_socket_t sock, ednet_socket_callback_t *cb);

int ednet_socket_connect(ednet_socket_t sock, ednet_addr_t *addr);
int ednet_socket_close(ednet_socket_t sock);

int ednet_socket_write(ednet_socket_t sock, ednet_ioctx_t *ioctx, ednet_rwcb cb);
int ednet_socket_read(ednet_socket_t sock, ednet_ioctx_t *ioctx, ednet_rwcb cb);

// server 
struct ednet_server;
typedef struct ednet_server *ednet_server_t;

//enum ednet_server_event{
//    EDNET_SERVER_EVENT_LISTENING = 0,
//    EDNET_SERVER_EVENT_CONNECTION,
//    EDNET_SERVER_EVENT_CLOSE,
//    EDNET_SERVER_EVENT_ERROR,
//};
typedef struct ednet_server_callback{
    int (*listening)(ednet_server_t *svr, int errcode);
    int (*connected)(ednet_server_t *svr, ednet_socket_t sock, int errcode);
    int (*close)(ednet_server_t *svr, int errcode);
//    int (*error)(ednet_server_t *svr, int errcode);
}ednet_server_callback_t;

int ednet_server_create(ednet_server_t *svr);
int ednet_server_destroy(ednet_server_t svr);

int ednet_server_setcallback(ednet_server_t svr, ednet_server_callback_t *cb);

int ednet_server_listen(ednet_server_t svr, ednet_addr_t *addr);
int ednet_server_close(ednet_server_t svr);

int ednet_init();
int ednet_fini();

#ifdef __cplusplus
}
#endif

#endif // __EDNET_H__

