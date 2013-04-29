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

#define kEDPNET_SOCK_STATUS_ZERO	0x0000
#define kEDPNET_SOCK_STATUS_INIT	0x0001
#define kEDPNET_SOCK_STATUS_CONNECT	0x0002
#define kEDPNET_SOCK_STATUS_WRITE	0x0100
#define kEDPNET_SOCK_STATUS_READ	0x0200

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

    edpnet_sock_cbs_t	es_cbs;
};
 
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
		s->es_cbs.data_ready(s, 0);
	}else{
	    spi_spin_lock(&s->es_lock);
	    s->es_status |= kEDPNET_SOCK_STATUS_CONNECT;
	    spi_spin_unlock(&s->es_lock);
	}
    }

    if(events & EPOLLOUT){
	ASSERT(s->es_status & kEDPNET_SOCK_STATUS_CONNECT);
	
	if(s->es_pendios){
	    spi_spin_lock(&s->es_lock);
	    ASSERT(!list_empty(&s->es_iowrites));
	    lhc = s->es_iowrites.next;
	    list_del(lhc);
	    if(!list_empty(&s->es_iowrites)){
		lhn = s->es_iowrites.next;
	    }else{
		s->es_status &= ~kEDPNET_SOCK_STATUS_WRITE;
	    }
	    spi_spin_unlock(&s->es_lock);

	    if(lhn != NULL){
		// FIXME:write data
	    }

	    ioc = list_entry(lhc, ec_node);
	    ioc->ec_iocb(s, ioc, 0);

	}else{
	    s->es_cbs.data_drain(s);
	}
    }

    if(events & EPOLLERR){
	s->es_cbs.sock_error(s, -1);
	//FIXME: write error...
    }
    
    if(events | EPOLLHUP){
	spi_spin_lock(&s->es_lock);
	s->es_status &= ~kEDPNET_SOCK_STATUS_CONNECT;
	spi_spin_unlock(&s->es_lock);

	s->es_cbs.sock_close(s, -1);
    }
}

static int edpnet_sock_init(edpnet_data_t *ed, edpnet_sock_t sock){
    struct edpnet_sock	*s = sock;

    s->es_sock = sock(PF_INET, SOCK_STREAM, 0);
    if(s->es_sock < 0){
	log_warn("init sock failure!\n");
	return -1;
    }

    if(set_nonblock(s->es_sock) < 0){
	close(s->es_sock);
	return -1;
    }

    INIT_LIST_HEAD(&s->es_node);
    INIT_LIST_HEAD(&s->es_ioreads);
    INIT_LIST_HEAD(&s->es_iowrites);

    if(watch_add(s->es_sock, sock_worker_cb, s) != 0){
	log_warn("watch sock handle fail!\n");
	close(s->es_sock);
	return -1;
    }

    spi_spin_init(&s->es_lock);

    s->es_status |= kEDPNET_SOCK_STATUS_INIT;

    spi_spin_lock(&ed->ed_lock);
    list_add(&s->es_node, &ed->ed_socks);
    spi_spin_unlock(&ed->ed_lock);

    return 0;
}

static int edpnet_sock_fini(edpnet_data_t *ed, edpnet_sock_t sock){
    struct edpnet_sock	*s = sock;

    if(s->es_status != 0){
	log_warn("fini sock in wrong status!\n"):
	return -1;
    }

    watch_del(s->es_sock);

    ASSERT(list_empty(&s->es_ios));
    spi_spin_fini(&s->es_lock);

    spi_spin_lock(&ed->ed_lock);
    list_del(&s->es_node);
    spi_spin_unlock(&ed->ed_lock);

    return 0;
}

int edpnet_sock_create(edpnet_sock_t *sock, edpnet_sock_cbs_t *cbs){
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
    memset(s, sizeof(*s));

    ret = edpnet_sock_init(ed, s);
    if(ret != 0){
	log_warn("initialize sock failure!\n");
	mheap_free(s);
	return ret;
    }

    s->es_cbs = cbs;
    *sock = s;

    return 0;
}

int edpnet_sock_destroy(edpnet_sock_t sock){
    edpnet_data_t	*ed = &__edpnet_data;
    struct edpnet_sock	*s = sock;
    int			ret;

    if(!ed->ed_init){
	log_warn("ednet not inited!\n");
	return -1;
    }

    s->es_status = kEDPNET_SOCK_STATUS_ZERO;
    edpnet_sock_fini(ed, sock);
    close(s->es_sock);

    mheap_free(s);

    return 0;
}

int edpnet_sock_connect(edpnet_sock_t sock, edpnet_addr_t *addr){
    struct edpnet_sock	*s = sock;
    struct sockaddr_in	sa;
    int			ret;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family	= AF_INET;
    sa.sin_port		= htons(addr->ea_port);
    sa.sin_addr.s_addr	= addr->ea_ip;

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
	case kedpnet_IOCTX_TYPE_IOVEC:
	    ret = readv(s->es_sock, io->ec_iov, io->ec_ionr);
	    break;

	case kedpnet_IOCTX_TYPE_IODATA:
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
struct edpnet_serv{
    int			es_status;

    int			es_sock;
    struct list_head	es_node;	// link to owner

    spi_spinlock_t	es_lock;
    struct list_head	es_socks;	// connected clients

    edpnet_serv_cbs_t	es_cbs;
};

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

int edpnet_init(){
    return -1;
}

int edpnet_fini(){
    return -1;
}

