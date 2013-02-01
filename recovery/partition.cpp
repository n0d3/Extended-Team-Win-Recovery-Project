/* Partition class for TWRP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <sstream>

#ifdef TW_INCLUDE_CRYPTO
	#include "cutils/properties.h"
#endif

#include "variables.h"
#include "common.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#ifdef TW_INCLUDE_LIBTAR
	#include "twrpTar.hpp"
#else
	#include "makelist.hpp"
#endif
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
	#include "../../external/yaffs2/yaffs2/utils/unyaffs.h"
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	#include "crypto/libcrypt_samsung/include/libcrypt_samsung.h"
#endif
}

using namespace std;

TWPartition::TWPartition(void) {
	Can_Be_Mounted = false;
	Can_Be_Wiped = false;
	Wipe_During_Factory_Reset = false;
	Wipe_Available_in_GUI = false;
	Is_SubPartition = false;
	Has_SubPartition = false;
	SubPartition_Of = "";
	Symlink_Path = "";
	Symlink_Mount_Point = "";
	Mount_Point = "";
	Backup_Path = "";
	Use_unyaffs_To_Restore = false;
	Path_For_DataOnExt = "";
	Actual_Block_Device = "";
	Primary_Block_Device = "";
	Alternate_Block_Device = "";
	Removable = false;
	Is_Present = false;
	Length = 0;
	Size = 0;
	Used = 0;
	Free = 0;
	Backup_Size = 0;
	Dalvik_Cache_Size = 0;
	NativeSD_Size = 0;
	Tar_exclude = "";
	Can_Be_Encrypted = false;
	Is_Encrypted = false;
	Is_Decrypted = false;
	Decrypted_Block_Device = "";
	Display_Name = "";
	Backup_Name = "";
	Backup_FileName = "";
	MTD_Name = "";
	Backup_Method = NONE;
	Has_Data_Media = false;
	Has_Android_Secure = false;
	Is_Storage = false;
	Storage_Path = "";
	Current_File_System = "";
	Fstab_File_System = "";
	Format_Block_Size = 0;
	Ignore_Blkid = false;
	Retain_Layout_Version = false;
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	EcryptFS_Password = "";
#endif
}

TWPartition::~TWPartition(void) {
	// Do nothing
}

/************************************************************************************
 * Process each line from recovery.fstab and create a TWPartition
 */
