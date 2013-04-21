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

#include "edpnet.h"

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


#define kEDNET_SOCKET_STATUS_ZERO	0x0000
#define kEDNET_SOCKET_STATUS_INIT	0x0001
#define kEDNET_SOCKET_STATUS_CONNECT	0x0002
#define kEDNET_SOCKET_STATUS_WRITE	0x0100
#define kEDNET_SOCKET_STATUS_READ	0x0200


struct ednet_socket{
    int			    es_status;
    int			    es_sock;
    struct list_head	    es_node;	// link to owner

    spi_spinlock_t	    es_lock;
    struct list_head	    es_ios;	// pending io

    ednet_socket_callback_t es_cb;
};
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

typedef struct ednet_data{
    int			ed_init;

    spi_spinlock_t	ed_lock;
    struct list_head	ed_socks;

    slab_t		ed_sockcache;
}ednet_data_t;

static ednet_data_t	__ednet_data = {};

static int ednet_socket_init(ednet_data_t *ed, ednet_socket_t sock){
    struct ednet_socket	*s = sock;

    s->es_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(s->es_sock < 0){
	log_warn("init socket failure!\n");
	return -1;
    }
    // FIXME: sock flags setting

    INIT_LIST_HEAD(&s->es_node);
    INIT_LIST_HEAD(&s->es_ios);

    if(edwatch_add(s->es_sock) != 0){
	log_warn("watch socket handle fail!\n");
	close(s->es_sock);
	return -1;
    }

    spi_spinlock_init(&s->es_lock);

    s->es_status &= kEDNET_SOCKET_STATUS_INIT;

    spi_spin_lock(&ed->ed_lock);
    list_add(&s->es_node, &ed->ed_socks);
    spi_spin_unlock(&ed->ed_lock);

    return 0;
}

static int ednet_socket_fini(ednet_data_t *ed, ednet_socket_t sock){
    struct ednet_socket	*s = sock;

    if(s->es_status != 0){
	log_warn("fini socket in wrong status!\n"):
	return -1;
    }

    edwatch_del(s->es_sock);

    ASSERT(list_empty(&s->es_ios));
    spi_spinlock_fini(&s->es_lock);

    spi_spin_lock(&ed->ed_lock);
    list_del(&s->es_node);
    spi_spin_unlock(&ed->ed_lock);

    return 0;
}

int ednet_socket_create(ednet_socket_t *sock){
    ednet_data_t    *ed = &__ednet_data;
    ednet_socket_t  s;
    int		    ret;

    if(!ed->ed_init){
	log_warn("ednet not inited!\n");
	return -1;
    }

    s = (ednet_socket_t)slab_alloc(ed->ed_sockcache);
    if(s == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }
    memset(s, sizeof(struct ednet_socket));
    ret = ednet_socket_init(ed, s);
    if(ret != 0){
	log_warn("initialize socket failure!\n");
	slab_free(ed->ed_sockcache, s);
	return ret;
    }

    *sock = s;

    return 0;
}

int ednet_socket_destroy(ednet_socket_t sock){
    ednet_data_t    *ed = &__ednet_data;
    ednet_socket_t  s;
    int		    ret;

    if(!ed->ed_init){
	log_warn("ednet not inited!\n");
	return -1;
    }

    ednet_socket_fini(ed, sock);

    slab_free(ed->ed_sockcache, sock);

    return 0;
}

int ednet_socket_setcallback(ednet_socket_t sock, ednet_socket_callback_t *cb){
    struct ednet_socket	*s = sock;

    s->es_cb =cb;

    return 0;
}

int ednet_socket_connect(ednet_socket_t sock, ednet_addr_t *addr){
    struct ednet_socket	*s = sock;
    struct sockaddr_in	sa;
    int			ret;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family	= AF_INET;
    sa.sin_port		= addr->ea_port;
    sa.sin_addr.s_addr	= addr->ea_ip;

    ret = connect(s->es_sock, (struct sockaddr *)&sa, sizeof(sa));
    if(ret < 0){
	log_warn("connect to server failure!\n");
	return ret;
    }

    return 0;
}

