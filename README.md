**Extended TWRP for HD2**

This branch is based on original TWRP. [More information about the project.](http://www.teamw.in/project/twrp2 "More Information")

The primary goal is to extend the recovery's functionality in order to meet the needs of The HD2: a device which has 3 different android loaders (cLK, MAGLDR and Haret) and various Android Rom configurations (DataOnExt, NativeSD, SD, Nand).

Apart from that, any logical and possible feature that can be added to a recovery
will be implemented along the way.

Extended Features:
* Proper detection of bootloader(cLK/MAGLDR/haret)
* Support for cLK's extra boot partitions
* Tweaked off-mode charging for cLK (device can wake up by pressing any key)
* Direct rebooting to selected boot partition for cLK bootloader
* Direct rebooting with selected kernel from NativeSD folder for MAGLDR bootloader
* Ability to communicate with cLK in order to change partitions' size if needed
* Built-in NativeSD manager(Backup - Restore - Delete - Fix Permissions - Wipe Data - Wipe Dalvik-Cache - [cLK]Kernel-Restore)
* Option to skip any NativeSD Rom during sd-ext's partition backup
* Option to adjust backup/restore process for DataOnExt method
*
* Nilfs2 support for sdcard's ext(2nd/3rd primary) partition
* NTFS support for sdcard's 1st primary partition
* Option for converting file system [ext2 - ext3 - ext4 - nilfs2] of sdcard's ext partition (without losing any data if there is enough space on the /sdcard)
* Option for adding a 3rd primary partition(mmcblk0p3 as /sdext2)
* Option to skip dalvik-cache during backup
* Ability to restore backups that were made using a CWM Recovery
* Ability to check SD Card's filesystem(s)
* Ability to run shell scripts from your SD Card (script location: /sdcard/TWRP/scripts)
* Ability to "run" recovery (AROMA based) apps with one click (app location: /sdcard/TWRP/app)
* Ability to select current theme (example of theme file location /sdcard/TWRP/theme/MyTheme/ui.zip)
* Ability to check the size of the backup to be restored
* Ability to take screenshot (screenshots location: /sdcard/TWRP/screenshots)
* Configurable haptic feedback
* Configurable system tweaks (cpu gov, cpu freq, i/o sched, drop_caches)
