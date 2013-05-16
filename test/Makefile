
CC	= gcc
CFLAGS	= -Wall -g -I../include -I../posix  -I../src 
LDFLAGS = -pthread

TARGET = edpio

objs = logger.o mcache.o hset.o
objs += worker.o emitter.o edp.o
objs += eio-epoll.o
objs += edpnet.o
#objs += main.o

objs-test := emit_test.o

objs-serv := serv_test.o

objs-sock := sock_test.o

vpath %.c ../src ../lib ../posix

%.o:%.c
	-$(CC) $(CFLAGS) -c -o $@ $<


sock:$(objs-sock) $(objs)
	$(CC) -Wall -o $(TARGET) $(objs) $(objs-sock) $(LDFLAGS)

serv:$(objs-serv) $(objs)
	$(CC) -Wall -o $(TARGET) $(objs) $(objs-serv) $(LDFLAGS)

emit:$(objs-test) $(objs)
	$(CC) -Wall -o $(TARGET) $(objs) $(objs-test) $(LDFLAGS)


#all:$(objs)
#	$(CC) -Wall -o $(TARGET) $(objs) $(LDFLAGS)


clean:
	rm -f $(objs) $(TARGET) $(objs-test) $(objs-serv) $(objs-sock)


