**Extended TWRP for HD2**

This branch is based on original TWRP. [More information about the project.](http://www.teamw.in/project/twrp2 "More Information")

The primary goal is to extend the recovery's functionality in order to meet the needs of The HD2: a device which has 3 different android loaders (cLK, MAGLDR and Haret) and various Android Rom configurations (DataOnExt, NativeSD, SD, Nand).

Apart from that, any logical and possible feature that can be added to a recovery
will be implemented along the way.

Extended Features:
* Proper detection of bootloader(cLK/MAGLDR/Haret)
* Nilfs2 support for sdcard's ext partition
* Option for converting file system [ext2 - ext3 - ext4 - nilfs2] of sdcard's ext partition without losing any data
* Option to wipe Data, Boot and cLK's sBoot
* Built-in NativeSD manager(Backup - Restore - Delete - Fix Permissions - Wipe Data - Wipe Dalvik-Cache - [cLK]Kernel-Restore)
* Option for adding a 2nd ext partition(mmcblk0p3 as /sdext2)
* Direct rebooting to selected boot partition for cLK bootloader
* Option to adjust the backup process for DataOnExt method
* Option to skip dalvik-cache during backup
* Option to skip existing NativeSD installations during backup
* Ability to restore backups that were made using a CWM Recovery
* Ability to take a screenshot by simply touching the top header/title
* Ability to check SD Card's filesystem(s)
* Ability to run shell scripts from the SD Card
