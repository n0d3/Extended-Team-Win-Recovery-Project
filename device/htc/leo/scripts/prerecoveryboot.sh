#!/sbin/sh

#############################################
# Find installed bootloader using the cmdline
#
if [ grep -Fxq "clk=" /proc/cmdline ]; then
# nothing to do in this case really
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
if [ -f "/sdcard/TWRP/theme/.use_external_p" ]; then
ui_zip=`cat /sdcard/TWRP/theme/.use_external_p`
busybox unzip -oq "$ui_zip" portrait/curtain.jpg -d /tmp
fi
if [ -f "/sdcard/TWRP/theme/.use_external_l" ]; then
ui_zip=`cat /sdcard/TWRP/theme/.use_external_l`
busybox unzip -oq "$ui_zip" landscape/curtain.jpg -d /tmp
fi
busybox umount /sdcard
