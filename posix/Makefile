
CC	= gcc
CFLAGS	= -Wall -O3 -I../include -I. -I../src
LDFLAGS = -pthread

TARGET = edp.io

objs = logger.o mcache.o hset.o

objs += epoll_worker.o edp.o main.o

vpath %.c ../src ../lib


%.o:%.c
	-$(CC) $(CFLAGS) -c -o $@ $<


all:$(objs)
	$(CC) -Wall -o $(TARGET) $(objs) $(LDFLAGS)


clean:
	rm -f $(objs) $(TARGET)


