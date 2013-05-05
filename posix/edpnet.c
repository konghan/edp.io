/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "edpnet.h"
#include "eio.h"

#include "mcache.h"
#include "logger.h"
#include "list.h"
#include "atomic.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#define kEDPNET_SOCK_STATUS_ZERO	0x0000
#define kEDPNET_SOCK_STATUS_INIT	0x0001
#define kEDPNET_SOCK_STATUS_CONNECT	0x0002
#define kEDPNET_SOCK_STATUS_WRITE	0x0100
#define kEDPNET_SOCK_STATUS_READ	0x0200

#define kEDPNET_SERV_PENDCLIENTS	64

typedef struct edpnet_data{
    int			ed_init;

    spi_spinlock_t	ed_lock;

    struct list_head	ed_socks;
    struct list_head	ed_servs;
}edpnet_data_t;

static edpnet_data_t	__edpnet_data = {};

static int set_nonblock(int sock){
    int	flags;

    flags = fcntl(sock, F_GETFL, 0);
    if(flags != -1){
	fcntl(sock, F_SETFL, flags|O_NONBLOCK);
	return 0;
    }

    return -1;
}

/*
 * edpnet - sock implementation
 */
struct edpnet_sock{
    int			es_status;

    int			es_sock;
    struct list_head	es_node;    // link to owner

    spi_spinlock_t	es_lock;
    atomic_t		es_pendios;
    struct list_head	es_iowrites; // pending write ios

    edpnet_sock_cbs_t	*es_cbs;
    void		*es_data;
};

static int sock_write(int sock, edpnet_ioctx_t *io){
    int		ret = -1;

    ASSERT(io != NULL);
    
    switch(io->ec_type){
	case kEDPNET_IOCTX_TYPE_IOVEC:
	    ret = writev(sock, io->ec_iov, io->ec_ionr);
	    break;

	case kEDPNET_IOCTX_TYPE_IODATA:
	    ret = write(sock, io->ec_data, io->ec_size);
	    break;

	default:
	    ASSERT(0);
	    break;
    }

    if(ret < 0){
	if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
	    ret = 0;
	}else{
	    ret = -errno;
	}
    }

    return ret;
}


 
static void sock_worker_cb(uint32_t events, void *data){
    struct edpnet_sock	*s = (struct edpnet_sock *)data;
    struct list_head	*lhc = NULL, *lhn = NULL;
    edpnet_ioctx_t	*ioc = NULL, *ion = NULL;
    int			ready = 0;
    int			ret;

    ASSERT(s != NULL);

    if(events & (EPOLLPRI | EPOLLIN)){
	if(s->es_status & kEDPNET_SOCK_STATUS_CONNECT){
	    spi_spin_lock(&s->es_lock);
	    if(!(s->es_status & kEDPNET_SOCK_STATUS_READ)){
		s->es_status |= kEDPNET_SOCK_STATUS_READ;
		ready = 1;
	    }
	    spi_spin_unlock(&s->es_lock);

	    if(ready)
		s->es_cbs->data_ready(s, s->es_data);
	}else{
	    spi_spin_lock(&s->es_lock);
	    s->es_status |= kEDPNET_SOCK_STATUS_CONNECT;
	    spi_spin_unlock(&s->es_lock);
	}
    }

    if(events & EPOLLOUT){
	ASSERT(s->es_status & kEDPNET_SOCK_STATUS_CONNECT);
	
	if(s->es_pendios){
	    // current write io is ok
	    spi_spin_lock(&s->es_lock);
	    ASSERT(!list_empty(&s->es_iowrites));
	    lhc = s->es_iowrites.next;
	    list_del(lhc);
	    atomic_dec(&s->es_pendios);
	    spi_spin_unlock(&s->es_lock);

	    // write next io to sock
	    while(s->es_pendios){
		spi_spin_lock(&s->es_lock);
		ASSERT(!list_empty(&s->es_iowrites));
		lhn = s->es_iowrites.next;
		ion = list_entry(lhn, edpnet_ioctx_t, ec_node);

		ret = sock_write(s->es_sock, ion);
		if(ret < 0){
		    list_del(lhn);
		    atomic_dec(&s->es_pendios);

		    spi_spin_unlock(&s->es_lock);
		    ion->ec_iocb(s, ion, ret);
		    spi_spin_lock(&s->es_lock);
		}else{
		    spi_spin_unlock(&s->es_lock);
		    break;
		}
	    }

	    if(s->es_pendios == 0){
		spi_spin_lock(&s->es_lock);
		s->es_status &= ~kEDPNET_SOCK_STATUS_WRITE;
		spi_spin_unlock(&s->es_lock);
	    }

	    // current write io callback
	    ioc = list_entry(lhc, edpnet_ioctx_t, ec_node);
	    ioc->ec_iocb(s, ioc, 0);
	}else{
	    s->es_cbs->data_drain(s, s->es_data);
	}
    }

    if(events & EPOLLERR){
	s->es_cbs->sock_error(s, s->es_data);
	//FIXME: clear pending writes
   }
    
    if(events | EPOLLHUP){
	spi_spin_lock(&s->es_lock);
	s->es_status &= ~kEDPNET_SOCK_STATUS_CONNECT;
	spi_spin_unlock(&s->es_lock);

	s->es_cbs->sock_close(s, s->es_data);

	//FIXME: clear pending writes
    }
}