bool TWPartition::Process_Fstab_Line(string Line, bool Display_Error) {
	char full_line[MAX_FSTAB_LINE_LENGTH], item[MAX_FSTAB_LINE_LENGTH];
	int line_len = Line.size(), index = 0, item_index = 0;
	char* ptr;
	string Flags;
	strncpy(full_line, Line.c_str(), line_len);

	for (index = 0; index < line_len; index++) {
		if (full_line[index] <= 32)
			full_line[index] = '\0';
	}
	Mount_Point = full_line;
	LOGI("Processing '%s'\n", Mount_Point.c_str());
	Backup_Path = Mount_Point;
	index = Mount_Point.size();
	while (index < line_len) {
		while (index < line_len && full_line[index] == '\0')
			index++;
		if (index >= line_len)
			continue;
		ptr = full_line + index;
		if (item_index == 0) {
			// File System
			Fstab_File_System = ptr;
			Current_File_System = ptr;
			item_index++;
		} else if (item_index == 1) {
			// Primary Block Device
			if (Fstab_File_System == "mtd" || Fstab_File_System == "yaffs2") {
				MTD_Name = ptr;
				Find_MTD_Block_Device(MTD_Name);
			} else if (Fstab_File_System == "bml") {
				if (Mount_Point == "/boot")
					MTD_Name = "boot";
				else if (Mount_Point == "/recovery")
					MTD_Name = "recovery";
				Primary_Block_Device = ptr;
				if (*ptr != '/')
					LOGE("Until we get better BML support, you will have to find and provide the full block device path to the BML devices e.g. /dev/block/bml9 instead of the partition name\n");
			} else if (*ptr != '/') {
				if (Display_Error)
					LOGE("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				else
					LOGI("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				return 0;
			} else {
				Primary_Block_Device = ptr;
				Find_Real_Block_Device(Primary_Block_Device, Display_Error);
			}
			item_index++;
		} else if (item_index > 1) {
			if (*ptr == '/') {
				// Alternate Block Device
				Alternate_Block_Device = ptr;
				Find_Real_Block_Device(Alternate_Block_Device, Display_Error);
			} else if (strlen(ptr) > 7 && strncmp(ptr, "length=", 7) == 0) {
				// Partition length
				ptr += 7;
				Length = atoi(ptr);
			} else if (strlen(ptr) > 6 && strncmp(ptr, "flags=", 6) == 0) {
				// Custom flags, save for later so that new values aren't overwritten by defaults
				ptr += 6;
				Flags = ptr;
				Process_Flags(Flags, Display_Error);
			} else if (strlen(ptr) == 4 && (strncmp(ptr, "NULL", 4) == 0 || strncmp(ptr, "null", 4) == 0 || strncmp(ptr, "null", 4) == 0)) {
				// Do nothing
			} else {
				// Unhandled data
				LOGI("Unhandled fstab information: '%s', %i, line: '%s'\n", ptr, index, Line.c_str());
			}
		}
		while (index < line_len && full_line[index] != '\0')
			index++;
	}

	if (!Is_File_System(Fstab_File_System) && !Is_Image(Fstab_File_System) && !Is_Swap(Fstab_File_System)) {
		if (Display_Error)
			LOGE("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		else
			LOGI("Unknown File System: '%s'\n", Fstab_File_System.c_str());
		return 0;
	} else if (Is_File_System(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_File_System(Display_Error);
		if (Mount_Point == "/system") {
			if (Find_Partition_Size()) {
				Display_Name = "System";
				Wipe_Available_in_GUI = true;
				Check_BuildProp();
			} else {
				Is_Present = false;
				Wipe_During_Factory_Reset = false;
				Wipe_Available_in_GUI = false;
				Removable = false;
				Can_Be_Mounted = false;
				Can_Be_Wiped = false;	
			}
		} else if (Mount_Point == "/data") {
			if (Find_Partition_Size()) {
				Display_Name = "Data";
				Wipe_Available_in_GUI = true;
				Wipe_During_Factory_Reset = true;
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /data
#ifdef RECOVERY_SDCARD_ON_DATA
				Has_Data_Media = true;
				Is_Storage = true;
				Storage_Path = "/data/media";
				Symlink_Path = Storage_Path;
				if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
					Make_Dir("/emmc", Display_Error);
					Symlink_Mount_Point = "/emmc";
				} else {
					Make_Dir("/sdcard", Display_Error);
					Symlink_Mount_Point = "/sdcard";
				}
				if (Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
					Storage_Path = "/data/media/0";
					Symlink_Path = Storage_Path;
					DataManager::SetValue(TW_INTERNAL_PATH, "/data/media/0");
					UnMount(true);
				}
#endif
#ifdef TW_INCLUDE_CRYPTO
			Can_Be_Encrypted = true;
			char crypto_blkdev[255];
			property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
			if (strcmp(crypto_blkdev, "error") != 0) {
				DataManager::SetValue(TW_DATA_BLK_DEVICE, Primary_Block_Device);
				DataManager::SetValue(TW_IS_DECRYPTED, 1);
				Is_Encrypted = true;
				Is_Decrypted = true;
				Decrypted_Block_Device = crypto_blkdev;
				LOGI("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
			} else if (!Mount(false)) {
				Is_Encrypted = true;
				Is_Decrypted = false;
				Can_Be_Mounted = false;
				Current_File_System = "emmc";
				Setup_Image(Display_Error);
				DataManager::SetValue(TW_IS_ENCRYPTED, 1);
				DataManager::SetValue(TW_CRYPTO_PASSWORD, "");
				DataManager::SetValue("tw_crypto_display", "");
			} else {
				// Filesystem is not encrypted and the mount
				// succeeded, so get it back to the original
				// unmounted state
				UnMount(false);
			}
	#ifdef RECOVERY_SDCARD_ON_DATA
			if (!Is_Encrypted || (Is_Encrypted && Is_Decrypted))
				Recreate_Media_Folder();
	#endif
#else
	#ifdef RECOVERY_SDCARD_ON_DATA
			Recreate_Media_Folder();
	#endif
#endif
			} else {
				Is_Present = false;
				Wipe_During_Factory_Reset = false;
				Wipe_Available_in_GUI = false;
				Removable = false;
				Can_Be_Mounted = false;
				Can_Be_Wiped = false;				
			}
		} else if (Mount_Point == "/cache") {
			Display_Name = "Cache";
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
			if (Mount(false) && !TWFunc::Path_Exists("/cache/recovery/.")) {
				LOGI("Recreating /cache/recovery folder.\n");
				if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0) 
					return -1;
			}
			CheckFor_Dalvik_Cache(); // check for dalvik-cache in /cache (Is this really needed?)
		} else if (Mount_Point == "/datadata") {
			Wipe_During_Factory_Reset = true;
			Display_Name = "DataData";
			Is_SubPartition = true;
			SubPartition_Of = "/data";
			DataManager::SetValue(TW_HAS_DATADATA, 1);
		} else if (Mount_Point == "/sd-ext") {
			if (Find_Partition_Size()) {
				Wipe_During_Factory_Reset = true;
				Display_Name = "SD-Ext";
				Wipe_Available_in_GUI = true;
				Removable = true;
				DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
				DataManager::SetValue(TW_SDEXT_SIZE, (int)(Size / 1048576));
				CheckFor_DataOnExt(); // check if data_path is valid and try to fix otherwise
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /sd-ext
				CheckFor_NativeSD(); // check for NativeSD installations in /sd-ext
				DataManager::GetValue(TW_DATA_PATH, Path_For_DataOnExt); // save TW_DATA_PATH to Path_For_DataOnExt
			} else {
				Is_Present = false;
				Wipe_During_Factory_Reset = false;
				Wipe_Available_in_GUI = false;
				Removable = false;
				Can_Be_Mounted = false;
				Can_Be_Wiped = false;
				DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
				DataManager::SetValue(TW_SDEXT_SIZE, 0);
				DataManager::SetValue(TW_DATA_ON_EXT, 0);
				DataManager::SetValue(TW_SKIP_NATIVESD, 0);
				DataManager::SetValue(TW_DATA_ON_EXT_CHECK, 0);
			}
		} else if (Mount_Point == "/sdext2") {
			if (Find_Partition_Size()) {
				Wipe_During_Factory_Reset = true;
				Display_Name = "SDExt2";
				Wipe_Available_in_GUI = true;
				Removable = true;
				DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
				DataManager::SetValue(TW_SDEXT2_SIZE, (int)(Size / 1048576));
			} else {
				Is_Present = false;
				Wipe_During_Factory_Reset = false;
				Wipe_Available_in_GUI = false;
				Removable = false;
				Can_Be_Mounted = false;
				Can_Be_Wiped = false;
				DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
				DataManager::SetValue(TW_SDEXT2_SIZE, 0);
			}
		} else if (Mount_Point == "/boot") {
			Display_Name = "Boot";
			DataManager::SetValue("tw_boot_is_mountable", 1);
		}
#ifdef TW_EXTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Storage_Path = EXPAND(TW_EXTERNAL_STORAGE_PATH);
			Removable = true;
		}
#else
		if (Mount_Point == "/sdcard") {
			if (Find_Partition_Size()) {
				Is_Storage = true;
				Storage_Path = "/sdcard";
				Removable = true;
				DataManager::SetValue(TW_HAS_EXTERNAL, 1);
	#ifndef RECOVERY_SDCARD_ON_DATA
				Setup_AndSec();
				Mount_Storage_Retry();
	#endif
			} else {
				Is_Present = false;
				Is_Storage = false;
				Removable = false;
				DataManager::SetValue(TW_HAS_EXTERNAL, 0);
			}
		}
#endif
#ifdef TW_INTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_INTERNAL_STORAGE_PATH)) {
			Is_Storage = true;
			Storage_Path = EXPAND(TW_INTERNAL_STORAGE_PATH);
#ifndef RECOVERY_SDCARD_ON_DATA
			Setup_AndSec();
			Mount_Storage_Retry();
#endif
		}
#else
		if (Mount_Point == "/emmc") {
			Is_Storage = true;
			Storage_Path = "/emmc";
#ifndef RECOVERY_SDCARD_ON_DATA
			Setup_AndSec();
			Mount_Storage_Retry();
#endif
		}
#endif
	} else if (Is_Image(Fstab_File_System)) {
		Find_Actual_Block_Device();
		Setup_Image(Display_Error);
		if (Mount_Point == "/boot") {
			DataManager::SetValue("tw_boot_is_mountable", 0);
		}
	} else if (Is_Swap(Fstab_File_System)) {
		LOGI("Swap detected.\n");
		if (Mount_Point == "/sdext2") {
			Is_Present = false;
			LOGI("/sdext2 will not be available.\n");
			Can_Be_Mounted = false;
			Can_Be_Wiped = false;
			DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
			DataManager::SetValue(TW_SDEXT2_SIZE, 0);
		} else if (Mount_Point == "/sd-ext") {
			Is_Present = false;
			LOGI("/sd-ext will not be available.\n");
			Can_Be_Mounted = false;
			Can_Be_Wiped = false;
			DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
			DataManager::SetValue(TW_SDEXT_SIZE, 0);
		}
	}

	// Process any custom flags
	if (Flags.size() > 0)
		Process_Flags(Flags, Display_Error);

	return true;
}

bool TWPartition::Process_Flags(string Flags, bool Display_Error) {
	char flags[MAX_FSTAB_LINE_LENGTH];
	int flags_len, index = 0;
	char* ptr;

	strcpy(flags, Flags.c_str());
	flags_len = Flags.size();
	for (index = 0; index < flags_len; index++) {
		if (flags[index] == ';')
			flags[index] = '\0';
	}

	index = 0;
	while (index < flags_len) {
		while (index < flags_len && flags[index] == '\0')
			index++;
		if (index >= flags_len)
			continue;
		ptr = flags + index;
		if (strcmp(ptr, "removable") == 0) {
			Removable = true;
		} else if (strcmp(ptr, "storage") == 0) {
			Is_Storage = true;
		} else if (strcmp(ptr, "canbewiped") == 0) {
			Can_Be_Wiped = true;
		} else if (strcmp(ptr, "wipeingui") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
		} else if (strcmp(ptr, "wipeduringfactoryreset") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
		} else if (strlen(ptr) > 15 && strncmp(ptr, "subpartitionof=", 15) == 0) {
			ptr += 15;
			Is_SubPartition = true;
			SubPartition_Of = ptr;
		} else if (strcmp(ptr, "ignoreblkid") == 0) {
			Ignore_Blkid = true;
		} else if (strcmp(ptr, "retainlayoutversion") == 0) {
			Retain_Layout_Version = true;
		} else if (strlen(ptr) > 8 && strncmp(ptr, "symlink=", 8) == 0) {
			ptr += 8;
			Symlink_Path = ptr;
		} else if (strlen(ptr) > 8 && strncmp(ptr, "display=", 8) == 0) {
			ptr += 8;
			Display_Name = ptr;
		} else if (strlen(ptr) > 10 && strncmp(ptr, "blocksize=", 10) == 0) {
			ptr += 10;
			Format_Block_Size = atoi(ptr);
		} else if (strlen(ptr) > 7 && strncmp(ptr, "length=", 7) == 0) {
			ptr += 7;
			Length = atoi(ptr);
		} else {
			if (Display_Error)
				LOGE("Unhandled flag: '%s'\n", ptr);
			else
				LOGI("Unhandled flag: '%s'\n", ptr);
		}
		while (index < flags_len && flags[index] != '\0')
			index++;
	}
	return true;
}

/************************************************************************************
 * Setup partition details according to supported filesystems 
 */
bool TWPartition::Is_File_System(string File_System) {
	if(File_System == "ext2"
	|| File_System == "ext3"
	|| File_System == "ext4"
	|| File_System == "vfat"
	|| File_System == "yaffs2"
#ifdef TW_INCLUDE_NILFS2
	|| File_System == "nilfs2"
#endif
#ifdef TW_INCLUDE_NTFS_3G
	|| File_System == "ntfs"
#endif
#ifdef TW_INCLUDE_EXFAT
	|| File_System == "exfat"
#endif
	|| File_System == "auto")
		return true;
	else
		return false;
}

bool TWPartition::Is_Image(string File_System) {
	if (File_System == "emmc" || File_System == "mtd" || File_System == "bml")
		return true;
	else
		return false;
}

bool TWPartition::Is_Swap(string File_System) {
	if(File_System == "swap")
		return true;
	else
		return false;
}

void TWPartition::Setup_File_System(bool Display_Error) {
	Can_Be_Mounted = true;
	Can_Be_Wiped = true;

	// Make the mount point folder if it doesn't exist
	Make_Dir(Mount_Point, Display_Error);
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	Backup_Method = FILES;
}

void TWPartition::Setup_Image(bool Display_Error) {
	Display_Name = Mount_Point.substr(1, Mount_Point.size() - 1);
	Backup_Name = Display_Name;
	if (Current_File_System == "emmc")
		Backup_Method = DD;
	else if (Current_File_System == "mtd" || Current_File_System == "bml")
		Backup_Method = FLASH_UTILS;
	else
		LOGI("Unhandled file system '%s' on image '%s'\n", Current_File_System.c_str(), Display_Name.c_str());
	if (Find_Partition_Size()) {
		Can_Be_Wiped = true;
		Used = Size;
		Backup_Size = Size;
	} else {
		if (Display_Error)
			LOGE("Unable to find parition size for '%s'\n", Mount_Point.c_str());
		else
			LOGI("Unable to find parition size for '%s'\n", Mount_Point.c_str());
	}
}

void TWPartition::Setup_AndSec(void) {
	Backup_Name = "and-sec";
	Has_Android_Secure = true;
	Symlink_Path = Mount_Point + "/.android_secure";
	Symlink_Mount_Point = "/and-sec";
	Backup_Path = Symlink_Mount_Point;
	Make_Dir("/and-sec", true);
	Recreate_AndSec_Folder();
}

void TWPartition::Recreate_AndSec_Folder(void) {
	if (!Has_Android_Secure)
		return;
	if (!Mount(true)) {
		LOGE("Unable to recreate android secure folder.\n");
	} else if (!TWFunc::Path_Exists(Symlink_Path)) {
		LOGI("Recreating android secure folder.\n");
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		mkdir(Symlink_Path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}

void TWPartition::Recreate_DataOnExt_Folder(void) {
	int dataonext = 0;
	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
	if (!dataonext)
		return;
	string data_pth;
	DataManager::GetValue(TW_DATA_PATH, data_pth);
	if (data_pth == "/sd-ext")
		return;
	string root_pth;
	root_pth = TWFunc::Get_Root_Path(data_pth);
	if (root_pth != Mount_Point)
		return;

	data_pth += "/";
	if (!Mount(true)) {
		LOGE("Unable to recreate folder for DataOnExt.\n");
	} else if (!TWFunc::Path_Exists(data_pth)) {
		LOGI("Recreating data folder for DataOnExt.\n");
		if (!TWFunc::Recursive_Mkdir(data_pth))
			LOGI("Could not create '%s'\n", data_pth.c_str());
		UnMount(true);
	}
}

void TWPartition::Recreate_Media_Folder(void) {
	string Command;

	if (!Mount(true)) {
		LOGE("Unable to recreate /data/media folder.\n");
	} else if (!TWFunc::Path_Exists("/data/media")) {
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		LOGI("Recreating /data/media folder.\n");
		mkdir("/data/media", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}

bool TWPartition::Make_Dir(string Path, bool Display_Error) {
	if (!TWFunc::Path_Exists(Path)) {
		if (mkdir(Path.c_str(), 0777) == -1) {
			if (Display_Error)
				LOGE("Can not create '%s' folder.\n", Path.c_str());
			else
				LOGI("Can not create '%s' folder.\n", Path.c_str());
			return false;
		} else {
			LOGI("Created '%s' folder.\n", Path.c_str());
			return true;
		}
	}
	return true;
}

/************************************************************************************
 * Find block device
 */
void TWPartition::Find_Real_Block_Device(string& Block, bool Display_Error) {
	char device[512], realDevice[512];

	strcpy(device, Block.c_str());
	memset(realDevice, 0, sizeof(realDevice));
	while (readlink(device, realDevice, sizeof(realDevice)) > 0)
	{
		strcpy(device, realDevice);
		memset(realDevice, 0, sizeof(realDevice));
	}

	if (device[0] != '/') {
		if (Display_Error)
			LOGE("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		else
			LOGI("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		return;
	} else {
		Block = device;
		return;
	}
}

void TWPartition::Find_Actual_Block_Device(void) {
	if (Is_Decrypted) {
		Actual_Block_Device = Decrypted_Block_Device;
		if (TWFunc::Path_Exists(Primary_Block_Device))
			Is_Present = true;
	} else if (TWFunc::Path_Exists(Primary_Block_Device)) {
		Is_Present = true;
		Actual_Block_Device = Primary_Block_Device;
		return;
	} else if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
		Actual_Block_Device = Alternate_Block_Device;
		Is_Present = true;
	} else {
		Is_Present = false;
	}
}

bool TWPartition::Find_MTD_Block_Device(string MTD_Name) {
	FILE *fp = NULL;
	char line[255];

	fp = fopen("/proc/mtd", "rt");
	if (fp == NULL) {
		LOGE("Device does not support /proc/mtd\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		char device[32], label[32];
		unsigned long size = 0;
		char* fstype = NULL;
		int deviceId;

		sscanf(line, "%s %lx %*s %*c%s", device, &size, label);

		// Skip header and blank lines
		if ((strcmp(device, "dev:") == 0) || (strlen(line) < 8))
			continue;

		// Strip off the trailing " from the label
		label[strlen(label)-1] = '\0';

		if (strcmp(label, MTD_Name.c_str()) == 0) {
			// We found our device
			// Strip off the trailing : from the device
			device[strlen(device)-1] = '\0';
			if (sscanf(device,"mtd%d", &deviceId) == 1) {
				sprintf(device, "/dev/block/mtdblock%d", deviceId);
				Primary_Block_Device = device;
				fclose(fp);
				return true;
			}
		}
	}
	fclose(fp);

	return false;
}

/************************************************************************************
 * Get partition's size
 */
bool TWPartition::Update_Size(bool Display_Error) {
	bool ret = false, Was_Already_Mounted = false;

	if (!Can_Be_Mounted && !Is_Encrypted)
		return false;

	Was_Already_Mounted = Is_Mounted();
	if (!Was_Already_Mounted) {
		if (Removable || Is_Encrypted) {
			if (!Mount(false))
				return true;
		} else {		
			if (!Mount(Display_Error))
				return false;
		}
	}
	ret = Get_Size_Via_statfs(Display_Error);
	if (!ret || Size == 0) {
		if (!Get_Size_Via_df(Display_Error)) {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	}
	if (Removable) {
		if (Size > 0) {
			Is_Present = true;
			Wipe_Available_in_GUI = true;
			Can_Be_Mounted = true;
			Can_Be_Wiped = true;
		} else {
			Is_Present = false;
			Wipe_Available_in_GUI = false;
			Can_Be_Mounted = false;
			Can_Be_Wiped = false;
		}
	}

	if (Has_Data_Media) {
		if (Mount(Display_Error)) {
			unsigned long long data_media_used, actual_data;
			Used = TWFunc::Get_Folder_Size("/data", Display_Error);
			data_media_used = TWFunc::Get_Folder_Size("/data/media", Display_Error);
			actual_data = Used - data_media_used;
			Backup_Size = actual_data;
			int bak = (int)(Backup_Size / 1048576LLU);
			int total = (int)(Size / 1048576LLU);
			int us = (int)(Used / 1048576LLU);
			int fre = (int)(Free / 1048576LLU);
			int datmed = (int)(data_media_used / 1048576LLU);
			LOGI("Data backup size is %iMB, size: %iMB, used: %iMB, free: %iMB, in data/media: %iMB.\n", bak, total, us, fre, datmed);
		} else {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	} else if (Has_Android_Secure) {
		if (Mount(Display_Error)) {
			if (TWFunc::Path_Exists(Backup_Path))
				Backup_Size = TWFunc::Get_Folder_Size(Backup_Path, Display_Error);
		} else {
			if (!Was_Already_Mounted)
				UnMount(false);
			return false;
		}
	}
	if (!Was_Already_Mounted)
		UnMount(false);
	return true;
}

bool TWPartition::Get_Size_Via_statfs(bool Display_Error) {
	struct statfs st;
	string Local_Path = Mount_Point + "/.";

	int skip_dalvik;
	DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);

	if (statfs(Local_Path.c_str(), &st) != 0) {
		if (!Removable) {
			if (Display_Error)
				LOGE("Unable to statfs '%s'\n", Local_Path.c_str());
			else
				LOGI("Unable to statfs '%s'\n", Local_Path.c_str());
		}
		return false;
	}
	Size = (st.f_blocks * st.f_bsize);
	Used = ((st.f_blocks - st.f_bfree) * st.f_bsize);
	Free = (st.f_bfree * st.f_bsize);
	if (Mount_Point == "/sd-ext") {
		if (Size == 0)
			DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
		// sd-ext is a special case due to DataOnExt & NativeSD features
		int dataonext, skip_native;
		DataManager::GetValue(TW_SKIP_NATIVESD, skip_native);
		DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (!Path_For_DataOnExt.empty()) {
			LOGI("Path_For_DataOnExt: '%s'\n", Path_For_DataOnExt.c_str());
			// In case the entered data_path changed after user interaction
			if (Path_For_DataOnExt != data_pth) {
				if (CheckFor_DataOnExt() == 1) {
					// Get the "validated" values 
					DataManager::GetValue(TW_DATA_PATH, data_pth);
					DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
				}
				Path_For_DataOnExt = data_pth;
				// Recheck for new data_pth
				CheckFor_Dalvik_Cache();
				CheckFor_NativeSD();
			}
		}
		if (dataonext) {
			LOGI("TW_DATA_PATH: '%s'\n", data_pth.c_str());
			if (!Is_Mounted())
				system(("mount " + Primary_Block_Device + " " + Mount_Point).c_str());
				//Mount(Display_Error);
			if (TWFunc::Path_Exists(data_pth))
				Backup_Size = TWFunc::Get_Folder_Size(data_pth, true);
			else
				Backup_Size = 0;
			if (data_pth == "/sd-ext" && skip_native)
				Backup_Size -= NativeSD_Size;
		} else {
			//LOGI("DataOnExt mode: OFF\n");
			Backup_Size = Used;
			if (Backup_Size > 0 && skip_native)
				Backup_Size -= NativeSD_Size;
		}
	} else
		Backup_Size = Used;
	if (Backup_Size > 0 && skip_dalvik)
		Backup_Size -= Dalvik_Cache_Size;
	
	return true;
}

bool TWPartition::Get_Size_Via_df(bool Display_Error) {
	FILE* fp;
	char command[255], line[512];
	int include_block = 1;
	unsigned int min_len;
	string result;

	int skip_dalvik;
	DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);

	min_len = Actual_Block_Device.size() + 2;
	sprintf(command, "df %s > /tmp/dfoutput.txt", Mount_Point.c_str());
	TWFunc::Exec_Cmd(command, result);
	fp = fopen("/tmp/dfoutput.txt", "rt");
	if (fp == NULL) {
		LOGI("Unable to open /tmp/dfoutput.txt.\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long blocks, used, available;
		char device[64];
		char tmpString[64];

		if (strncmp(line, "Filesystem", 10) == 0)
			continue;
		if (strlen(line) < min_len) {
			include_block = 0;
			continue;
		}
		if (include_block) {
			sscanf(line, "%s %lu %lu %lu", device, &blocks, &used, &available);
		} else {
			// The device block string is so long that the df information is on the next line
			int space_count = 0;
			sprintf(tmpString, "/dev/block/%s", Actual_Block_Device.c_str());
			while (tmpString[space_count] == 32)
				space_count++;
			sscanf(line + space_count, "%lu %lu %lu", &blocks, &used, &available);
		}

		// Adjust block size to byte size
		Size = blocks * 1024ULL;
		Used = used * 1024ULL;
		Free = available * 1024ULL;
	}
	if (Mount_Point == "/sd-ext") {
		// sd-ext is a special case due to DataOnExt & NativeSD features
		int dataonext, skip_native;
		DataManager::GetValue(TW_SKIP_NATIVESD, skip_native);
		DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (!Path_For_DataOnExt.empty()) {
			LOGI("Path_For_DataOnExt: '%s'\n", Path_For_DataOnExt.c_str());
			// In case the entered data_path changed after user interaction
			if (Path_For_DataOnExt != data_pth) {
				if (CheckFor_DataOnExt() == 1) {
					// Get the "validated" values 
					DataManager::GetValue(TW_DATA_PATH, data_pth);
					DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
				}
				Path_For_DataOnExt = data_pth;
				// Recheck for new data_pth
				CheckFor_Dalvik_Cache();
				CheckFor_NativeSD();
			}
		}
		if (dataonext) {
			LOGI("TW_DATA_PATH: '%s'\n", data_pth.c_str());
			if (!Is_Mounted())
				system(("mount " + Primary_Block_Device + " " + Mount_Point).c_str());
				//Mount(Display_Error);
			if (TWFunc::Path_Exists(data_pth))
				Backup_Size = TWFunc::Get_Folder_Size(data_pth, true);
			else
				Backup_Size = 0;
			if (data_pth == "/sd-ext" && skip_native)
				Backup_Size -= NativeSD_Size;
		} else {
			//LOGI("DataOnExt mode: OFF\n");
			Backup_Size = Used;
			if (Backup_Size > 0 && skip_native)
				Backup_Size -= NativeSD_Size;
		}
	} else
		Backup_Size = Used;
	if (Backup_Size > 0 && skip_dalvik)
		Backup_Size -= Dalvik_Cache_Size;
	fclose(fp);
	return true;
}

bool TWPartition::Find_Partition_Size(void) {
	FILE* fp;
	char line[512];
	string tmpdevice;

	// In this case, we'll first get the partitions we care about (with labels)
	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL)
		return false;

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long major, minor, blocks;
		char device[512];
		char tmpString[64];

		if (strlen(line) < 7 || line[0] == 'm')	 continue;
		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (tmpdevice == Primary_Block_Device/* || tmpdevice == Alternate_Block_Device*/) {
			// Adjust block size to byte size
			Size = blocks * 1024ULL;
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return false;
}

/************************************************************************************
 * Mount, UnMount, etc.
 */
bool TWPartition::Mount(bool Display_Error) {
	if (Is_Mounted()) {
		return true;
	} else if (!Can_Be_Mounted) {
		return false;
	}

	Find_Actual_Block_Device();

	// Check the current file system before mounting
	Check_FS_Type();

	string Command;
	if (Current_File_System == "swap")
		return false;
	if (Fstab_File_System == "yaffs2") {
		// mount an MTD partition as a YAFFS2 filesystem.
		mtd_scan_partitions();
		const MtdPartition* partition;
		partition = mtd_find_partition_by_name(MTD_Name.c_str());
		if (partition == NULL) {
			LOGE("Failed to find '%s' partition to mount at '%s'\n",
			MTD_Name.c_str(), Mount_Point.c_str());
			return false;
		}
		if (mtd_mount_partition(partition, Mount_Point.c_str(), Fstab_File_System.c_str(), 0)) {
			if (Display_Error)
				LOGE("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
			else
				LOGI("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
			return false;
		} else
			return true;
	}
	if (Current_File_System == "exfat") {
		if (TWFunc::Path_Exists("/sbin/exfat-fuse")) {
			Command = "exfat-fuse -o nonempty " + Actual_Block_Device + " " + Mount_Point;
			if (system(Command.c_str()) == 0)
				return true;
			else
				return false;
		} else
			return false;
	}
	if (Current_File_System == "ntfs") {
		if (TWFunc::Path_Exists("/sbin/ntfs-3g")) {
			Command = "ntfs-3g -o umask=0 " + Actual_Block_Device + " " + Mount_Point;
			system(Command.c_str());
			Command = "busybox mount -o umask=0 " + Actual_Block_Device + " " + Mount_Point;
			if (system(Command.c_str()) == 0)
				return true;
			else
				return false;
		} else
			return false;
	}
	if (Current_File_System == "nilfs2") {
		if (TWFunc::Path_Exists("/sbin/mount.nilfs2")) {
			Command = "mount.nilfs2 -f " + Actual_Block_Device + " " + Mount_Point;
			if (system(Command.c_str()) == 0)
				return true;
			else
				return false;
		} else
			return false;
	}
	if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), 0, NULL) != 0) {
		if (Display_Error)
			LOGE("Unable to mount '%s'\n", Mount_Point.c_str());
		else
			LOGI("Unable to mount '%s'\n", Mount_Point.c_str());
		LOGI("Actual block device: '%s', current file system: '%s'\n", Actual_Block_Device.c_str(), Current_File_System.c_str());
		return false;
	} else {
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		string MetaEcfsFile = EXPAND(TW_EXTERNAL_STORAGE_PATH);
		MetaEcfsFile += "/.MetaEcfsFile";
		if (EcryptFS_Password.size() > 0 && PartitionManager.Mount_By_Path("/data", false) && TWFunc::Path_Exists(MetaEcfsFile)) {
			if (mount_ecryptfs_drive(EcryptFS_Password.c_str(), Mount_Point.c_str(), Mount_Point.c_str(), 0) != 0) {
				if (Display_Error)
					LOGE("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
				else
					LOGI("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
			} else {
				LOGI("Successfully mounted ecryptfs for '%s'\n", Mount_Point.c_str());
			}
		}
#endif

		if (!Symlink_Mount_Point.empty()) {
			Command = "mount '" + Symlink_Path + "' '" + Symlink_Mount_Point + "'";
			if (system(Command.c_str()) == 0)
				return true;
			else
				return false;
		}
		return true;
	}
	return true;
}

bool TWPartition::UnMount(bool Display_Error) {
	if (Is_Mounted()) {
		int never_unmount_system;
		string Command;

		DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
		if (never_unmount_system == 1 && Mount_Point == "/system")
			return true; // Never unmount system if you're not supposed to unmount it

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		if (EcryptFS_Password.size() > 0) {
			if (unmount_ecryptfs_drive(Mount_Point.c_str()) != 0) {
				if (Display_Error)
					LOGE("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
				else
					LOGI("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
			} else {
				LOGI("Successfully unmounted ecryptfs for '%s'\n", Mount_Point.c_str());
			}
		}
#endif

		if (Current_File_System == "")
			Check_FS_Type();

		if (Current_File_System == "nilfs2") {
			Command = "umount.nilfs2 -n " + Symlink_Mount_Point;
			if (system(Command.c_str()) == 0)
				return true;
		}

		if (!Symlink_Mount_Point.empty())
			umount(Symlink_Mount_Point.c_str());

		if (umount(Mount_Point.c_str()) != 0) {
			if (Display_Error)
				LOGE("Unable to unmount '%s'\nRetrying with 'umount -l'\n", Mount_Point.c_str());
			else
				LOGI("Unable to unmount '%s'\nRetrying with 'umount -l'\n", Mount_Point.c_str());
			Command = "umount -l " + Mount_Point;
			if (system(Command.c_str()) == 0) {
				return true;
			}
			if (Display_Error)
				LOGE("Unmounting failed\n");
			else
				LOGI("Unmounting failed\n");
			return false;
		} else
			return true;
	} else {
		return true;
	}
}

bool TWPartition::Is_Mounted(void) {
	if (!Can_Be_Mounted)
		return false;

	struct stat st1, st2;
	string test_path;

	// Check to see if the mount point directory exists
	test_path = Mount_Point + "/.";
	if (stat(test_path.c_str(), &st1) != 0)  return false;

	// Check to see if the directory above the mount point exists
	test_path = Mount_Point + "/../.";
	if (stat(test_path.c_str(), &st2) != 0)  return false;

	// Compare the device IDs -- if they match then we're (probably) using tmpfs instead of an actual device
	int ret = (st1.st_dev != st2.st_dev) ? true : false;

	return ret;
}

void TWPartition::Mount_Storage_Retry(void) {
	// On some devices, storage doesn't want to mount right away, retry and sleep
	if (!Mount(true)) {
		int retry_count = 5;
		while (retry_count > 0 && !Mount(false)) {
			usleep(500000);
			retry_count--;
		}
		Mount(true);
	}
}

/************************************************************************************
 * Partition wipping
 */
bool TWPartition::Wipe() {
	if (Is_File_System(Current_File_System))
		return Wipe(Current_File_System);
	else
		return Wipe(Fstab_File_System);
}

bool TWPartition::Wipe(string New_File_System) {
	bool wiped = false, update_crypt = false;
	int check;
	string Layout_Filename = Mount_Point + "/.layout_version";

	if (!Can_Be_Wiped) {
		LOGE("Partition '%s' cannot be wiped.\n", Mount_Point.c_str());
		return false;
	}

	if (Mount_Point == "/cache")
		tmplog_offset = 0;

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	if (Mount_Point == "/data" && Mount(false)) {
		if (TWFunc::Path_Exists("/data/system/edk_p_sd"))
			TWFunc::copy_file("/data/system/edk_p_sd", "/tmp/edk_p_sd", 0600);
	}
#endif

	if (Retain_Layout_Version && Mount(false) && TWFunc::Path_Exists(Layout_Filename))
		TWFunc::copy_file(Layout_Filename, "/.layout_version", 0600);
	else
		unlink("/.layout_version");

	if (Has_Data_Media) {
		wiped = Wipe_Data_Without_Wiping_Media();
	} else {

		DataManager::GetValue(TW_RM_RF_VAR, check);
		if (check)
			wiped = Wipe_RMRF();
		else if (New_File_System == "ext4")
			wiped = Wipe_EXT4();
		else if (New_File_System == "ext2" || New_File_System == "ext3")
			wiped = Wipe_EXT23(New_File_System);
		else if (New_File_System == "vfat")
			wiped = Wipe_FAT();
		else if (New_File_System == "yaffs2")
			wiped = Wipe_MTD();
#ifdef TW_INCLUDE_EXFAT
		else if (New_File_System == "exfat")
			wiped = Wipe_EXFAT();	
#endif
#ifdef TW_INCLUDE_NIFS2
		else if (New_File_System == "nilfs2")
			wiped = Wipe_NILFS2();		
#endif
#ifdef TW_INCLUDE_MKNTFS
		else if (New_File_System == "ntfs")
			wiped = Wipe_NTFS();		
#endif
		else if (New_File_System == "mtd")
			return DataManager::Wipe_MTD_By_Name(MTD_Name);
		else {
			LOGE("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), New_File_System.c_str());
			unlink("/.layout_version");
			return false;
		}
		update_crypt = wiped;
	}

	if (wiped) {
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		if (Mount_Point == "/data" && Mount(false)) {
			if (TWFunc::Path_Exists("/tmp/edk_p_sd")) {
				Make_Dir("/data/system", true);
				TWFunc::copy_file("/tmp/edk_p_sd", "/data/system/edk_p_sd", 0600);
			}
		}
#endif
		if (Mount_Point == "/cache")
			DataManager::Output_Version();

		if (TWFunc::Path_Exists("/.layout_version") && Mount(false))
			TWFunc::copy_file("/.layout_version", Layout_Filename, 0600);

		if (update_crypt) {
			Setup_File_System(false);
			if (Is_Encrypted && !Is_Decrypted) {
				// just wiped an encrypted partition back to its unencrypted state
				Is_Encrypted = false;
				Is_Decrypted = false;
				Decrypted_Block_Device = "";
				if (Mount_Point == "/data") {
					DataManager::SetValue(TW_IS_ENCRYPTED, 0);
					DataManager::SetValue(TW_IS_DECRYPTED, 0);
				}
			}
		}
		// If the partition was wiped update sizes
		Update_Size(true);
	}
	return wiped;
}

bool TWPartition::Wipe_AndSec(void) {
	if (!Has_Android_Secure)
		return false;

	if (!Mount(true))
		return false;

	ui_print("Wiping .android_secure\n");
	TWFunc::removeDir(Mount_Point + "/.android_secure/", true);
	ui_print("Done.\n");
    return true;
}

bool TWPartition::Decrypt(string Password) {
	LOGI("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
	// Is this needed?
	return 1;
}

bool TWPartition::Wipe_Encryption() {
	bool Save_Data_Media = Has_Data_Media;

	if (!UnMount(true))
		return false;

	Has_Data_Media = false;
	if (Wipe(Fstab_File_System)) {
		Has_Data_Media = Save_Data_Media;
		if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
			Recreate_Media_Folder();
		}
		ui_print("You may need to reboot recovery to be able to use /data again.\n");
		return true;
	} else {
		Has_Data_Media = Save_Data_Media;
		LOGE("Unable to format to remove encryption.\n");
		return false;
	}
	return false;
}

bool TWPartition::Wipe_EXT23(string File_System) {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/mke2fs")) {
		string command, result;

		ui_print("Formatting %s using mke2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mke2fs -t " + File_System + " -m 0 " + Actual_Block_Device;
		LOGI("mke2fs command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			Current_File_System = File_System;
			Recreate_AndSec_Folder();
			Recreate_DataOnExt_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXT4() {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/make_ext4fs")) {
		string Command, result;

		ui_print("Formatting %s using make_ext4fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		Command = "make_ext4fs";
		if (!Is_Decrypted && Length != 0) {
			// Only use length if we're not decrypted
			char len[32];
			sprintf(len, "%i", Length);
			Command += " -l ";
			Command += len;
		}
		Command += " " + Actual_Block_Device;
		LOGI("make_ext4fs command: %s\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command, result) == 0) {
			Current_File_System = "ext4";
			Recreate_AndSec_Folder();
			Recreate_DataOnExt_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_EXT23("ext4");

	return false;
}

bool TWPartition::Wipe_NILFS2() {
	string command, result;

	if (TWFunc::Path_Exists("/sbin/mkfs.nilfs2")) {
		if (!UnMount(true))
			return false;

		ui_print("Formatting %s using mkfs.nilfs2...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkfs.nilfs2 " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			Current_File_System = "nilfs2";
			Recreate_AndSec_Folder();
			Recreate_DataOnExt_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_FAT() {
	string command, result;

	if (TWFunc::Path_Exists("/sbin/mkdosfs")) {
		if (!UnMount(true))
			return false;

		ui_print("Formatting %s using mkdosfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkdosfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			Current_File_System = "vfat";
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	}
	else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXFAT() {
	string command, result;

	if (TWFunc::Path_Exists("/sbin/mkexfatfs")) {
		if (!UnMount(true))
			return false;

		ui_print("Formatting %s using mkexfatfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkexfatfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_NTFS() {
	string command, result;

	if (TWFunc::Path_Exists("/sbin/mkntfs")) {
		if (!UnMount(true))
			return false;

		ui_print("Formatting %s using mkntfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkntfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			Current_File_System = "ntfs";
			Recreate_AndSec_Folder();
			ui_print("Done.\n");
			return true;
		} else {
			LOGE("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_MTD() {
	if (!UnMount(true))
		return false;

	ui_print("MTD Formatting \"%s\"\n", MTD_Name.c_str());

	mtd_scan_partitions();
	const MtdPartition* mtd = mtd_find_partition_by_name(MTD_Name.c_str());
	if (mtd == NULL) {
		LOGE("No mtd partition named '%s'", MTD_Name.c_str());
        	return false;
	}

	MtdWriteContext* ctx = mtd_write_partition(mtd);
	if (ctx == NULL) {
        	LOGE("Can't write '%s', failed to format.", MTD_Name.c_str());
        	return false;
	}
   	if (mtd_erase_blocks(ctx, -1) == -1) {
        	mtd_write_close(ctx);
        	LOGE("Failed to format '%s'", MTD_Name.c_str());
        	return false;
	}
	if (mtd_write_close(ctx) != 0) {
        	LOGE("Failed to close '%s'", MTD_Name.c_str());
        	return false;
	}
	Current_File_System = "yaffs2";
	Recreate_AndSec_Folder();
	ui_print("Done.\n");
    	return true;
}

bool TWPartition::Wipe_RMRF() {
	if (!Mount(true))
		return false;

	ui_print("Removing all files under '%s'\n", Mount_Point.c_str());
	TWFunc::removeDir(Mount_Point, true);
	Recreate_AndSec_Folder();
	Recreate_DataOnExt_Folder();
	return true;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
	string dir;

	// This handles wiping data on devices with "sdcard" in /data/media
	if (!Mount(true))
		return false;

	ui_print("Wiping data without wiping /data/media ...\n");

	DIR* d;
	d = opendir("/data");
	if (d != NULL) {
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)   continue;
			// The media folder is the "internal sdcard"
			// The .layout_version file is responsible for determining whether 4.2 decides up upgrade
			//    the media folder for multi-user.
			if (strcmp(de->d_name, "media") == 0 || strcmp(de->d_name, ".layout_version") == 0)   continue;
			
			dir = "/data/";
			dir.append(de->d_name);
			if (de->d_type == DT_DIR) {
				TWFunc::removeDir(dir, false);
			} else if (de->d_type == DT_REG || de->d_type == DT_LNK || de->d_type == DT_FIFO || de->d_type == DT_SOCK) {
				if (!unlink(dir.c_str()))
					LOGI("Unable to unlink '%s'\n", dir.c_str());
			}
		}
		closedir(d);
		ui_print("Done.\n");
		return true;
	}
	ui_print("Dirent failed to open /data, error!\n");
	return false;
}

/************************************************************************************
 * Partition's backup stuff
 */
bool TWPartition::Backup(string backup_folder) {
	if (Backup_Method == FILES)
		return Backup_Tar(backup_folder);
	else if (Backup_Method == DD)
		return Backup_DD(backup_folder);
	else if (Backup_Method == FLASH_UTILS)
		return Backup_Dump_Image(backup_folder);
	LOGE("Unknown backup method for '%s'\n", Mount_Point.c_str());
	return false;
}

string TWPartition::Backup_Method_By_Name() {
	if (Backup_Method == NONE)
		return "none";
	else if (Backup_Method == FILES)
		return "files";
	else if (Backup_Method == DD)
		return "dd";
	else if (Backup_Method == FLASH_UTILS)
		return "flash_utils";
	else
		return "undefined";
	return "ERROR!";
}

bool TWPartition::Backup_Tar(string backup_folder) {
	char back_name[255], split_index[5];
	string Full_FileName, Split_FileName, Tar_Args = "", Tar_Excl = "", Command, data_pth, pathTodatafolder;
	int use_compression, index, backup_count, dataonext;
	struct stat st;
	unsigned long long total_bsize = 0, file_size;
#ifdef TW_INCLUDE_LIBTAR
	twrpTar tar;
#endif
	if (!Mount(true))
		return false;

	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
	if (Backup_Path == "/and-sec") {
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, "Android Secure", "Backing Up");
		ui_print("Backing up %s...\n", "Android Secure");
	} else if (Backup_Path == "/sd-ext" && dataonext) {	
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, "DataOnExt", "Backing Up");
		ui_print("Backing up DataOnExt...\n");
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (data_pth.size() > 7)
			pathTodatafolder = data_pth.substr(8, data_pth.size() - 1);
		else
			pathTodatafolder = "./*";
	} else {
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
		ui_print("Backing up %s...\n", Display_Name.c_str());
	}

	// Skip dalvik-cache during backup?
	if (Backup_Path == "/data" || Backup_Path == "/sd-ext" || Backup_Path == "/sdext2") {
		int skip_dalvik;
		DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);
		if (skip_dalvik)
			Tar_Excl += " --exclude='dalvik-cache' --exclude='dalvik-cache/*'";
	}
	// Skip any NativeSD Rom during backup of sd-ext
	if (Backup_Path == "/sd-ext" || Backup_Path == "/sdext2") {
		int skip_native;
		DataManager::GetValue(TW_SKIP_NATIVESD, skip_native);
		if (skip_native)
			Tar_Excl += Tar_exclude;
	}

	// Use Compression?
	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
#ifndef TW_INCLUDE_LIBTAR
	if (use_compression)
		Tar_Args += "-cz";
	else
		Tar_Args += "-c";
#endif
	// Set Backup_FileName
	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;
	Full_FileName = backup_folder + Backup_FileName;
	if (Backup_Size > MAX_ARCHIVE_SIZE) {
		// This backup needs to be split into multiple archives
		ui_print("Breaking backup file into multiple archives...\n");
		if (Backup_Path == "/sd-ext" && dataonext)
			sprintf(back_name, "%s", data_pth.c_str());
		else 
			sprintf(back_name, "%s", Backup_Path.c_str());		
#ifdef TW_INCLUDE_LIBTAR
		tar.setdir(back_name);
		tar.setfn(Full_FileName);
		backup_count = tar.splitArchiveThread();
		if (backup_count == -1) {
			LOGE("Error tarring split files!\n");
			return false;
		}
#else
		backup_count = MakeList::Make_File_List(back_name);
		if (backup_count < 1) {
			LOGE("Error generating file list!\n");
			return false;
		}
		for (index=0; index<backup_count; index++) {
			sprintf(split_index, "%03i", index);
			Full_FileName = backup_folder + Backup_FileName + split_index;
			Command = "tar " + Tar_Args + Tar_Excl + " -f '" + Full_FileName + "' -T /tmp/list/filelist" + split_index;
			LOGI("Backup command: '%s'\n", Command.c_str());
			ui_print("Backup archive %i of %i...\n", (index + 1), backup_count);
			system(Command.c_str()); // sending backup command formed earlier above

			file_size = TWFunc::Get_File_Size(Full_FileName);
			if (file_size == 0) {
				LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str()); // oh noes! file size is 0, abort! abort!
				return false;
			}
			total_bsize += file_size;
		}
		ui_print(" * Total size: %llu bytes.\n", total_bsize);
		system("cd /tmp && rm -rf list");
#endif
	} else {
		Full_FileName = backup_folder + Backup_FileName;
#ifdef TW_INCLUDE_LIBTAR
		if (use_compression) {
			tar.setdir(Backup_Path);
			tar.setfn(Full_FileName);
			if (tar.createTarGZThread() != 0)
				return -1;
			string gzname = Full_FileName + ".gz";
			rename(gzname.c_str(), Full_FileName.c_str());
		}
		else {
			tar.setdir(Backup_Path);
			tar.setfn(Full_FileName);
			if (tar.createTarThread() != 0)
				return -1;
		}
#else
		if (Has_Data_Media)
			Command = "cd " + Backup_Path + " && tar " + Tar_Args + " ./ --exclude='media*' -f '" + Full_FileName + "'";
		else {
			if (Backup_Path == "/sd-ext" && dataonext) {
				Command = "cd " + Backup_Path + " && tar "+ Tar_Args + Tar_Excl + " -f '" + Full_FileName + "' " + pathTodatafolder;			
			} else {
				Command = "cd " + Backup_Path + " && tar " + Tar_Args + Tar_Excl + " -f '" + Full_FileName + "' ./*";
			}
		}
		LOGI("Backup command: '%s'\n", Command.c_str());
		system(Command.c_str());
#endif
		if (TWFunc::Get_File_Size(Full_FileName) == 0) {
			LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
			return false;
		}
	}
	if (Backup_Path == "/sd-ext" && dataonext) {
		// Create a file to recognize that this is DataOnExt and not a typical sd-ext backup
		if (pathTodatafolder == "./*")
			pathTodatafolder = "";
		else
			pathTodatafolder = "/" + pathTodatafolder;
		Command = "echo /sd-ext" + pathTodatafolder + ">" + backup_folder + ".dataonext";
		system(Command.c_str());
	}
	return true;
}

bool TWPartition::Backup_DD(string backup_folder) {
	string Full_FileName, Command, result;
	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	ui_print("Backing up %s...\n", Display_Name.c_str());

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = backup_folder + Backup_FileName;

	Command = "dd if=" + Actual_Block_Device + " of='" + Full_FileName + "'";
	LOGI("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command, result);
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

bool TWPartition::Backup_Dump_Image(string backup_folder) {
	string Full_FileName, Command;
	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	ui_print("Backing up %s...\n", Display_Name.c_str());

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = backup_folder + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGI("Backup command: '%s'\n", Command.c_str());
	system(Command.c_str());
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return false;
	}
	return true;
}

/************************************************************************************
 * Partition restoring...
 */
bool TWPartition::Restore(string restore_folder) {
	size_t first_period, second_period;
	string Restore_File_System, FileName;
	
	FileName = Backup_FileName;
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	LOGI("Restore filename is: %s\n", Backup_FileName.c_str());

	// Parse backup filename to extract the file system before wiping
	first_period = FileName.find(".");
	if (first_period == string::npos) {
		LOGE("Unable to find file system (first period).\n");
		return false;
	}
	// Fix for android_secure's case in case of cwm-backup type
	if (first_period == 0) { // '.android_secure.vfat.tar'
		FileName = FileName.substr(1, (FileName.size() - 1));
		first_period = FileName.find(".");
	}
	Restore_File_System = FileName.substr(first_period + 1, FileName.size() - first_period - 1);
	second_period = Restore_File_System.find(".");
	if (second_period == string::npos) {
		// Fix for boot.img's case in case of cwm-backup type
		if (Restore_File_System == "img" && DataManager::Detect_BLDR() == 1/*cLK*/)
			Restore_File_System = "mtd";
		else {
			LOGE("Unable to find file system (second period).\n");
			return false;
		}
	} else
		Restore_File_System.resize(second_period);

	LOGI("Restore file system is: '%s'.\n", Restore_File_System.c_str());
	if (Is_File_System(Restore_File_System)) {
		if (Use_unyaffs_To_Restore)
			return Restore_Yaffs_Image(restore_folder);
		else
			return Restore_Tar(restore_folder, Restore_File_System);
	} else if (Is_Image(Restore_File_System)) {
		if (Restore_File_System == "emmc")
			return Restore_DD(restore_folder);
		else if (Restore_File_System == "mtd" || Restore_File_System == "bml")
			return Restore_Flash_Image(restore_folder);
	}
	LOGE("Unknown restore method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Restore_Tar(string restore_folder, string Restore_File_System) {
	string Full_FileName, Command, data_pth;
	int index = 0, dataonext = 0;
	char split_index[5];

	Current_File_System = Restore_File_System;
	if (Backup_Name == "sd-ext") {
		// Check if this is a sd-ext backup featuring DataOnExt
		dataonext = TWFunc::Path_Exists(restore_folder + "/.dataonext");
		if (dataonext) {
			LOGI("The sd-ext backup is actually 'DataOnExt'.\n");
			// get the DATA_PATH stored inside '.dataonext' file
			FILE *fp;
			fp = fopen((restore_folder + "/.dataonext").c_str(), "rt");
			char tmp[255];
			fgets(tmp, sizeof(tmp), fp);
			tmp[strlen(tmp) - 1] = '\0';
			data_pth = tmp;
			fclose(fp);
			LOGI("DataOnExt path: '%s'\n", data_pth.c_str());
			// decide if to wipe partition
			if (data_pth == "/sd-ext")
				Wipe();
			else {
				// rm the returned path instead of formatting the entire partition.
				// Maybe there are other data on it (another NativeSD Rom).
				Mount(true);
				if (TWFunc::Path_Exists(data_pth)) {
					ui_print("Wiping %s...\n", data_pth.c_str());
					Command = "rm -rf " + data_pth + "/* &> /dev/null";
					system(Command.c_str());
					Command = "rm -rf " + data_pth + "/.* &> /dev/null";
					system(Command.c_str());
				}
				UnMount(true);
			}		
			// Set TWRP values
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			DataManager::SetValue(TW_DATA_PATH, data_pth);
		} else {
			Wipe();
			// Set TWRP value
			DataManager::SetValue(TW_DATA_ON_EXT, 0);
		}
	} else {
		if (Has_Android_Secure) {
			ui_print("Wiping android secure...\n");
			if (!Wipe_AndSec())
				return false;
		} else if (!Wipe()) {
				ui_print("Failed wiping %s...\n", Display_Name.c_str());
				return false;
		}
	}

	if (Backup_Name == "sd-ext" || Backup_Name == "sdext2") {
		if ((!dataonext) || (dataonext && data_pth == Backup_Path)) {
			// Set number of mounts that will trigger a filesystem check from settings
			int c_var;
			DataManager::GetValue("tw_num_of_mounts_for_fs_check", c_var);
			char temp[255];
			string n_mounts;
			memset(temp, 0, sizeof(temp));
			sprintf(temp, "%i", c_var);
			n_mounts = temp;	
			system(("tune2fs -c " + n_mounts + " " + Primary_Block_Device).c_str());
		}
	}

	if (!Mount(true))
		return false;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	ui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_FileName)) {
		// Backup is multiple archives
		LOGI("Backup is multiple archives.\n");
		sprintf(split_index, "%03i", index);
		Full_FileName += split_index;
		while (TWFunc::Path_Exists(Full_FileName)) {
			ui_print("Restoring archive %i...\n", index+1);
			LOGI("Restoring '%s'...\n", Full_FileName.c_str());
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
			tar.setdir("/");
			tar.setfn(Full_FileName);
			if (tar.extractTarThread() != 0)
				return false;
#else
			if (Check_Tar_Entry(Full_FileName, "sd-ext") || Check_Tar_Entry(Full_FileName, "system") || Check_Tar_Entry(Full_FileName, "data"))
				Command = "cd / && tar -xf '" + Full_FileName + "'";
			else			
				Command = "cd " + Backup_Path + " && tar -xf '" + Full_FileName + "'";
			LOGI("Restore command: '%s'\n", Command.c_str());
			system(Command.c_str());	
#endif
			index++;		
			sprintf(split_index, "%03i", index);
			Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
		}
		if (index == 0) {
			LOGE("Error locating restore file: '%s'\n", Full_FileName.c_str());
			return false;
		}
	} else {
#ifdef TW_INCLUDE_LIBTAR
		twrpTar tar;
		tar.setdir(Backup_Path);
		tar.setfn(Full_FileName);
		if (tar.extractTarThread() != 0)
			return false;
#else
		// For restoring a CWM backup of sd-ext
		if (Check_Tar_Entry(Full_FileName, "sd-ext"))
			Command = "cd / && tar -xf '" + Full_FileName + "'";			
		// For restoring a CWM backup of android_secure
		else if (Check_Tar_Entry(Full_FileName, ".android_secure"))
			Command = "cd " + Storage_Path + " && tar -xf '" + Full_FileName + "'";			
		else
			Command = "cd " + Backup_Path + " && tar -xf '" + Full_FileName + "'";
		LOGI("Restore command: '%s'\n", Command.c_str());
		system(Command.c_str());
#endif
	}
	return true;
}

bool TWPartition::Restore_DD(string restore_folder) {
	string Full_FileName, Command, result;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGE("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGE("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}

	ui_print("Restoring %s...\n", Display_Name.c_str());
	Command = "dd bs=4096 if='" + Full_FileName + "' of=" + Actual_Block_Device;
	LOGI("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command, result);
	return true;
}

bool TWPartition::Restore_Flash_Image(string restore_folder) {
	string Full_FileName, Command, result;

	ui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGE("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGE("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGI("Erase command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command, result);
	Command = "flash_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGI("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command, result);
	return true;
}

// Make TWRP compatible with CWM Backup type
bool TWPartition::Restore_Yaffs_Image(string restore_folder) {
	Use_unyaffs_To_Restore = false; // always reset value to false
	string Full_FileName, Command;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGE("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGE("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}	

	if (!Wipe())
		return false;

	if (!Mount(true))
		return false;
	
	ui_print("Restoring %s...\n", Display_Name.c_str());
	char* backup_file_image = (char*)Full_FileName.c_str();
	char* backup_path = (char*)Mount_Point.c_str();
	unyaffs(backup_file_image, backup_path, NULL);
	return true;
}

void TWPartition::Change_FS_Type(string type) {
	Current_File_System = type;
	return;
}

/************************************************************************************
 * Various Checks used in code
 */
void TWPartition::Check_FS_Type() {
	if (Fstab_File_System == "yaffs2" || Fstab_File_System == "mtd" || Fstab_File_System == "bml")
		return; // Running blkid on some mtd devices causes a massive crash

	Find_Actual_Block_Device();
	if (!Is_Present) {
		LOGI("Unable to Find_Actual_Block_Device() for %s.\n", Mount_Point.c_str());
		return;
	}

	string Command;
#ifdef TW_INCLUDE_EXFAT
	// blkid doesn't return the fs-type of an exfat partition.
	// Using dumpexfat's exit code seems to do the trick.
	if (Mount_Point == "/sdcard" || Mount_Point == "/external" || Mount_Point == "/external_sd") {
		// Only storage can be formatted to exfat type
		Command = "dumpexfat " + Actual_Block_Device + "&> /dev/null";
		if (system(Command.c_str()) == 0) {
			Current_File_System = "exfat";
			return;
		}
	}
#endif

	string result, fstype;
	Command = "blkid " + Actual_Block_Device + " | rev | cut -d '\"' -f 2 | rev";
	TWFunc::Exec_Cmd(Command, result);
	if (!result.empty()) {
		if (result.find("ext2") != string::npos)
			fstype = "ext2";
		else if (result.find("ext3") != string::npos)
			fstype = "ext3";
		else if (result.find("ext4") != string::npos)
			fstype = "ext4";
		else if (result.find("vfat") != string::npos)
			fstype = "vfat";
#ifdef TW_INCLUDE_NILFS2
		else if (result.find("nilfs2") != string::npos)
			fstype = "nilfs2";
#endif
#ifdef TW_INCLUDE_NTFS_3G
		else if (result.find("ntfs") != string::npos)
			fstype = "ntfs";
#endif
		else
			fstype = "auto";
	} else
		fstype = "auto";

	if (Current_File_System != fstype) {
		LOGI("'%s' was '%s' now set to '%s'\n", Mount_Point.c_str(), Current_File_System.c_str(), fstype.c_str());
		Current_File_System = fstype;
	}

	return;
}

bool TWPartition::Check_MD5(string restore_folder) {
	string Full_Filename;
	char split_filename[512];
	int index = 0;

	// Using the nandroid.md5 file to know if this is a CWM backup.
	// Split that file to match TWRP's style.
	if (TWFunc::Path_Exists(restore_folder + "/nandroid.md5")) {
		string Command = "cwmbackuphandler.sh " + restore_folder;
		system(Command.c_str());
	}

	Full_Filename = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_Filename)) {
		// This is a split archive, we presume
		sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		while (index < 1000 && TWFunc::Path_Exists(split_filename)) {
			if (TWFunc::Check_MD5(split_filename) == 0) {
				LOGE("MD5 failed to match on '%s'.\n", split_filename);
				return false;
			}
			index++;
			sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		}
		return true;
	} else {
		// Single file archive
		if (TWFunc::Check_MD5(Full_Filename) == 0) {
			LOGE("MD5 failed to match on '%s'.\n", split_filename);
			return false;
		} else
			return true;
	}
	return false;
}

// Check a tar file for a given top entry
bool TWPartition::Check_Tar_Entry(string tar_file, string entry) {
	bool ret = false;
	string cmd, result;

	cmd = "tar -tf " + tar_file + " | sed 1q";
	TWFunc::Exec_Cmd(cmd, result);
	if (!result.empty()) {
		if (result.find(entry) != string::npos)
			ret = true;
	}
	return ret;
}

void TWPartition::Check_BuildProp(void) {
	if (!Mount(true)) {
		LOGI("Unable to check %s/build.prop.\n", Mount_Point.c_str());
	} else {
		// Quick check build.prop for [DataOnExt] string
		if (TWFunc::Path_Exists("/system/build.prop")) {
			LOGI("Checking /system/build.prop...\n");
			if (system("grep -Fxqi \"DataOnExt\" /system/build.prop") == 0) {
				LOGI("DataOnExt method is probably used by the installed Rom\n");
				// Automatically set the TWRP values
				// BUT we need the data_path too... :/
				/*int dataonext;
				DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
				if (!dataonext) {
					DataManager::SetValue(TW_DATA_ON_EXT, 1);
					DataManager::SetValue(TW_DATA_ON_EXT_CHECK, 1);
				}*/
			}
		}
		UnMount(false);
	}
}

void TWPartition::CheckFor_NativeSD(void) {
	string current_storage_path = DataManager::GetCurrentStoragePath();
	if (!PartitionManager.Is_Mounted_By_Path(current_storage_path))
		PartitionManager.Mount_Current_Storage(false);

	NativeSD_Size = 0;
	if (!Mount(true)) {
		LOGI("Unable to check %s for NativeSD Roms.\n", Mount_Point.c_str());
	} else {
		DIR* Dir = opendir("/sdcard/NativeSD");
		if (Dir == NULL) {
			LOGI("No NativeSD Roms detected.\n");
		} else {
			int count = 0, dataonext;
			NativeSD_Size = 0;
			Tar_exclude = "";
			DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
			string data_pth;
			DataManager::GetValue(TW_DATA_PATH, data_pth);
			LOGI("Checking for installed NativeSD Roms...\n");
			LOGI("If any found, the backup size of sd-ext will be adjusted.\n");
			struct dirent* DirEntry;
			while ((DirEntry = readdir(Dir)) != NULL) {		
				string dname = DirEntry->d_name;
				if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
					continue;
				if (DirEntry->d_type == DT_DIR) {
					if (dname.find("TWRP") == string::npos && dname.find("twrp") == string::npos
					&& dname.find("4EXT") == string::npos && dname.find("4ext") == string::npos 
					&& dname.find("CWM") == string::npos && dname.find("cwm") == string::npos
					&& dname.find("TOUCH") == string::npos && dname.find("touch") == string::npos
					&& dname.find("RECOVERY") == string::npos && dname.find("recovery") == string::npos) {
						string pathToCheck = "/sd-ext/" + dname;
						if (dataonext) {
							// Some Roms use the same /data for NativeSD and DataOnExt versions
							// We don't want to skip the entire NativeSD-Rom in case its /data is used by the DataOnExt-Rom
							string dataOf_pathToCheck = "/sd-ext/" + dname + "/data";
							if (dataOf_pathToCheck != data_pth) {
								if (TWFunc::Path_Exists(pathToCheck)) {
									count++;
									LOGI("Excluding : %s\n", pathToCheck.c_str());
									NativeSD_Size += TWFunc::Get_Folder_Size(pathToCheck, true);
									Tar_exclude += (" --exclude='" + dname + "' --exclude='" + dname + "/*'");
								}
							}
						} else {
							// Since we have a standard NAND-Rom we will exclude the entire NativeSD-Rom
							if (TWFunc::Path_Exists(pathToCheck)) {
								count++;
								NativeSD_Size += TWFunc::Get_Folder_Size(pathToCheck, true);
								Tar_exclude += (" --exclude='" + dname + "' --exclude='" + dname + "/*'");
							}
						}
					} else
						LOGI("Ignoring '%s'(SD-Recovery folder)...\n", dname.c_str());
				}
			}
			if (count == 0) // "/sdcard/NativeSD" was found but did not contain any Roms
				LOGI("No change to the size of sd-ext.\n");
			closedir(Dir);
		}
		UnMount(false);
	}
}

void TWPartition::CheckFor_Dalvik_Cache(void) {
	Dalvik_Cache_Size = 0;
	if (!Mount(true)) {
		LOGI("Unable to check %s for dalvik-cache.\n", Mount_Point.c_str());
	} else {
		int dataonext;
		DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
		if (Mount_Point == "/cache") {
			if (TWFunc::Path_Exists("/cache/dalvik-cache"))
				Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/cache/dalvik-cache", true);
			else
				LOGI("No '/cache/dalvik-cache' found.\n");
			// Don't unmount /cache
		} else if (Mount_Point == "/data") {
			if (dataonext) {
				int dalvikonnand;
				DataManager::GetValue(TW_BACKUP_NAND_DATA, dalvikonnand);
				if (dalvikonnand) {
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/data", true);
				}
			} else {
				if (TWFunc::Path_Exists("/data/dalvik-cache"))
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/data/dalvik-cache", true);
				else
					LOGI("No '/data/dalvik-cache' found.\n");
			}
			UnMount(false);
		} else if (Mount_Point == "/sd-ext") {
			if (dataonext) {
				string data_pth;
				DataManager::GetValue(TW_DATA_PATH, data_pth);
				string dalvik_pth = data_pth + "/dalvik-cache";
				if (TWFunc::Path_Exists(dalvik_pth))
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size(dalvik_pth, true);
				else
					LOGI("No '%s' found.\n", dalvik_pth.c_str());
			} else {
				if (TWFunc::Path_Exists("/sd-ext/dalvik-cache"))
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/sd-ext/dalvik-cache", true);
				else
					LOGI("No '/sd-ext/dalvik-cache' found.\n");
			}
			UnMount(false);
		} else if (Mount_Point == "/sdext2") {
			if (TWFunc::Path_Exists("/sdext2/dalvik-cache"))
				Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/sdext2/dalvik-cache", true);
			else
				LOGI("No '/sdext2/dalvik-cache' found.\n");
			UnMount(false);
		}
	}	
}

/* Make some logical checks to avoid all the mess with path set for DataOnExt...
 *
 * returns 	-1	INVALID PATH 
 *		 0	INITIAL PATH IS VALID
 *		 1 	CHANGED VALID PATH */
int TWPartition::CheckFor_DataOnExt(void) {
	int check;
	DataManager::GetValue(TW_DATA_ON_EXT_CHECK, check);
	if (check) {
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		LOGI(">> Checking given data_pth: '%s'\n", data_pth.c_str());
		string dtpth = data_pth;
	
		if (!Mount(true)) {
			LOGI(">> Can't mount '/sd-ext'\n");
			dtpth = "null";
			DataManager::SetValue(TW_DATA_PATH, dtpth, 1);
			DataManager::SetValue(TW_DATA_ON_EXT, 0);
			return -1;
		}
	
		// Check the root path of dtpth
		if (dtpth != "/sd-ext") { 
			// Case where the user entered a modified path(/sd-ext/ROM-NAME/data)
			string rootpth = TWFunc::Get_Root_Path(dtpth);
			if (rootpth != "/sd-ext") {
				// Don't allow any other root folder than /sd-ext
				LOGI(">> Changing data_path's root folder '%s'\n", rootpth.c_str());
				dtpth = "/sd-ext" + dtpth.substr(rootpth.size(), dtpth.size() - rootpth.size());
				LOGI(">> data_path to be checked: '%s'\n", dtpth.c_str());
			}
		}

		LOGI(">> Checking for packages.xml...\n");

		// Check if 'packages.xml' exists where it is supposed to be
		string pkg_xml_path = dtpth + "/system/packages.xml";
		LOGI("[1]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGI(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGI(">> '%s' is a valid data_pth!\n", dtpth.c_str());
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 0;
		}
		// Else check the parent dir in case we're lucky
		string parentpth = TWFunc::Get_Path(dtpth);
		pkg_xml_path = parentpth + "system/packages.xml";
		LOGI("[2]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGI(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGI(">> '%s' is a valid data_pth!\n", parentpth.substr(0, parentpth.size() - 1).c_str());
			DataManager::SetValue(TW_DATA_PATH, parentpth.substr(0, parentpth.size() - 1), 1);
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 1;
		}
		// Else check if it is located in a subfolder
		string subpth = dtpth + "/data";
		pkg_xml_path = subpth + "/system/packages.xml";
		LOGI("[3]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGI(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGI(">> '%s' is a valid data_pth!\n", subpth.c_str());
			DataManager::SetValue(TW_DATA_PATH, subpth, 1);
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 1;
		}

		// Since we reached so far, scan the entire /sd-ext for 'packages.xml'
		LOGI(">> Primary check for packages.xml FAILED.\n");
		LOGI("[4]Scanning entire sd-ext for packages.xml...\n");
		string cmd = "find /sd-ext -type f -maxdepth 4 -iname packages.xml | sed -n 's!/system/packages.xml!!g p' > /tmp/dataonextpath.txt";
		system(cmd.c_str());
		FILE *fp;
		fp = fopen("/tmp/dataonextpath.txt", "rt");
		if (fp == NULL)	{
			LOGI(">> Unable to open dataonextpath.txt.\n");
			dtpth = "null";
		} else {
			char cmdOutput[255];
			while (fgets(cmdOutput, sizeof(cmdOutput), fp) != NULL) {
				cmdOutput[strlen(cmdOutput) - 1] = '\0';
				//LOGI("cmdOutput : %s - len(%d)\n", cmdOutput, strlen(cmdOutput));		
				dtpth = cmdOutput;
				if (dtpth.size() > 7) {
					LOGI(">> '%s' is a valid data_pth!\n", dtpth.c_str());
					break;
				} else
				if (dtpth.size() < 7) {
					LOGI(">> Invalid data_pth: '%s'\n", dtpth.c_str());
					dtpth = "null";
				}
				memset(cmdOutput, 0, sizeof(cmdOutput));
			}
			fclose(fp);
		}
		if (TWFunc::Path_Exists("/tmp/dataonextpath.txt"))
			system("rm -f /tmp/dataonextpath.txt");

		if (dtpth != "null") { 
			// We found a 'packages.xml' file and the path is probably set correctly
			LOGI("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			DataManager::SetValue(TW_DATA_PATH, dtpth, 1);
			UnMount(false);
			return 1;
		} else {
			// We didn't find a 'packages.xml' file, but there is a chance that
			// the rom is fresh-installed(so no packages.xml will be present)
			// Check instead for data's folder-structure
			dtpth = data_pth;
			LOGI(">> Secondary check for packages.xml FAILED.\n");
			LOGI(">> Scanning for data structure...\n");
			LOGI("[5]Checking path: '%s'\n", dtpth.c_str());
			int dir_num = 0;
			int check = TWFunc::SubDir_Check(dtpth, "app", "local", "misc", "", "", 1);
			if (check == 1) {
				LOGI("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
			} else {
				// Move up to /sd-ext and scan from there
				dtpth = "/sd-ext";
				DIR* Dir = opendir(dtpth.c_str());
				if (Dir == NULL) {
					dtpth = "null";
					DataManager::SetValue(TW_DATA_ON_EXT, 0);
				} else {
					struct dirent* DirEntry;
					while ((DirEntry = readdir(Dir)) != NULL) {
						string dname = dtpth + "/" + DirEntry->d_name + "/data";
						if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
							continue;
						if (DirEntry->d_type == DT_DIR) {
							dir_num++;
							if (TWFunc::SubDir_Check(dname, "app", "local", "misc", "", "", 1) == 1) {
								dtpth = dname;
								LOGI("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
								DataManager::SetValue(TW_DATA_ON_EXT, 1);
								break;
							}
						}
					}
					closedir(Dir);
				}
				if (dir_num == 0) {
					LOGI(">> Tertiary check for data structure FAILED.\n");
					LOGI(">> No folders found under /sd-ext.\n");
					dtpth = "null";
					DataManager::SetValue(TW_DATA_ON_EXT, 0);
					DataManager::SetValue(TW_DATA_PATH, dtpth, 1);
					UnMount(false);
					return -1;
				}
			}	
			DataManager::SetValue(TW_DATA_PATH, dtpth, 1);
			UnMount(false);
			return 1;	
		}
	}/* else			
		LOGI("Path for DataOnExt will not be checked.\n");*/

	return 0;	
}

