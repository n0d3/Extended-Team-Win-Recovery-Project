#!/sbin/sh

sleep 2

# Get fs-type of mmcblk0p3
filesystem=$1
# Set fs-type in fstab
if [ -z "$filesystem" ]; then
sed -i '
/sdext2/ {
c\
/sdext2		auto		/dev/block/mmcblk0p3	/dev/block/mmcblk0
}
' /etc/recovery.fstab
else
sed -i '
/sdext2/ {
c\
/sdext2		'${filesystem}'		/dev/block/mmcblk0p3	/dev/block/mmcblk0
}
' /etc/recovery.fstab
sleep 2
# Not really needed since fstab will be rewritten from 'Update_System_Details()'
if grep -Fxq "/sdext2" /etc/fstab
then
sed -i '
/sdext2/ {
c\
/dev/block/mmcblk0p3 /sdext2 '$filesystem' rw
}
' /etc/fstab
else
sed -i '
/sd-ext/ {
a\
/dev/block/mmcblk0p3 /sdext2 '$filesystem' rw
}
' /etc/fstab
fi

fi
