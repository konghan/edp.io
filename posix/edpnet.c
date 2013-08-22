/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#include "edp.h"
#include "emitter.h"
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
#include <arpa/inet.h>

#define kEDPNET_SERV_PENDCLIENTS	64

/*
 * edpnet - common part implementation
 */

typedef struct edpnet_data{
    int			ed_init;

    spi_spinlock_t	ed_lock;

    struct list_head	ed_socks;
    struct list_head	ed_servs;

    mcache_t		ed_evcache;
}edpnet_data_t;

static edpnet_data_t	__edpnet_data = {};

static inline edp_event_t * edpnet_alloc_event(){
    edpnet_data_t   *ed = &__edpnet_data;

    if(!ed->ed_init)
	return NULL;

    return (edp_event_t *)mcache_alloc(ed->ed_evcache);
}

static inline void edpnet_free_event(edp_event_t *ev){
    edpnet_data_t   *ed = &__edpnet_data;

    ASSERT(ed->ed_init);

    mcache_free(ed->ed_evcache, ev);
}

static int set_nonblock(int sock){
    int	flags;

    flags = fcntl(sock, F_GETFL, 0);
    if(flags != -1){
	fcntl(sock, F_SETFL, flags|O_NONBLOCK);
	return 0;
    }

    return -1;
}

// convert ipv4 or ipv6 address from text form to binary form
int edpnet_pton(int type, const char *src, void *dst){
    int	 af;

    if(type == kEDPNET_ADDR_TYPE_IPV4){
	af = AF_INET;
    }else if(type == kEDPNET_ADDR_TYPE_IPV6){
	af = AF_INET6;
    }else{
	return -EINVAL;
    }

    return (inet_pton(af, src, dst) == 1) ? 0 : -EINVAL;
}

// convert ipv4 or ipv6 address form binary form to text form
const char* edpnet_ntop(int type, const void *src, char *dst, int len){
    int	 af;

    if(type == kEDPNET_ADDR_TYPE_IPV4){
	af = AF_INET;
    }else if(type == kEDPNET_ADDR_TYPE_IPV6){
	af = AF_INET6;
    }else{
	return NULL;
    }

    return inet_ntop(af, src, dst, (socklen_t)len);
}


/*
 * edpnet - sock implementation
 */
//enum edpnet_sock_status{
#define kEDPNET_SOCK_STATUS_ZERO	0x0000
#define kEDPNET_SOCK_STATUS_INIT	0x0001
#define kEDPNET_SOCK_STATUS_MONITOR	0x0002
#define kEDPNET_SOCK_STATUS_CONNECT	0x0004
//};
#define kEDPNET_SOCK_STATUS_WRITE	0x0100
#define kEDPNET_SOCK_STATUS_READ	0x0200

enum edpnet_sock_handler{
    kEDPNET_SOCK_EPOLLOUT = 0,
    kEDPNET_SOCK_EPOLLIN,
    kEDPNET_SOCK_EPOLLERR,
    kEDPNET_SOCK_EPOLLHUP,
};

struct edpnet_sock{
    int			es_status;

    int			es_sock;	// sock handle
    struct list_head	es_node;	// link to owner

    spi_spinlock_t	es_lock;	// data protect lock
    atomic_t		es_pendios;	// pending write io number
    struct list_head	es_iowrites;	// pending write ios
    ioctx_t		*es_write;	// current write io ptr

    edpnet_sock_cbs_t	*es_cbs;	// async event callbacks
    void		*es_data;	// user private data

    emit_t		es_emit;
};

