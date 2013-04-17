#!/sbin/sh

# Set the minimum required size of the recovery's partition
min_size=0x008FFFFF;
# Get the real size of the existing recovery partition
rec_size=0x$(sed -n '/recovery/p' /proc/mtd | cut -d ' ' -f 2);

# Print messages function
OUTFD=$(ps | grep -v "grep" | grep -o -E "update_binary(.*)" | cut -d " " -f 3)
ui_print() {
	if [ "$OUTFD" = "" ]; then
		echo "${1}"
	else
		echo "ui_print ${1} " 1>&$OUTFD
		echo "ui_print " 1>&$OUTFD
	fi
}

# Print readable sizes
prt_rec_size=$(($rec_size / 1048576));
prt_min_size=$((($min_size + 1) / 1048576));
ui_print "  Recovery's partition size = $prt_rec_size MB"
ui_print "  Minimum required size     = $prt_min_size MB"

# Clean istall - Remove previous settings and version files
if [ -f /sdcard/TWRP/.twrps ]; then
	rm -f "/sdcard/TWRP/*twrps";
fi
if [ -f /sdcard/TWRP/.version ]; then
	rm -f "/sdcard/TWRP/*version";
fi
# Decide whether we should proceed or not
check=$(($rec_size - $min_size));
if [[ $check -gt 0 ]]; then
	exit 0;
else
	exit 1;
fi

