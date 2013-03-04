#!/sbin/sh

sleep 2

if grep -Fxq "clk=" /proc/cmdline
then
sed -i '
/\/boot/ {
c\
/boot		mtd		boot
}
' /etc/recovery.fstab
else
sed -i '
/\/boot/ {
c\
/boot		yaffs2		boot
}
' /etc/recovery.fstab
fi

sleep 2

# /sdcard check
if [ -b "/dev/block/mmcblk0p1" ]
then

# Get fs-type of mmcblk0p1
filesystem=`busybox blkid /dev/block/mmcblk0p1 | rev | cut -d '"' -f 2 | rev`
# Set fs-type in fstab
if [[ ! -z "$filesystem" ]]; then
sed -i '
/sdcard/ {
c\
/sdcard		'${filesystem}'		/dev/block/mmcblk0p1	/dev/block/mmcblk0
}
' /etc/recovery.fstab
fi

fi

sleep 2

# /sd-ext check
if [ -b "/dev/block/mmcblk0p2" ]
then

# Get fs-type of mmcblk0p2
filesystem=`busybox blkid /dev/block/mmcblk0p2 | rev | cut -d '"' -f 2 | rev`
# Set fs-type in fstab
if [[ -z "$filesystem" ]]; then
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
fi

else

sed -i '
/sd-ext/ {
c\
/sd-ext		auto		/dev/block/mmcblk0p2	/dev/block/mmcblk0
}
' /etc/recovery.fstab

fi

sleep 2

# /sdext2 check
if [ -b "/dev/block/mmcblk0p3" ]
then

# Get fs-type of mmcblk0p3
filesystem=`busybox blkid /dev/block/mmcblk0p3 | rev | cut -d '"' -f 2 | rev`
# Set fs-type in fstab
if [[ -z "$filesystem" ]]; then
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
fi

else

sed -i '
/sdext2/ {
c\
/sdext2		auto		/dev/block/mmcblk0p3	/dev/block/mmcblk0
}
' /etc/recovery.fstab

fi
