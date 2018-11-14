dmesg -c > /dev/null
umount /my/
rmmod nullfs
insmod nullfs.ko

mount -t nullfs none /my/

cp /etc/fstab /my/fstab
stat -c'%n      size: %s blocks:%b' /etc/fstab
stat -c'%n      size: %s blocks:%b' /my/fstab

mkdir /tmp/testdir 
mkdir /my/testdir
stat -c'%n      size: %s blocks:%b' /tmp/testdir
stat -c'%n      size: %s blocks:%b' /my/testdir


for FILE in `echo /my/file /tmp/file`; do
	dd if=/dev/zero of=$FILE bs=1024 count=2000>/dev/null 2>&1
	stat -c'%n	size: %s blocks:%b' $FILE
done

grep nullfs /proc/mounts
umount /my/
rmmod nullfs
insmod nullfs.ko
mount -t nullfs none /my/ -o write=fstab
grep nullfs /proc/mounts
for file in `echo fstab passwd`; do
	cp  /etc/$file /my/
	wc -l /my/$file;
done

dmesg| tail -n 5
