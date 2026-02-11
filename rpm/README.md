RPM Build

Examples how to build rpm package from source.

### RHEL 8.4 
```
 yum install git rpm-build elfutils-libelf-devel kernel-abi-stablelists kernel-rpm-macros -y
 git clone http://github.com/abbbi/nullfsvfs nullfsvfs-0.12
 tar -czvf /root/rpmbuild/SOURCES/nullfsvfs-kmod-0.12.tar.gz nullfsvfs-0.12
 rpmbuild -ba nullfsvfs-0.12/rpm/nullfsvfs-kmod.spec
 rpm -i /root/rpmbuild/RPMS/x86_64/kmod-nullfsvfs-0.12-1.el8.x86_64.rpm
 modprobe nullfsvfs
```
