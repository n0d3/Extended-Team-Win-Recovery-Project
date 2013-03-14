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
