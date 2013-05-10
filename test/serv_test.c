
#include "edp.h"
#include "edpnet.h"
#include "emitter.h"

#include "logger.h"
#include "mcache.h"

#define kSERV_IOBUF_MAX		256

struct serv_data;
typedef struct sock_session{
    edpnet_sock_t	ss_sock;
    struct list_head	ss_node;    // link to serv

    struct serv_data	*ss_serv;

    edpnet_sock_cbs_t	ss_cbs;

    spi_spinlock_t	ss_rlock;
    struct list_head	ss_ios;
}sock_sess_t;

typedef struct serv_data{
    edpnet_serv_t	sd_serv;
    edpnet_serv_cbs_t	sd_cbs;

    spi_spinlock_t	sd_lock;
    struct list_head	sd_socks;   // connected client
}serv_data_t;

static serv_data_t  __serv_data = {};

static void ioctx_free(edpnet_ioctx_t *ioc);
static edpnet_ioctx_t *ioctx_alloc(uint32_t type, uint32_t size){
    edpnet_ioctx_t  *ioc;

    ASSERT(size > 0);

    ioc = mheap_alloc(sizeof(*ioc));
    if(ioc == NULL){
	return NULL;
    }

    ioc->ec_data = mheap_alloc(size);
    if(ioc->ec_data == NULL){
	ioctx_free(ioc);
	return NULL;
    }

    edpnet_ioctx_init(ioc, type);
    ioc->ec_size = size;

    return ioc;
}

static void ioctx_free(edpnet_ioctx_t *ioc){
    ASSERT(ioc != NULL);

    mheap_free(ioc->ec_data);
    mheap_free(ioc);
}

static void sock_connect(edpnet_sock_t sock, void *data){
    log_info("sock is connected\n");
}

void sock_write_cb(edpnet_sock_t sock, struct edpnet_ioctx *ioctx, int errcode){
    ioctx_free(ioctx);
}

static void data_ready(edpnet_sock_t sock, void *data){
    sock_sess_t	    *ss = (sock_sess_t *)data;
    edpnet_ioctx_t  *ioc;
    int		    ret;

    log_info("data ready for read\n");
    ASSERT(ss != NULL);

    while(1){
	ioc = ioctx_alloc(kEDPNET_IOCTX_TYPE_IODATA, kSERV_IOBUF_MAX);
        if(ioc == NULL){
	    log_warn("alloc ioctx fail\n");
	    break;
	}
    
	ret = edpnet_sock_read(ss->ss_sock, ioc);
	if(ret < 0){
	    log_warn("no more data\n");
	    ioctx_free(ioc);
	    break;
	}

	edpnet_sock_write(ss->ss_sock, ioc, sock_write_cb);
    }
}

static void data_drain(edpnet_sock_t sock, void *data){
    log_info("data drain for write\n");
}

static void sock_error(edpnet_sock_t sock, void *data){
    log_info("sock error\n");
}

static void sock_close(edpnet_sock_t sock, void *data){
    log_info("sock close\n");
}

static int sock_sess_init(sock_sess_t *ss, serv_data_t *sd){
    ASSERT((ss != NULL) && (sd != NULL));

    ss->ss_cbs.sock_connect = sock_connect;
    ss->ss_cbs.sock_close   = sock_close;
    ss->ss_cbs.sock_error   = sock_error;
    ss->ss_cbs.data_ready   = data_ready;
    ss->ss_cbs.data_drain   = data_drain;

    INIT_LIST_HEAD(&ss->ss_ios);
    spi_spin_init(&ss->ss_rlock);

    spi_spin_lock(&sd->sd_lock);
    list_add(&ss->ss_node, &sd->sd_socks);
    spi_spin_unlock(&sd->sd_lock);

    edpnet_sock_set(ss->ss_sock, &ss->ss_cbs, ss);

    return 0;
}

static int serv_connected(edpnet_serv_t serv, edpnet_sock_t sock, void *data){
    serv_data_t	    *sd = (serv_data_t *)data;
    sock_sess_t	    *ss;
    int		    ret;

    ASSERT(sd == &__serv_data);

    ss = mheap_alloc(sizeof(*ss));
    if(ss == NULL){
	log_warn("alloc sock-sess fail\n");
	edpnet_sock_destroy(sock);
	return -ENOMEM;
    }
    memset(ss, 0, sizeof(*ss));

    ss->ss_sock = sock;
    INIT_LIST_HEAD(&ss->ss_node);
    ss->ss_serv = sd;

    ret = sock_sess_init(ss, sd);
    if(ret != 0){
	log_warn("init sock session fail:%d\n", ret);
	mheap_free(ss);
	return ret;
    }

    return 0;
}

static int serv_close(edpnet_serv_t serv, void *data){
    serv_data_t	    *sd = (serv_data_t *)data;

    ASSERT(sd == &__serv_data);

    //FIXME: add cleanup code

    return 0;
}

static int serv_init(){
    serv_data_t	    *sd = &__serv_data;
    edpnet_addr_t   addr;
    int		    ret;

    INIT_LIST_HEAD(&sd->sd_socks);
    spi_spin_init(&sd->sd_lock);

    sd->sd_cbs.connected    = serv_connected;
    sd->sd_cbs.close	    = serv_close;

    ret = edpnet_serv_create(&sd->sd_serv, &sd->sd_cbs, sd);
    if(ret != 0){
	log_warn("create edpnet server fail:%d\n", ret);
	spi_spin_fini(&sd->sd_lock);
	return -1;
    }

    ret = edpnet_pton(kEDPNET_ADDR_TYPE_IPV4, "127.0.0.1", &addr.ea_v4.eia_ip);
    if(ret != 0){
	log_warn("conver ip string to binary fail:%d\n", ret);
	spi_spin_fini(&sd->sd_lock);
	edpnet_serv_destroy(sd->sd_serv);
	return -1;
    }
    addr.ea_v4.eia_port = 3020;

    ret = edpnet_serv_listen(sd->sd_serv, &addr);
    if(ret != 0){
	log_warn("edpnet serv listen fail:%d\n", ret);
	spi_spin_fini(&sd->sd_lock);
	edpnet_serv_destroy(sd->sd_serv);
	return -1;
    }

    return 0;
}

static int serv_fini(){
    serv_data_t	    *sd = &__serv_data;

    edpnet_serv_destroy(sd->sd_serv);

    spi_spin_fini(&sd->sd_lock);

    return 0;
}

int main(){
    edp_init(1);

    // initializing your application
    serv_init();

    edp_loop();

    edp_fini();
    return 0;
}


