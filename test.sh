umount /my/
rmmod nullfs
insmod nullfs.ko

mount -t nullfs none /my/

for FILE in `echo /my/file /tmp/file`; do
	dd if=/dev/zero of=$FILE bs=1024 count=2000>/dev/null 2>&1
	stat -c'%n	size: %s blocks:%b' $FILE
done
