ROOT = .

################
# Local settings
################

# Compiler
SOLARIS_CC 	?= /opt/csw/bin/gcc
TILERA_CC	?= tile-gcc
CC ?= gcc

# Profile
ifeq ($(VERSION),DEBUG)
     CFLAGS	+= -g -DDEBUG -O0
else
     CFLAGS	+= -O3 -DNDEBUG 
endif

BINDIR		?= $(ROOT)/bin
BUILDIR		?= $(ROOT)/build

$(shell [ -d "$(BUILDIR)" ] || mkdir -p $(BUILDIR))
$(shell [ -d "$(BINDIR)" ] || mkdir -p $(BINDIR))

LIBAO_INC = $(ROOT)/atomic_ops

#############################
# Platform dependent settings
#############################
#
# GCC thread-local storage requires "significant 
# support from the linker (ld), dynamic linker
# (ld.so), and system libraries (libc.so and libpthread.so), so it is
# not available everywhere." source: GCC-doc.
#
# pthread_spinlock is replaced by pthread_mutex 
# on MacOS X, as it might not be supported. 
# Comment LOCK = MUTEX below to enable.

ifndef OS_NAME
    OS_NAME = $(shell uname -s)
endif

ifeq ($(OS_NAME), Darwin)
    OS = MacOS
    DEFINES += -UTLS
    LOCK = MUTEX
endif

ifeq ($(OS_NAME), Linux)
    OS = Linux
    DEFINES += -DTLS
endif

ifeq ($(OS_NAME), SunOS)
    OS = Solaris
    CC = $(SOLARIS_CC)
    DEFINES += -DTLS
endif

#################################
# Architecture dependent settings
#################################

ifndef ARCH
    ARCH_NAME = $(shell uname -m)
endif

ifeq ($(ARCH_NAME), i386)
    ARCH = x86
    CFLAGS += -m32
    LDFLAGS += -m32
endif

ifeq ($(ARCH_NAME), i686)
    ARCH = x86
    CFLAGS += -m32
    LDFLAGS += -m32
endif

ifeq ($(ARCH_NAME), x86_64)
    ARCH = x86_64
    CFLAGS += -m64
    LDFLAGS += -m64
endif

ifeq ($(ARCH_NAME), sun4v)
    ARCH = sparc64
    CFLAGS += -DSPARC=1 -DINLINED=1 -m64
    LDFLAGS += -lrt -m64
endif

ifeq ($(PLATFORM_NUMA), 1)
    LDFLAGS += -lnuma
endif


#################
# Global settings
#################

CFLAGS += -D_REENTRANT
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DLOCKFREE
CFLAGS += -Wall
CFLAGS += -fno-strict-aliasing
CFLAGS += -I$(LIBAO_INC) -I$(ROOT)/include

#LDFLAGS += -L$(LIBAO)/lib -latomic_ops 
LDFLAGS += -lpthread -lrt -lm

LINDENFLAGS = -DCACHE_LINE_SIZE=`getconf LEVEL1_DCACHE_LINESIZE` -DINTEL

BINS = $(BINDIR)/spray

.PHONY:	all clean

all:	spray

measurements.o:
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/measurements.o measurements.c

ssalloc.o:
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/ssalloc.o ssalloc.c

skiplist.o:
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/skiplist.o skiplist.c

fraser.o: skiplist.h 
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/fraser.o fraser.c 

linden.o: linden.h
	$(CC) $(CFLAGS) $(LINDENFLAGS) -c -o $(BUILDIR)/linden.o linden.c

linden_common.o: linden_common.h
	$(CC) $(CFLAGS) $(LINDENFLAGS) -c -o $(BUILDIR)/linden_common.o linden_common.c

gc.o: gc/gc.h
	$(CC) $(CFLAGS) $(LINDENFLAGS) -c -o $(BUILDIR)/gc.o gc/gc.c

ptst.o: gc/ptst.h
	$(CC) $(CFLAGS) $(LINDENFLAGS) -c -o $(BUILDIR)/ptst.o gc/ptst.c

intset.o: skiplist.h fraser.h
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/intset.o intset.c

test.o: skiplist.h fraser.h intset.h
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/test.o test.c

pqueue.o: skiplist.h intset.h
	$(CC) $(CFLAGS) -c -o $(BUILDIR)/pqueue.o pqueue.c

spray: measurements.o ssalloc.o skiplist.o fraser.o intset.o test.o pqueue.o linden.o linden_common.o gc.o ptst.o
	$(CC) $(CFLAGS) $(BUILDIR)/pqueue.o $(BUILDIR)/measurements.o $(BUILDIR)/ssalloc.o $(BUILDIR)/skiplist.o $(BUILDIR)/fraser.o $(BUILDIR)/intset.o $(BUILDIR)/test.o $(BUILDIR)/linden.o $(BUILDIR)/linden_common.o $(BUILDIR)/ptst.o $(BUILDIR)/gc.o -o $(BINS) $(LDFLAGS)

clean:
	-rm -f $(BINS)
