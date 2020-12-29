[![Build Status](https://travis-ci.org/abbbi/nullfsvfs.svg?branch=master)](https://travis-ci.org/abbbi/nullfsvfs)

## Index

 - [About](#nullfsvfs)
 - [Usage](#usage)
 - [Use Cases](#usecases)
 - [Keeping data](#keep)
 - [Mount options](#supported)

# nullfsvfs
a virtual file system that behaves like /dev/null

It can handle regular file operations like mkdir/rmdir/ln but writing to files
does not store any data. The file size is however saved, so reading from the
files behaves like reading from /dev/zero with a fixed size.

Writing and reading is basically an NOOP, so it can be used for performance
testing with applications that require directory structures. As it is
implemented as kernel module, instead of using FUSE, there is absolutely no
overhead for copying application data from user to kernel space while
performing write or read operations.

![alt text](https://github.com/abbbi/nullfsvfs/raw/master/nullfs.jpg)

### usage
```
# make
make -C /lib/modules/4.18.5/build M=/home/abi/lwnfs modules
make[1]: Entering directory '/usr/src/linux-headers-4.18.5'
  Building modules, stage 2.
  MODPOST 1 modules
make[1]: Leaving directory '/usr/src/linux-headers-4.18.5'

# insmod nullfs.ko 
# mkdir /sinkhole
# mount -t nullfs none /sinkhole/
# mkdir /sinkhole/testdir
# touch /sinkhole/testdir/myfile
# echo foobar > /sinkhole/testdir/myfile
# ls -lah /sinkhole/testdir/myfile
-rw-r--r-- 1 root root 0 Nov  8 20:17 /sinkhole/testdir/myfile
# cat /sinkhole/testdir/myfile
# cat /dev/zero | pv > /sinkhole/testdir/myfile
11.1GiB 0:00:04 [3.85GiB/s] [     <=>      ] 
# cat /sinkhole/testdir/myfile
# 
```

File size is preserved to work around applications that do size checks:

```
# dd if=/dev/zero of=/nullfs/DATA bs=1M count=20
# 20+0 records in
# 20+0 records out
# 20971520 bytes (21 MB, 20 MiB) copied, 0.00392452 s, 5.3 GB/s
# stat -c%s /nullfs/DATA
# 20971520
```

Reading from the files does not copy anything to userspace and is an NOOP;
makes it behave like reading from /dev/zero:

```
# dd if=/nullfs/DATA of=/tmp/REALFILE
# 40960+0 records in
# 40960+0 records out
# 20971520 bytes (21 MB, 20 MiB) copied, 0.0455288 s, 461 MB/s
# hexdump -C /tmp/REALFILE
# 00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```


### keep

There is the possiblity to exclude certain files from beeing sent into the void.
For example if the file matching "fstab" should be kept in memory, one can mount
nullfs with the "write=" option. 

```
# mount -t nullfs none /sinkhole/ -o write=fstab
# cp /etc/fstab /sinkhole/
# wc -l /sinkhole/fstab 
14 /sinkhole/fstab
# cp /etc/passwd /sinkhole/
# wc -l /sinkhole/passwd 
0 /sinkhole/passwd
```

Another option is using the sysfs interface to change the exclude string
after the module has been loaded:

```
 echo foo  > /sys/fs/nullfs/exclude 
```

Keep in mind that file data is kept in memory and no boundary checks are done,
so this might fill up your RAM in case you exclude big files from beeing
nulled.

### usecases

See: [Use Cases ](https://github.com/abbbi/nullfsvfs/labels/Usecase)

The module has been used for performance testing with redis, see:

 https://www.usenix.org/system/files/atc20-george.pdf

### supported

The following mount options are supported:
```
 -o mode=      set permissions on mount directory ( mount .. -o mode=777 )
 -o uid=       set uid on mount directory ( mount .. -o uid=1000 )
 -o gid=       set gid on mount directory ( mount .. -o gid=1000 )
 
```

### todos/ideas

* replace simple_statfs call with real one, show free space of a directory that
  can be passed during kernel module load
* return the size of the files with contents like /dev/zero
* support multiple parameters for write= option
* allow regex for write= option via trace.h