static int sock_init(edpnet_sock_t sock){
    struct edpnet_sock	*s = sock;
    
    if(set_nonblock(s->es_sock) < 0){
	close(s->es_sock);
	return -1;
    }

    INIT_LIST_HEAD(&s->es_node);
    INIT_LIST_HEAD(&s->es_iowrites);

    spi_spin_init(&s->es_lock);
    s->es_status |= kEDPNET_SOCK_STATUS_INIT;

    if(eio_addfd(s->es_sock, sock_worker_cb, s) != 0){
	log_warn("watch sock handle fail!\n");
	close(s->es_sock);
	return -1;
    }

    return 0;
}

static int sock_fini(edpnet_sock_t sock){
    struct edpnet_sock	*s = sock;

    if(s->es_status != 0){
	log_warn("fini sock in wrong status!\n");
	return -1;
    }

    eio_delfd(s->es_sock);

    ASSERT(list_empty(&s->es_iowrites));
    spi_spin_fini(&s->es_lock);

    return 0;
}

int edpnet_sock_create(edpnet_sock_t *sock, edpnet_sock_cbs_t *cbs, void *data){
    edpnet_data_t	*ed = &__edpnet_data;
    struct edpnet_sock	*s;
    int			ret;

    if(!ed->ed_init){
	log_warn("ednet not inited!\n");
	return -1;
    }

    s = mheap_alloc(sizeof(*s));
    if(s == NULL){
	log_warn("no enough memory!\n");
	return -ENOMEM;
    }
    memset(s, 0, sizeof(*s));
    
    s->es_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(s->es_sock < 0){
	log_warn("init sock failure!\n");
	mheap_free(s);
	return -1;
    }

    ret = sock_init(s);
    if(ret != 0){
	log_warn("initialize sock failure!\n");
	mheap_free(s);
	return ret;
    }

    s->es_cbs	= cbs;
    s->es_data	= data;
    *sock = s;

    return 0;
}

int edpnet_sock_destroy(edpnet_sock_t sock){
    edpnet_data_t	*ed = &__edpnet_data;
    struct edpnet_sock	*s = sock;

    if(!ed->ed_init){
	log_warn("ednet not inited!\n");
	return -1;
    }

    s->es_status = kEDPNET_SOCK_STATUS_ZERO;
    sock_fini(sock);
    close(s->es_sock);

    mheap_free(s);

    return 0;
}

int edpnet_sock_set(edpnet_sock_t sock, edpnet_sock_cbs_t *cbs, void *data){
    struct edpnet_sock *s = sock;

    ASSERT(s != NULL);

    if(cbs != NULL)
	s->es_cbs = cbs;

    if(data != NULL)
	s->es_data = data;

    return 0;
}

