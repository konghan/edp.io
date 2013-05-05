
#include "edp.h"
#include "emitter.h"

#include "logger.h"
#include "mcache.h"


struct emit_test{
    emit_t	et_emit;

    // user define data
    struct emit_test *et_other;
};

static void sendrecv_cb(struct edp_event *ev, void *data, int errcode){
    static int count = 10;
    struct emit_test *e  = (struct emit_test *)data;


    count --;
    if(count > 0){
	log_info("redispatch event:%d - %d\n", e->et_other, e->et_other->et_emit);
	edp_event_init(ev, 0, kEDP_EVENT_PRIORITY_NORM);
	emit_dispatch(e->et_other->et_emit, ev, sendrecv_cb, e->et_other);
    }else{
        mheap_free(ev);
    }
}

static int send_handler(emit_t em, edp_event_t *ev){
    log_info("send handler is called!\n");

    return 0;
}

static int recv_handler(emit_t em, edp_event_t *ev){
    log_info("recv handler is called!\n");

    return 0;
}


static struct emit_test	    __e1 = {};
static struct emit_test	    __e2 = {};

int emit_test(){
    struct edp_event *ev;
    int	    ret;

    __e1.et_other = &__e2;
    __e2.et_other = &__e1;

    ev = mheap_alloc(sizeof(*ev));
    if(ev == NULL){
	log_info("alloc ev fail!\n");
	return -ENOMEM;
    }

    ret = emit_create(NULL, &__e1.et_emit);
    if(ret != 0){
	log_info("create emit 1 fail:%d\n", ret);
	mheap_free(ev);
	return ret;
    }
    emit_add_handler(__e1.et_emit, 0, send_handler);

    ret = emit_create(NULL, &__e2.et_emit);
    if(ret != 0){
	log_info("create emit 2 fail:%d\n", ret);
	emit_destroy(__e1.et_emit);
	mheap_free(ev);
	return ret;
    }
    emit_add_handler(__e2.et_emit, 0, recv_handler);

    edp_event_init(ev, 0, kEDP_EVENT_PRIORITY_NORM);

    emit_dispatch(__e1.et_emit, ev, sendrecv_cb, &__e1);
//    emit_dispatch(__e2.et_emit, ev, sendrecv_cb, &__e2);

    return 0;
}


int main(){
    edp_init(1);

    // initializing your application

    emit_test();

    edp_loop();

    edp_fini();
    return 0;
}


