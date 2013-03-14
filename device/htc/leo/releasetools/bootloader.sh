#!/bin/sh
#
# Copyright (C) 2012 the cmhtcleo team
#

echo "updater-script: Making Compatible Update script"
cd $REPACK/ota/META-INF/com/google/android

awk '{if (match($0,"0.200000, 10")) exit; print > "head"}' updater-script
awk '/0.200000, 10/ {p=1}; p==1 {print > "tail"}' updater-script
rm -rf updater-script

cat head > updater-script
rm -rf head

echo 'package_extract_file("checksys.sh","/tmp/checksys.sh");' >> updater-script
echo 'set_perm(0, 0, 755, "/tmp/checksys.sh");' >> updater-script
echo 'run_program("/tmp/checksys.sh");' >> updater-script
echo 'if file_getprop("/tmp/nfo.prop","clk") == "true" then' >> updater-script
echo '  ui_print("cLK detected, using ppp for data connections");' >> updater-script
echo 'else' >> updater-script
echo '  ui_print("MAGLDR detected, using rmnet for data connections");' >> updater-script
echo '  delete("/system/ppp");' >> updater-script
echo 'endif;' >> updater-script

cat tail >> updater-script
rm -rf tail

echo "updater-script: copying checksys.sh"
cp $ANDROID_BUILD_TOP/device/htc/leo/releasetools/checksys.sh $REPACK/ota