int edpnet_sock_connect(edpnet_sock_t sock, edpnet_addr_t *addr){
    struct edpnet_sock	*s = sock;
    struct sockaddr_in	sa;
    int			ret;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family	= AF_INET;
    if(addr->ea_type == kEDPNET_ADDR_TYPE_IPV4){
	sa.sin_port		= htons(addr->ea_v4.eia_port);
	sa.sin_addr.s_addr	= addr->ea_v4.eia_ip;
    }else if(addr->ea_type == kEDPNET_ADDR_TYPE_IPV6){
	//FIXME: IPv6 support
    }else{
	log_warn("IP address type is unkonw:%d\n", addr->ea_type);
	return -1;
    }

    ret = connect(s->es_sock, (struct sockaddr *)&sa, sizeof(sa));
    if(ret < 0){
	log_warn("connect to serv failure!\n");
	return ret;
    }

    return 0;
}

int edpnet_sock_write(edpnet_sock_t sock, edpnet_ioctx_t *io, edpnet_rwcb cb){
    struct edpnet_sock	*s = sock;
    int			ret = 0;

    ASSERT(io != NULL);

    io->ec_iocb  = cb;
    io->ec_sock  = sock;

    spi_spin_lock(&s->es_lock);
    if(list_empty(&s->es_iowrites) && (!(s->es_status & kEDPNET_SOCK_STATUS_WRITE))){
	list_add(&io->ec_node, &s->es_iowrites);
	s->es_status |= kEDPNET_SOCK_STATUS_WRITE;
	spi_spin_unlock(&s->es_lock);

	switch(io->ec_type){
	case kEDPNET_IOCTX_TYPE_IOVEC:
	    ret = writev(s->es_sock, io->ec_iov, io->ec_ionr);
	    break;

	case kEDPNET_IOCTX_TYPE_IODATA:
	    ret = write(s->es_sock, io->ec_data, io->ec_size);
	    break;

	default:
	    log_warn("ioctx:0x%x type unkown:%d\n", (uint64_t) io, io->ec_type);
	    ret = -1;
	}

	if(ret < 0){
	    spi_spin_lock(&s->es_lock);
	    list_del(&io->ec_node);
	    s->es_status &= ~kEDPNET_SOCK_STATUS_WRITE;
	    spi_spin_unlock(&s->es_lock);

	    cb(sock, io, ret);
	}

    }else{
	list_add_tail(&io->ec_node, &s->es_iowrites);
	spi_spin_unlock(&s->es_lock);
    }

    return ret;
}

int edpnet_sock_read(edpnet_sock_t sock, edpnet_ioctx_t *io){
    struct edpnet_sock	*s = sock;
    int			ready = 0;
    ssize_t		ret = -1;

    ASSERT(io != NULL);
    spi_spin_lock(&s->es_lock);
    if(s->es_status & kEDPNET_SOCK_STATUS_READ){
	ready = 1;
    }
    spi_spin_unlock(&s->es_lock);

    if(ready){
        switch(io->ec_type){
	case kEDPNET_IOCTX_TYPE_IOVEC:
	    ret = readv(s->es_sock, io->ec_iov, io->ec_ionr);
	    break;

	case kEDPNET_IOCTX_TYPE_IODATA:
	    ret = read(s->es_sock, io->ec_data, io->ec_size);
	    break;

	default:
	    log_warn("ioctx:0x%x type unkown:%d\n", (uint64_t) io, io->ec_type);
	    ret = -1;
	}

	if(ret < 0){
	    if(errno != EAGAIN){
		spi_spin_lock(&s->es_lock);
		s->es_status &= ~kEDPNET_SOCK_STATUS_READ;
		spi_spin_unlock(&s->es_lock);
	    }else{
		ret = -EAGAIN;
	    }
	}else{
	    io->ec_read = ret;
	}
    }

    return ret;
}

/*
 * edpnet - serv implementation
 */
enum{
    kEDPNET_SERV_STATUS_ZERO = 0,
    kEDPNET_SERV_STATUS_INIT,
    kEDPNET_SERV_STATUS_LISTEN,
    kEDPNET_SERV_STATUS_LISTENING,
};

struct edpnet_serv{
    int			es_status;

    int			es_sock;
    struct list_head	es_node;	// link to owner

    spi_spinlock_t	es_lock;
    struct list_head	es_socks;	// connected clients