static inline int sock_write(int sock, ioctx_t *io){
    int		ret = -1;

    ASSERT(io != NULL);
    
    switch(io->ioc_data_type){
	case kIOCTX_DATA_TYPE_VEC:
	    ret = writev(sock, io->ioc_iov, io->ioc_ionr);
	    break;

	case kIOCTX_DATA_TYPE_PTR:
	    ret = write(sock, io->ioc_data, io->ioc_size);
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

static inline void sock_write_next(struct edpnet_sock *s, int drain){
    struct list_head	*lhn = NULL;
    ioctx_t		*ion = NULL;
    int			nowrite = 1;
    int			ret;

    ASSERT(s != NULL);

    while(1){
	spi_spin_lock(&s->es_lock);
	// no more pending write ios
	if(s->es_pendios <= 0){
	    spi_spin_unlock(&s->es_lock);
	    nowrite = 1;
	    break;
	}
	ASSERT(!list_empty(&s->es_iowrites));
	lhn = s->es_iowrites.next;
	list_del(lhn);
	atomic_dec(&s->es_pendios);

	ion = list_entry(lhn, ioctx_t, ioc_node);
	ASSERT(ion->ioc_io_type == kIOCTX_IO_TYPE_SOCK);

	s->es_write = ion;
		
	spi_spin_unlock(&s->es_lock);

	ret = sock_write(s->es_sock, ion);
	if(ret != 0){
	    spi_spin_lock(&s->es_lock);
	    s->es_write = NULL;
	    spi_spin_unlock(&s->es_lock);
	    ion->ioc_iocb(s, ion, ret);
	}else{
	    nowrite = 0;
	    break;
	}
    }

    if((nowrite)&&(!drain)){
	spi_spin_lock(&s->es_lock);
	s->es_status &= ~kEDPNET_SOCK_STATUS_WRITE;
	spi_spin_unlock(&s->es_lock);
	
	// call data drain callback pfn
	s->es_cbs->data_drain(s, s->es_data);
    }
}

static int edpnet_sock_epollout_handler(emit_t em, edp_event_t *ev){
    struct edpnet_sock	*s;
    ioctx_t		*ioc = NULL;

    s = emit_get(em);
    ASSERT(s != NULL);

    if(!(s->es_status & kEDPNET_SOCK_STATUS_CONNECT)){
	spi_spin_lock(&s->es_lock);
	s->es_status |= kEDPNET_SOCK_STATUS_CONNECT;
	spi_spin_unlock(&s->es_lock);

	// call connect callback
	s->es_cbs->sock_connect(s, s->es_data);

    }else if(s->es_status & kEDPNET_SOCK_STATUS_WRITE){
        spi_spin_lock(&s->es_lock);
	if(s->es_write != NULL){
	    
	    // current write io is oky
	    ioc = s->es_write;
	    s->es_write = NULL;
	    spi_spin_unlock(&s->es_lock);

	    // call current write io's callbacks
	    ioc->ioc_iocb(s, ioc, 0);
	}else{
	    spi_spin_unlock(&s->es_lock);
	    
	    // write next io to sock
	    sock_write_next(s, 1);
	}
    }else{
	// call drain to notify caller
	s->es_cbs->data_drain(s, s->es_data);
    }

    return 0;
}

static int edpnet_sock_epollin_handler(emit_t em, edp_event_t *ev){
    struct edpnet_sock	*s;
    int			ready = 0;

    s = emit_get(em);
    ASSERT(s != NULL);

    ASSERT(s->es_status & kEDPNET_SOCK_STATUS_CONNECT);
	
    // data come in
    spi_spin_lock(&s->es_lock);
    if(!(s->es_status & kEDPNET_SOCK_STATUS_READ)){
	s->es_status |= kEDPNET_SOCK_STATUS_READ;
	ready = 1;
    }
    spi_spin_unlock(&s->es_lock);

    // call user regiested callback:data_ready
    if(ready)
	s->es_cbs->data_ready(s, s->es_data);
    
    return 0;
}

static int edpnet_sock_epollerr_handler(emit_t em, edp_event_t *ev){
    struct edpnet_sock	*s;

    s = emit_get(em);
    ASSERT(s != NULL);

    s->es_cbs->sock_error(s, s->es_data);
    
    //FIXME: clear pending writes

    return 0;
}
 
static int edpnet_sock_epollhup_handler(emit_t em, edp_event_t *ev){
    struct edpnet_sock	*s;

    s = emit_get(em);
    ASSERT(s != NULL);

    spi_spin_lock(&s->es_lock);
    s->es_status &= ~kEDPNET_SOCK_STATUS_CONNECT;
    spi_spin_unlock(&s->es_lock);

    s->es_cbs->sock_close(s, s->es_data);
	
    //FIXME: clear pending writes

    return 0;
}

static void edpnet_sock_done(edp_event_t *ev, void *data, int errcode){
    ASSERT(ev != NULL);

    edpnet_free_event(ev);
}

static int edpnet_sock_dispatch(struct edpnet_sock *sock, enum edpnet_sock_handler type){
    edp_event_t	    *ev;
    int		    ret;

    ASSERT(sock != NULL);

    ev = edpnet_alloc_event();
    if(ev == NULL){
	log_warn("alloc event fail\n");
	return -ENOMEM;
    }
    edp_event_init(ev, (short)type, kEDP_EVENT_PRIORITY_NORM);

    ret = emit_dispatch(sock->es_emit, ev, edpnet_sock_done, NULL);
    if(ret != 0){
	log_warn("dispatch event fail:%d\n", ret);
	edpnet_free_event(ev);
	return -1;
    }

    return 0;
}

static void sock_worker_cb(uint32_t events, void *data){
    struct edpnet_sock	*s = (struct edpnet_sock *)data;

    ASSERT(s != NULL);

    if(events & EPOLLOUT){
	edpnet_sock_dispatch(s, kEDPNET_SOCK_EPOLLOUT);
    }

    if(events & (EPOLLPRI | EPOLLIN)){
	edpnet_sock_dispatch(s, kEDPNET_SOCK_EPOLLIN);
    }

    if(events & EPOLLERR){
	edpnet_sock_dispatch(s, kEDPNET_SOCK_EPOLLERR);
    }
    
    if(events & EPOLLHUP){
	edpnet_sock_dispatch(s, kEDPNET_SOCK_EPOLLHUP);
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

    return 0;
}

static int sock_fini(edpnet_sock_t sock){
    struct edpnet_sock	*s = sock;

//    if(s->es_status != 0){
//	log_warn("fini sock in wrong status!\n");
//	return -1;
//    }

    if(s->es_status & kEDPNET_SOCK_STATUS_MONITOR){
	s->es_status &= ~kEDPNET_SOCK_STATUS_MONITOR;
	eio_delfd(s->es_sock);
    }

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

    ret = emit_create(s, &s->es_emit);
    if(ret !=0 ){
	log_warn("create emit fail:%d\n", ret);
	return ret;
    }
    emit_add_handler(s->es_emit, kEDPNET_SOCK_EPOLLOUT, edpnet_sock_epollout_handler);
    emit_add_handler(s->es_emit, kEDPNET_SOCK_EPOLLIN, edpnet_sock_epollin_handler);
    emit_add_handler(s->es_emit, kEDPNET_SOCK_EPOLLERR, edpnet_sock_epollerr_handler);
    emit_add_handler(s->es_emit, kEDPNET_SOCK_EPOLLHUP, edpnet_sock_epollhup_handler);

    s->es_sock = socket(PF_INET, SOCK_STREAM, 0);
    if(s->es_sock < 0){
	log_warn("init sock failure!\n");
	emit_destroy(s->es_emit);
	mheap_free(s);
	return -1;
    }

    ret = sock_init(s);
    if(ret != 0){
	log_warn("initialize sock failure!\n");
	emit_destroy(s->es_emit);
	mheap_free(s);
	return ret;
    }

    edpnet_sock_set(s, cbs, data);
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

    emit_destroy(s->es_emit);
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

    if(!(s->es_status & kEDPNET_SOCK_STATUS_MONITOR)){
        if(eio_addfd(s->es_sock, sock_worker_cb, s) != 0){
	    log_warn("watch sock handle fail!\n");
	    close(s->es_sock);
	    return -1;
	}
	s->es_status |= kEDPNET_SOCK_STATUS_MONITOR;
    }
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
	ASSERT(0);
    }else{
	log_warn("IP address type is unknow:%d\n", addr->ea_type);
	return -1;
    }

    ret = connect(s->es_sock, (struct sockaddr *)&sa, sizeof(sa));
    if((ret < 0) && (errno != EINPROGRESS)){
	log_warn("connect to serv failure:%d\n", errno);
	return ret;
    }

    return 0;
}

int edpnet_sock_write(edpnet_sock_t sock, ioctx_t *io, edpnet_writecb cb){
    struct edpnet_sock	*s = sock;
    int			ret = 0;

    ASSERT((io != NULL) && (io->ioc_io_type == kIOCTX_IO_TYPE_SOCK));

    io->ioc_iocb  = cb;
    io->ioc_sock  = sock;

    spi_spin_lock(&s->es_lock);
    if((!(s->es_status & kEDPNET_SOCK_STATUS_WRITE)) && (s->es_write == NULL)){
	s->es_write = io;
	s->es_status |= kEDPNET_SOCK_STATUS_WRITE;
	spi_spin_unlock(&s->es_lock);

	ret = sock_write(s->es_sock, io);
	if(ret != 0){
	    // ret < 0, write fail; ret > 0, data size ret have been writed.

	    spi_spin_lock(&s->es_lock);
	    s->es_write = NULL;
	    spi_spin_unlock(&s->es_lock);

	    // write fail or success, callback to caller
	    cb(sock, io, ret);

	    // write another pending write io
	    edpnet_sock_dispatch(s, kEDPNET_SOCK_EPOLLOUT);
//	    sock_write_next(s, 0);
	}else{
	    // write return EAGAIN or EWOULDBLOCK
	    // do nothing
	}

    }else{
	list_add_tail(&io->ioc_node, &s->es_iowrites);
	atomic_inc(&s->es_pendios);
	spi_spin_unlock(&s->es_lock);
    }

    return ret;
}

int edpnet_sock_read(edpnet_sock_t sock, ioctx_t *io){
    struct edpnet_sock	*s = sock;
    int			ready = 0;
    ssize_t		ret = -1;

    ASSERT((io != NULL) && (io->ioc_io_type == kIOCTX_IO_TYPE_SOCK));

    spi_spin_lock(&s->es_lock);
    if(s->es_status & kEDPNET_SOCK_STATUS_READ){
	ready = 1;
    }
    spi_spin_unlock(&s->es_lock);

    if(ready){
        switch(io->ioc_data_type){
	case kIOCTX_DATA_TYPE_VEC:
	    ret = readv(s->es_sock, io->ioc_iov, io->ioc_ionr);
	    break;

	case kIOCTX_DATA_TYPE_PTR:
	    ret = read(s->es_sock, io->ioc_data, io->ioc_size);
	    break;

	default:
	    log_warn("ioctx:0x%x type unkown:%d\n", (uint64_t) io, io->ioc_data_type);
	    ret = -1;
	}

	if(ret < 0){
	    if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
		ret = -EAGAIN;
	    }
	    spi_spin_lock(&s->es_lock);
	    s->es_status &= ~kEDPNET_SOCK_STATUS_READ;
	    spi_spin_unlock(&s->es_lock);
	}else{
	    io->ioc_bytes = ret;
	}
    }

    return ret;
}

/*
 * edpnet - serv implementation
 */
enum{
    kEDPNET_SERV_STATUS_ZERO = 0,   // server data unkonw status
    kEDPNET_SERV_STATUS_INIT,	    // server data intialized
    kEDPNET_SERV_STATUS_LISTEN,	    // server is listening
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

/*
 *
 */
int edpnet_init(){
    edpnet_data_t	*ed = &__edpnet_data;

    if(eio_init(1) != 0){
	log_warn("init eio fail\n");
	return -1;
    }

    INIT_LIST_HEAD(&ed->ed_socks);
    INIT_LIST_HEAD(&ed->ed_servs);

    spi_spin_init(&ed->ed_lock);

    if(mcache_create(sizeof(edp_event_t), sizeof(int), MCACHE_FLAGS_NOWAIT,
		&ed->ed_evcache)){
	spi_spin_fini(&ed->ed_lock);
	eio_fini();

	return -1;
    }

    ed->ed_init = 1;

    return 0;
}

int edpnet_fini(){
    edpnet_data_t	*ed = &__edpnet_data;

    eio_fini();

    ed->ed_init = 0;
    spi_spin_fini(&ed->ed_lock);
    mcache_destroy(ed->ed_evcache);

    return 0;
}

