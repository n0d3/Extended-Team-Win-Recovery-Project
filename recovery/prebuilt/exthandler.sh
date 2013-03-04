#!/sbin/sh

sleep 2

# Get fs-type of mmcblk0p2
filesystem=$1
# Set fs-type in fstab
if [ -z "$filesystem" ]; then
sed -i '
/sd-ext/ {
c\
/sd-ext		auto		/dev/block/mmcblk0p2	/dev/block/mmcblk0
}
' /etc/recovery.fstab
else
sed -i '
/sd-ext/ {
c\
/sd-ext		'${filesystem}'		/dev/block/mmcblk0p2	/dev/block/mmcblk0
}
' /etc/recovery.fstab
sleep 2
# Not really needed since fstab will be rewritten from 'Update_System_Details()'
sed -i '
/sd-ext/ {
c\
/dev/block/mmcblk0p2 /sd-ext '$filesystem' rw
}
' /etc/fstab
fi
