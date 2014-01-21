/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
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

#include "libblkid/blkid.h"
#include "variables.h"
#include "twcommon.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "twrpDigest.hpp"
#include "twrpTar.hpp"
extern "C" {
	#include "mtdutils/mtdutils.h"
	#include "mtdutils/mounts.h"
	#include "../../external/yaffs2/yaffs2/utils/unyaffs.h"
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	#include "crypto/libcrypt_samsung/include/libcrypt_samsung.h"
#endif
#ifdef USE_EXT4
	#include "make_ext4fs.h"
#endif
#ifdef HAVE_SELINUX
	#include "selinux/selinux.h"
#endif
}

using namespace std;

#ifdef HAVE_SELINUX
extern struct selabel_handle *selinux_handle; 
#endif

struct flag_list {
	const char *name;
	unsigned flag;
};

static struct flag_list mount_flags[] = {
	{ "noatime",    MS_NOATIME },
	{ "noexec",     MS_NOEXEC },
	{ "nosuid",     MS_NOSUID },
	{ "nodev",      MS_NODEV },
	{ "nodiratime", MS_NODIRATIME },
	{ "ro",         MS_RDONLY },
	{ "rw",         0 },
	{ "remount",    MS_REMOUNT },
	{ "bind",       MS_BIND },
	{ "rec",        MS_REC },
	{ "unbindable", MS_UNBINDABLE },
	{ "private",    MS_PRIVATE },
	{ "slave",      MS_SLAVE },
	{ "shared",     MS_SHARED },
	{ "sync",       MS_SYNCHRONOUS },
	{ "defaults",   0 },
	{ 0,            0 },
};

TWPartition::TWPartition(void) {
	Can_Be_Mounted = false;
	Can_Be_Wiped = false;
	Can_Be_Backed_Up = false;
	Use_Rm_Rf = false;
	Skip_From_Restore = false;
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
	Swap = false;
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
	ORS_Mark = "";
	Display_Name = "";
	Backup_Display_Name = "";
	Restore_Display_Name = "";
	Alternate_Display_Name = "";
	Storage_Name = "";
	Backup_Name = "";
	Backup_FileName = "";
	MTD_Name = "";
	MTD_Dev = "";
	Backup_Method = NONE;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	Can_Encrypt_Backup = false;
	Use_Userdata_Encryption = false;
#endif
	Has_Data_Media = false;
	Has_Android_Secure = false;
	Is_Storage = false;
	Is_Settings_Storage = false;
	Storage_Path = "";
	Current_File_System = "";
	Fstab_File_System = "";
	Mount_Flags = 0;
	Mount_Options = "";
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
	bool skip = false;

	for (index = 0; index < line_len; index++) {
		if (full_line[index] == 34)
			skip = !skip;
		if (!skip && full_line[index] <= 32)
			full_line[index] = '\0';
	}
	Mount_Point = full_line;
	LOGINFO("Processing '%s'\n", Mount_Point.c_str());
	Backup_Path = Mount_Point;
	Storage_Path = Mount_Point;
	Display_Name = full_line + 1;
	Backup_Display_Name = Display_Name;
	Restore_Display_Name = Display_Name;
	Alternate_Display_Name = Display_Name;
	Storage_Name = Display_Name;
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
#ifdef TW_DEVICE_IS_HTC_LEO
				// Force "mtd" filesystem in case of magldr rboot format
				if (Mount_Point == "/boot" && DataManager::GetIntValue(TW_BOOT_IS_MTD) > 0) {
					Fstab_File_System = "mtd";
					Current_File_System = "mtd";
					LOGINFO("Boot partition's file-system forced to 'mtd'.\n");
				}
#endif
				MTD_Name = ptr;
				Find_MTD_Block_Device(MTD_Name);
				if (!Primary_Block_Device.empty())
					MTD_Dev = "/dev/mtd/mtd" + Primary_Block_Device.substr(Primary_Block_Device.size() - 1, 1);
			} else if (Fstab_File_System == "bml") {
				if (Mount_Point == "/boot")
					MTD_Name = "boot";
				else if (Mount_Point == "/recovery")
					MTD_Name = "recovery";
				Primary_Block_Device = ptr;
				if (*ptr != '/')
					LOGERR("Until we get better BML support, you will have to find and provide the full block device path to the BML devices e.g. /dev/block/bml9 instead of the partition name\n");
			} else if (*ptr != '/') {
				if (Display_Error)
					LOGERR("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
				else
					LOGINFO("Invalid block device on '%s', '%s', %i\n", Line.c_str(), ptr, index);
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
				LOGINFO("Unhandled fstab information: '%s', %i, line: '%s'\n", ptr, index, Line.c_str());
			}
		}
		while (index < line_len && full_line[index] != '\0')
			index++;
	}

	// Check the file system before setting up the partition
	Check_FS_Type();
	if (Is_File_System(Current_File_System)) {
		if (Is_Present)
			Setup_File_System(Display_Error);
		if (Mount_Point == "/system") {
			if (Is_Present) {
				ORS_Mark = "S";
				Display_Name = "System";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = Display_Name;
				Storage_Name = Display_Name;
				Wipe_Available_in_GUI = true;
				Can_Be_Backed_Up = true;
#ifdef TW_DEVICE_IS_HTC_LEO
				Check_BuildProp();
#endif
			}
		} else if (Mount_Point == "/data") {
			if (Is_Present) {
				ORS_Mark = "D";
				Display_Name = "Data";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = "DataOnNand";
				Storage_Name = Display_Name;
				Wipe_Available_in_GUI = true;
				Wipe_During_Factory_Reset = true;
				Can_Be_Backed_Up = true;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
#endif
#ifdef RECOVERY_SDCARD_ON_DATA
				Storage_Name = "Internal Storage";
				Has_Data_Media = true;
				Is_Storage = true;
				Is_Settings_Storage = true;
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
					LOGINFO("Data already decrypted, new block device: '%s'\n", crypto_blkdev);
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
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /data
			}
		} else if (Mount_Point == "/cache") {
			if (Is_Present) {
				ORS_Mark = "C";
				Display_Name = "Cache";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = Display_Name;
				Storage_Name = Display_Name;
				Wipe_Available_in_GUI = true;
				Wipe_During_Factory_Reset = true;
				Can_Be_Backed_Up = true;
				Recreate_Cache_Recovery_Folder();
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /cache (Is this really needed?)
			}
		} else if (Mount_Point == "/datadata") {
			if (Is_Present) {
				Wipe_During_Factory_Reset = true;
				Display_Name = "DataData";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = Display_Name;
				Storage_Name = Display_Name;
				Is_SubPartition = true;
				SubPartition_Of = "/data";
				DataManager::SetValue(TW_HAS_DATADATA, 1);
				Can_Be_Backed_Up = true;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
#endif
			} else
				DataManager::SetValue(TW_HAS_DATADATA, 0);
		} else if (Mount_Point == "/sd-ext") {
			if (Is_Present) {
				ORS_Mark = "E";
				Wipe_During_Factory_Reset = true;
				Display_Name = "SD-Ext";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = "DataOnExt";
				Storage_Name = Display_Name;
				Wipe_Available_in_GUI = true;
				Removable = true;
				Can_Be_Backed_Up = true;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
#endif
				DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
				//DataManager::SetValue(TW_SDEXT_SIZE, (int)(Size / 1048576));
				//DataManager::SetValue("tw_sdpart_file_system", Current_File_System, 1);
#ifdef TW_DEVICE_IS_HTC_LEO
				CheckFor_DataOnExt(); // check if data_path is valid and try to fix otherwise
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /sd-ext
				CheckFor_NativeSD(); // check for NativeSD installations in /sd-ext
				DataManager::GetValue(TW_DATA_PATH, Path_For_DataOnExt); // save TW_DATA_PATH to Path_For_DataOnExt
#else
				CheckFor_Dalvik_Cache(); // check for dalvik-cache in /sd-ext
#endif
			} else {
				DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
				DataManager::SetValue(TW_SDEXT_SIZE, 0);
				DataManager::SetValue(TW_DATA_ON_EXT, 0);
				DataManager::SetValue(TW_SKIP_NATIVESD, 0);
				DataManager::SetValue(TW_DATA_ON_EXT_CHECK, 0);
			}
		} else if (Mount_Point == "/sdext2") {
			if (Is_Present) {
				ORS_Mark = "F";
				Wipe_During_Factory_Reset = true;
				Display_Name = "SDExt2";
				Backup_Display_Name = Display_Name;
				Restore_Display_Name = Display_Name;
				Alternate_Display_Name = Display_Name;
				Storage_Name = Display_Name;
				Wipe_Available_in_GUI = true;
				Removable = true;
				Can_Be_Backed_Up = true;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
#endif
				DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
				//DataManager::SetValue(TW_SDEXT2_SIZE, (int)(Size / 1048576));
				//DataManager::SetValue("tw_sdpart2_file_system", Current_File_System, 1);
			} else {
				DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
				DataManager::SetValue(TW_SDEXT2_SIZE, 0);
			}
		} else if (Mount_Point == "/boot") {
			ORS_Mark = "B";
			Display_Name = "Boot";
			Backup_Display_Name = Display_Name;
			Restore_Display_Name = Display_Name;
			Alternate_Display_Name = Display_Name;
			Wipe_Available_in_GUI = true;
			DataManager::SetValue("tw_boot_is_mountable", 1);
			Can_Be_Backed_Up = true;
		}	
#ifdef TW_EXTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
			if (Is_Present) {
				Is_Storage = true;
				Is_Settings_Storage = true;
				Storage_Path = EXPAND(TW_EXTERNAL_STORAGE_PATH);
				Removable = true;
				Wipe_Available_in_GUI = true;
				DataManager::SetValue(TW_HAS_EXTERNAL, 1);
			} else
				DataManager::SetValue(TW_HAS_EXTERNAL, 0);
		}
