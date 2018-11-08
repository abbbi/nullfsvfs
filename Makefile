obj-m := nullfs.o
lwn-objs := nullfs.o

all: ko
ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
