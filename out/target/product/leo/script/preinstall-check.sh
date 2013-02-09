#!/sbin/sh

if grep -Fxq "clk=" /proc/cmdline
then
	if [ "`which clkpartmgr`" ]; then
		ZIP_NAME=HD2_Extended_TWRP_2.4.1.0.r1.zip
		PART1_NAME=recovery
		PART1_MINSIZE=6
		############################################################################
		# clkpartmgr is a simple cmdline tool to get TWRP and cLK		   #
		# communicate in case we need to change the p-table layout.		   #
		#									   #
		# Usage: clkpartmgr [OPTIONAL:<zip-name>] <ptn-name:min-size>		   #
		#									   #
		# Example:								   #
		#	  If we want to install a Rom package called 'CM10_v1.zip'	   #
		#	  and the minimum required sizes of 'boot' and 'system'		   #
		#	  partitions are 5MB and 260MB respectively, then 		   #
		#	  clkpartmgr could be called when installing this Rom		   #
		#	  to make sure that the sizes of those partitions are adequate:	   #
		#		"clkpartmgr CM10.1_v1.zip boot:5 system:260"		   #
		#	  In case a partition is smaller than the required value	   #
		#	  the p-table needs to be changed, so clkpartmgr automatically	   #
		#	  reboots the device into cLK, where the changes are applied,	   #
		#	  and re-enters recovery to continue installing the 'CM10_v1.zip'. #
		#									   #
		# Notes:								   #
		#	  *if size_of_partition >= min_size => No change to p-table.	   #
		#	  *if the name of the zip is not given then after changing	   #
		#	   the p-table layout, cLK will boot into recovery but		   #
		#	   the installation of the zip will not automatically continue.	   #
		############################################################################
		clkpartmgr $ZIP_NAME $PART1_NAME:$PART1_MINSIZE
		exit 0;
	fi
fi

# Set the minimum required size of the recovery's partition
min_size=0x005FFFFF;
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