#else
		if (Mount_Point == "/sdcard" || Mount_Point == "/external_sd" || Mount_Point == "/external_sdcard") {
			if (Is_Present) {
				Is_Storage = true;
				Is_Settings_Storage = true;
				Storage_Path = "/sdcard";
				Display_Name = "SDCard";
				Removable = true;
				Wipe_Available_in_GUI = true;
				DataManager::SetValue(TW_HAS_EXTERNAL, 1);
				//DataManager::SetValue("tw_sdcard_file_system", Current_File_System, 1);
	#ifndef RECOVERY_SDCARD_ON_DATA
				Setup_AndSec();
				Mount_Storage_Retry();
	#endif
			} else
				DataManager::SetValue(TW_HAS_EXTERNAL, 0);
		}
#endif
#ifdef TW_INTERNAL_STORAGE_PATH
		if (Mount_Point == EXPAND(TW_INTERNAL_STORAGE_PATH)) {
			if (Is_Present) {
				Is_Storage = true;
				Is_Settings_Storage = true;
				Storage_Path = EXPAND(TW_INTERNAL_STORAGE_PATH);
				Wipe_Available_in_GUI = true;
	#ifndef RECOVERY_SDCARD_ON_DATA
				Setup_AndSec();
				Mount_Storage_Retry();
	#endif
			}
		}
#else
		if (Mount_Point == "/emmc" || Mount_Point == "/internal_sd" || Mount_Point == "/internal_sdcard") {
			if (Is_Present) {
				Is_Storage = true;
				Is_Settings_Storage = true;
				Wipe_Available_in_GUI = true;
				Storage_Path = "/emmc";
	#ifndef RECOVERY_SDCARD_ON_DATA
				Setup_AndSec();
				Mount_Storage_Retry();
	#endif
			}
		}
#endif		
	} else if (Is_Image(Current_File_System)) {
		if (Is_Present)
			Setup_Image(Display_Error);
		if (Mount_Point == "/boot") {
			ORS_Mark = "B";
			Display_Name = "Boot";
			Backup_Display_Name = Display_Name;
			Restore_Display_Name = Display_Name;
			Alternate_Display_Name = Display_Name;
			Can_Be_Backed_Up = true;
			Wipe_Available_in_GUI = true;
			DataManager::SetValue("tw_boot_is_mountable", 0);
		} else if (Mount_Point == "/recovery") {
			ORS_Mark = "R";
			Display_Name = "Recovery";
			Backup_Display_Name = Display_Name;
			Restore_Display_Name = Display_Name;
			Alternate_Display_Name = Display_Name;
			Can_Be_Backed_Up = true;
		}
	} else if (Is_Swap(Current_File_System)) {
		Swap = true;
		Removable = true;
		Can_Be_Wiped = false;
		Wipe_During_Factory_Reset = false;
		Wipe_Available_in_GUI = false;
		if (Mount_Point == "/sdext2") {
			LOGINFO("/sdext2 points to a swap partition.\n");
			DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
			DataManager::SetValue(TW_SDEXT2_SIZE, 0);
		} else if (Mount_Point == "/sd-ext") {
			LOGINFO("/sd-ext points to a swap partition.\n");
			DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
			DataManager::SetValue(TW_SDEXT_SIZE, 0);
		}
	} else {
		// Leave Current_File_System == ptr
		if (Display_Error)
			LOGERR("Unknown File System: '%s'\n", Current_File_System.c_str());
		else
			LOGINFO("Unknown File System: '%s'\n", Current_File_System.c_str());
		return 0;
	}
	// Process any custom flags
	if (Flags.size() > 0)
		Process_Flags(Flags, Display_Error);

	return true;
}

bool TWPartition::Process_FS_Flags(string& Options, int Flags) {
	int i;
	char *p;
	char *savep;
	char fs_options[250];

	strlcpy(fs_options, Options.c_str(), sizeof(fs_options));
	Options = "";

	p = strtok_r(fs_options, ",", &savep);
	while (p) {
		/* Look for the flag "p" in the flag list "fl"
		* If not found, the loop exits with fl[i].name being null.
		*/
		for (i = 0; mount_flags[i].name; i++) {
			if (strncmp(p, mount_flags[i].name, strlen(mount_flags[i].name)) == 0) {
				Flags |= mount_flags[i].flag;
				break;
			}
		}

		if (!mount_flags[i].name) {
			if (Options.size() > 0)
				Options += ",";
			Options += p;
		}
		p = strtok_r(NULL, ",", &savep);
	}

	return true;
}

bool TWPartition::Process_Flags(string Flags, bool Display_Error) {
	char flags[MAX_FSTAB_LINE_LENGTH];
	int flags_len, index = 0, ptr_len;
	char* ptr;
	bool skip = false, has_display_name = false, has_storage_name = false, has_backup_name = false;

	strcpy(flags, Flags.c_str());
	flags_len = Flags.size();
	for (index = 0; index < flags_len; index++) {
		if (flags[index] == 34)
			skip = !skip;
		if (!skip && flags[index] == ';')
			flags[index] = '\0';
	}

	index = 0;
	while (index < flags_len) {
		while (index < flags_len && flags[index] == '\0')
			index++;
		if (index >= flags_len)
			continue;
		ptr = flags + index;
		ptr_len = strlen(ptr);
		if (strcmp(ptr, "removable") == 0) {
			Removable = true;
		} else if (strcmp(ptr, "storage") == 0) {
			Is_Storage = true;
		} else if (strcmp(ptr, "settingsstorage") == 0) {
			Is_Storage = true;
		} else if (strcmp(ptr, "canbewiped") == 0) {
			Can_Be_Wiped = true;
		} else if (strcmp(ptr, "usermrf") == 0) {
			Use_Rm_Rf = true;
		} else if (ptr_len > 7 && strncmp(ptr, "backup=", 7) == 0) {
			ptr += 7;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y')
				Can_Be_Backed_Up = true;
			else
				Can_Be_Backed_Up = false;
		} else if (strcmp(ptr, "wipeingui") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
		} else if (strcmp(ptr, "wipeduringfactoryreset") == 0) {
			Can_Be_Wiped = true;
			Wipe_Available_in_GUI = true;
			Wipe_During_Factory_Reset = true;
		} else if (ptr_len > 15 && strncmp(ptr, "subpartitionof=", 15) == 0) {
			ptr += 15;
			Is_SubPartition = true;
			SubPartition_Of = ptr;
		} else if (strcmp(ptr, "ignoreblkid") == 0) {
			Ignore_Blkid = true;
		} else if (strcmp(ptr, "retainlayoutversion") == 0) {
			Retain_Layout_Version = true;
		} else if (ptr_len > 8 && strncmp(ptr, "symlink=", 8) == 0) {
			ptr += 8;
			Symlink_Path = ptr;
		} else if (ptr_len > 8 && strncmp(ptr, "display=", 8) == 0) {
			has_display_name = true;
			ptr += 8;
			if (*ptr == '\"') ptr++;
			Display_Name = ptr;
			if (Display_Name.substr(Display_Name.size() - 1, 1) == "\"") {
				Display_Name.resize(Display_Name.size() - 1);
			}
		} else if (ptr_len > 11 && strncmp(ptr, "storagename=", 11) == 0) {
			has_storage_name = true;
			ptr += 11;
			if (*ptr == '\"') ptr++;
			Storage_Name = ptr;
			if (Storage_Name.substr(Storage_Name.size() - 1, 1) == "\"") {
				Storage_Name.resize(Storage_Name.size() - 1);
			}
		} else if (ptr_len > 11 && strncmp(ptr, "backupname=", 10) == 0) {
			has_backup_name = true;
			ptr += 10;
			if (*ptr == '\"') ptr++;
			Backup_Display_Name = ptr;
			if (Backup_Display_Name.substr(Backup_Display_Name.size() - 1, 1) == "\"") {
				Backup_Display_Name.resize(Backup_Display_Name.size() - 1);
			}
		} else if (ptr_len > 10 && strncmp(ptr, "blocksize=", 10) == 0) {
			ptr += 10;
			Format_Block_Size = atoi(ptr);
		} else if (ptr_len > 7 && strncmp(ptr, "length=", 7) == 0) {
			ptr += 7;
			Length = atoi(ptr);
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		} else if (ptr_len > 17 && strncmp(ptr, "canencryptbackup=", 17) == 0) {
			ptr += 17;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y')
				Can_Encrypt_Backup = true;
			else
				Can_Encrypt_Backup = false;
		} else if (ptr_len > 21 && strncmp(ptr, "userdataencryptbackup=", 21) == 0) {
			ptr += 21;
			if (*ptr == '1' || *ptr == 'y' || *ptr == 'Y') {
				Can_Encrypt_Backup = true;
				Use_Userdata_Encryption = true;
			} else {
				Use_Userdata_Encryption = false;
			}
#endif
		} else if (ptr_len > 8 && strncmp(ptr, "fsflags=", 8) == 0) {
			ptr += 8;
			if (*ptr == '\"') ptr++;

			Mount_Options = ptr;
			if (Mount_Options.substr(Mount_Options.size() - 1, 1) == "\"") {
				Mount_Options.resize(Mount_Options.size() - 1);
			}
			Process_FS_Flags(Mount_Options, Mount_Flags);
		} else {
			if (Display_Error)
				LOGERR("Unhandled flag: '%s'\n", ptr);
			else
				LOGINFO("Unhandled flag: '%s'\n", ptr);
		}
		while (index < flags_len && flags[index] != '\0')
			index++;
	}
	if (has_display_name && !has_storage_name && !Has_Data_Media)
		Storage_Name = Display_Name;
	if (!has_display_name && has_storage_name && !Has_Data_Media)
		Display_Name = Storage_Name;
	if (has_display_name && !has_backup_name && Backup_Display_Name != "Android Secure")
		Backup_Display_Name = Display_Name;
	if (!has_display_name && has_backup_name)
		Display_Name = Backup_Display_Name;
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
#ifdef TW_INCLUDE_F2FS
	|| File_System == "f2fs"
#endif
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
		LOGINFO("Unhandled file system '%s' on image '%s'\n", Current_File_System.c_str(), Display_Name.c_str());
	if (Find_Partition_Size()) {
		Can_Be_Wiped = true;
		Used = Size;
		Backup_Size = Size;
	} else {
		if (Display_Error)
			LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		else
			LOGINFO("Unable to find partition size for '%s'\n", Mount_Point.c_str());
	}
}

