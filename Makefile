obj-m := nullfs.o

ifdef WITH_NFS
CFLAGS_nullfs.o := -DWITH_NFS=1
endif

all: ko
ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
