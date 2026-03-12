#
# Makefile for the malloc lab driver
#
CC = gcc
CFLAGS = -Wall -Wextra -O3 -g -std=gnu99 -DDRIVER -Wno-unused-function -Wno-unused-parameter -Wno-comment

# -Wno-unused-but-set-variable is supported by GCC but not Apple Clang.
# Detect and add it only if the compiler supports it.
CFLAGS += $(shell $(CC) -Wno-unused-but-set-variable -x c -c /dev/null -o /dev/null 2>/dev/null && echo -Wno-unused-but-set-variable)

OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o 

all: mdriver

mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS)

mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

clean:
	rm -f *~ *.o mdriver