/* Setup an extra boot partition (for cLK bootloader)
 */
int TWPartition::Setup_Extra_Boot(string name, string mtd_num) {
	if (PartitionManager.Find_Partition_By_Path("/" + name) != NULL) {
		LOGINFO("Partition '%s' already processed.\n", name.c_str());
		return 1;
	}

	string x = name;
	x.resize(1);
	if (x == "s")
		ORS_Mark = "U";
	else
		ORS_Mark = x;
	Display_Name = x + "Boot";
	Backup_Display_Name = Display_Name;
	Restore_Display_Name = Display_Name;
	Alternate_Display_Name = Display_Name;

	Mount_Point = "/" + name;
	Backup_Path = Mount_Point;

	Fstab_File_System = "mtd";
	Current_File_System = "mtd";

	Can_Be_Backed_Up = true;
	Can_Be_Mounted = false;
	Can_Be_Wiped = true;
	Wipe_Available_in_GUI = true;

	Primary_Block_Device = "/dev/block/mtdblock" + mtd_num;
	MTD_Dev = "/dev/mtd/mtd" + mtd_num;
	Find_Actual_Block_Device();
	if (!Is_Present)
		return 1;

	Storage_Name = name;
	Backup_Name = name;
	MTD_Name = name;

	Backup_Method = FLASH_UTILS;

	Find_Partition_Size();
	Used = Size;
	Backup_Size = Size;

	return 0;
}

void TWPartition::Setup_AndSec(void) {
	Backup_Display_Name = "Android Secure";
	Restore_Display_Name = Backup_Display_Name;
	Alternate_Display_Name = Display_Name;
	Backup_Name = "and-sec";
	Can_Be_Backed_Up = true;
	Has_Android_Secure = true;
	ORS_Mark = "A";
	Symlink_Path = Mount_Point + "/.android_secure";
	Symlink_Mount_Point = "/and-sec";
	Backup_Path = Symlink_Mount_Point;
	Make_Dir("/and-sec", true);
	Recreate_AndSec_Folder();
}

void TWPartition::Recreate_Cache_Recovery_Folder(void) {
	if (Mount_Point != "/cache")
		return;

	string path = "/cache/recovery";
	if (!Mount(false)) {
		LOGERR("Unable to recreate /cache/recovery folder.\n");
	} else if (!TWFunc::Path_Exists(path + "/")) {
		LOGINFO("Recreating /cache/recovery folder.\n");
		mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
	}
}

void TWPartition::Recreate_AndSec_Folder(void) {
	if (!Has_Android_Secure)
		return;

	if (!Is_Mounted())
		Mount_Storage_Retry();
	if (!Is_Mounted())
		LOGERR("Unable to recreate android secure folder.\n");
	else if (!TWFunc::Path_Exists(Symlink_Path)) {
		LOGINFO("Recreating android secure folder.\n");
		mkdir(Symlink_Path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
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
		LOGERR("Unable to recreate folder for DataOnExt.\n");
	} else if (!TWFunc::Path_Exists(data_pth)) {
		LOGINFO("Recreating data folder for DataOnExt.\n");
		if (!TWFunc::Recursive_Mkdir(data_pth))
			LOGINFO("Could not create '%s'\n", data_pth.c_str());
		UnMount(true);
	}
}

void TWPartition::Recreate_Media_Folder(void) {
	if (!Mount(true)) {
		LOGERR("Unable to recreate /data/media folder.\n");
	} else if (!TWFunc::Path_Exists("/data/media/")) {
		PartitionManager.Mount_By_Path(Symlink_Mount_Point, true);
		LOGINFO("Recreating /data/media folder.\n");
		mkdir("/data/media", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
		PartitionManager.UnMount_By_Path(Symlink_Mount_Point, true);
	}
}

bool TWPartition::Make_Dir(string Path, bool Display_Error) {
	if (!TWFunc::Path_Exists(Path)) {
		if (mkdir(Path.c_str(), 0777) == -1) {
			if (Display_Error)
				LOGERR("Can not create '%s' folder.\n", Path.c_str());
			else
				LOGINFO("Can not create '%s' folder.\n", Path.c_str());
			return false;
		} else {
			LOGINFO("Created '%s' folder.\n", Path.c_str());
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
			LOGERR("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		else
			LOGINFO("Invalid symlink path '%s' found on block device '%s'\n", device, Block.c_str());
		return;
	} else {
		Block = device;
		//LOGINFO("Block: '%s'\n", Block.c_str());
		return;
	}
}

void TWPartition::Find_Actual_Block_Device(void) {
	if (TWFunc::Path_Exists(Primary_Block_Device)) {		
		Actual_Block_Device = (Is_Decrypted ? Decrypted_Block_Device : Primary_Block_Device);
		Is_Present = true;
		//LOGINFO("Actual_Block_Device: '%s'\n", Actual_Block_Device.c_str());
		return;
	} else if (!Alternate_Block_Device.empty() && TWFunc::Path_Exists(Alternate_Block_Device)) {
		Actual_Block_Device = Alternate_Block_Device;
		Is_Present = true;
		//LOGINFO("Actual_Block_Device: '%s'\n", Actual_Block_Device.c_str());
		return;
	}
	//LOGINFO("Actual_Block_Device: 'null'\n");
	Actual_Block_Device = "";
	Is_Present = false;
}

bool TWPartition::Find_MTD_Block_Device(string MTD_Name) {
	FILE *fp = NULL;
	char line[255];

	fp = fopen("/proc/mtd", "rt");
	if (fp == NULL) {
		LOGERR("Device does not support /proc/mtd\n");
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
				//LOGINFO("Primary_Block_Device: '%s'\n", Primary_Block_Device.c_str());
				fclose(fp);
				return true;
			}
		}
	}
	fclose(fp);
	//LOGINFO("Primary_Block_Device: 'null'\n");
	return false;
}

/************************************************************************************
 * Get partition's size
 */
bool TWPartition::Update_Size(bool Display_Error) {
	bool ret = false, Was_Already_Mounted = false;

	if (!Can_Be_Mounted && !Is_Encrypted)
		return false;

	if (Swap) {
		Can_Be_Wiped = false;
		Wipe_During_Factory_Reset = false;
		Wipe_Available_in_GUI = false;
		Backup_Size = 0;
		return true;
	}

	Was_Already_Mounted = Is_Mounted();
	if (!Was_Already_Mounted) {
		if (Removable || Is_Encrypted) {
			if (!Mount(Display_Error))
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
				UnMount(Display_Error);
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
	if (Mount_Point == "/cache") {
		if (Size - Backup_Size < 1024) {
			LOGINFO("Cleaning /cache/recovery logs to free some space...\n");
			string rm;
			unsigned long long l = 0;

			if (TWFunc::Path_Exists("/cache/recovery/last_log"))
				l = TWFunc::Get_File_Size("/cache/recovery/last_log");
			rm = "rm -f /cache/recovery/last_log";
			if (TWFunc::Exec_Cmd(rm) == 0)
				Backup_Size -= l;
			if (TWFunc::Path_Exists("/cache/recovery/last_install"))
				l = TWFunc::Get_File_Size("/cache/recovery/last_install");
			rm = "rm -f /cache/recovery/last_install";
			if (TWFunc::Exec_Cmd(rm) == 0)
				Backup_Size -= l;
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
			LOGINFO("Data backup size is %iMB, size: %iMB, used: %iMB, free: %iMB, in data/media: %iMB.\n", bak, total, us, fre, datmed);
		} else {
			if (!Was_Already_Mounted)
				UnMount(Display_Error);
			return false;
		}
	} else if (Has_Android_Secure || !Symlink_Mount_Point.empty()) {
		if (Mount(Display_Error)) {
			if (TWFunc::Path_Exists(Backup_Path))
				Backup_Size = TWFunc::Get_Folder_Size(Backup_Path, Display_Error);
		}
	}
	if (!Was_Already_Mounted)
		UnMount(Display_Error);
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
				LOGERR("Unable to statfs '%s'\n", Local_Path.c_str());
			else
				LOGINFO("Unable to statfs '%s'\n", Local_Path.c_str());
		}
		return false;
	}
	Size = (st.f_blocks * st.f_bsize);
	Used = ((st.f_blocks - st.f_bfree) * st.f_bsize);
	Free = (st.f_bfree * st.f_bsize);
	if (Mount_Point == "/sd-ext") {
		if (Size == 0)
			DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
#ifdef TW_DEVICE_IS_HTC_LEO
		// sd-ext is a special case due to DataOnExt & NativeSD features
		int dataonext, skip_native;
		DataManager::GetValue(TW_SKIP_NATIVESD, skip_native);
		DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (!Path_For_DataOnExt.empty()) {
			//LOGINFO("Path_For_DataOnExt: '%s'\n", Path_For_DataOnExt.c_str());
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
			//LOGINFO("TW_DATA_PATH: '%s'\n", data_pth.c_str());
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
			//LOGINFO("DataOnExt mode: OFF\n");
#else
			CheckFor_Dalvik_Cache();
#endif
			Backup_Size = Used;
#ifdef TW_DEVICE_IS_HTC_LEO
			if (Backup_Size > 0 && skip_native)
				Backup_Size -= NativeSD_Size;
		}
	} else if (Mount_Point == "/data") {
		CheckFor_Dalvik_Cache();
		Backup_Size = Used;
#endif
	} else {
		Backup_Size = Used;
	}
	if (Backup_Size > 0 && skip_dalvik)
		Backup_Size -= Dalvik_Cache_Size;
	
	return true;
}

bool TWPartition::Get_Size_Via_df(bool Display_Error) {
	FILE* fp;
	char line[512];
	int include_block = 1;
	unsigned int min_len;
	string Command;

	int skip_dalvik;
	DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);

	min_len = Actual_Block_Device.size() + 2;
	Command = "df " + Mount_Point + " > /tmp/dfoutput.txt";
	TWFunc::Exec_Cmd(Command);
	fp = fopen("/tmp/dfoutput.txt", "rt");
	if (fp == NULL) {
		LOGINFO("Unable to open /tmp/dfoutput.txt.\n");
		return false;
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
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
	Command = "cd /tmp && rm -f dfoutput.txt";
	TWFunc::Exec_Cmd(Command);
	if (Mount_Point == "/sd-ext") {
#ifdef TW_DEVICE_IS_HTC_LEO
		// sd-ext is a special case due to DataOnExt & NativeSD features
		int dataonext, skip_native;
		DataManager::GetValue(TW_SKIP_NATIVESD, skip_native);
		DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (!Path_For_DataOnExt.empty()) {
			//LOGINFO("Path_For_DataOnExt: '%s'\n", Path_For_DataOnExt.c_str());
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
			//LOGINFO("TW_DATA_PATH: '%s'\n", data_pth.c_str());
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
			//LOGINFO("DataOnExt mode: OFF\n");
#else
			CheckFor_Dalvik_Cache();
#endif
			Backup_Size = Used;
#ifdef TW_DEVICE_IS_HTC_LEO
			if (Backup_Size > 0 && skip_native)
				Backup_Size -= NativeSD_Size;
		}
	} else if (Mount_Point == "/data") {
		CheckFor_Dalvik_Cache();
		Backup_Size = Used;
#endif
	} else {
		Backup_Size = Used;
	}
	if (Backup_Size > 0 && skip_dalvik)
		Backup_Size -= Dalvik_Cache_Size;
	fclose(fp);
	return true;
}

unsigned long long TWPartition::Get_Blk_Size(void) {
	FILE* fp;
	char line[512];
	string tmpdevice;
	unsigned long long primary_blk_sz = 0, alternate_blk_sz = 0;
	
	fp = fopen("/proc/dumchar_info", "rt");
	if (fp != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			char label[32], device[32];
			unsigned long size = 0;

			sscanf(line, "%s %lx %*lx %*lu %s", label, &size, device);

			// Skip header, annotation  and blank lines
			if ((strncmp(device, "/dev/", 5) != 0) || (strlen(line) < 8))
				continue;

			tmpdevice = "/dev/";
			tmpdevice += label;
			if (!Alternate_Block_Device.empty() && tmpdevice == Alternate_Block_Device)
				alternate_blk_sz = size;
			else if (tmpdevice == Primary_Block_Device)			
				primary_blk_sz = size;
		}
		goto done;
	}

	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		unsigned long major, minor, blocks;
		char device[512];
		char tmpString[64];

		if (strlen(line) < 7 || line[0] == 'm')
			continue;

		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (!Alternate_Block_Device.empty() && tmpdevice == Alternate_Block_Device)
			alternate_blk_sz = blocks * 1024ULL;
		else if (tmpdevice == Primary_Block_Device)			
			primary_blk_sz = blocks * 1024ULL;
	}

done:
	fclose(fp);
	if (primary_blk_sz > 0)
		return primary_blk_sz;
	if (alternate_blk_sz > 0)
		return alternate_blk_sz;

	return 0;
}

