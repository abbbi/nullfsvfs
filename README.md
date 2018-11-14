# nullfsvfs
a virtual file system that behaves like /dev/null

Mostly based on the ramfs example from the linux kernel and the lwnfs file
system.

A virtual filesystem that behaves like /dev/null, but on the VFS layer of the
kernel. Existing alternatives are mostly based on FUSE which do not perform
good because of user/kernel space mapping operations.

It can handle regular file operations like mkdir/rmdir/ln but writing to files
does not store any data.

In my tests i could get up to 3.85 Gib/s so it should be fine for performance
tests that need directory operations.


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
# stat -c%s proxmox.iso 
641517568
# cp proxmox.iso /sinkhole/
# stat -c%s /sinkhole/proxmox.iso 
641517568
```

### keep data for certain files

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

Keep in mind that file data is kept in memory and no boundary checks are done,
so this might fill up your RAM in case you exclude big files from beeing
nulled.

### todos/ideas

* replace simple_statfs call with real one, show free space of a directory that
  can be passed during kernel module load
* return the size of the files with contents like /dev/zero
* support multiple parameters for write= option
* allow regex for write= option via trace.h
* use /sysfs/ instead of write= option
