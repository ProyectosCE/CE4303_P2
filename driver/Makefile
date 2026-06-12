obj-m += mi_driver.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC ?= gcc
AR ?= ar
CFLAGS := -Wall -Wextra -O2

LIB_NAME := libpovdriver.a
LIB_OBJS := pov_driver_lib.o

all: module $(LIB_NAME) pov_send_demo pov_send_text_demo receptor_pico

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

$(LIB_NAME): $(LIB_OBJS)
	$(AR) rcs $(LIB_NAME) $(LIB_OBJS)

pov_driver_lib.o: pov_driver_lib.c pov_driver_lib.h
	$(CC) $(CFLAGS) -c pov_driver_lib.c -o pov_driver_lib.o

pov_send_demo: pov_send_demo.c $(LIB_NAME) pov_driver_lib.h
	$(CC) $(CFLAGS) pov_send_demo.c -L. -lpovdriver -o pov_send_demo

pov_send_text_demo: pov_send_text_demo.c $(LIB_NAME) pov_driver_lib.h
	$(CC) $(CFLAGS) pov_send_text_demo.c -L. -lpovdriver -o pov_send_text_demo

receptor_pico: receptor.c
	$(CC) $(CFLAGS) receptor.c -o receptor_pico

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.a pov_send_demo pov_send_text_demo receptor_pico