bool TWPartition::Find_Partition_Size(void) {
	unsigned long long blk_sz = Get_Blk_Size();	
	if (blk_sz > 0) {
		Size = blk_sz;
		return true;
	}
	return false; // block dev not present!
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

	// Check file system before mounting
	/*if (!Check_FS_Type()) {
		LOGINFO("Unable to find actual block device for %s.\n", Mount_Point.c_str());
		return false;
	}*/
	if (Current_File_System == "swap")
		return false;
	if (Current_File_System == "yaffs2") {
		// mount an MTD partition as a YAFFS2 filesystem.
		const unsigned long flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
		int ret = mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), flags, NULL);
		if (ret < 0) {
			ret = mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), flags | MS_RDONLY, NULL);
			if (ret < 0) {
				if (Display_Error)
					LOGERR("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				else
					LOGINFO("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				return false;
			} else {
				LOGINFO("Mounted '%s' (MTD) as RO\n", Mount_Point.c_str());
				return true;
			}
		} else {
			struct stat st;
			string test_path = Mount_Point;
			ret = stat(test_path.c_str(), &st);
			if (ret < 0) {
				if (Display_Error)
					LOGERR("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				else
					LOGINFO("Failed to mount '%s' (MTD)\n", Mount_Point.c_str());
				return false;
			}
			mode_t new_mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
			if (new_mode != st.st_mode) {
				LOGINFO("Fixing execute permissions for %s\n", Mount_Point.c_str());
				ret = chmod(Mount_Point.c_str(), new_mode);
				if (ret < 0) {
					if (Display_Error)
						LOGERR("Couldn't fix permissions for %s: %s\n", Mount_Point.c_str(), strerror(errno));
					else
						LOGINFO("Couldn't fix permissions for %s: %s\n", Mount_Point.c_str(), strerror(errno));
					return false;
				}
			}
			return true;			
		}
	}
	string Command;
	if (Current_File_System == "nilfs2" && TWFunc::Path_Exists("/sbin/mount.nilfs2")) {
		Command = "mount.nilfs2 -f " + Actual_Block_Device + " " + Mount_Point;
		if (TWFunc::Exec_Cmd(Command) != 0)
			LOGINFO("Mounting '%s' using 'mount.nilfs2' failed.", Actual_Block_Device.c_str());
	}
	if(Current_File_System == "exfat" || Current_File_System == "ntfs") {
		if (Current_File_System == "exfat") {
			if (TWFunc::Path_Exists("/sbin/exfat-fuse")) {
				Command = "exfat-fuse -o nonempty,big_writes,max_read=131072,max_write=131072 " + Actual_Block_Device + " " + Mount_Point;
				if (TWFunc::Exec_Cmd(Command) != 0)
					return false;
			} else
				return false;
		} else if (Current_File_System == "ntfs") {
			if (TWFunc::Path_Exists("/sbin/ntfs-3g")) {
				Command = "ntfs-3g -o rw,umask=0 " + Actual_Block_Device + " " + Mount_Point;
				if (TWFunc::Exec_Cmd(Command) != 0) {
					Command = "mount -o rw,umask=0 " + Actual_Block_Device + " " + Mount_Point;
					if (TWFunc::Exec_Cmd(Command) != 0)
						return false;
				}
			} else {
				Command = "mount -o rw,umask=0 " + Actual_Block_Device + " " + Mount_Point;
				if (TWFunc::Exec_Cmd(Command) != 0)
					return false;
			}
		}

		// .android_secure
		if (!Symlink_Mount_Point.empty() && TWFunc::Path_Exists(Symlink_Path)) {
			Command = "mount '" + Symlink_Path + "' '" + Symlink_Mount_Point + "'";
			if (TWFunc::Exec_Cmd(Command) != 0)
				return false;
			if (TWFunc::Path_Exists(Backup_Path))
				Backup_Size = TWFunc::Get_Folder_Size(Backup_Path, Display_Error);
			return true;
		}
		return false;
	}
	if (mount(Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str(), Mount_Flags, Mount_Options.c_str()) != 0) {
		LOGINFO("Unable to natively mount:\n'%s' => '%s' as '%s'\n  Retrying using 'mount'...\n", Actual_Block_Device.c_str(), Mount_Point.c_str(), Current_File_System.c_str());
		Command = "mount " + Actual_Block_Device + " " + Mount_Point;
		if (TWFunc::Exec_Cmd(Command) == 0)
			return true;
		if (Display_Error)
			LOGERR("Mounting failed\n");
		else
			LOGINFO("Mounting failed\n");
		return false;
	} else {
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		string MetaEcfsFile = EXPAND(TW_EXTERNAL_STORAGE_PATH);
		MetaEcfsFile += "/.MetaEcfsFile";
		if (EcryptFS_Password.size() > 0 && PartitionManager.Mount_By_Path("/data", false) && TWFunc::Path_Exists(MetaEcfsFile)) {
			if (mount_ecryptfs_drive(EcryptFS_Password.c_str(), Mount_Point.c_str(), Mount_Point.c_str(), 0) != 0) {
				if (Display_Error)
					LOGERR("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
				else
					LOGINFO("Unable to mount ecryptfs for '%s'\n", Mount_Point.c_str());
			} else {
				LOGINFO("Successfully mounted ecryptfs for '%s'\n", Mount_Point.c_str());
				Is_Decrypted = true;
			}
		} else if (Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH)) {
			if (Is_Decrypted)
				LOGINFO("Mounting external storage, '%s' is not encrypted\n", Mount_Point.c_str());
			Is_Decrypted = false;
		}
#endif
		// .android_secure
		if (!Symlink_Mount_Point.empty() && TWFunc::Path_Exists(Symlink_Path)) {
			Command = "mount '" + Symlink_Path + "' '" + Symlink_Mount_Point + "'";
			if (TWFunc::Exec_Cmd(Command) != 0)
				return false;
			if (TWFunc::Path_Exists(Backup_Path))
				Backup_Size = TWFunc::Get_Folder_Size(Backup_Path, Display_Error);
			return true;
		}
		return true;
	}

	return true;
}

