
CC	= gcc
CFLAGS	= -Wall -O3 -I../include -I. -I../src
LDFLAGS = -pthread

TARGET = edpio

objs = logger.o mcache.o hset.o
objs += worker.o emitter.o edp.o
objs += eio-epoll.o
objs += edpnet.o
objs += main.o

vpath %.c ../src ../lib


%.o:%.c
	-$(CC) $(CFLAGS) -c -o $@ $<


all:$(objs)
	$(CC) -Wall -o $(TARGET) $(objs) $(LDFLAGS)


clean:
	rm -f $(objs) $(TARGET)


