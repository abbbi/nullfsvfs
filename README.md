[![nullfsvfs CI on ubuntu-latest](https://github.com/abbbi/nullfsvfs/actions/workflows/ci-ubuntu-latest.yml/badge.svg?branch=master)](https://github.com/abbbi/nullfsvfs/actions/workflows/ci-ubuntu-latest.yml)

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->

- [nullfs](#nullfs)
  - [installation](#installation)
    - [manual installation](#manual-installation)
    - [debian package](#debian-package)
  - [Usage](#usage)
    - [Keeping file data](#keeping-file-data)
    - [ACL](#acl)
    - [usecases](#usecases)
    - [supported mount options](#supported-mount-options)
    - [todos/ideas](#todosideas)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# nullfs
A virtual file system that behaves like /dev/null

It can handle regular file operations like mkdir/rmdir/ln but writing to files
does not store any data. The file size is however saved, reading from the
files behaves like reading from /dev/zero.

Writing and reading is basically an NOOP, so it can be used for performance
testing with applications that require directory structures.  Implemented as
kernel module, instead of using FUSE, there is absolutely no overhead for
copying application data from user to kernel space while performing write or
read operations.

![alt text](https://github.com/abbbi/nullfsvfs/raw/master/nullfs.jpg)

```
# sudo make
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
# pv < /dev/zero > /sinkhole/testdir/myfile
11.1GiB 0:00:04 [3.85GiB/s] [     <=>      ] 
# cat /sinkhole/testdir/myfile
# 
```

File size is preserved to work around applications that do size checks:

```
# dd if=/dev/zero of=/nullfs/DATA bs=1M count=20
20+0 records in
20+0 records out
20971520 bytes (21 MB, 20 MiB) copied, 0.00392452 s, 5.3 GB/s
# stat -c%s /nullfs/DATA
20971520
```

Reading from the files does not copy anything to userspace and is an NOOP;
makes it behave like reading from /dev/zero:

```
# dd if=/nullfs/DATA of=/tmp/REALFILE
40960+0 records in
40960+0 records out
20971520 bytes (21 MB, 20 MiB) copied, 0.0455288 s, 461 MB/s
# hexdump -C /tmp/REALFILE
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```


## installation

### manual installation

To install the module for the running linux kernel use:

```
 # make -C /lib/modules/$(uname -r)/build M=$PWD modules_install INSTALL_MOD_DIR=kernel/fs/nullfs
 # depmod
```

Running `depmod` is mandatory. Now the module can be loaded via:

```
 # modprobe nullfs
``` 

To automatically load the module during system boot, create a configuration
file suitable for your distribution (usually located in */etc/modules-load.d*):

```
 # echo nullfs > /etc/modules-load.d/nullfs.conf
```

Example entry for `/etc/fstab`, mounting the filesystem to `/nullfs`:


```
none    /nullfs nullfs auto
```

### debian package

```
 # apt-get install debhelper dkms dh-dkms
 # dpkg-buildpackage -uc -us -rfakeroot
 # dpkg -i ../nullfsvfs_<version>_amd64.deb
```

## Usage
### Keeping file data

There is the possibility to exclude certain files from being sent into the
void.  If, for example, the file matching "fstab" should be kept in memory,
mount nullfs with the "write=" option.

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
 # echo foo  > /sys/fs/nullfs/exclude
```

Keep in mind that file data is kept in memory and no boundary checks are done,
so this might fill up your RAM in case you exclude big files from being
nulled.

### ACL

It is possible to set POSIX ACL attributes via `setfacl` so it appears the
filesystem supports them, they are not saved.

Works with recent linux kernels (5.x), nullfs builds fine with older kernels
(4.x, 3.x) but setting ACL information fails with "Operation not supported".

### usecases

See: [Use Cases ](https://github.com/abbbi/nullfsvfs/labels/Usecase)

The module has been used for performance testing with redis, see:

 https://www.usenix.org/system/files/atc20-george.pdf

Please report usecases as regular issues!

### supported mount options

The following mount options are supported:
```
 -o mode=      set permissions on mount directory ( mount .. -o mode=777 )
 -o uid=       set uid on mount directory ( mount .. -o uid=1000 )
 -o gid=       set gid on mount directory ( mount .. -o gid=1000 )
 -o write=fn   keep data for specific file ( mount .. -o write=fstab )
```

### todos/ideas

* replace simple_statfs call with real one, show free space of a directory that
  can be passed during kernel module load
* support multiple parameters for write= option
* allow regex for write= option via trace.h
* simulate xattr support?