    edpnet_serv_cbs_t	*es_cbs;
    void		*es_data;
};

static void serv_worker_cb(uint32_t events, void *data){
    struct edpnet_serv *s = (struct edpnet_serv *)data;
    struct edpnet_sock *sock;

    ASSERT(s != NULL);

    if(events & EPOLLIN){
	sock = mheap_alloc(sizeof(*sock));
	if(sock == NULL){
	    log_warn("no memory for sock!\n");
	    return ;
	}
	memset(sock, 0, sizeof(*sock));

	sock->es_sock = accept(s->es_sock, NULL, NULL);
	if(sock->es_sock < 0){
	    log_warn("accept client fail!\n");
	    return ;
	}
	
	if(sock_init(sock) < 0){
	    sock_fini(sock);
	    mheap_free(sock);
	}else{
	    s->es_cbs->connected(s, sock, s->es_data);
	}
    }

    if(events & EPOLLOUT){
	ASSERT(0);
    }

    if(events & (EPOLLERR | EPOLLHUP)){
	s->es_cbs->close(s, s->es_data);
    }
}

int edpnet_serv_create(edpnet_serv_t *serv, edpnet_serv_cbs_t *cbs, void *data){
    struct edpnet_serv	    *s;

    ASSERT((serv != NULL) && (cbs != NULL));

    s = mheap_alloc(sizeof(*s));
    if(s == NULL){
	log_warn("no enough memory for serv!\n");
	return -ENOMEM;
    }
    memset(s, 0, sizeof(*s));

    INIT_LIST_HEAD(&s->es_node);
    INIT_LIST_HEAD(&s->es_socks);
    s->es_cbs = cbs;
    s->es_data = data;

    s->es_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(s->es_sock < 0){
	log_warn("init sock failure!\n");
	mheap_free(s);
	return -1;
    }

    if(set_nonblock(s->es_sock) < 0){
	close(s->es_sock);
	mheap_free(s);
	return -1;
    }

    spi_spin_init(&s->es_lock);

    if(eio_addfd(s->es_sock, serv_worker_cb, s) != 0){
	log_warn("watch serv handle fail!\n");
	spi_spin_fini(&s->es_lock);
	close(s->es_sock);
	mheap_free(s);
	return -1;
    }

    s->es_status = kEDPNET_SERV_STATUS_INIT;

    *serv = s;

    return 0;
}

int edpnet_serv_destroy(edpnet_serv_t serv){
    struct edpnet_serv	*s = serv;

    ASSERT(s != NULL);

    eio_delfd(s->es_sock);

    spi_spin_lock(&s->es_lock);
    s->es_status = kEDPNET_SERV_STATUS_ZERO;
    spi_spin_unlock(&s->es_lock);

    close(s->es_sock);

    spi_spin_fini(&s->es_lock);

    mheap_free(s);

    return 0;
}

int edpnet_serv_listen(edpnet_serv_t serv, edpnet_addr_t *addr){
    struct edpnet_serv	*s = serv;
    struct sockaddr_in	sa;
    int			ret;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family	= AF_INET;

    if(addr->ea_type == kEDPNET_ADDR_TYPE_IPV4){
        sa.sin_port		= htons(addr->ea_v4.eia_port);
	sa.sin_addr.s_addr	= addr->ea_v4.eia_ip;
    }else if(addr->ea_type == kEDPNET_ADDR_TYPE_IPV6){
	//FIXME: IPv6 support
	ASSERT(0);
    }else{
	log_warn("IP address type unkonw:%d\n", addr->ea_type);
	return -1;
    }

    ret = bind(s->es_sock, (struct sockaddr*)&sa, sizeof(sa));
    if(ret < 0){
	return ret;
    }

    ret = listen(s->es_sock, kEDPNET_SERV_PENDCLIENTS);
    if(ret == 0){
	spi_spin_lock(&s->es_lock);
	s->es_status |= kEDPNET_SERV_STATUS_LISTEN;
	spi_spin_unlock(&s->es_lock);
    }

    return ret;
}

int edpnet_init(){
    return -1;
}

int edpnet_fini(){
    return -1;
}

