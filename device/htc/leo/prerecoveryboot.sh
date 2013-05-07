#!/sbin/sh

#############################################
# Find installed bootloader using the cmdline
#
if grep -Fxq "clk=" /proc/cmdline; then
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

#############################################
# Extract theme's curtain.jpg from ui.zip
#
busybox mount -t auto /dev/block/mmcblk0p1 /sdcard
if [ -f "/sdcard/TWRP/theme/.use_external" ]; then
ui_zip=`cat /sdcard/TWRP/theme/.use_external`
busybox unzip -oq "$ui_zip" images/curtain.jpg -d /tmp
fi
busybox umount /sdcard