bool TWPartition::UnMount(bool Display_Error) {
	if (Is_Mounted()) {
		int never_unmount_system;
		string Command, result;

		DataManager::GetValue(TW_DONT_UNMOUNT_SYSTEM, never_unmount_system);
		if (never_unmount_system == 1 && Mount_Point == "/system")
			return true; // Never unmount system if you're not supposed to unmount it

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		if (EcryptFS_Password.size() > 0) {
			if (unmount_ecryptfs_drive(Mount_Point.c_str()) != 0) {
				if (Display_Error)
					LOGERR("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
				else
					LOGINFO("Unable to unmount ecryptfs for '%s'\n", Mount_Point.c_str());
			} else {
				LOGINFO("Successfully unmounted ecryptfs for '%s'\n", Mount_Point.c_str());
			}
		}
#endif

		if (Current_File_System == "nilfs2" && TWFunc::Path_Exists("/sbin/umount.nilfs2")) {
			Command = "umount.nilfs2 -n " + Symlink_Mount_Point;
			if (TWFunc::Exec_Cmd(Command) == 0)
				return true;
		}

		if (!Symlink_Mount_Point.empty())
			umount(Symlink_Mount_Point.c_str());

		if (umount(Mount_Point.c_str()) != 0) {
			LOGINFO("Unable to unmount '%s'\nRetrying with 'umount -l'\n", Mount_Point.c_str());
			Command = "umount -l " + Mount_Point;
			if (TWFunc::Exec_Cmd(Command) == 0) {
				return true;
			}
			if (Display_Error)
				LOGERR("Unmounting failed\n");
			else
				LOGINFO("Unmounting failed\n");
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
	if (!Mount(false)) {
		int retry_count = 5;
		while (retry_count > 0 && !Mount(false)) {
			usleep(500000);
			retry_count--;
		}
		Mount(false);
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
	string theme_path, tmp_path = "/tmp/ui.zip";

	if (!Can_Be_Wiped) {
		LOGERR("Partition '%s' cannot be wiped.\n", Mount_Point.c_str());
		return false;
	}

	if (Mount_Point == "/cache")
		Log_Offset = 0;

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	if (Mount_Point == "/data" && Mount(false)) {
		if (TWFunc::Path_Exists("/data/system/edk_p_sd"))
			TWFunc::copy_file("/data/system/edk_p_sd", "/tmp/edk_p_sd", 0600);
	}
#endif
	// Save theme file if in use
	if (Mount_Point == "/sdcard") {
		theme_path = DataManager::GetStrValue(TW_SEL_THEME_PATH);
		if (!theme_path.empty())
			rename(theme_path.c_str(), tmp_path.c_str());
	}

	if (Retain_Layout_Version && Mount(false) && TWFunc::Path_Exists(Layout_Filename))
		TWFunc::copy_file(Layout_Filename, "/.layout_version", 0600);
	else
		unlink("/.layout_version");

#ifdef TW_DEVICE_IS_HTC_LEO
	if (Mount_Point == "/sd-ext") {
		int dataonext;
		string data_pth;
		dataonext = DataManager::GetIntValue(TW_DATA_ON_EXT);
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (dataonext && data_pth != "/sd-ext") {
			if (!Mount(true))
				return false;
			struct stat st;
			LOGINFO("data_pth = '%s'\n", data_pth.c_str());
			if (stat(data_pth.c_str(), &st) == 0) {
				gui_print("Wiping %s\n", Alternate_Display_Name.c_str());
				string Command;
				Command = "rm -rf " + data_pth+ "/*";
				TWFunc::Exec_Cmd(Command);
				Command = "rm -rf " + data_pth + "/.*";
				TWFunc::Exec_Cmd(Command);
				gui_print("[%s wipe done]\n", Alternate_Display_Name.c_str());
    	    		} else
				gui_print("[%s was not detected]\n", Alternate_Display_Name.c_str());
			UnMount(true);
			wiped = true;
			goto done;
		}
	}
#endif

	if (Has_Data_Media) {
		wiped = Wipe_Data_Without_Wiping_Media();
	} else {

		DataManager::GetValue(TW_RM_RF_VAR, check);
		if (check || Use_Rm_Rf)
			wiped = Wipe_RMRF();
		else if (New_File_System == "ext4")
			wiped = Wipe_EXT4();
		else if (New_File_System == "ext2" || New_File_System == "ext3")
			wiped = Wipe_EXT23(New_File_System);
		else if (New_File_System == "vfat")
			wiped = Wipe_FAT();
		else if (New_File_System == "yaffs2")
			wiped = Wipe_YAFFS2();
#ifdef TW_INCLUDE_F2FS
		else if (New_File_System == "f2fs")
			wiped = Wipe_F2FS();
#endif
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
			wiped = Wipe_MTD();
		else {
			LOGERR("Unable to wipe '%s' -- unknown file system '%s'\n", Mount_Point.c_str(), New_File_System.c_str());
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
		if (Mount_Point == "/sdcard") {
			DataManager::SetupTwrpFolder();
			// restore theme file if needed
			if (!theme_path.empty() && TWFunc::Path_Exists(tmp_path))
				rename(tmp_path.c_str(), theme_path.c_str());
		}

		if (Mount_Point == "/cache") {
			DataManager::Output_Version();
			TWFunc::Update_Rotation_File(DataManager::GetIntValue(TW_ROTATION));
		}
	}

done:
	if (wiped) {
		if (TWFunc::Path_Exists("/.layout_version") && Mount(false))
			TWFunc::copy_file("/.layout_version", Layout_Filename, 0600);

		if (update_crypt) {
			if (Is_Encrypted && !Is_Decrypted) {
				Setup_File_System(false);
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
		if (Can_Be_Mounted)
			Update_Size(false);
		else {
			Find_Partition_Size();
			Used = Size;
			Backup_Size = Size;
		}
	}
	return wiped;
}

bool TWPartition::Wipe_AndSec(void) {
	if (!Has_Android_Secure)
		return false;

	if (!Mount(true))
		return false;

	gui_print("Wiping %s\n", Backup_Display_Name.c_str());
	TWFunc::removeDir(Mount_Point + "/.android_secure/", true);
	gui_print("[Android Secure wipe done]\n");
	return true;
}

bool TWPartition::Decrypt(string Password) {
	LOGINFO("STUB TWPartition::Decrypt, password: '%s'\n", Password.c_str());
	// Is this needed?
	return 1;
}

bool TWPartition::Wipe_Encryption() {
	bool Save_Data_Media = Has_Data_Media;

	if (!UnMount(true))
		return false;

	Has_Data_Media = false;
	Decrypted_Block_Device = "";
	Is_Decrypted = false;
	Is_Encrypted = false;
	if (Wipe(Fstab_File_System)) {
		Has_Data_Media = Save_Data_Media;
		if (Has_Data_Media && !Symlink_Mount_Point.empty()) {
			Recreate_Media_Folder();
		}
		gui_print("[%s wipe done]\n", Display_Name.c_str());
		gui_print("[Rebooting recovery is recommended]\n");
		return true;
	} else {
		Has_Data_Media = Save_Data_Media;
		LOGERR("Unable to format to remove encryption.\n");
		return false;
	}
	return false;
}

bool TWPartition::Wipe_EXT23(string File_System) {
	if (!UnMount(true))
		return false;

	if (TWFunc::Path_Exists("/sbin/mke2fs")) {
		string command;

		gui_print("Formatting %s using mke2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mke2fs -t " + File_System + " -m 0 " + Actual_Block_Device;
		LOGINFO("mke2fs command: %s\n", command.c_str());
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = File_System;
			Recreate_AndSec_Folder();
#ifdef TW_DEVICE_IS_HTC_LEO
			Recreate_DataOnExt_Folder();
#endif
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXT4() {
	if (!UnMount(true))
		return false;
#if defined(HAVE_SELINUX) && defined(USE_EXT4) 
	gui_print("Formatting %s using make_ext4fs function.\n", Display_Name.c_str());
	if (make_ext4fs(Actual_Block_Device.c_str(), Length, Mount_Point.c_str(), selinux_handle) != 0) {
		LOGERR("Unable to wipe '%s' using function call.\n", Mount_Point.c_str());
		return false;
	} else {
	#ifdef HAVE_SELINUX
		//set default context on lost+found
		/*
		char *selinux_context = NULL;
		LOGINFO("getting context for %s\n", Mount_Point.c_str());
		if (lgetfilecon(Mount_Point.c_str(), &selinux_context) >= 0) {
			LOGINFO("setting context %s on %s\n", selinux_context, sedir.c_str());
			if (lsetfilecon(sedir.c_str(), selinux_context) < 0) {
				return false;
			}
		}
		*/
		string sedir = Mount_Point + "/lost+found";
		PartitionManager.Mount_By_Path(sedir.c_str(), true);
		rmdir(sedir.c_str());
		mkdir(sedir.c_str(), S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
	#endif
		return true;
	}
#else
	if (TWFunc::Path_Exists("/sbin/make_ext4fs")) {
		string Command;

		gui_print("Formatting %s using make_ext4fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		Command = "make_ext4fs";
		if (!Is_Decrypted && Length != 0) {
			// Only use length if we're not decrypted
			char len[32];
			sprintf(len, "%i", Length);
			Command += " -l ";
			Command += len;
		}
		if (TWFunc::Path_Exists("/file_contexts")) {
			Command += " -S /file_contexts";
		}
		Command += " -a " + Mount_Point + " " + Actual_Block_Device;
		LOGINFO("make_ext4fs command: %s\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command) == 0) {
			Current_File_System = "ext4";
			Recreate_AndSec_Folder();
#ifdef TW_DEVICE_IS_HTC_LEO
			Recreate_DataOnExt_Folder();
#endif
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_EXT23("ext4");
#endif
	return false;
}

bool TWPartition::Wipe_NILFS2() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkfs.nilfs2")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkfs.nilfs2...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkfs.nilfs2 " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = "nilfs2";
			Recreate_AndSec_Folder();
#ifdef TW_DEVICE_IS_HTC_LEO
			Recreate_DataOnExt_Folder();
#endif
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_FAT() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkdosfs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkdosfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkdosfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = "vfat";
			Recreate_AndSec_Folder();
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	}
	else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_EXFAT() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkexfatfs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkexfatfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkexfatfs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_F2FS() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkfs.f2fs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkfs.f2fs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkfs.f2fs " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Recreate_AndSec_Folder();
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_NTFS() {
	string command;

	if (TWFunc::Path_Exists("/sbin/mkntfs")) {
		if (!UnMount(true))
			return false;

		gui_print("Formatting %s using mkntfs...\n", Display_Name.c_str());
		Find_Actual_Block_Device();
		command = "mkntfs -f " + Actual_Block_Device;
		if (TWFunc::Exec_Cmd(command) == 0) {
			Current_File_System = "ntfs";
			Recreate_AndSec_Folder();
			gui_print("[%s wipe done]\n", Display_Name.c_str());
			return true;
		} else {
			LOGERR("Unable to wipe '%s'.\n", Mount_Point.c_str());
			return false;
		}
		return true;
	} else
		return Wipe_RMRF();

	return false;
}

bool TWPartition::Wipe_YAFFS2() {
	if (!UnMount(true))
		return false;

	const MtdPartition* mtd;
	mtd = mtd_find_partition_by_name(MTD_Name.c_str());
	gui_print("YAFFS2 Formatting \"%s\"...\n", MTD_Name.c_str());
	if (mtd == NULL) {
		LOGERR("No mtd partition named '%s'", MTD_Name.c_str());
        	return false;
	}

	MtdWriteContext* ctx = mtd_write_partition(mtd);
	if (ctx == NULL) {
        	LOGERR("Can't write '%s', failed to format.", MTD_Name.c_str());
        	return false;
	}
   	if (mtd_erase_blocks(ctx, -1) == -1) {
        	mtd_write_close(ctx);
        	LOGERR("Failed to format '%s'", MTD_Name.c_str());
        	return false;
	}
	if (mtd_write_close(ctx) != 0) {
        	LOGERR("Failed to close '%s'", MTD_Name.c_str());
        	return false;
	}
	Current_File_System = "yaffs2";
	gui_print("[%s wipe done]\n", Display_Name.c_str());
    	return true;
}

bool TWPartition::Wipe_MTD() {
	string command;

	const MtdPartition* mtd;
	mtd = mtd_find_partition_by_name(MTD_Name.c_str());
	gui_print("MTD Formatting \"%s\"...\n", MTD_Name.c_str());
	if (mtd == NULL) {
		LOGERR("No mtd partition named '%s'", MTD_Name.c_str());
        	return false;
	}
	string eraseimg = "erase_image " + MTD_Name;
	if (TWFunc::Exec_Cmd(eraseimg) != 0) {
		LOGERR("Failed to format '%s'", MTD_Name.c_str());
		return false;
	}	
	Current_File_System = "mtd";
	gui_print("[%s wipe done]\n", Display_Name.c_str());
    	return true;
}

bool TWPartition::Wipe_RMRF() {
	if (!Mount(true))
		return false;

	gui_print("Removing all files under '%s' ...\n", Mount_Point.c_str());
	TWFunc::removeDir(Mount_Point, true);
	Recreate_AndSec_Folder();
#ifdef TW_DEVICE_IS_HTC_LEO
	Recreate_DataOnExt_Folder();
#endif
	gui_print("[%s wipe done]\n", Display_Name.c_str());
	return true;
}

bool TWPartition::Wipe_Data_Without_Wiping_Media() {
	string dir;

	// This handles wiping data on devices with "sdcard" in /data/media
	if (!Mount(true))
		return false;

	gui_print("Wiping data without wiping '/data/media' ...\n");

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
					LOGINFO("Unable to unlink '%s'\n", dir.c_str());
			}
		}
		closedir(d);
		gui_print("Done.\n");
		return true;
	}
	gui_print("Dirent failed to open '/data', error!\n");
	return false;
}

/************************************************************************************
 * Partition's backup stuff
 */
int TWPartition::Backup(string backup_folder) {
	if (Backup_Method == FILES)
		return Backup_Tar(backup_folder);
	else if (Backup_Method == DD)
		return Backup_DD(backup_folder);
	else if (Backup_Method == FLASH_UTILS)
		return Backup_Dump_Image(backup_folder);
	LOGERR("Unknown backup method for '%s'\n", Mount_Point.c_str());
	return 0;
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

int TWPartition::Backup_Tar(string backup_folder) {
	char back_name[255], split_index[5];
	string Full_FileName, Split_FileName, Tar_Args = "", Tar_Excl = "", Command, result;
	int use_compression, use_encryption = 0, index, backup_count, skip_dalvik;
	struct stat st;
	unsigned long long total_bsize = 0, file_size;
	twrpTar tar;

	if (!Mount(true))
		return 0;

	DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);
#ifdef TW_DEVICE_IS_HTC_LEO
	string data_pth, pathTodatafolder;
	int skip_native = DataManager::GetIntValue(TW_SKIP_NATIVESD);
	int dataonext = DataManager::GetIntValue(TW_DATA_ON_EXT);
	if (Backup_Path == "/sd-ext" && dataonext) {	
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, "DataOnExt", "Backing Up");
		gui_print("Backing up DataOnExt...\n");
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (data_pth.size() > 7)
			pathTodatafolder = data_pth.substr(7, data_pth.size() - 1);
		else
			pathTodatafolder = "";
	} else {
#endif
		TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Backup_Display_Name, "Backing Up");
		gui_print("Backing up %s...\n", Backup_Display_Name.c_str());
#ifdef TW_DEVICE_IS_HTC_LEO
	}
#endif
	// Skip dalvik-cache during backup?
	if ((Backup_Path == "/data" || Backup_Path == "/sd-ext" || Backup_Path == "/sdext2") && skip_dalvik) {
		Tar_Excl += "dalvik-cache";
	}

	// Skip any NativeSD Rom during backup of sd-ext
	if ((Backup_Path == "/sd-ext" || Backup_Path == "/sdext2") && skip_native) {
		Tar_Excl += Tar_exclude;
	}

	// Use Compression?
	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	tar.use_compression = use_compression;
	DataManager::GetValue("tw_encrypt_backup", use_encryption);
	if (Can_Encrypt_Backup && use_encryption) {
		tar.use_encryption = use_encryption;
		if (Use_Userdata_Encryption)
			tar.userdata_encryption = use_encryption;
	}
#endif
	// Set Backup_FileName
	sprintf(back_name, "%s.%s.win", Backup_Name.c_str(), Current_File_System.c_str());
	Backup_FileName = back_name;
	Full_FileName = backup_folder + Backup_FileName;
	tar.has_data_media = Has_Data_Media;
	if (Backup_Path == "/data" && Backup_Size < 2097152) {
		gui_print("Skipping Data (Empty partition).\n", Backup_Name.c_str());
		return -1;
	}
	if (Backup_Size > MAX_ARCHIVE_SIZE) {
		// This backup needs to be split into multiple archives
		gui_print("Breaking backup file into multiple archives...\n");
#ifdef TW_DEVICE_IS_HTC_LEO
		if (Backup_Path == "/sd-ext" && dataonext)
			sprintf(back_name, "%s", data_pth.c_str());
		else
#endif
			sprintf(back_name, "%s", Backup_Path.c_str());		
		tar.setexcl(Tar_Excl);
		tar.setdir(back_name);
		tar.setfn(Full_FileName);
		backup_count = tar.splitArchiveFork();
		if (backup_count == -1) {
			LOGERR("Error tarring split files!\n");
			return 0;
		}
	} else {
		Full_FileName = backup_folder + Backup_FileName;
		tar.setexcl(Tar_Excl);
#ifdef TW_DEVICE_IS_HTC_LEO
		if (Backup_Path == "/sd-ext" && dataonext)
			tar.setdir(Backup_Path + pathTodatafolder);
		else
#endif
			tar.setdir(Backup_Path);
		tar.setfn(Full_FileName);
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		if (tar.createTarFork() != 0)
			return 0;
		if (use_compression && !use_encryption) {
#else
		if (use_compression) {
			if (tar.createTarGZFork() != 0)
				return -1;
#endif
			string gzname = Full_FileName + ".gz";
			rename(gzname.c_str(), Full_FileName.c_str());
		}
#ifdef TW_EXCLUDE_ENCRYPTED_BACKUPS
		else {
			if (tar.createTarFork() != 0)
				return -1;
		}
#else
		if (Can_Encrypt_Backup && use_encryption)
			Full_FileName += "000";
#endif
		if (TWFunc::Get_File_Size(Full_FileName) == 0) {
			LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
			return -1;
		}
	}
#ifdef TW_DEVICE_IS_HTC_LEO
	if (Backup_Path == "/sd-ext" && dataonext) {
		// Create a file to recognize that this is DataOnExt and not a typical sd-ext backup
		Command = "echo /sd-ext" + pathTodatafolder + ">" + backup_folder + ".dataonext";
		TWFunc::Exec_Cmd(Command);
	}
#endif
	if (skip_dalvik && Dalvik_Cache_Size > 0) {
		// Create a file to recognize that this is a backup without dalvik-cache
		// and store the size of dalvik-cache inside for restore purposes
		Command = "echo " + Backup_Path + ":" + TWFunc::to_string((int)Dalvik_Cache_Size) + ">" + backup_folder + ".nodalvikcache";
		TWFunc::Exec_Cmd(Command);
	}
	return 1;
}

int TWPartition::Backup_DD(string backup_folder) {
	char backup_size[32];
	string Full_FileName, Command, DD_BS;
	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	gui_print("Backing up %s...\n", Display_Name.c_str());

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = backup_folder + Backup_FileName;
	sprintf(backup_size, "%llu", Backup_Size);
	DD_BS = backup_size;

	Command = "dd if=" + Actual_Block_Device + " of='" + Full_FileName + "'" + " bs=" + DD_BS + "c count=1";
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return -1;
	}
	return 1;
}

int TWPartition::Backup_Dump_Image(string backup_folder) {
	string Full_FileName, Command;
	TWFunc::GUI_Operation_Text(TW_BACKUP_TEXT, Display_Name, "Backing Up");
	gui_print("Backing up %s...\n", Display_Name.c_str());

	Backup_FileName = Backup_Name + "." + Current_File_System + ".win";
	Full_FileName = backup_folder + Backup_FileName;

	Command = "dump_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGINFO("Backup command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	if (TWFunc::Get_File_Size(Full_FileName) == 0) {
		// Actual size may not match backup size due to bad blocks on MTD devices so just check for 0 bytes
		LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
		return -1;
	}
	return 1;
}

/************************************************************************************
 * Partition restoring...
 */
bool TWPartition::Restore(string restore_folder) {
	Skip_From_Restore = false;
	size_t first_period, second_period;
	string Restore_File_System, FileName;
	
	FileName = Backup_FileName;
	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	LOGINFO("Restore filename is: %s\n", Backup_FileName.c_str());

	// Parse backup filename to extract the file system before wiping
	first_period = FileName.find(".");
	if (first_period == string::npos) {
		LOGERR("Unable to find file system (first period).\n");
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
		if (Restore_File_System == "img" && Current_File_System == "mtd")
			Restore_File_System = "mtd";
		else {
			LOGERR("Unable to find file system (second period).\n");
			return false;
		}
	} else
		Restore_File_System.resize(second_period);

	LOGINFO("Restore file system is: '%s'.\n", Restore_File_System.c_str());
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
	LOGERR("Unknown restore method for '%s'\n", Mount_Point.c_str());
	return false;
}

bool TWPartition::Restore_Tar(string restore_folder, string Restore_File_System) {
	string Full_FileName, Command, data_pth;
	int index = 0, dataonext = 0;
	char split_index[5];

	Current_File_System = Restore_File_System;
	if (Backup_Name == "sd-ext") {
#ifdef TW_DEVICE_IS_HTC_LEO
		// Check if this is a sd-ext backup featuring DataOnExt
		dataonext = TWFunc::Path_Exists(restore_folder + "/.dataonext");
		if (dataonext) {
			LOGINFO("The sd-ext backup is actually 'DataOnExt'.\n");
			// get the DATA_PATH stored inside '.dataonext' file
			FILE *fp;
			fp = fopen((restore_folder + "/.dataonext").c_str(), "rt");
			char tmp[255];
			fgets(tmp, sizeof(tmp), fp);
			tmp[strlen(tmp) - 1] = '\0';
			data_pth = tmp;
			fclose(fp);
			LOGINFO("DataOnExt path: '%s'\n", data_pth.c_str());
			// decide if to wipe partition
			if (data_pth == "/sd-ext")
				Wipe();
			else {
				// rm the returned path instead of formatting the entire partition.
				// Maybe there are other data on it (another NativeSD Rom).
				Mount(true);
				if (TWFunc::Path_Exists(data_pth)) {
					gui_print("Wiping %s...\n", data_pth.c_str());
					Command = "rm -rf " + data_pth + "/* &> /dev/null";
					TWFunc::Exec_Cmd(Command);
					Command = "rm -rf " + data_pth + "/.* &> /dev/null";
					TWFunc::Exec_Cmd(Command);
				}
				UnMount(true);
			}		
			// Set TWRP values
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			DataManager::SetValue(TW_DATA_PATH, data_pth);
		} else {
#endif
			Wipe();
			// Set TWRP value
			DataManager::SetValue(TW_DATA_ON_EXT, 0);
#ifdef TW_DEVICE_IS_HTC_LEO
		}
#endif
	} else {
		if (Has_Android_Secure) {
			if (!Wipe_AndSec())
				return false;
		} else if (!Wipe()) {
				gui_print("Failed wiping %s...\n", Display_Name.c_str());
				return false;
		}
	}

	if (Backup_Name == "sd-ext" || Backup_Name == "sdext2") {
#ifdef TW_DEVICE_IS_HTC_LEO
		if ((!dataonext) || (dataonext && data_pth == Backup_Path)) {
#endif
			// Set number of mounts that will trigger a filesystem check from settings
			Command = "tune2fs -c " + DataManager::GetStrValue("tw_num_of_mounts_for_fs_check") + " " + Primary_Block_Device;
			TWFunc::Exec_Cmd(Command);
#ifdef TW_DEVICE_IS_HTC_LEO
		}
#endif
	}

	if (!Mount(true))
		return false;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Restore_Display_Name, "Restoring");
	gui_print("Restoring %s...\n", Restore_Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_FileName)) {
		// Backup is multiple archives
		LOGINFO("Backup is multiple archives.\n");
		sprintf(split_index, "%03i", index);
		Full_FileName += split_index;
		while (TWFunc::Path_Exists(Full_FileName)) {
			gui_print("Restoring archive %i...\n", index+1);
			LOGINFO("Restoring '%s'...\n", Full_FileName.c_str());
			if (!TWFunc::TarExtract(Full_FileName, Backup_Path))
				return false;
			index++;		
			sprintf(split_index, "%03i", index);
			Full_FileName = restore_folder + "/" + Backup_FileName + split_index;
		}
		if (index == 0) {
			LOGERR("Error locating restore file: '%s'\n", Full_FileName.c_str());
			return false;
		}
	} else {
		string tarDir = Backup_Path;
		if (Backup_Path == "/sd-ext") {
			// Check needed for restoring a CWM backup of sd-ext
			if (TWFunc::TarEntryExists(Full_FileName, "sd-ext/"))
				tarDir = "/";
		} else if (Backup_Path == "/and-sec") {
			// Check needed for restoring a CWM backup of android_secure
			if (TWFunc::TarEntryExists(Full_FileName, ".android_secure/"))
				tarDir = Storage_Path;
		}
		if (!TWFunc::TarExtract(Full_FileName, tarDir))
			return false;
	}
	return true;
}

bool TWPartition::Restore_DD(string restore_folder) {
	string Full_FileName, Command;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGERR("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}

	gui_print("Restoring %s...\n", Display_Name.c_str());
	Command = "dd bs=4096 if='" + Full_FileName + "' of=" + Actual_Block_Device;
	LOGINFO("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

bool TWPartition::Restore_Flash_Image(string restore_folder) {
	string Full_FileName, Command;

	gui_print("Restoring %s...\n", Display_Name.c_str());
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGERR("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}
	// Sometimes flash image doesn't like to flash due to the first 2KB matching, so we erase first to ensure that it flashes
	Command = "erase_image " + MTD_Name;
	LOGINFO("Erase command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	Command = "flash_image " + MTD_Name + " '" + Full_FileName + "'";
	LOGINFO("Restore command: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command);
	return true;
}

// Make TWRP compatible with CWM Backup type
bool TWPartition::Restore_Yaffs_Image(string restore_folder) {
	Use_unyaffs_To_Restore = false; // always reset value to false
	string Full_FileName;

	TWFunc::GUI_Operation_Text(TW_RESTORE_TEXT, Display_Name, "Restoring");
	Full_FileName = restore_folder + "/" + Backup_FileName;

	if (!Find_Partition_Size()) {
		LOGERR("Unable to find partition size for '%s'\n", Mount_Point.c_str());
		return false;
	}
	unsigned long long backup_size = TWFunc::Get_File_Size(Full_FileName);
	if (backup_size > Size) {
		LOGERR("Size (%iMB) of backup '%s' is larger than target device '%s' (%iMB)\n",
			(int)(backup_size / 1048576LLU), Full_FileName.c_str(),
			Actual_Block_Device.c_str(), (int)(Size / 1048576LLU));
		return false;
	}	

	if (!Wipe())
		return false;

	if (!Mount(true))
		return false;
	
	gui_print("Restoring %s...\n", Display_Name.c_str());
	char* backup_file_image = (char*)Full_FileName.c_str();
	char* backup_path = (char*)Mount_Point.c_str();
	unyaffs(backup_file_image, backup_path, NULL);
	return true;
}

void TWPartition::Change_FS_Type(string type) {
	Current_File_System = type;
	return;
}

void TWPartition::Change_Restore_Display_Name(string name) {
	Restore_Display_Name = name;
	return;
}

/************************************************************************************
 * Various Checks used in code
 */
bool TWPartition::Check_FS_Type() {
	Find_Actual_Block_Device();
	if(Is_Present
	// Check the file system for removable devices
	// Use fstab's file system in all other cases
	&& Fstab_File_System != "yaffs2"
	&& Fstab_File_System != "mtd"
	&& Fstab_File_System != "bml") {
		blkid_probe pr = blkid_new_probe_from_filename(Actual_Block_Device.c_str());
		if (blkid_do_fullprobe(pr)) {
			blkid_free_probe(pr);
			LOGINFO("Can't probe device %s\n", Actual_Block_Device.c_str());
		} else {
			const char* type;
			if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) < 0) {
				blkid_free_probe(pr);
				LOGINFO("can't find filesystem on device %s\n", Actual_Block_Device.c_str());
			} else {
				blkid_free_probe(pr);
				Current_File_System = type;
			}
		}
	} else if (!Is_Present)
		return false;

	return true;
}

unsigned int TWPartition::FS_Type_Via_statfs() {
	struct statfs st;

	string Local_Path = Mount_Point + "/.";
	if (statfs(Local_Path.c_str(), &st) != 0)
		return 0;

	return st.f_type;
}

bool TWPartition::Check_MD5(string restore_folder) {
	if (DataManager::GetIntValue(TW_SKIP_MD5_CHECK_VAR) > 0)
		return true;

	string Full_Filename, md5file, NandroidMD5;
	char split_filename[512];
	int index = 0;
	twrpDigest md5sum;

	// Check if nandroid.md5 file exists and...
	NandroidMD5 = restore_folder + "/nandroid.md5";
	if (TWFunc::Path_Exists(NandroidMD5)) {
		// ...split it to match TWRP's style.
		TWFunc::Split_NandroidMD5(NandroidMD5);
	}
	memset(split_filename, 0, sizeof(split_filename));
	Full_Filename = restore_folder + "/" + Backup_FileName;
	if (!TWFunc::Path_Exists(Full_Filename)) {
		 // This is a split archive, we presume
		sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
		LOGINFO("split_filename: %s\n", split_filename);
		md5file = split_filename;
		md5file += ".md5";
		if (!TWFunc::Path_Exists(md5file)) {
			LOGERR("No md5 file found for '%s'.\n", split_filename);
			LOGERR("Please select 'Skip MD5 verification' to restore.\n");
			return false;
		}
		md5sum.setfn(split_filename);
		while (index < 1000 && TWFunc::Path_Exists(split_filename)) {
			if (md5sum.verify_md5digest() != 0) {
				LOGERR("MD5 failed to match on '%s'.\n", split_filename);
				return false;
			}
			index++;
			sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
			md5sum.setfn(split_filename);
		}
		return true;
	} else {
		// Single file archive
		md5file = Full_Filename + ".md5";
		if (!TWFunc::Path_Exists(md5file)) {
			LOGERR("No md5 file found for '%s'.\n", Full_Filename.c_str());
			LOGERR("Please select 'Skip MD5 verification' to restore.\n");
			return false;
		}
		md5sum.setfn(Full_Filename);
		if (md5sum.verify_md5digest() != 0) {
			LOGERR("MD5 failed to match on '%s'.\n", Full_Filename.c_str());
			return false;
		} else
			return true;
	}
	return false;
}

void TWPartition::Check_BuildProp(void) {
	if (!Mount(true)) {
		LOGINFO("Unable to check %s/build.prop.\n", Mount_Point.c_str());
	} else {
		// Quick check build.prop for [DataOnExt] string
		if (TWFunc::Path_Exists("/system/build.prop")) {
			LOGINFO("Checking /system/build.prop...\n");
			if (system("grep -Fxqi \"DataOnExt\" /system/build.prop") == 0) {
				LOGINFO("DataOnExt method is probably used by the installed Rom\n");
				DataManager::SetValue("tw_doe", 1);
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
		LOGINFO("Unable to check %s for NativeSD Roms.\n", Mount_Point.c_str());
	} else {
		DIR* Dir = opendir("/sdcard/NativeSD");
		if (Dir == NULL) {
			LOGINFO("No NativeSD Roms detected.\n");
		} else {
			int count = 0, dataonext;
			NativeSD_Size = 0;
			Tar_exclude = "";
			DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
			string data_pth;
			DataManager::GetValue(TW_DATA_PATH, data_pth);
			LOGINFO("Checking for installed NativeSD Roms...\n");
			LOGINFO("If any found, the backup size of sd-ext will be adjusted.\n");
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
									LOGINFO("Excluding : %s\n", pathToCheck.c_str());
									NativeSD_Size += TWFunc::Get_Folder_Size(pathToCheck, true);
									Tar_exclude += (" " + dname);
								}
							}
						} else {
							// Since we have a standard NAND-Rom we will exclude the entire NativeSD-Rom
							if (TWFunc::Path_Exists(pathToCheck)) {
								count++;
								NativeSD_Size += TWFunc::Get_Folder_Size(pathToCheck, true);
								Tar_exclude += (" " + dname);
							}
						}
					} else
						LOGINFO("Ignoring '%s'(SD-Recovery folder)...\n", dname.c_str());
				}
			}
			if (count == 0) // "/sdcard/NativeSD" was found but did not contain any Roms
				LOGINFO("No change to the size of sd-ext.\n");
			closedir(Dir);
		}
		UnMount(false);
	}
}

void TWPartition::CheckFor_Dalvik_Cache(void) {
	Dalvik_Cache_Size = 0;
	if (!Mount(true)) {
		LOGINFO("Unable to check %s for dalvik-cache.\n", Mount_Point.c_str());
	} else {
#ifdef TW_DEVICE_IS_HTC_LEO
		int dataonext =	DataManager::GetIntValue(TW_DATA_ON_EXT);
#else
		int dataonext =	0;
#endif
		if (Mount_Point == "/cache") {
			if (TWFunc::Path_Exists("/cache/dalvik-cache"))
				Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/cache/dalvik-cache", true);
			else
				LOGINFO("No '/cache/dalvik-cache' found.\n");
			// Don't unmount /cache
		} else if (Mount_Point == "/data") {
			if (dataonext) {
				// Check /data for *.dex files.
				// If any found, then this partition is entirely used for dalvik-cache (DalvikOnNand)
				DIR* Dir = opendir(Mount_Point.c_str());
				if (Dir != NULL) {
					int dalvikonnand = 0;
					struct dirent* DirEntry;
					string search_str = ".", extn;
					while ((DirEntry = readdir(Dir)) != NULL) {
						if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
							continue;
						string dname = DirEntry->d_name;
						if (DirEntry->d_type == DT_REG) {
							size_t last_occur = dname.rfind(search_str);
							if (last_occur == string::npos)
								continue;
							extn = dname.substr((last_occur + 1), (dname.size() - last_occur - 1));
							if (strncmp(extn.c_str(), "dex", 3) == 0)
								dalvikonnand++;
						}							
					}
					closedir(Dir);
					if (dalvikonnand > 0) {
						Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/data", true);
					}
				}
			} else {
				if (TWFunc::Path_Exists("/data/dalvik-cache"))
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/data/dalvik-cache", true);
				else
					LOGINFO("No '/data/dalvik-cache' found.\n");
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
					LOGINFO("No '%s' found.\n", dalvik_pth.c_str());
			} else {
				if (TWFunc::Path_Exists("/sd-ext/dalvik-cache"))
					Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/sd-ext/dalvik-cache", true);
				else
					LOGINFO("No '/sd-ext/dalvik-cache' found.\n");
			}
			UnMount(false);
		} else if (Mount_Point == "/sdext2") {
			if (TWFunc::Path_Exists("/sdext2/dalvik-cache"))
				Dalvik_Cache_Size = TWFunc::Get_Folder_Size("/sdext2/dalvik-cache", true);
			else
				LOGINFO("No '/sdext2/dalvik-cache' found.\n");
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
		LOGINFO(">> Checking given data_pth: '%s'\n", data_pth.c_str());
		string dtpth = data_pth;
	
		if (!Mount(true)) {
			LOGINFO(">> Can't mount '/sd-ext'\n");
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
				LOGINFO(">> Changing data_path's root folder '%s'\n", rootpth.c_str());
				dtpth = "/sd-ext" + dtpth.substr(rootpth.size(), dtpth.size() - rootpth.size());
				LOGINFO(">> data_path to be checked: '%s'\n", dtpth.c_str());
			}
		}

		LOGINFO(">> Checking for packages.xml...\n");

		// Check if 'packages.xml' exists where it is supposed to be
		string pkg_xml_path = dtpth + "/system/packages.xml";
		LOGINFO("[1]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGINFO(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGINFO(">> '%s' is a valid data_pth!\n", dtpth.c_str());
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 0;
		}
		// Else check the parent dir in case we're lucky
		string parentpth = TWFunc::Get_Path(dtpth);
		pkg_xml_path = parentpth + "system/packages.xml";
		LOGINFO("[2]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGINFO(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGINFO(">> '%s' is a valid data_pth!\n", parentpth.substr(0, parentpth.size() - 1).c_str());
			DataManager::SetValue(TW_DATA_PATH, parentpth.substr(0, parentpth.size() - 1), 1);
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 1;
		}
		// Else check if it is located in a subfolder
		string subpth = dtpth + "/data";
		pkg_xml_path = subpth + "/system/packages.xml";
		LOGINFO("[3]Checking path: '%s'\n", pkg_xml_path.c_str());
		if (TWFunc::Path_Exists(pkg_xml_path)) {
			LOGINFO(">> Found '%s'\n", pkg_xml_path.c_str());
			LOGINFO(">> '%s' is a valid data_pth!\n", subpth.c_str());
			DataManager::SetValue(TW_DATA_PATH, subpth, 1);
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			UnMount(false);
			return 1;
		}

		// Since we reached so far, scan the entire /sd-ext for 'packages.xml'
		LOGINFO(">> Primary check for packages.xml FAILED.\n");
		LOGINFO("[4]Scanning entire sd-ext for packages.xml...\n");
		string cmd = "find /sd-ext -type f -maxdepth 4 -iname packages.xml | sed -n 's!/system/packages.xml!!g p' > /tmp/dataonextpath.txt";
		system(cmd.c_str());
		FILE *fp;
		fp = fopen("/tmp/dataonextpath.txt", "rt");
		if (fp == NULL)	{
			LOGINFO(">> Unable to open dataonextpath.txt.\n");
			dtpth = "null";
		} else {
			char cmdOutput[255];
			while (fgets(cmdOutput, sizeof(cmdOutput), fp) != NULL) {
				cmdOutput[strlen(cmdOutput) - 1] = '\0';
				//LOGINFO("cmdOutput : %s - len(%d)\n", cmdOutput, strlen(cmdOutput));		
				dtpth = cmdOutput;
				if (dtpth.size() > 7) {
					LOGINFO(">> '%s' is a valid data_pth!\n", dtpth.c_str());
					break;
				} else
				if (dtpth.size() < 7) {
					LOGINFO(">> Invalid data_pth: '%s'\n", dtpth.c_str());
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
			LOGINFO("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
			DataManager::SetValue(TW_DATA_ON_EXT, 1);
			DataManager::SetValue(TW_DATA_PATH, dtpth, 1);
			UnMount(false);
			return 1;
		} else {
			// We didn't find a 'packages.xml' file, but there is a chance that
			// the rom is fresh-installed(so no packages.xml will be present)
			// Check instead for data's folder-structure
			dtpth = data_pth;
			LOGINFO(">> Secondary check for packages.xml FAILED.\n");
			LOGINFO(">> Scanning for data structure...\n");
			LOGINFO("[5]Checking path: '%s'\n", dtpth.c_str());
			int dir_num = 0;
			int check = TWFunc::SubDir_Check(dtpth, "app", "local", "misc", "", "", 1);
			if (check == 1) {
				LOGINFO("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
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
								LOGINFO("[*]Result: '%s' will be used for data_pth.\n", dtpth.c_str());
								DataManager::SetValue(TW_DATA_ON_EXT, 1);
								break;
							}
						}
					}
					closedir(Dir);
				}
				if (dir_num == 0) {
					LOGINFO(">> Tertiary check for data structure FAILED.\n");
					LOGINFO(">> No folders found under /sd-ext.\n");
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
		LOGINFO("Path for DataOnExt will not be checked.\n");*/

	return 0;	
}

