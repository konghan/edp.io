
#include "edp.h"
#include "edpnet.h"
#include "emitter.h"

#include "logger.h"
#include "mcache.h"

#define kSERV_IOBUF_MAX		256

// sock data
typedef struct sock_data{
    edpnet_sock_t	sd_sock;
    edpnet_sock_cbs_t	sd_cbs;
}sock_data_t;

static const char __msg[] = "sock test string...\n\0";
static const short __port = 2020;

static sock_data_t  __sock_data = {};

// implementations

static void ioctx_free(ioctx_t *ioc);
static ioctx_t *ioctx_alloc(uint16_t iotype, size_t size){
    ioctx_t  *ioc;

    ASSERT(size > 0);

    ioc = mheap_alloc(sizeof(*ioc));
    if(ioc == NULL){
	return NULL;
    }
    ioctx_init(ioc, iotype, kIOCTX_DATA_TYPE_PTR);

    ioc->ioc_data = mheap_alloc(size);
    if(ioc->ioc_data == NULL){
	ioctx_free(ioc);
	return NULL;
    }
    ioc->ioc_size = size;

    return ioc;
}

static void ioctx_free(ioctx_t *ioc){
    ASSERT(ioc != NULL);

    if(ioc->ioc_data_type == kIOCTX_DATA_TYPE_PTR)
	mheap_free(ioc->ioc_data);

    mheap_free(ioc);
}

void sock_write_cb(edpnet_sock_t sock, struct ioctx *ioc, int errcode){
    ioctx_free(ioc);

    log_info("sock write callback\n");
}

static void sock_connect(edpnet_sock_t sock, void *data){
    sock_data_t	*sd = (sock_data_t *)data;
    ioctx_t	*ioc;

    log_info("sock is connected\n");

    ASSERT(sd != NULL);

    ioc = ioctx_alloc(kIOCTX_IO_TYPE_SOCK, kSERV_IOBUF_MAX);
    if(ioc == NULL){
	log_warn("alloc ioctx fail\n");
	return ;
    }
    strncpy(ioc->ioc_data, __msg, strlen(__msg));
    ioc->ioc_size = strlen(__msg);

    edpnet_sock_write(sd->sd_sock, ioc, sock_write_cb);
}


static void data_ready(edpnet_sock_t sock, void *data){
    sock_data_t	*sd = (sock_data_t *)data;
    ioctx_t	*ioc;
    int		ret;

    log_info("data ready for read\n");
    ASSERT(sd != NULL);

    ioc = ioctx_alloc(kIOCTX_IO_TYPE_SOCK, kSERV_IOBUF_MAX);
    if(ioc == NULL){
	log_warn("alloc ioctx fail\n");
	return ;
    }
    
    ret = edpnet_sock_read(sd->sd_sock, ioc);
    if(ret < 0){
        log_warn("no more data\n");
        ioctx_free(ioc);
	return ;
    }

    edpnet_sock_write(sd->sd_sock, ioc, sock_write_cb);
}

static void data_drain(edpnet_sock_t sock, void *data){
    ioctx_t	*ioc;

    log_info("data drain for write\n");

    ioc = ioctx_alloc(kIOCTX_IO_TYPE_SOCK, kSERV_IOBUF_MAX);
    if(ioc == NULL){
	log_warn("alloc ioctx fail\n");
	return ;
    }

    strncpy(ioc->ioc_data, __msg, strlen(__msg));
    ioc->ioc_size = strlen(__msg);

    edpnet_sock_write(sock, ioc, sock_write_cb);
}

static void sock_error(edpnet_sock_t sock, void *data){
    log_info("sock error\n");
}

static void sock_close(edpnet_sock_t sock, void *data){
    log_info("sock close\n");
//    edpnet_sock_destroy(sock);
}

static int sock_init(){
    sock_data_t	    *sd = &__sock_data;
    edpnet_addr_t   addr;
    int		    ret;

    sd->sd_cbs.sock_connect = sock_connect;
    sd->sd_cbs.sock_close   = sock_close;
    sd->sd_cbs.sock_error   = sock_error;
    sd->sd_cbs.data_ready   = data_ready;
    sd->sd_cbs.data_drain   = data_drain;

    log_info("sock init been called!\n");

    ret = edpnet_sock_create(&sd->sd_sock, &sd->sd_cbs, sd);
    if(ret != 0){
	log_warn("create edpnet sock fail:%d\n", ret);
	return -1;
    }
    log_info("create sock success\n");

    addr.ea_type = kEDPNET_ADDR_TYPE_IPV4;
    ret = edpnet_pton(kEDPNET_ADDR_TYPE_IPV4, "127.0.0.1", &addr.ea_v4.eia_ip);
    if(ret != 0){
	log_warn("conver ip string to binary fail:%d\n", ret);
	edpnet_sock_destroy(sd->sd_sock);
	return -1;
    }
    addr.ea_v4.eia_port = __port;

    log_info("initialize address ok\n");

    ret = edpnet_sock_connect(sd->sd_sock, &addr);
    if(ret != 0){
	log_warn("edpnet sock listen fail:%d\n", ret);
	edpnet_sock_destroy(sd->sd_sock);
	return -1;
    }

    log_info("call sock connect...\n");

    return 0;
}

int main(){
    edp_init(1);

    // initializing your application
    sock_init();

    edp_loop();

    edp_fini();
    return 0;
}