int ednet_socket_close(ednet_socket_t sock){
    struct ednet_socket *s = sock;

    close(s->es_sock);

    return 0;
}

static int ednet_socket_write_impl(ednet_ioctx_t *io){
    struct ednet_socket *s;
    int			ret = -1;
    
    ASSERT(io != NULL);
    
    s = (struct ednet_socket *)io->ec_sock;
    ASSERT((s->es_status & kEDNET_SOCKET_STATUS_WRITE) == 0);

    switch(io->ec_type){
	case kEDNET_IOCTX_TYPE_IOVEC:
	    ret = writev(s->es_sock, io->ec_iov, io->ec_ionr);
	    break;

	case kEDNET_IOCTX_TYPE_IODATA:
	    ret = write(s->es_sock, io->ec_data, io->ec_size);
	    break;

	default:
	    log_warn("ioctx:0x%x type unkown:%d\n", (uint64_t) io, io->ec_type);
    }

    if(ret < 0){
	list_del(&io->ec_node);
	//FIXME: report error to user
    }

    return ret;
}

int ednet_socket_write(ednet_socket_t sock, ednet_ioctx_t *ioctx, ednet_rwcb cb){
    struct ednet_socket	*s = sock;

    ASSERT(ioctx != NULL);

    ioctx->ec_iocb  = cb;
    ioctx->ec_sock  = sock;

    spi_spin_lock(&s->es_lock);
    if(list_empty(&s->es_ios)&&(!(s->es_status & kEDNET_SOCKET_STATUS_WRITE))){
	list_add(&ioctx->ec_node, &s->es_ios);
	s->es_status &= kEDNET_SOCKET_STATUS_WRITE;
	ednet_socket_write_impl(ioctx);
    }else{
	list_add_tail(&ioctx->ec_node, &s->es_ios);
    }
    spi_spin_unlock(&s->es_lock);

    return 0;
}

static int ednet_socket_read_impl(ednet_ioctx_t *io){
    struct ednet_socket *s;
    int			ret = -1;
    
    ASSERT(io != NULL);
    
    s = (struct ednet_socket *)io->ec_sock;
    ASSERT((s->es_status & kEDNET_SOCKET_STATUS_READ) == 0);

    switch(io->ec_type){
	case kEDNET_IOCTX_TYPE_IOVEC:
	    ret = readv(s->es_sock, io->ec_iov, io->ec_ionr);
	    break;

	case kEDNET_IOCTX_TYPE_IODATA:
	    ret = read(s->es_sock, io->ec_data, io->ec_size);
	    break;

	default:
	    log_warn("ioctx:0x%x type unkown:%d\n", (uint64_t) io, io->ec_type);
    }

    if(ret < 0){
	list_del(&io->ec_node);
	//FIXME: report error to user
    }

    return ret;
}

int ednet_socket_read(ednet_socket_t sock, ednet_ioctx_t *ioctx, ednet_rwcb cb){
    struct ednet_socket	*s = sock;

    ASSERT(ioctx != NULL);

    ioctx->ec_iocb  = cb;
    ioctx->ec_sock  = sock;

    spi_spin_lock(&s->es_lock);
    if(list_empty(&s->es_ios)&&(!(s->es_status & kEDNET_SOCKET_STATUS_READ))){
	list_add(&ioctx->ec_node, &s->es_ios);
	s->es_status &= kEDNET_SOCKET_STATUS_READ;
	ednet_socket_read_impl(ioctx);
    }else{
	list_add_tail(&ioctx->ec_node, &s->es_ios);
    }
    spi_spin_unlock(&s->es_lock);

    return 0;
}

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

int ednet_init(){
    return -1;
}

int ednet_fini(){
    return -1;
}

