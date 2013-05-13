/*
 * Copyright (c) 2013, Konghan. All rights reserved.
 * Distributed under the BSD license, see the LICENSE file.
 */

#ifndef __IOCTX_H__
#define __IOCTX_H__

#include "edp_sys.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

enum io_context_data_type{
    kIOCTX_DATA_TYPE_VEC = 11,    // iovec array
    kIOCTX_DATA_TYPE_PTR,	    // raw data
};

enum io_contex_io_type{
    kIOCTX_IO_TYPE_SOCK = 22,
    kIOCTX_IO_TYPE_BLKDEV,
};

struct edpnet_sock;
struct ioctx;
typedef void (*edpnet_writecb)(struct edpnet_sock *sock, struct ioctx *ioc, int errcode);

typedef struct ioctx{
    uint16_t		ioc_io_type;
    uint16_t		ioc_data_type;    // IOVEC or IODATA

    union {
	// edpnet sock read & write io
	struct {
	    struct list_head	ioc_node;    // link to owner
	    edpnet_writecb	ioc_iocb;
	    struct edpnet_sock	*ioc_sock;
	    size_t		ioc_bytes;
	};
    };

    union{
	struct{
	    uint32_t	    ioc_ionr;
	    struct iovec    *ioc_iov;
	};
	struct{
	    uint32_t	ioc_size;
	    void	*ioc_data;
	};
    };
}ioctx_t;

static inline void ioctx_init(ioctx_t *ioc, uint16_t iotype, uint16_t datatype){
    ASSERT(ioc != NULL);

    memset(ioc, 0, sizeof(*ioc));

    ioc->ioc_io_type	= iotype;
    ioc->ioc_data_type	= datatype;

    switch(iotype){
	case kIOCTX_IO_TYPE_SOCK:
	    INIT_LIST_HEAD(&ioc->ioc_node);
	    break;
	
	default:
	    ASSERT(0);
    }
}


#ifdef __cplusplus
}
#endif

#endif // __IOCTX_H__

