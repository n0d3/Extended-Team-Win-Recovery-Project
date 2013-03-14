#!/bin/sh

# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

DEVICE=leo
MANUFACTURER=htc
STAGESYS=~/cm-$DEVICE/system

if [ "$1" = "pull" ]; then
  if [ -s "$STAGESYS" ]; then
  rm -R ~/cm-$DEVICE
  fi
 mkdir ~/cm-$DEVICE
 mkdir $STAGESYS
 mkdir $STAGESYS/bin
 mkdir $STAGESYS/lib
 mkdir $STAGESYS/etc
 adb pull /system/bin $STAGESYS/bin
 adb pull /system/lib $STAGESYS/lib
 adb pull /system/etc $STAGESYS/etc
fi

OUTDIR=../../../vendor/$MANUFACTURER/$DEVICE
BASE=$OUTDIR/proprietary
rm -rf $BASE/*

for FILE in `cat proprietary-files.txt | grep -v ^# | grep -v ^$`; do
    DIR=`dirname $FILE`
    if [ ! -d $BASE/$DIR ]; then
        mkdir -p $BASE/$DIR
    fi
    cp $STAGESYS/$FILE $BASE/$FILE
done

./setup-makefiles.sh
