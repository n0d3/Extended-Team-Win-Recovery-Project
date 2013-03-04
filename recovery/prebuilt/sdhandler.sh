#!/sbin/sh

sleep 2

# Get fs-type of mmcblk0p1
filesystem=$1
# Change /sdcard line in recovery.fstab
sed -i '
/sdcard/ {
c\
/sdcard		'${filesystem}'		/dev/block/mmcblk0p1	/dev/block/mmcblk0
}
' /etc/recovery.fstab
sleep 2
# Not really needed since fstab will be rewritten from 'Update_System_Details()'
sed -i '
/sdcard/ {
c\
/dev/block/mmcblk0p1 /sdcard '$filesystem' rw
}
' /etc/fstab

