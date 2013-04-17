/* Partition Management classes for TWRP
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
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "variables.h"
#include "twcommon.h"
#include "ui.h"
#include "bootloader.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"
#include "twrpDigest.hpp"
#include "twrpTar.hpp"

#ifdef TW_INCLUDE_CRYPTO
	#ifdef TW_INCLUDE_JB_CRYPTO
		#include "crypto/jb/cryptfs.h"
	#else
		#include "crypto/ics/cryptfs.h"
	#endif
	#include "cutils/properties.h"
#endif

extern RecoveryUI* ui;
int TWPartitionManager::Fstab_Proc_Done = 0;
int TWPartitionManager::SD_Partitioning_Done_Once = 0;
int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];
	bool Found_Settings_Storage = false;

	fstabFile = fopen(Fstab_Filename.c_str(), "rt");
	if (fstabFile == NULL) {
		LOGERR("Critical Error: Unable to open fstab at '%s'.\n", Fstab_Filename.c_str());
		return false;
	}
	Partitions.clear();
	while (fgets(fstab_line, sizeof(fstab_line), fstabFile) != NULL) {
		if (fstab_line[0] != '/')
			continue;

		if (fstab_line[strlen(fstab_line) - 1] != '\n')
			fstab_line[strlen(fstab_line)] = '\n';

		TWPartition* partition = new TWPartition();
		string line = fstab_line;
		memset(fstab_line, 0, sizeof(fstab_line));

		if (partition->Process_Fstab_Line(line, Display_Error)) {
			Partitions.push_back(partition);
			if (partition->Is_Settings_Storage) {
				Found_Settings_Storage = true;
				// Why not try to read SettingsFile as soon as storage can be accessible!
				DataManager::ReadSettingsFile();
			}
		} else {
			delete partition;
		}
	}
	fclose(fstabFile);
	if (!Found_Settings_Storage) {
		std::vector<TWPartition*>::iterator iter;
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Is_Storage) {
				(*iter)->Is_Settings_Storage = true;
				Found_Settings_Storage = true;
				LOGINFO("Settings storage is '%s'\n", (*iter)->Storage_Path.c_str());
				break;
			}
		}
		if (!Found_Settings_Storage)
			LOGERR("Unable to locate storage partition for storing settings file.\n");
	} 
	if (!Write_Fstab()) {
		if (Display_Error)
			LOGERR("Error creating fstab\n");
		else
			LOGINFO("Error creating fstab\n");
	}
	Update_System_Details(true);
	UnMount_Main_Partitions();
	Fstab_Proc_Done = 1;
	return true;
}

int TWPartitionManager::Write_Fstab(void) {
	FILE *fp;
	std::vector<TWPartition*>::iterator iter;
	string Line;

	fp = fopen("/etc/fstab", "w");
	if (fp == NULL) {
		LOGINFO("Can not open /etc/fstab.\n");
		return false;
	}
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
#ifdef TW_INCLUDE_NTFS_3G
			if ((*iter)->Current_File_System == "ntfs") {
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " ntfs-3g rw,umask=0 0 0\n";
			} else
#endif
#ifdef TW_INCLUDE_EXFAT
			if ((*iter)->Current_File_System == "exfat") {
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " exfat-fuse rw 0 0\n";
			} else
#endif
			if ((*iter)->Current_File_System == "swap")
				Line = (*iter)->Actual_Block_Device + " swap swap defaults 0 0\n";
			else
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
			fputs(Line.c_str(), fp);
		}
		// Handle subpartition tracking
		if ((*iter)->Is_SubPartition) {
			TWPartition* ParentPartition = Find_Partition_By_Path((*iter)->SubPartition_Of);
			if (ParentPartition)
				ParentPartition->Has_SubPartition = true;
			else
				LOGERR("Unable to locate parent partition '%s' of '%s'\n", (*iter)->SubPartition_Of.c_str(), (*iter)->Mount_Point.c_str());
		}
	}
	fclose(fp);
	return true;
}

void TWPartitionManager::Output_Partition_Logging(void) {
	std::vector<TWPartition*>::iterator iter;

	printf("Partition Logs:\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++)
		Output_Partition((*iter));
}

void TWPartitionManager::Output_Partition(TWPartition* Part) {
	unsigned long long mb = 1048576;

	printf("%s | ", Part->Mount_Point.c_str());
	if (Part->Actual_Block_Device.empty())
		printf("/---/-----/--------- | Size: %iMB Used: %iMB ", (int)(Part->Size / mb), (int)(Part->Used / mb));
	else
		printf("%s | Size: %iMB Used: %iMB ", Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb), (int)(Part->Used / mb));
	if (Part->Dalvik_Cache_Size > 0)
		printf("Dalvik-Cache: %iMB ", (int)(Part->Dalvik_Cache_Size / mb));
	if (Part->NativeSD_Size > 0)
		printf("NativeSD: %iMB ", (int)(Part->NativeSD_Size / mb));		
	printf("Free: %iMB Backup Size: %iMB", (int)(Part->Free / mb), (int)(Part->Backup_Size / mb));
	printf("\n   Flags: ");
	if (Part->Swap)
		printf("Swap ");
	if (Part->Can_Be_Wiped)
		printf("Can_Be_Wiped ");
	if (Part->Can_Be_Backed_Up)
		printf("Can_Be_Backed_Up ");
	if (Part->Wipe_During_Factory_Reset)
		printf("Wipe_During_Factory_Reset ");
	if (Part->Wipe_Available_in_GUI)
		printf("Wipe_Available_in_GUI ");
	if (Part->Is_SubPartition)
		printf("Is_SubPartition ");
	if (Part->Has_SubPartition)
		printf("Has_SubPartition ");
	if (Part->Removable)
		printf("Removable ");
	if (Part->Is_Present)
		printf("Is_Present ");
	if (Part->Can_Be_Encrypted)
		printf("Can_Be_Encrypted ");
	if (Part->Is_Encrypted)
		printf("Is_Encrypted ");
	if (Part->Is_Decrypted)
		printf("Is_Decrypted ");
	if (Part->Has_Data_Media)
		printf("Has_Data_Media ");
	if (Part->Has_Android_Secure)
		printf("Has_Android_Secure ");
	if (Part->Is_Storage)
		printf("Is_Storage ");
	if (Part->Is_Settings_Storage)
		printf("Is_Settings_Storage "); 
	printf("\n");
	if (!Part->Primary_Block_Device.empty())
		printf("   Primary_Block_Device: %s\n", Part->Primary_Block_Device.c_str());
	if (!Part->MTD_Dev.empty())
		printf("   MTD_Dev: %s\n", Part->MTD_Dev.c_str());
	if (!Part->Fstab_File_System.empty())
		printf("   Fstab_File_System: %s\n", Part->Fstab_File_System.c_str());
	if (!Part->Current_File_System.empty())
		printf("   Current_File_System: %s\n", Part->Current_File_System.c_str());
	if (Part->Can_Be_Mounted) {
		if (!Part->SubPartition_Of.empty())
			printf("   SubPartition_Of: %s\n", Part->SubPartition_Of.c_str());
		if (!Part->Symlink_Path.empty())
			printf("   Symlink_Path: %s\n", Part->Symlink_Path.c_str());
		if (!Part->Symlink_Mount_Point.empty())
			printf("   Symlink_Mount_Point: %s\n", Part->Symlink_Mount_Point.c_str());
		if (!Part->Alternate_Block_Device.empty())
			printf("   Alternate_Block_Device: %s\n", Part->Alternate_Block_Device.c_str());
		if (!Part->Decrypted_Block_Device.empty())
			printf("   Decrypted_Block_Device: %s\n", Part->Decrypted_Block_Device.c_str());
		if (Part->Length != 0)
			printf("   Length: %i\n", Part->Length);
		if (!Part->Storage_Path.empty())
			printf("   Storage_Path: %s\n", Part->Storage_Path.c_str());
		if (Part->Format_Block_Size != 0)
			printf("   Format_Block_Size: %i\n", Part->Format_Block_Size);
	}
	if (Part->Size > 0) {
		if (!Part->Backup_Path.empty())
			printf("   Backup_Path: %s\n", Part->Backup_Path.c_str());
		if (!Part->Backup_FileName.empty())
			printf("   Backup_FileName: %s\n", Part->Backup_FileName.c_str());
		if (!Part->Display_Name.empty())
			printf("   Display_Name: %s\n", Part->Display_Name.c_str());
		if (!Part->Storage_Name.empty())
			printf("   Storage_Name: %s\n", Part->Storage_Name.c_str());
		if (!Part->Backup_Name.empty())
			printf("   Backup_Name: %s\n", Part->Backup_Name.c_str());
		if (!Part->Backup_Display_Name.empty())
			printf("   Backup_Display_Name: %s\n", Part->Backup_Display_Name.c_str());
		if (!Part->MTD_Name.empty())
			printf("   MTD_Name: %s\n", Part->MTD_Name.c_str());
		string back_meth = Part->Backup_Method_By_Name();
		printf("   Backup_Method: %s\n\n", back_meth.c_str());
	} else
		printf("\n\n");
}

int TWPartitionManager::Mount_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	if (Local_Path == "/tmp")
		return true;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			ret = (*iter)->Mount(Display_Error);
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Mount(Display_Error);
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		LOGERR("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGINFO("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

int TWPartitionManager::Mount_By_Block(string Block, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Mount(Display_Error);
			}
			return Part->Mount(Display_Error);
		} else
			return Part->Mount(Display_Error);
	}
	if (Display_Error)
		LOGERR("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGINFO("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Mount_By_Name(string Name, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Mount(Display_Error);
			}
			return Part->Mount(Display_Error);
		} else
			return Part->Mount(Display_Error);
	}
	if (Display_Error)
		LOGERR("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGINFO("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::UnMount_By_Path(string Path, bool Display_Error) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			ret = (*iter)->UnMount(Display_Error);
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->UnMount(Display_Error);
		}
	}
	if (found) {
		return ret;
	} else if (Display_Error) {
		LOGERR("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGINFO("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	}
	return false;
}

int TWPartitionManager::UnMount_By_Block(string Block, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->UnMount(Display_Error);
			}
			return Part->UnMount(Display_Error);
		} else
			return Part->UnMount(Display_Error);
	}
	if (Display_Error)
		LOGERR("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGINFO("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::UnMount_By_Name(string Name, bool Display_Error) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->UnMount(Display_Error);
			}
			return Part->UnMount(Display_Error);
		} else
			return Part->UnMount(Display_Error);
	}
	if (Display_Error)
		LOGERR("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGINFO("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Path(string Path) {
	TWPartition* Part = Find_Partition_By_Path(Path);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for path '%s'\n", Path.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGINFO("Is_Mounted: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Mount_Current_Storage(bool Display_Error) {
	string current_storage_path = DataManager::GetCurrentStoragePath();

	if (Mount_By_Path(current_storage_path, Display_Error)) {
		TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
		if (FreeStorage)
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		return true;
	}
	return false;
}

int TWPartitionManager::Mount_Settings_Storage(bool Display_Error) {
	return Mount_By_Path(DataManager::GetSettingsStoragePath(), Display_Error);
}

TWPartition* TWPartitionManager::Find_Partition_By_Path(string Path) {
	std::vector<TWPartition*>::iterator iter;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path))
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Block(string Block) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Primary_Block_Device == Block || (*iter)->Alternate_Block_Device == Block || ((*iter)->Is_Decrypted && (*iter)->Decrypted_Block_Device == Block))
			return (*iter);
	}
	return NULL;
}

TWPartition* TWPartitionManager::Find_Partition_By_Name(string Name) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Display_Name == Name)
			return (*iter);
	}
	return NULL;
}

int TWPartitionManager::Check_Backup_Name(bool Display_Error) {
	// Check the backup name to ensure that it is the correct size and contains only valid characters
	// and that a backup with that name doesn't already exist
	char backup_name[MAX_BACKUP_NAME_LEN];
	char backup_loc[255], tw_image_dir[255];
	int copy_size;
	int index, cur_char;
	string Backup_Name, Backup_Loc;

	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	copy_size = Backup_Name.size();
	// Check size
	if (copy_size > MAX_BACKUP_NAME_LEN) {
		if (Display_Error)
			LOGERR("Backup name is too long.\n");
		return -2;
	}

	// Check each character
	strncpy(backup_name, Backup_Name.c_str(), copy_size);
	if (copy_size == 1 && strcmp(backup_name, "0") == 0)
		return 0; // A "0" (zero) means to use the current timestamp for the backup name
	for (index=0; index<copy_size; index++) {
		cur_char = (int)backup_name[index];
		if (cur_char == 32
		|| (cur_char >= 48  && cur_char <= 57) 
		|| (cur_char >= 65 && cur_char <= 91) 
		|| (cur_char == 93)
		|| (cur_char == 95)
		|| (cur_char >= 97 && cur_char <= 123) 
		|| (cur_char == 125)
		|| (cur_char == 45)
		|| (cur_char == 46)) {
			// These are valid characters
			// Numbers
			// Upper case letters
			// Lower case letters
			// Space
			// and -_.{}[]
		} else {
			if (Display_Error)
				LOGERR("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
	strcpy(backup_loc, Backup_Loc.c_str());
	sprintf(tw_image_dir,"%s/%s", backup_loc, Backup_Name.c_str());
	if (TWFunc::Path_Exists(tw_image_dir)) {
		if (Display_Error)
			LOGERR("A backup with this name already exists.\n");
		return -4;
	}

	// No problems found, return 0
	return 0;
}

bool TWPartitionManager::Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename)
{
	string command;
	string Full_File = Backup_Folder + Backup_Filename;
	string result;
	twrpDigest md5sum;

	if (!generate_md5) 
		return true;

	TWFunc::GUI_Operation_Text(TW_GENERATE_MD5_TEXT, "Generating MD5");
	gui_print(" * Generating md5...\n");

	if (TWFunc::Path_Exists(Full_File)) {
		md5sum.setfn(Backup_Folder + Backup_Filename);
		if (md5sum.computeMD5() == 0) {
			if (md5sum.write_md5digest() == 0)
				gui_print(" * MD5 Created.\n");
			else {
				gui_print(" * MD5 write-error.\n");
				return -1;
			}
		} else {
			gui_print(" * MD5 compute-error!\n");
		}
	} else {
		char filename[512];
		int index = 0;
		string strfn;
		sprintf(filename, "%s%03i", Full_File.c_str(), index);
		strfn = filename;
		while (TWFunc::Path_Exists(filename) == true) {			
			md5sum.setfn(filename);
			if (md5sum.computeMD5() == 0) {
				if (md5sum.write_md5digest() != 0) {
					gui_print(" * MD5 write-error.\n");
					return false;
				}
			} else {
				gui_print(" * MD5 compute-error.\n");
				return -1;
			}
			index++;
			sprintf(filename, "%s%03i", Full_File.c_str(), index);
			strfn = filename;
		}
		if (index == 0) {
			LOGERR("Backup file: '%s' not found!\n", filename);
			return false;
		}
		gui_print(" * MD5 Created.\n");
	}
	return true;
}

bool TWPartitionManager::Backup_Partition(TWPartition* Part,
					  string Backup_Folder,
					  bool generate_md5,
					  unsigned long long* img_bytes_remaining,
					  unsigned long long* file_bytes_remaining,
					  unsigned long *img_time,
					  unsigned long *file_time,
					  unsigned long long *img_bytes,
					  unsigned long long *file_bytes) {
	time_t start, stop;
	int img_bps;
	unsigned long long file_bps;
	unsigned long total_time, remain_time, section_time;
	int use_compression, backup_time;
	float pos;

	if (Part == NULL)
		return true;

	DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	// We know the speed for both, how far into the whole backup are we, based on time
	total_time = (*img_bytes / (unsigned long)img_bps) + (*file_bytes / (unsigned long)file_bps);
	remain_time = (*img_bytes_remaining / (unsigned long)img_bps) + (*file_bytes_remaining / (unsigned long)file_bps);

	pos = (total_time - remain_time) / (float) total_time;
	DataManager::SetProgress(pos);

	LOGINFO("Estimated Total time: %lu  Estimated remaining time: %lu\n", total_time, remain_time);

	// And get the time
	if (Part->Backup_Method == 1)
		section_time = Part->Backup_Size / file_bps;
	else
		section_time = Part->Backup_Size / img_bps;

	// Set the position
	pos = section_time / (float) total_time;
	DataManager::ShowProgress(pos, section_time);

	time(&start);

	if (Part->Backup(Backup_Folder)) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Can_Be_Backed_Up && (*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
					if (!(*subpart)->Backup(Backup_Folder))
						return false;
					if (!Make_MD5(generate_md5, Backup_Folder, (*subpart)->Backup_FileName))
						return false;
					if (Part->Backup_Method == 1) {
						*file_bytes_remaining -= (*subpart)->Backup_Size;
					} else {
						*img_bytes_remaining -= (*subpart)->Backup_Size;
					}
				}
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGINFO("Partition Backup time: %d\n", backup_time);
		if (Part->Backup_Method == 1) {
			*file_bytes_remaining -= Part->Backup_Size;
			*file_time += backup_time;
		} else {
			*img_bytes_remaining -= Part->Backup_Size;
			*img_time += backup_time;
		}
		return Make_MD5(generate_md5, Backup_Folder, Part->Backup_FileName);
	} else {
		return false;
	}
}

int TWPartitionManager::Run_Backup(void) {
	int check, skip_md5_gen, do_md5, partition_count = 0, dataonext = 0, skip_free_space_check = 0;
	string Backup_Folder, Backup_Name, Full_Backup_Path;
	unsigned long long total_bytes = 0, file_bytes = 0, img_bytes = 0, free_space = 0, img_bytes_remaining, file_bytes_remaining, subpart_size;
	unsigned long img_time = 0, file_time = 0;
	TWPartition* backup_sys = NULL;
	TWPartition* backup_data = NULL;
	TWPartition* backup_cache = NULL;
	TWPartition* backup_recovery = NULL;
	TWPartition* backup_boot = NULL;
	TWPartition* backup_andsec = NULL;
	TWPartition* backup_sdext = NULL;
	TWPartition* backup_sdext2 = NULL;
	TWPartition* storage = NULL;
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	seconds = time(0);
	t = localtime(&seconds);

	time(&total_start);

	Update_System_Details(false);

	if (!Mount_Current_Storage(true))
		return false;

	// Get which partitions to backup from PartitionList
	int tw_backup_boot = -1;
	int tw_backup_system = -1;
	int tw_backup_data = -1;
	int tw_backup_recovery = -1;
	int tw_backup_cache = -1;
	int tw_backup_andsec = -1;
	int tw_backup_sdext = -1;
	int tw_backup_sdext2 = -1;

	string Backup_List;
	DataManager::GetValue("tw_backup_list", Backup_List);
	if (!Backup_List.empty()) {
		string backup_path;
		size_t start_pos = 0, end_pos;
		end_pos = Backup_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Backup_List.size()) {
			backup_path = Backup_List.substr(start_pos, end_pos - start_pos);

			if (backup_path == "/system")
				tw_backup_system = 1;
			else if (backup_path == "/data")
				tw_backup_data = 1;
			else if (backup_path == "/recovery")
				tw_backup_recovery = 1;
			else if (backup_path == "/cache")
				tw_backup_cache = 1;
			else if (backup_path == "/boot")
				tw_backup_boot = 1;
			else if (backup_path == "/and-sec")
				tw_backup_andsec = 1;
			else if (backup_path == "/sd-ext")
				tw_backup_sdext = 1;
			else if (backup_path == "/sdext2")
				tw_backup_sdext2 = 1;

			start_pos = end_pos + 1;
			end_pos = Backup_List.find(";", start_pos);
		}
	}

	DataManager::GetValue(TW_SKIP_MD5_GENERATE_VAR, skip_md5_gen);
	if (skip_md5_gen > 0)
		do_md5 = false;
	else
		do_md5 = true;

	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);	

	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	if (Backup_Name == "(Current Date)" || Backup_Name == "0" || Backup_Name.empty()) {
		char timestamp[255];
		sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
		Backup_Name = timestamp;
	}
	LOGINFO("Backup Name is: '%s'\n", Backup_Name.c_str());
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";
	LOGINFO("Full_Backup_Path is: '%s'\n", Full_Backup_Path.c_str());

	gui_print("\n[BACKUP STARTED]\n");
    	
	LOGINFO("Calculating backup details...\n");
	if (tw_backup_system > 0) {
		backup_sys = Find_Partition_By_Path("/system");
		if (backup_sys != NULL) {
			partition_count++;
			if (backup_sys->Backup_Method == 1)
				file_bytes += backup_sys->Backup_Size;
			else
				img_bytes += backup_sys->Backup_Size;
		} else {
			LOGERR("Unable to locate system partition.\n");
			DataManager::SetValue(TW_BACKUP_SYSTEM_VAR, 0);
		}
	}
	if (tw_backup_data > 0) {
		backup_data = Find_Partition_By_Path("/data");
		if (backup_data != NULL) {
			partition_count++;
			subpart_size = 0;
			if (backup_data->Has_SubPartition) {
				for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
					if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == backup_data->Mount_Point)
						subpart_size += (*subpart)->Backup_Size;
				}
			}
			if (backup_data->Backup_Method == 1)
				file_bytes += backup_data->Backup_Size + subpart_size;
			else
				img_bytes += backup_data->Backup_Size + subpart_size;
		} else {
			LOGERR("Unable to locate data partition.\n");
			DataManager::SetValue(TW_BACKUP_DATA_VAR, 0);
		}
	}
	if (tw_backup_cache > 0) {
		backup_cache = Find_Partition_By_Path("/cache");
		if (backup_cache != NULL) {
			partition_count++;
			if (backup_cache->Backup_Method == 1)
				file_bytes += backup_cache->Backup_Size;
			else
				img_bytes += backup_cache->Backup_Size;
		} else {
			LOGERR("Unable to locate cache partition.\n");
			DataManager::SetValue(TW_BACKUP_CACHE_VAR, 0);
		}
	}
	if (tw_backup_recovery > 0) {
		backup_recovery = Find_Partition_By_Path("/recovery");
		if (backup_recovery != NULL) {
			partition_count++;
			if (backup_recovery->Backup_Method == 1)
				file_bytes += backup_recovery->Backup_Size;
			else
				img_bytes += backup_recovery->Backup_Size;
		} else {
			LOGERR("Unable to locate recovery partition.\n");
			DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
		}
	}
#ifndef TW_HAS_NO_BOOT_PARTITION
	if (tw_backup_boot > 0) {
		backup_boot = Find_Partition_By_Path("/boot");
		if (backup_boot != NULL) {
			partition_count++;
			if (backup_boot->Backup_Method == 1)
				file_bytes += backup_boot->Backup_Size;
			else
				img_bytes += backup_boot->Backup_Size;
		} else {
			LOGERR("Unable to locate boot partition.\n");
			DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
		}
	}
#endif
	if (tw_backup_andsec > 0) {
		backup_andsec = Find_Partition_By_Path("/and-sec");
		if (backup_andsec != NULL) {
			partition_count++;
			if (backup_andsec->Backup_Method == 1)
				file_bytes += backup_andsec->Backup_Size;
			else
				img_bytes += backup_andsec->Backup_Size;
		} else {
			LOGERR("Unable to locate android secure partition.\n");
			DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
		}
	}
	if (tw_backup_sdext > 0) {
		backup_sdext = Find_Partition_By_Path("/sd-ext");
		if (backup_sdext != NULL) {
			partition_count++;
			if (backup_sdext->Backup_Method == 1)
				file_bytes += backup_sdext->Backup_Size;
			else
				img_bytes += backup_sdext->Backup_Size;
		} else {
			LOGERR("Unable to locate sd-ext partition.\n");
			DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
		}
	}
	if (tw_backup_sdext2 > 0) {
		backup_sdext2 = Find_Partition_By_Path("/sdext2");
		if (backup_sdext2 != NULL) {
			partition_count++;
			if (backup_sdext2->Backup_Method == 1)
				file_bytes += backup_sdext2->Backup_Size;
			else
				img_bytes += backup_sdext2->Backup_Size;
		} else {
			LOGERR("Unable to locate sdext2 partition.\n");
			DataManager::SetValue(TW_BACKUP_SDEXT2_VAR, 0);
		}
	}

	if (partition_count == 0) {
		gui_print("No partitions selected for backup.\n");
		return false;
	}
	total_bytes = file_bytes + img_bytes;
	gui_print(" * Total number of partitions to back up: %d\n", partition_count);
    	gui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		DataManager::GetValue(TW_SKIP_SD_FREE_SZ_CHECK, skip_free_space_check);
		if (skip_free_space_check)
			gui_print(" * Storage's free space check skipped.\n");
		else {
			free_space = storage->Free;
			gui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
			if (free_space - (32 * 1024 * 1024) < total_bytes) {
				// We require an extra 32MB just in case
				gui_print(" * Not enough free space on storage.\n");
				return false;
			}
		}
	} else {
		LOGERR("Unable to locate storage device.\n");
		return false;
	}
	img_bytes_remaining = img_bytes;
    	file_bytes_remaining = file_bytes;
	gui_print(" * Backup Folder:%s\n @ %s\n", Backup_Name.c_str(), Backup_Folder.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGERR("Failed to make backup folder.\n");
		return false;
	}

	DataManager::SetProgress(0.0);

	if (!Backup_Partition(backup_sys, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_data, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_cache, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_recovery, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_boot, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_andsec, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sdext, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sdext2, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;

	// Average BPS
	if (img_time == 0)
		img_time = 1;
	if (file_time == 0)
		file_time = 1;
	int img_bps = (int)img_bytes / (int)img_time;
	unsigned long long file_bps = file_bytes / (int)file_time;

	gui_print("Average backup rate for file systems: %llu MB/sec\n", (file_bps / (1024 * 1024)));
	gui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);
	unsigned long long actual_backup_size = TWFunc::Get_Folder_Size(Full_Backup_Path, true);
	actual_backup_size /= (1024LLU * 1024LLU);

	int prev_img_bps, use_compression;
	unsigned long long prev_file_bps;
	DataManager::GetValue(TW_BACKUP_AVG_IMG_RATE, prev_img_bps);
	img_bps += (prev_img_bps * 4);
	img_bps /= 5;

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, prev_file_bps);
	else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, prev_file_bps);
	file_bps += (prev_file_bps * 4);
	file_bps /= 5;

	DataManager::SetValue(TW_BACKUP_AVG_IMG_RATE, img_bps);
	if (use_compression)
		DataManager::SetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::SetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	gui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details(true);
	UnMount_Main_Partitions();
	gui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
	string backup_log = Full_Backup_Path + "recovery.log";
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
    	return true;
}

bool TWPartitionManager::Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count) {
	time_t Start, Stop;
	time(&Start);
	DataManager::ShowProgress(1.0 / (float)partition_count, 150);
	if (!Part->Restore(Restore_Name))
		return false;
	if (Part->Has_SubPartition) {
		std::vector<TWPartition*>::iterator subpart;

		for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
			if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
				if (!(*subpart)->Restore(Restore_Name))
					return false;
			}
		}
	}
	time(&Stop);
	gui_print("[%s done (%d seconds)]\n\n", Part->Display_Name.c_str(), (int)difftime(Stop, Start));
	return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int hserr, skip_md5_check, check, partition_count = 0;
	TWPartition* restore_sys = NULL;
	TWPartition* restore_data = NULL;
	TWPartition* restore_cache = NULL;
	TWPartition* restore_boot = NULL;
	TWPartition* restore_andsec = NULL;
	TWPartition* restore_sdext = NULL;
	TWPartition* restore_sdext2 = NULL;
	TWPartition* restore_sp1 = NULL;
	TWPartition* restore_sp2 = NULL;
	TWPartition* restore_sp3 = NULL;
	time_t rStart, rStop;
	time(&rStart);

	// Get which partitions to restore from PartitionList
	int tw_restore_boot = -1;
	int tw_restore_system = -1;
	int tw_restore_data = -1;
	int tw_restore_cache = -1;
	int tw_restore_andsec = -1;
	int tw_restore_sdext = -1;
	int tw_restore_sdext2 = -1;

	string Restore_List;
	DataManager::GetValue("tw_restore_selected", Restore_List);
	if (!Restore_List.empty()) {
		string restore_path;
		size_t start_pos = 0, end_pos;
		end_pos = Restore_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Restore_List.size()) {
			restore_path = Restore_List.substr(start_pos, end_pos - start_pos);

			if (restore_path == "/system")
				tw_restore_system = 1;
			else if (restore_path == "/data")
				tw_restore_data = 1;
			else if (restore_path == "/cache")
				tw_restore_cache = 1;
			else if (restore_path == "/boot")
				tw_restore_boot = 1;
			else if (restore_path == "/and-sec")
				tw_restore_andsec = 1;
			else if (restore_path == "/sd-ext")
				tw_restore_sdext = 1;
			else if (restore_path == "/sdext2")
				tw_restore_sdext2 = 1;

			start_pos = end_pos + 1;
			end_pos = Restore_List.find(";", start_pos);
		}
	}

	// Needed for any size checks
	string Full_FileName, part_size, parts, Command, data_size, dalvik_nfo, dalvik_host;
	int dataonext, dalvik_found_on_data = 1, dalvik_found_on_sdext = 1, dalvik_found_on_sdext2 = 1, nodalvikcache = 0;
	unsigned long long min_size = 0, dt_size = 0, dc_size = 0, tar_size = 0, file_size = 0;
	// TODO: 1048576(=1MB) or 131072(=1block) ?
	//	 Test and see which one is better to use...
	unsigned long long multiple = 1048576;

	gui_print("\n[RESTORE STARTED]\n\n");
	gui_print(" * Restore folder:%s\n @ %s\n", TWFunc::Get_Filename(Restore_Name).c_str(), TWFunc::Get_Path(Restore_Name).c_str());
	if (!Mount_Current_Storage(true))
		return false;

	// Handle size errors?
	DataManager::GetValue("tw_handle_restore_size", hserr);
	if (hserr > 0) {
		LOGINFO("TWRP will adjust partitions' size if needed.\n");
		// Check if this is a backup without dalvik-cache
		nodalvikcache = TWFunc::Path_Exists(Restore_Name + "/.nodalvikcache");
		if (nodalvikcache) {
			TWFunc::read_file(Restore_Name + "/.nodalvikcache", dalvik_nfo);
			vector<string> split;
			split = TWFunc::split_string(dalvik_nfo, ':', true);
			if (split.size() > 0) {
				dalvik_host = split[0]; // path of the partition that dalvik-cache is located
				stringstream ss(split[1]);
				ss >> dc_size; // stored size of dalvik-cache (in bytes)
			}
		}
	}
	DataManager::GetValue(TW_RESTORE_IS_DATAONEXT, dataonext);
	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, skip_md5_check);
	if (tw_restore_system > 0) {
		restore_sys = Find_Partition_By_Path("/system");
		if (restore_sys == NULL)
			LOGERR("Unable to locate system partition.\n");
		else {
			if (hserr > 0) {
				LOGINFO("Comparing %s's size to %s's size...\n", restore_sys->Backup_FileName.c_str(), restore_sys->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sys->Backup_FileName;				
				if (restore_sys->Use_unyaffs_To_Restore) {
					// This is a yaffs2.img
					file_size = TWFunc::Get_File_Size(Full_FileName);
					min_size = TWFunc::RoundUpSize(file_size, multiple);
				} else {
					// This is an archive
					twrpTar tar;
					tar.setfn(Full_FileName);
					tar_size = tar.uncompressedSize();
					min_size = TWFunc::RoundUpSize(tar_size, multiple);
				}
				// Now compare the size of the partition and the minimum required size
				LOGINFO("'system': size=%llub / min-req-size=%llub\n", restore_sys->Size, min_size);
				if (restore_sys->Size > min_size) {
					LOGINFO("%s's size is adequate to restore %s.\n", restore_sys->Backup_Name.c_str(), restore_sys->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGINFO("Size mismatch for %s.\n", restore_sys->Backup_Name.c_str());
					// cLK can deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						part_size += " system:" + TWFunc::to_string((int)(min_size/131072));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_data = NULL;
						LOGERR("Size of 'system' partition is less than needed.\n");
						LOGERR("Skipping 'system' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "S";
			} else
				partition_count++;
		}
	}
	if (tw_restore_data > 0) {
		restore_data = Find_Partition_By_Path("/data");
		if (restore_data == NULL)
			LOGERR("Unable to locate data partition.\n");
		else {
			if (hserr > 0) {
				LOGINFO("Comparing %s's size to %s's size...\n", restore_data->Backup_FileName.c_str(), restore_data->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_data->Backup_FileName;				
				if (restore_data->Use_unyaffs_To_Restore) {
					// This is a yaffs2.img
					file_size = TWFunc::Get_File_Size(Full_FileName);
					min_size = TWFunc::RoundUpSize(file_size, multiple);
				} else {
					// This is an archive
					twrpTar tar;
					tar.setfn(Full_FileName);
					tar_size = tar.uncompressedSize();
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!dataonext && !tar.entryExists("data/dalvik-cache/"))
						dalvik_found_on_data = 0;
					min_size = TWFunc::RoundUpSize(tar_size, multiple);
				}
				if (nodalvikcache && dalvik_host == "/data")
					min_size += dc_size;
				dt_size = min_size;
				// Now compare the size of the partition and the minimum required size
				LOGINFO("'data': size=%llub / min-req-size=%llub\n", restore_data->Size, min_size);
				if (restore_data->Size > min_size) {
					LOGINFO("%s's size is adequate to restore %s.\n", restore_data->Backup_Name.c_str(), restore_data->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGINFO("Size mismatch for %s.\n", restore_data->Backup_Name.c_str());
					// cLK can deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						data_size = " userdata:" + TWFunc::to_string((int)(min_size/131072));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_data = NULL;
						LOGERR("Size of 'data' partition is less than needed.\n");
						LOGERR("Skipping 'data' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "D";
			} else
				partition_count++;
		}
	}
	if (tw_restore_cache > 0) {
		restore_cache = Find_Partition_By_Path("/cache");
		if (restore_cache == NULL)
			LOGERR("Unable to locate cache partition.\n");
		else {
			partition_count++;
			parts += "C";
		}
	}
#ifndef TW_HAS_NO_BOOT_PARTITION
	if (tw_restore_boot > 0) {
		restore_boot = Find_Partition_By_Path("/boot");
		if (restore_boot == NULL)
			LOGERR("Unable to locate boot partition.\n");
		else {
			DataManager::GetValue(TW_RESTORE_BOOT_VAR, check);
			LOGINFO("TW_RESTORE_BOOT_VAR = %i\n", check);
			if (check == 2) {
				if (!TWFunc::Path_Exists(Restore_Name + "/boot.mtd.win")) {
					string cmd = "cd '" + Restore_Name + "' && tar -xf " + restore_boot->Backup_FileName + " ./initrd.gz ./zImage";
					LOGINFO("%s will be converted to boot.mtd.win\n", restore_boot->Backup_FileName.c_str());
					if (system(cmd.c_str()) == 0) {
						if(TWFunc::Path_Exists(Restore_Name + "/initrd.gz")
						&& TWFunc::Path_Exists(Restore_Name + "/zImage")) {
							// Create 'boot' mtd image
							cmd = "mkbootimg --kernel '" + Restore_Name + "/zImage'\
									 --ramdisk '" + Restore_Name + "/initrd.gz'\
									 --cmdline \"\"\
									 --base 0x11800000\
									 --output '" + Restore_Name + "/boot.mtd.win'";
							if (system(cmd.c_str()) == 0) {
								if (TWFunc::Path_Exists(Restore_Name + "/boot.mtd.win")) {
									// Remove the tar and the md5 file
									cmd = "cd '" + Restore_Name + "' && rm -rf " + restore_boot->Backup_FileName + "*";
									system(cmd.c_str());
									// Set new backup name
									restore_boot->Backup_FileName = "boot.mtd.win";
									// Create an MD5 file for the new file
									cmd = "cd '" + Restore_Name + "' && md5sum boot.mtd.win > boot.mtd.win.md5";
									LOGINFO("cmd = %s\n", cmd.c_str());
									system(cmd.c_str());
									LOGINFO("boot.mtd.win created!\n");
								}
							}
						}
					}
				}
			}
			if (check == 3) {
				if (!TWFunc::Path_Exists(Restore_Name + "/boot.yaffs2.win")) {
					string cmd = "cd '" + Restore_Name + "' && unpackbootimg " + restore_boot->Backup_FileName;
					LOGINFO("%s will be converted to boot.yaffs2.win\n", restore_boot->Backup_FileName.c_str());
					if (system(cmd.c_str()) == 0) {
						string ramdisk = Restore_Name + "/" + restore_boot->Backup_FileName + "-ramdisk.gz";
						string kernel = Restore_Name + "/" + restore_boot->Backup_FileName + "-zImage";
						if(TWFunc::Path_Exists(ramdisk)
						&& TWFunc::Path_Exists(kernel)) {
							string initrd = Restore_Name + "/initrd.gz";
							string zImage = Restore_Name + "/zImage";
							rename(ramdisk.c_str(), initrd.c_str());
							rename(kernel.c_str(), zImage.c_str());
							// Create 'boot' tar
							cmd = "cd '" + Restore_Name + "' && tar -cf boot.yaffs2.win initrd.gz zImage";
							if (system(cmd.c_str()) == 0) {
								if (TWFunc::Path_Exists(Restore_Name + "/boot.yaffs2.win")) {
									// Remove all the boot.img* files
									cmd = "cd '" + Restore_Name + "' && rm -rf " + restore_boot->Backup_FileName + "*";
									system(cmd.c_str());
									// Set new backup name
									restore_boot->Backup_FileName = "boot.yaffs2.win";
									// Create an MD5 file for the new file
									cmd = "cd '" + Restore_Name + "' && md5sum boot.yaffs2.win > boot.yaffs2.win.md5";
									system(cmd.c_str());
									LOGINFO("boot.yaffs2.win created!\n");
								}
							}
						}
					}
				}
			}
			if (hserr > 0) {
				LOGINFO("Comparing %s's size to %s's size...\n", restore_boot->Backup_FileName.c_str(), restore_boot->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_boot->Backup_FileName;
				if (restore_boot->Backup_FileName.find("mtd") != string::npos || restore_boot->Backup_FileName == "boot.img") {
					// This is a dumped img
					file_size = TWFunc::Get_File_Size(Full_FileName);
					min_size = TWFunc::RoundUpSize(file_size, multiple);
				} else {
					// This is an archive
					twrpTar tar;
					tar.setfn(Full_FileName);
					tar_size = tar.uncompressedSize();
					min_size = TWFunc::RoundUpSize(tar_size, multiple);
				}
				// Now compare the size of the partition and the minimum required size
				LOGINFO("'boot': size=%llub / min-req-size=%llub\n", restore_boot->Size, min_size);
				if (restore_boot->Size > min_size) {
					LOGINFO("%s's size is adequate to restore %s.\n", restore_boot->Backup_Name.c_str(), restore_boot->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGINFO("Size mismatch for %s.\n", restore_boot->Backup_Name.c_str());
					// cLK can help us deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						part_size += " boot:" + TWFunc::to_string((int)(min_size/131072));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_boot = NULL;
						LOGERR("Size of 'boot' partition is less than needed.\n");
						LOGERR("Skipping 'boot' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "B";
			} else
				partition_count++;
		}
	}
#endif
	if (tw_restore_andsec > 0) {
		restore_andsec = Find_Partition_By_Path("/and-sec");
		if (restore_andsec == NULL)
			LOGERR("Unable to locate android secure partition.\n");
		else {
			partition_count++;
			parts += "A";
		}
	}
	if (tw_restore_sdext > 0) {
		restore_sdext = Find_Partition_By_Path("/sd-ext");
		if (restore_sdext == NULL)
			LOGERR("Unable to locate sd-ext partition.\n");
		else {
			if (hserr > 0) {
				LOGINFO("Comparing %s's size to %s's size...\n", restore_sdext->Backup_FileName.c_str(), restore_sdext->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sdext->Backup_FileName;
				if (!TWFunc::Path_Exists(Full_FileName)) {
					int index = 0;
					char split_index[5];
					sprintf(split_index, "%03i", index);
					Full_FileName += split_index;
					while (TWFunc::Path_Exists(Full_FileName)) {
						gui_print("Getting size of archive %i...\n", index+1);
						twrpTar tar;
						tar.setfn(Full_FileName);
						min_size += tar.uncompressedSize();
						index++;		
						sprintf(split_index, "%03i", index);
						Full_FileName = Restore_Name + "/" + restore_sys->Backup_FileName + split_index;
					}
					if (index == 0)
						LOGERR("Error locating restore file: '%s'\n", Full_FileName.c_str());
				} else {
					twrpTar tar;
					tar.setfn(Full_FileName);
					min_size = tar.uncompressedSize();
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!tar.entryExists("sd-ext/dalvik-cache/"))
						dalvik_found_on_sdext = 0;
				}
				if (nodalvikcache && dalvik_host == "/sd-ext")
					min_size += dc_size;
				// Now compare the size of the partition and the minimum required size
				LOGINFO("'sd-ext': size=%llub / min-req-size=%llub\n", restore_sdext->Size, min_size);
				if (restore_sdext->Size > min_size) {
					LOGINFO("%s's size is adequate to restore %s.\n", restore_sdext->Backup_Name.c_str(), restore_sdext->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGINFO("Size mismatch for %s.\n", restore_sdext->Backup_Name.c_str());
					// TODO: TWRP could repartition card in this case
					//	 but better just inform that sd-ext partition is too small for this backup to be restored
					restore_sdext = NULL;
					LOGERR("Size of 'sd-ext' partition is less than needed.\n");
					LOGERR("Skipping 'sd-ext' from restoring process.\n");
				}
				parts += "E";
			} else
				partition_count++;
		}
	}
	if (tw_restore_sdext2 > 0) {
		restore_sdext2 = Find_Partition_By_Path("/sdext2");
		if (restore_sdext2 == NULL)
			LOGERR("Unable to locate sdext2 partition.\n");
		else {
			if (hserr > 0) {
				LOGINFO("Comparing %s's size to %s's size...\n", restore_sdext2->Backup_FileName.c_str(), restore_sdext2->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sdext2->Backup_FileName;
				if (!TWFunc::Path_Exists(Full_FileName)) {
					int index = 0;
					char split_index[5];
					sprintf(split_index, "%03i", index);
					Full_FileName += split_index;
					while (TWFunc::Path_Exists(Full_FileName)) {
						gui_print("Checking archive %i...\n", index+1);
						twrpTar tar;
						tar.setfn(Full_FileName);
						min_size += tar.uncompressedSize();
						index++;		
						sprintf(split_index, "%03i", index);
						Full_FileName = Restore_Name + "/" + restore_sdext2->Backup_FileName + split_index;
					}
					if (index == 0)
						LOGERR("Error locating restore file: '%s'\n", Full_FileName.c_str());
				} else {
					twrpTar tar;
					tar.setfn(Full_FileName);
					min_size = tar.uncompressedSize();
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!tar.entryExists("sdext2/dalvik-cache/"))
						dalvik_found_on_sdext2 = 0;
				}
				if (nodalvikcache && dalvik_host == "/sdext2")
					min_size += dc_size;
				// Now compare the size of the partition and the minimum required size
				LOGINFO("'sdext2': size=%llub / min-req-size=%llub\n", restore_sdext2->Size, min_size);
				if (restore_sdext2->Size > min_size) {
					LOGINFO("%s's size is adequate to restore %s.\n", restore_sdext2->Backup_Name.c_str(), restore_sdext2->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGINFO("Size mismatch for %s.\n", restore_sdext2->Backup_Name.c_str());
					// TODO: TWRP could repartition card in this case
					//	 but better just inform that sdext2 partition is too small for this backup to be restored
					restore_sdext2 = NULL;
					LOGERR("Size of 'sdext2' partition is less than needed.\n");
					LOGERR("Skipping 'sdext2' from restoring process.\n");
				}
				parts += "X";
			} else
				partition_count++;
		}
	}
	
	if (hserr > 0 && DataManager::Detect_BLDR() == 1) {
		if (!data_size.empty()) {
			// .nodalvikcache file was not found
			if (!nodalvikcache) {
				// data backup is to be restored, BUT no dalvik-cache was detected inside 
				if (restore_data != NULL && !dalvik_found_on_data) {
					// Either sdext backup is to be restored, BUT no dalvik-cache was detected inside
					if((restore_sdext != NULL && !dalvik_found_on_sdext)
					/* or sdext2 backup is to be restored, BUT no dalvik-cache was detected inside	  */
					|| (restore_sdext2 != NULL && !dalvik_found_on_sdext2)) {
						int i = 1; float incr = 1.0;
						DataManager::GetValue(TW_INCR_SIZE, i);
						incr += (float)i / (float)100;
						LOGINFO("Increment set: %f\n", incr);
						// increase data's new size just to be safe
						data_size = " userdata:" + TWFunc::to_string((int)(incr*dt_size/131072));						
					}
				}
			}
			part_size += data_size;
		}
		// create clkpartmgr's command
		Command = "clkpartmgr --restore=" + TWFunc::Get_Filename(Restore_Name); // folder's name
		if (skip_md5_check > 0)
			parts += "M"; // arg for skipping md5 check
		if (!parts.empty())
			Command += (" --partitions=" + parts); // which parts to restore
		if (!part_size.empty()) {
			Command += part_size; // the part of command that holds which partitions are going to be resized
			LOGINFO("Command: %s\n", Command.c_str());
			// notify user
			gui_print("The backup you selected to restore, probably\n");
			gui_print("requires at least one larger partition.\n");
			gui_print("'clkpartmgr' will be executed in order to check\n");
			gui_print("if we need to enter cLK and adjust the PTABLE.\n");
			// set TW_HANDLE_RESTORE_SIZE to -1 since we already handled the size mismatch
			DataManager::SetValue("tw_handle_restore_size", -1);
			sleep(5);
			// run clkpartmgr's command 
			if (system(Command.c_str()) == 1)
				DataManager::SetValue("tw_handle_restore_size", 1);
		} else
			return false;
	}

	if (partition_count == 0) {
		LOGERR("No partitions selected for restore.\n");
		return false;
	}

	if (skip_md5_check > 0)
		gui_print("Skipping MD5 check based on user setting.\n");
	else {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		gui_print("Verifying MD5...\n");
		if (restore_sys != NULL && !restore_sys->Check_MD5(Restore_Name))
			return false;
		if (restore_data != NULL && !restore_data->Check_MD5(Restore_Name))
			return false;
		if (restore_data != NULL && restore_data->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == restore_data->Mount_Point) {
					if (!(*subpart)->Check_MD5(Restore_Name))
						return false;
				}
			}
		}
		if (restore_cache != NULL && !restore_cache->Check_MD5(Restore_Name))
			return false;
		if (restore_boot != NULL && !restore_boot->Check_MD5(Restore_Name))
			return false;
		if (restore_andsec != NULL && !restore_andsec->Check_MD5(Restore_Name))
			return false;
		if (restore_sdext != NULL && !restore_sdext->Check_MD5(Restore_Name))
			return false;
		if (restore_sdext2 != NULL && !restore_sdext2->Check_MD5(Restore_Name))
			return false;
		gui_print("Done verifying MD5.\n");
	}

	gui_print("Restoring %i partitions...\n", partition_count);
	DataManager::SetProgress(0.0);
	if (restore_sys != NULL && !Restore_Partition(restore_sys, Restore_Name, partition_count))
		return false;
	if (restore_data != NULL && !Restore_Partition(restore_data, Restore_Name, partition_count))
		return false;
	if (restore_cache != NULL && !Restore_Partition(restore_cache, Restore_Name, partition_count))
		return false;
	if (restore_boot != NULL && !Restore_Partition(restore_boot, Restore_Name, partition_count))
		return false;
	if (restore_andsec != NULL && !Restore_Partition(restore_andsec, Restore_Name, partition_count))
		return false;
	if (restore_sdext != NULL && !Restore_Partition(restore_sdext, Restore_Name, partition_count))
		return false;
	if (restore_sdext2 != NULL && !Restore_Partition(restore_sdext2, Restore_Name, partition_count))
		return false;

	sleep(3);
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	Update_System_Details(true);
	UnMount_Main_Partitions();
	time(&rStop);
	gui_print("[RESTORE COMPLETED IN %d SECONDS]\n\n",(int)difftime(rStop,rStart));
	// hserr will be negative if we are restoring via OpenRecoveryScript
	// reset to 1, the pre-restore selected value
	if (hserr < 0)
		DataManager::SetValue("tw_handle_restore_size", 1);
	return true;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	// Start with the default values
	string Restore_List;
	int tw_restore_boot = -1;
	int tw_restore_system = -1;
	int tw_restore_data = -1;
	int tw_restore_sdext = -1;
	int tw_restore_sdext2 = -1;
	int tw_restore_is_dataonext = 0;
	bool get_date = true;
	bool split_archive = false;

	DIR* d;
	d = opendir(Restore_Name.c_str());
	if (d == NULL) {
		LOGERR("Error opening '%s'\n", Restore_Name.c_str());
		return;
	}

	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, "recovery.log"))
			continue;

		string dname = de->d_name;
		string filename = de->d_name;
		string search_str = ".", label, fstype, extn;
		if (de->d_type == DT_REG) {
			// Check if this is a backup featuring DataOnExt
			if (dname == ".dataonext")
				tw_restore_is_dataonext = 1;
			// Find last occurrence of period to immediately check file's extension 
			size_t last_occur = dname.rfind(search_str);
			if (last_occur == string::npos) {
				LOGERR("Unable to get required partition's info from '%s'.\n", de->d_name);
				continue;
			}
			extn = dname.substr((last_occur + 1), (dname.size() - last_occur - 1));
			
			if (extn.size() > 3) {
				if (strncmp(extn.c_str(), "win-", 4) == 0) {
					split_archive = false;
					continue; // skip irrelevant files
				}
				// check if file's extension has the format *.winxxx, cause this is part of a split archive
				if (strncmp(extn.c_str(), "winxxx", 3) == 0) {
					split_archive = true;
				} else {
					split_archive = false;
					continue; // skip irrelevant files
				}
			} else {
				split_archive = false;
				if (extn != "win" && extn != "img" && extn != "tar")
					continue; // skip irrelevant files
			}

			// Get the date once
			if (get_date) {
				string file_path = Restore_Name + "/" + dname;
				struct stat st;
				stat(file_path.c_str(), &st);
				string backup_date = ctime((const time_t*)(&st.st_mtime));
				DataManager::SetValue(TW_RESTORE_FILE_DATE, backup_date);
				get_date = false;
			}

			// Now find first occurrence of period in filename
			size_t first_occur = dname.find_first_of(search_str);
			// In case we are dealing with a CWM backup the android_secure's backup
			// name will start with a period, so adjust the name to avoid trouble
			if (first_occur == 0) {
				dname = dname.substr(1, (dname.size() - 1));
				first_occur = dname.find_first_of(search_str);
			}

			// Get the name of the partition
			label = dname.substr(0, first_occur);
			// If this is a CWM backup containing an '.android_secure.vfat.tar' change the label to TWRP standards
			if (label == "android_secure")
				label = "and-sec";
			// If this is a split archive and we already processed the first part continue
			if (split_archive) {
				if (label == "sd-ext" && tw_restore_sdext == 1)
					continue;
				else if (label == "sdext2" && tw_restore_sdext2 == 1)
					continue;
				else if (label == "system" && tw_restore_system == 1)
					continue;
				else if (label == "data" && tw_restore_data == 1)
					continue;
			}

			// If first and last occurrence are the same then this probably is
			// a CWM backup from an mtd partition (i.e. boot.img)
			if (first_occur == last_occur)
				fstype = "mtd";
			else
				fstype = dname.substr((first_occur + 1), (last_occur - first_occur - 1));
			
			// Check if the backed up partition exists in current ptable
			TWPartition* Part = Find_Partition_By_Path(label);
			if (Part == NULL) {
				LOGERR("Unable to locate partition by backup name: '%s'\n", label.c_str());
				continue;
			}

			// Check if we are dealing with a CWM backup...
			Part->Use_unyaffs_To_Restore = false;
			if (extn == "img" && fstype == "yaffs2") {
				// ...in order to use unyaffs to restore
				Part->Use_unyaffs_To_Restore = true;
			}
			// If we have a part of split archive, use the proper filename(without the numbers at the end)
			if (split_archive)
				Part->Backup_FileName = filename.substr(0, (filename.size() - 3));
			else
				Part->Backup_FileName = filename;

			LOGINFO("*Backup_FileName = %s\n", Part->Backup_FileName.c_str());
			LOGINFO("*File System = %s\n", fstype.c_str());
			LOGINFO("*extn = %s\n", extn.c_str());
			LOGINFO("*Backup_Path = %s\n", Part->Backup_Path.c_str());

			// HD2's boot partition depends on which bootloader we have
			if (Part->Backup_Path == "/boot") {
				// cLK installed
				if (DataManager::Detect_BLDR() == 1) {
					if (fstype == "mtd")
						tw_restore_boot = 1;
					else if (fstype == "yaffs2") {
						tw_restore_boot = 0;
						continue;
					}
				// MAGLDR installed
				} else if (DataManager::Detect_BLDR() == 2) {
					if((fstype == "mtd" && Part->Current_File_System == "mtd")
					|| (fstype == "yaffs2" && Part->Current_File_System == "yaffs2"))
						tw_restore_boot = 1;
					else if (fstype == "yaffs2" && Part->Current_File_System == "mtd")
						tw_restore_boot = 2;
					else if (fstype == "mtd" && Part->Current_File_System == "yaffs2")
						tw_restore_boot = 3;
				} else
					tw_restore_boot = 1;
			}
			else if (Part->Backup_Path == "/system") {
				if (tw_restore_boot == 0)
					continue;
				else
					tw_restore_system = 1;
			} else if (Part->Backup_Path == "/data")
				tw_restore_data = 1;
			else if (Part->Backup_Path == "/sd-ext")
				tw_restore_sdext = 1;
			else if (Part->Backup_Path == "/sdext2")
				tw_restore_sdext2 = 1;

			Restore_List += Part->Backup_Path + ";";
		}
	}
	closedir(d);

	// Set the final values
	DataManager::SetValue(TW_RESTORE_BOOT_VAR, tw_restore_boot);
	DataManager::SetValue(TW_RESTORE_IS_DATAONEXT, tw_restore_is_dataonext);
	DataManager::SetValue("tw_restore_list", Restore_List);
	DataManager::SetValue("tw_restore_selected", Restore_List);
	return;
}

int TWPartitionManager::Wipe_By_Path(string Path) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;
	string Local_Path = TWFunc::Get_Root_Path(Path);

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Mount_Point == Local_Path || (!(*iter)->Symlink_Mount_Point.empty() && (*iter)->Symlink_Mount_Point == Local_Path)) {
			if (Path == "/and-sec")
				ret = (*iter)->Wipe_AndSec();
			else
				ret = (*iter)->Wipe();
			found = true;
		} else if ((*iter)->Is_SubPartition && (*iter)->SubPartition_Of == Local_Path) {
			(*iter)->Wipe();
		}
	}
	if (found) {
		return ret;
	} else
		LOGERR("Wipe: Unable to find partition for path '%s'\n", Local_Path.c_str());
	return false;
}

int TWPartitionManager::Wipe_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Wipe();
			}
			return Part->Wipe();
		} else
			return Part->Wipe();
	}
	LOGERR("Wipe: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Wipe_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point)
					(*subpart)->Wipe();
			}
			return Part->Wipe();
		} else
			return Part->Wipe();
	}
	LOGERR("Wipe: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

bool TWPartitionManager::Wipe_MTD_By_Name(string ptnName) {
	gui_print("MTD Formatting \"%s\"\n", ptnName.c_str());

    	TWPartition* Part = Find_Partition_By_Name(ptnName);
    	if (Part == NULL) {
        	LOGERR("No mtd partition named '%s'", ptnName.c_str());
        	return false;
    	}
	string eraseimg = "erase_image " + ptnName;
	if (system(eraseimg.c_str()) != 0) {
		LOGERR("Failed to format '%s'", ptnName.c_str());
		return false;
	}	
	gui_print("Done.\n");
    	return true;
}

int TWPartitionManager::Factory_Reset(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = true;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Wipe_During_Factory_Reset && (*iter)->Is_Present) {
			if (!(*iter)->Wipe())
				ret = false;
		} else if ((*iter)->Has_Android_Secure) {
			if (!(*iter)->Wipe_AndSec())
				ret = false;
		}
	}
	return ret;
}

int TWPartitionManager::Wipe_All_But_SDCARD(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = true;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Present) {
			if((*iter)->Mount_Point == "/boot"
			|| (*iter)->Mount_Point == "/cache"
			|| (*iter)->Mount_Point == "/system"
			|| (*iter)->Mount_Point == "/data"
			|| (*iter)->Mount_Point == "/sd-ext") {
				if (!(*iter)->Wipe())
					ret = false;
			}
		} 
		if ((*iter)->Has_Android_Secure) {
			if (!(*iter)->Wipe_AndSec())
				ret = false;
		}
	}
	return ret;
}

int TWPartitionManager::Wipe_Dalvik_Cache(void) {
	int dataonext, dalvikonnand;
	string data_pth;
	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
	DataManager::GetValue(TW_BACKUP_NAND_DATA, dalvikonnand);
	DataManager::GetValue(TW_DATA_PATH, data_pth);

	gui_print("\nWiping Dalvik Cache Directories...\n");
	// cache
	TWPartition* Cache = Find_Partition_By_Path("/cache");
	if (Cache != NULL) {
		if (Cache->Dalvik_Cache_Size > 0) {
			if (Cache->Mount(true))
				TWFunc::removeDir("/cache/dalvik-cache", false);
			gui_print("Cleaned: /cache/dalvik-cache...\n");
			Cache->Dalvik_Cache_Size = 0;
		}		
	}
	// data
	TWPartition* Data = Find_Partition_By_Path("/data");
	if (Data != NULL) {	
		if (Data->Dalvik_Cache_Size > 0) {
			if (Data->Mount(true)) {
				if (dataonext) {
					if (dalvikonnand) {
						TWFunc::removeDir("/data", false);
						gui_print("Cleaned: DalvikOnNand...\n");
					}
				} else {
					TWFunc::removeDir("/data/dalvik-cache", false);
					gui_print("Cleaned: /data/dalvik-cache...\n");
				}
				Data->Dalvik_Cache_Size = 0;
			}
		}	
	}
	// sd-ext
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (SDext->Is_Present) {
			if (SDext->Mount(true)) {
				string dalvik_pth = "/sd-ext/dalvik-cache";
				if (dataonext)
					dalvik_pth = data_pth + "/dalvik-cache";
				if (TWFunc::Path_Exists(dalvik_pth)) {
					TWFunc::removeDir(dalvik_pth, false);
					gui_print("Cleaned: %s...\n", dalvik_pth.c_str());
					SDext->Dalvik_Cache_Size = 0;
				}
			}
		}
	}
	gui_print("Dalvik Cache Directories Wipe Complete!\n\n");

	return true;
}

int TWPartitionManager::Wipe_Rotate_Data(void) {
	if (!Mount_By_Path("/data", true))
		return false;

	unlink("/data/misc/akmd*");
	unlink("/data/misc/rild*");
	gui_print("Rotation data wiped.\n");
	return true;
}

int TWPartitionManager::Wipe_Battery_Stats(void) {
	struct stat st;

	if (!Mount_By_Path("/data", true))
		return false;

	if (0 != stat("/data/system/batterystats.bin", &st)) {
		gui_print("No Battery Stats Found. No Need To Wipe.\n");
	} else {
		remove("/data/system/batterystats.bin");
		gui_print("Cleared battery stats.\n");
	}
	return true;
}

int TWPartitionManager::Wipe_Android_Secure(void) {
	std::vector<TWPartition*>::iterator iter;
	int ret = false;
	bool found = false;

	// Iterate through all partitions
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Has_Android_Secure) {
			ret = (*iter)->Wipe_AndSec();
			found = true;
		}
	}
	if (found) {
		return ret;
	} else {
		LOGERR("No android secure partitions found.\n");
	}
	return false;
}

int TWPartitionManager::Format_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->UnMount(true))
			return false;

		return dat->Wipe_Encryption();
	} else {
		LOGERR("Unable to locate /data.\n");
		return false;
	}
	return false;
}

int TWPartitionManager::Wipe_Media_From_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->Has_Data_Media) {
			LOGERR("This device does not have /data/media\n");
			return false;
		}
		if (!dat->Mount(true))
			return false;

		gui_print("Wiping internal storage -- /data/media...\n");
		TWFunc::removeDir("/data/media", false);
		if (mkdir("/data/media", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
			return -1;
		if (dat->Has_Data_Media) {
			dat->Recreate_Media_Folder();
		}
		return true;
	} else {
		LOGERR("Unable to locate /data.\n");
		return false;
	}
	return false;
}

void TWPartitionManager::Refresh_Sizes(void) {
	Update_System_Details(true);
	return;
}

void TWPartitionManager::Update_System_Details(bool Display_Msg) {
	std::vector<TWPartition*>::iterator iter;
	int data_size = 0;

	if (Display_Msg)
		gui_print("Updating partition details...\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
			(*iter)->Update_Size(Display_Msg);
			if ((*iter)->Mount_Point == "/system") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SYSTEM_SIZE, backup_display_size);
			} else if ((*iter)->Mount_Point == "/data" || (*iter)->Mount_Point == "/datadata") {
				data_size += (int)((*iter)->Backup_Size / 1048576LLU);
			} else if ((*iter)->Mount_Point == "/cache") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_CACHE_SIZE, backup_display_size);
			} else if ((*iter)->Mount_Point == "/sd-ext") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SDEXT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					//DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/sdext2") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SDEXT2_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					//DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_SDEXT2_VAR, 0);
				} else {
					DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
				}
			} else if ((*iter)->Has_Android_Secure) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_ANDSEC_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					//DataManager::SetValue(TW_HAS_ANDROID_SECURE, 0);
					DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_ANDROID_SECURE, 1);
			} else if ((*iter)->Mount_Point == "/boot") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue("tw_has_boot_partition", 0);
					DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
				} else
					DataManager::SetValue("tw_has_boot_partition", 1);
			}
#ifdef SP1_NAME
			if ((*iter)->Backup_Name == EXPAND(SP1_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP1_SIZE, backup_display_size);
			}
#endif
#ifdef SP2_NAME
			if ((*iter)->Backup_Name == EXPAND(SP2_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP2_SIZE, backup_display_size);
			}
#endif
#ifdef SP3_NAME
			if ((*iter)->Backup_Name == EXPAND(SP3_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP3_SIZE, backup_display_size);
			}
#endif
		} else {
			// Handle unmountable partitions in case we reset defaults
			if ((*iter)->Mount_Point == "/boot") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_BOOT_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					//DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/recovery") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					//DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/data") {
				data_size += (int)((*iter)->Backup_Size / 1048576LLU);
			} else if ((*iter)->Mount_Point == "/sd-ext") {
				//DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
				DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
			} else if ((*iter)->Mount_Point == "/sdext2") {
				//DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
				DataManager::SetValue(TW_BACKUP_SDEXT2_VAR, 0);
			}
#ifdef SP1_NAME
			if ((*iter)->Backup_Name == EXPAND(SP1_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP1_SIZE, backup_display_size);
			}
#endif
#ifdef SP2_NAME
			if ((*iter)->Backup_Name == EXPAND(SP2_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP2_SIZE, backup_display_size);
			}
#endif
#ifdef SP3_NAME
			if ((*iter)->Backup_Name == EXPAND(SP3_NAME)) {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_SP3_SIZE, backup_display_size);
			}
#endif
		}
	}
	DataManager::SetValue(TW_BACKUP_DATA_SIZE, data_size);
	string current_storage_path = DataManager::GetCurrentStoragePath();
	TWPartition* FreeStorage = Find_Partition_By_Path(current_storage_path);
	if (FreeStorage != NULL) {
		// Attempt to mount storage
		if (!FreeStorage->Mount(false)) {
			// We couldn't mount storage... check to see if we have dual storage
			int has_dual_storage;
			DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual_storage);
			if (has_dual_storage == 1) {
				// We have dual storage, see if we're using the internal storage that should always be present
				if (current_storage_path == DataManager::GetSettingsStoragePath()) {
					if (!FreeStorage->Is_Encrypted) {
						// Not able to use internal, so error!
						LOGERR("Unable to mount internal storage.\n");
					}
					DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
				} else {
					// We were using external, flip to internal
					DataManager::SetValue(TW_USE_EXTERNAL_STORAGE, 0);
					current_storage_path = DataManager::GetCurrentStoragePath();
					FreeStorage = Find_Partition_By_Path(current_storage_path);
					if (FreeStorage != NULL) {
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
					} else {
						LOGERR("Unable to locate internal storage partition.\n");
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
					}
				}
			} else {
				// No dual storage and unable to mount storage, error!
				LOGERR("Unable to mount storage.\n");
				DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
			}
		} else {
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		}
	} else {
		LOGINFO("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
	}
	if (!Write_Fstab())
		LOGERR("Error creating fstab\n");

	if (Display_Msg)
		gui_print("Partition details updated.\n");
	return;
}

int TWPartitionManager::Decrypt_Device(string Password) {
#ifdef TW_INCLUDE_CRYPTO
	int ret_val, password_len;
	char crypto_blkdev[255], cPassword[255];
	size_t result;

	property_set("ro.crypto.state", "encrypted");
	#ifdef TW_INCLUDE_JB_CRYPTO
		// No extra flags needed
	#else
		property_set("ro.crypto.fs_type", CRYPTO_FS_TYPE);
		property_set("ro.crypto.fs_real_blkdev", CRYPTO_REAL_BLKDEV);
		property_set("ro.crypto.fs_mnt_point", CRYPTO_MNT_POINT);
		property_set("ro.crypto.fs_options", CRYPTO_FS_OPTIONS);
		property_set("ro.crypto.fs_flags", CRYPTO_FS_FLAGS);
		property_set("ro.crypto.keyfile.userdata", CRYPTO_KEY_LOC);
		#ifdef CRYPTO_SD_FS_TYPE
			property_set("ro.crypto.sd_fs_type", CRYPTO_SD_FS_TYPE);
			property_set("ro.crypto.sd_fs_real_blkdev", CRYPTO_SD_REAL_BLKDEV);
			property_set("ro.crypto.sd_fs_mnt_point", EXPAND(TW_INTERNAL_STORAGE_PATH));
		#endif
		property_set("rw.km_fips_status", "ready");
	#endif

	// some samsung devices store "footer" on efs partition
	TWPartition *efs = Find_Partition_By_Path("/efs");
	if(efs && !efs->Is_Mounted())
		efs->Mount(false);
	else
		efs = 0;
	#ifdef TW_EXTERNAL_STORAGE_PATH
		#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
			TWPartition* sdcard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
			if (sdcard && sdcard->Mount(false)) {
				property_set("ro.crypto.external_encrypted", "1");
				property_set("ro.crypto.external_blkdev", sdcard->Actual_Block_Device.c_str());
			} else {
				property_set("ro.crypto.external_encrypted", "0");
			}
		#endif
	#endif

	strcpy(cPassword, Password.c_str());
	int pwret = cryptfs_check_passwd(cPassword);

	if (pwret != 0) {
		LOGERR("Failed to decrypt data.\n");
		return -1;
	}

	if(efs)
		efs->UnMount(false);

	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") == 0) {
		LOGERR("Error retrieving decrypted data block device.\n");
	} else {
		TWPartition* dat = Find_Partition_By_Path("/data");
		if (dat != NULL) {
			DataManager::SetValue(TW_DATA_BLK_DEVICE, dat->Primary_Block_Device);
			DataManager::SetValue(TW_IS_DECRYPTED, 1);
			dat->Is_Decrypted = true;
			dat->Decrypted_Block_Device = crypto_blkdev;
			dat->Setup_File_System(false);
			gui_print("Data successfully decrypted, new block device: '%s'\n", crypto_blkdev);

	#ifdef CRYPTO_SD_FS_TYPE
			char crypto_blkdev_sd[255];
			property_get("ro.crypto.sd_fs_crypto_blkdev", crypto_blkdev_sd, "error");
			if (strcmp(crypto_blkdev_sd, "error") == 0) {
				LOGERR("Error retrieving decrypted data block device.\n");
			} else if(TWPartition* emmc = Find_Partition_By_Path(EXPAND(TW_INTERNAL_STORAGE_PATH))){
				emmc->Is_Decrypted = true;
				emmc->Decrypted_Block_Device = crypto_blkdev_sd;
				emmc->Setup_File_System(false);
				gui_print("Internal SD successfully decrypted, new block device: '%s'\n", crypto_blkdev_sd);
			}
	#endif //ifdef CRYPTO_SD_FS_TYPE
	#ifdef TW_EXTERNAL_STORAGE_PATH
		#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
			char is_external_decrypted[255];
			property_get("ro.crypto.external_use_ecryptfs", is_external_decrypted, "0");
			if (strcmp(is_external_decrypted, "1") == 0) {
				sdcard->Is_Decrypted = true;
				sdcard->EcryptFS_Password = Password;
				sdcard->Decrypted_Block_Device = sdcard->Actual_Block_Device;
				string MetaEcfsFile = EXPAND(TW_EXTERNAL_STORAGE_PATH);
				MetaEcfsFile += "/.MetaEcfsFile";
				if (!TWFunc::Path_Exists(MetaEcfsFile)) {
					// External storage isn't actually encrypted so unmount and remount without ecryptfs
					sdcard->UnMount(false);
					sdcard->Mount(false);
				}
			} else {
				sdcard->Is_Decrypted = false;
				sdcard->Decrypted_Block_Device = "";
			}
		#endif
	#endif //ifdef TW_EXTERNAL_STORAGE_PATH

			// Sleep for a bit so that the device will be ready
			sleep(1);
	#ifdef RECOVERY_SDCARD_ON_DATA
			if (dat->Mount(false) && TWFunc::Path_Exists("/data/media/0")) {
				dat->Storage_Path = "/data/media/0";
				dat->Symlink_Path = dat->Storage_Path;
				DataManager::SetValue(TW_INTERNAL_PATH, "/data/media/0");
				dat->UnMount(false);
				DataManager::SetBackupFolder("/data/media/0");
				Output_Partition(dat);
			}
	#endif
			Update_System_Details(false);
			UnMount_Main_Partitions();
		} else
			LOGERR("Unable to locate data partition.\n");
	}
	return 0;
#else
	LOGERR("No crypto support was compiled into this build.\n");
	return -1;
#endif
	return 1;
}

int TWPartitionManager::Fix_Permissions(void) {
	int result = 0, sdext_mounted = 0;
	int dataonext, data_size = 0;
	Update_System_Details(false);

	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (SDext->Is_Present) {
			if (SDext->Mount(true))
				sdext_mounted == 1;
		}
	}
	if (sdext_mounted) LOGERR("Fix_Permissions(sdext mounted)");
	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);
	if (dataonext) {
		string data_pth;
		DataManager::GetValue(TW_DATA_PATH, data_pth);
		if (Is_Mounted_By_Path("/data"))
			UnMount_By_Path("/data", true);
		sleep(2);
		system(("busybox mount -o bind " + data_pth + " /data").c_str());
		sleep(2);	
	} else {
		if (!Mount_By_Path("/data", true))
			return -1;
	}
	if (!Mount_By_Path("/system", true))
		return -1;

	fixPermissions perms;
	result = perms.fixPerms(true, true);
	UnMount_Main_Partitions();
	if (sdext_mounted)
		SDext->UnMount(true);
	if (result == 0)
		gui_print("Done.\n\n");
	return result;
}

int TWPartitionManager::Open_Lun_File(string Partition_Path, string Lun_File) {
	int fd;
	TWPartition* Part = Find_Partition_By_Path(Partition_Path);

	if (Part == NULL) {
		LOGERR("Unable to locate volume information for USB storage mode.");
		return false;
	}
	if (!Part->UnMount(true))
		return false;

	if ((fd = open(Lun_File.c_str(), O_WRONLY)) < 0) {
		LOGERR("Unable to open ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		return false;
	}

	if (write(fd, Part->Actual_Block_Device.c_str(), Part->Actual_Block_Device.size()) < 0) {
		LOGERR("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

int TWPartitionManager::usb_storage_enable(void) {
	int has_dual, has_data_media;
	char lun_file[255];
	string ext_path;
	bool has_multiple_lun = false;
	string Lun_File_str = CUSTOM_LUN_FILE;
	size_t found = Lun_File_str.find("%");

	DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual);
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (has_dual == 1 && has_data_media == 0) {
		if (found != string::npos) {
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			if (TWFunc::Path_Exists(lun_file))
				has_multiple_lun = true;
		}
		if (!has_multiple_lun) {
			// Device doesn't have multiple lun files, mount current storage
			sprintf(lun_file, CUSTOM_LUN_FILE, 0);
			return Open_Lun_File(DataManager::GetCurrentStoragePath(), lun_file);
		} else {
			// Device has multiple lun files
			sprintf(lun_file, CUSTOM_LUN_FILE, 0);
			if (!Open_Lun_File(DataManager::GetSettingsStoragePath(), lun_file))
				return false;
			DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);
			sprintf(lun_file, CUSTOM_LUN_FILE, 1);
			return Open_Lun_File(ext_path, lun_file);
		}
	} else {
		if (has_data_media == 0)
			ext_path = DataManager::GetCurrentStoragePath();
		else
			DataManager::GetValue(TW_EXTERNAL_PATH, ext_path);
		if (found != string::npos)
			sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		else
			strcpy(lun_file, CUSTOM_LUN_FILE);
		return Open_Lun_File(ext_path, lun_file);
	}
	return true;
}

int TWPartitionManager::usb_storage_disable(void) {
	int fd, index, lun_count = 1;
	char lun_file[255];
	size_t found;
	string Lun_File_str = CUSTOM_LUN_FILE;
	found = Lun_File_str.find("%");
	if (found == string::npos)
		lun_count = 1; // Device doesn't have multiple lun files
	else
		lun_count = 2; // Device has multiple lun files

	for (index=0; index<lun_count; index++) {
		if (lun_count == 2)
			sprintf(lun_file, CUSTOM_LUN_FILE, index);
		else
			strcpy(lun_file, CUSTOM_LUN_FILE);

		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			Mount_All_Storage();
			Update_System_Details(false);
			if (index == 0) {
				LOGERR("Unable to open ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		char ch = 0;
		if (write(fd, &ch, 1) < 0) {
			close(fd);
			Mount_All_Storage();
			Update_System_Details(false);
			if (index == 0) {
				LOGERR("Unable to write to ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		close(fd);
	}
	Mount_All_Storage();
	Update_System_Details(false);
	UnMount_Main_Partitions();
	return true;
}

void TWPartitionManager::Mount_All_Storage(void) {
	std::vector<TWPartition*>::iterator iter;

	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Is_Storage)
			(*iter)->Mount(false);
	}
}

void TWPartitionManager::UnMount_Main_Partitions(void) {
	// Unmounts system and data if data is not data/media
	// Also unmounts boot if boot is mountable
	LOGINFO("Unmounting main partitions...\n");

	TWPartition* Boot_Partition = Find_Partition_By_Path("/boot");

	UnMount_By_Path("/system", true);
#ifndef RECOVERY_SDCARD_ON_DATA
	UnMount_By_Path("/data", true);
#endif
	if (Boot_Partition != NULL && Boot_Partition->Can_Be_Mounted)
		Boot_Partition->UnMount(true);
}

int TWPartitionManager::Check_SDCard(void) {	
	gui_print("Checking SD Card's partitions...\n");
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGERR("Unable to locate SD Card.\n");
		return false;
	}
	if (!SDCard->UnMount(true))
		return false;

	string Command, result, sd_format;
	sd_format = SDCard->Current_File_System;
	if (sd_format == "vfat" && TWFunc::Path_Exists("/sbin/dosfsck")) {
			gui_print("Checking vfat filesystem @/sdcard...\n");	
			Command = "dosfsck -avw " + SDCard->Primary_Block_Device;
			LOGINFO("Command is: '%s'\n", Command.c_str());
			TWFunc::Exec_Cmd(Command, result);
	}
#ifdef TW_INCLUDE_MKNTFS
	else if (sd_format == "ntfs" && TWFunc::Path_Exists("/sbin/ntfsck")) {
		gui_print("Checking ntfs filesystem @/sdcard...\n");
		Command = "ntfsck " + SDCard->Primary_Block_Device;
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
	} 
#endif
#ifdef TW_INCLUDE_EXFAT
	else if (sd_format == "exfat" && TWFunc::Path_Exists("/sbin/exfatfsck")) {
			gui_print("Checking vfat filesystem @/sdcard...\n");	
			Command = "exfatfsck -v " + SDCard->Primary_Block_Device;
			LOGINFO("Command is: '%s'\n", Command.c_str());
			TWFunc::Exec_Cmd(Command, result);
	}
#endif
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (SDext->Size != 0 && SDext->Current_File_System != "swap") {
			if (!SDext->UnMount(true))
				return false;
			gui_print("Checking %s filesystem @/sd-ext...\n", SDext->Current_File_System.c_str());
			Command = "e2fsck -f -p -v " + SDext->Primary_Block_Device;
			LOGINFO("Command is: '%s'\n", Command.c_str());
			TWFunc::Exec_Cmd(Command, result);
		}
	}
	
	TWPartition* SDext2 = Find_Partition_By_Path("/sdext2");
	if (SDext2 != NULL) {
		if (SDext2->Size != 0 && SDext2->Current_File_System != "swap") {
			if (!SDext2->UnMount(true))
				return false;
			gui_print("Checking %s filesystem @/sdext2...\n", SDext2->Current_File_System.c_str());
			Command = "e2fsck -f -p -v " + SDext2->Primary_Block_Device;
			LOGINFO("Command is: '%s'\n", Command.c_str());
			TWFunc::Exec_Cmd(Command, result);
		}
	}
	SDCard->Mount(true);
	gui_print("SD Card's partition(s) check complete.\n");
	return true;
}

int TWPartitionManager::Partition_SDCard(void) {
	string Command, result, mkntfs_arg;
	string Device;
	int quick_format = 0, n_mounts, reboot_needed = 0;
	string sdcard_fs, sdext_fs, sdext2_fs;
	string sdcard_end, sdext_end, sdext2_end, sdswap_end;
	int sdcard_size, sdext_size, sdext2_size, sdswap_size, total_size = 0;
	char boot_recovery[1024];
	strcpy(boot_recovery, "recovery\n--partition=");

	if (SD_Partitioning_Done_Once) {
		gui_print("Please reboot before re-partitioning.\n");
		return false;
	}

	gui_print("Partitioning SD Card...\n");
	// Find present card's partitions and unmount all of them
	Command = "swapoff -a";
	TWFunc::Exec_Cmd(Command, result);
	TWPartition* Cache = Find_Partition_By_Path("/cache");
	TWPartition* SDext2 = Find_Partition_By_Path("/sdext2");
	if (SDext2 != NULL) {
		if (!SDext2->UnMount(true))
			return false;
	}
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	}
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard != NULL) {
		// Save settings file to cache/recovery
		if (Cache != NULL) {
			Cache->Mount(false);
			if (!TWFunc::Path_Exists("/cache/recovery/."))
				mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP);
			if (TWFunc::Path_Exists("/cache/recovery/.")) {
				unsigned long long l = TWFunc::Get_File_Size("/sdcard/TWRP/.twrps");
				if (Cache->Size - Cache->Backup_Size <= l) {
					string rm, res;
					rm = "rm -f /cache/recovery/last_log";
					TWFunc::Exec_Cmd(rm, res);
					rm = "rm -f /cache/recovery/last_install";
					TWFunc::Exec_Cmd(rm, res);
				}
				TWFunc::copy_file("/sdcard/TWRP/.twrps", "/cache/recovery/.twrps", 0755);
				LOGINFO("Saved a copy of settings file to /cache/recovery.\n");
			}
			Cache->UnMount(false);
		}
		if (!SDCard->UnMount(true))
			return false;
	} else {
		LOGERR("Unable to locate device to partition.\n");
		return false;
	}

	// Use the root block device
	Device = SDCard->Actual_Block_Device;
	Device.resize(strlen("/dev/block/mmcblkX"));

#ifdef TW_GET_STORAGE_PTABLE_USING_PARTED // not defined anywhere
	struct sdpartition {
		bool exists;
		string number;
		string start;
		string end;
		string size;
		string type;
		string fs;
		string flags;
	} sdcard, sdext, sdext2, sdswap;

	// Use 'parted print' to check present ptable
	Command = "parted " + Device + " unit MB print";
	LOGINFO("Command is: '%s'\n", Command.c_str());
	TWFunc::Exec_Cmd(Command, result);
	LOGINFO("%s", result.c_str());
	if (!result.empty()) {
		vector<string> lines;
		// get each line of output
		lines = TWFunc::split_string(result, '\n', true);
		if (lines.size() > 4) {
			int i;
			vector<string> line_parts;
			// The disk's total size is at 2nd line (lines[1])
			line_parts = TWFunc::split_string(lines[1], ' ', true);
			total_size = atoi(line_parts[2].substr(0, line_parts[2].size() - 2).c_str());
			// The partition table info start at 6th line (lines[5])
			for (i = 5; i < (int)lines.size(); i++) {
				line_parts = TWFunc::split_string(lines[i], ' ', true);
				if (line_parts[0] == "1") {
					// #1 is always sdcard
					sdcard.exists = true;
					sdcard.number = line_parts[0];
					sdcard.start = line_parts[1];
					sdcard.end = line_parts[2];
					sdcard.size = line_parts[3];
					sdcard.type = line_parts[4];
					sdcard.fs = line_parts[5];
					if (line_parts.size() > 6)
						sdcard.flags = line_parts[6];
				} else
				if (line_parts[0] == "2") {
					// #2 may be either swap or sd-ext
					if (line_parts[5] == "linux-swap(v1)") {
						sdswap.exists = true;
						sdswap.number = line_parts[0];
						sdswap.start = line_parts[1];
						sdswap.end = line_parts[2];
						sdswap.size = line_parts[3];
						sdswap.type = line_parts[4];
						sdswap.fs = line_parts[5];
						if (line_parts.size() > 6)
							sdswap.flags = line_parts[6];
					} else {
						sdext.exists = true;
						sdext.number = line_parts[0];
						sdext.start = line_parts[1];
						sdext.end = line_parts[2];
						sdext.size = line_parts[3];
						sdext.type = line_parts[4];
						sdext.fs = line_parts[5];
						if (line_parts.size() > 6)
							sdext.flags = line_parts[6];
					}
				} else
				if (line_parts[0] == "3") {
					// #3 may be either swap or sdext2
					if (line_parts[5] == "linux-swap(v1)") {
						sdswap.exists = true;
						sdswap.number = line_parts[0];
						sdswap.start = line_parts[1];
						sdswap.end = line_parts[2];
						sdswap.size = line_parts[3];
						sdswap.type = line_parts[4];
						sdswap.fs = line_parts[5];
						if (line_parts.size() > 6)
							sdswap.flags = line_parts[6];
					} else {
						sdext2.exists = true;
						sdext2.number = line_parts[0];
						sdext2.start = line_parts[1];
						sdext2.end = line_parts[2];
						sdext2.size = line_parts[3];
						sdext2.type = line_parts[4];
						sdext2.fs = line_parts[5];
						if (line_parts.size() > 6)
							sdext2.flags = line_parts[6];
					}
				} else
				if (line_parts[0] == "4") {
					// #4 may be swap
					if (line_parts[5] == "linux-swap(v1)") {
						sdswap.exists = true;
						sdswap.number = line_parts[0];
						sdswap.start = line_parts[1];
						sdswap.end = line_parts[2];
						sdswap.size = line_parts[3];
						sdswap.type = line_parts[4];
						sdswap.fs = line_parts[5];
						if (line_parts.size() > 6)
							sdswap.flags = line_parts[6];
					}
				}
			}
			LOGINFO("Storage's block device is '%s' [Total Size = %iMB]\n", Device.c_str(), total_size);
			// Print current ptable
			LOGINFO("Current partition table layout:\n");
			if (sdcard.exists)
				LOGINFO("1st %s partition\n* FS: %s\n* Size: %s\n", sdcard.type.c_str(), sdcard.fs.c_str(), sdcard.size.c_str());
			if (sdext.exists)
				LOGINFO("2nd %s partition\n* FS: %s\n* Size: %s\n", sdext.type.c_str(), sdext.fs.c_str(), sdext.size.c_str());
			if (sdext2.exists)
				LOGINFO("3rd %s partition\n* FS: %s\n* Size: %s\n", sdext2.type.c_str(), sdext2.fs.c_str(), sdext2.size.c_str());
			if (sdswap.exists)
				LOGINFO("Swap\n* Size: %s\n", sdswap.size.c_str());
		}
	}
#endif

	if (total_size == 0) {
		// Find the total size using /proc/partitions:
		FILE *fp = fopen("/proc/partitions", "rt");
		if (fp == NULL) {
			LOGERR("Unable to open /proc/partitions\n");
			return false;
		}
		char line[512];
		while (fgets(line, sizeof(line), fp) != NULL) {
			unsigned long major, minor, blocks;
			char device[512];
			char tmpString[64];
			string tmpdevice;

			if (strlen(line) < 7 || line[0] == 'm')
				continue;

			sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

			tmpdevice = "/dev/block/";
			tmpdevice += device;
			if (tmpdevice == Device) {
				// Adjust block size to MB
				total_size = (int)(blocks * 1024ULL  / 1000000LLU);
				//total_size = (int)(blocks / 1024UL);
				break;
			}
		}
		fclose(fp);
	}

	// Get the new values set by the user
	sdext_size = DataManager::GetIntValue("tw_sdext_size");
	sdext2_size = (sdext_size == 0 ? 0 : DataManager::GetIntValue("tw_sdext2_size"));
	sdswap_size = DataManager::GetIntValue("tw_swap_size");
	sdcard_size = total_size - (sdext_size + sdext2_size + sdswap_size);
	// Set up the sizes & args to use in commands
	sdcard_end = TWFunc::to_string(sdcard_size);
	sdext_end = TWFunc::to_string(sdcard_size + sdext_size);
	sdext2_end = TWFunc::to_string(sdcard_size + sdext_size + sdext2_size);
	sdswap_end = TWFunc::to_string(sdcard_size + sdext_size + sdext2_size + sdswap_size);
	n_mounts = DataManager::GetIntValue("tw_num_of_mounts_for_fs_check");
	quick_format = DataManager::GetIntValue("tw_mkntfs_quick_format");

	// Do some basic checks
	DataManager::GetValue("tw_sdcard_file_system", sdcard_fs);
	if (sdcard_fs.empty()) {
		LOGERR("sdcard's file-system type was not set.\n");
		return false;
	}
	DataManager::GetValue("tw_sdpart_file_system", sdext_fs);
	if (sdext_size > 0 && sdext_fs.empty()) {
		LOGERR("sdext's file-system type was not set.\n");
		return false;
	}
	DataManager::GetValue("tw_sdpart2_file_system", sdext2_fs);
	if (sdext2_size > 0 && sdext2_fs.empty()) {
		LOGERR("sdext2's file-system type was not set.\n");
		return false;
	}

	// Print new ptable
	LOGINFO("New partition table layout:\n");
	LOGINFO("1st primary partition\n* FS: %s\n* Size: %iMB\n", sdcard_fs.c_str(), sdcard_size);
	if (sdext_size > 0)
		LOGINFO("2nd primary partition\n* FS: %s\n* Size: %iMB\n", sdext_fs.c_str(), sdext_size);
	if (sdext2_size > 0)
		LOGINFO("3rd primary partition\n* FS: %s\n* Size: %iMB\n", sdext2_fs.c_str(), sdext2_size);
	if (sdswap_size > 0)
		LOGINFO("Swap\n* Size: %iMB\n", sdswap_size);

	// Final check
	if (sdcard_size == 0) {
		LOGERR("sdcard's size can't be zero!\n");
		return false;
	} else if (sdcard_size < 0) {
		LOGERR("[%s%s%s] size > [storage] size!\n",
			(sdext_size > 0 ? "sd-ext" : ""),
			(sdext2_size > 0 ? " + sdext2" : ""),
			(sdswap_size > 0 ? " + swap" : ""));
		return false;
	}

	// Start the partitioning by removing the ptable
	gui_print("Removing current partition table...\n");
	Command = "parted -s " + Device + " mklabel msdos";
	LOGINFO("Command is: '%s'\n", Command.c_str());
	if (TWFunc::Exec_Cmd(Command, result) != 0) {
		LOGERR("Unable to remove partition table.\n");
		Update_System_Details(false);
		return false;
	}

	// Create first primary partition
	gui_print("Creating 1st primary partition [/sdcard]...\n");
	gui_print("Formatting %s as %s...\n", SDCard->Actual_Block_Device.c_str(), sdcard_fs.c_str());
	if (sdcard_fs == "vfat") {
		Command = "parted -s " + Device + " mkpartfs primary fat32 0 " + sdcard_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
	}
#ifdef TW_INCLUDE_MKNTFS
	else if (sdcard_fs == "ntfs") {
		Command = "parted -s " + Device + " mkpart primary 0 " + sdcard_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
		if (quick_format)
			mkntfs_arg = "-vIf ";
		else {
			mkntfs_arg = "-vI ";
			gui_print("=>This process will take a long time<=\n");
		}
		Command = "mkntfs " + mkntfs_arg + SDCard->Actual_Block_Device;
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
	}
#endif
#ifdef TW_INCLUDE_EXFAT
	else if (sdcard_fs == "exfat") {
		Command = "parted -s " + Device + " mkpart primary 0 " + sdcard_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
		Command = "mkexfatfs " + SDCard->Actual_Block_Device;
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
	}
#endif
	else {
		LOGERR("Unsupported file-system type was set.\n");
		return false;
	}
	SDCard->Change_FS_Type(sdcard_fs);
	Write_Fstab();
	sync();
	
	// Recreate .android_secure and TWRP folder
	if (sdcard_fs == "vfat")
		Command = "mount " + SDCard->Actual_Block_Device + " " + SDCard->Mount_Point;
	else if (sdcard_fs == "ntfs")
		Command = "ntfs-3g -o rw,umask=0 " + SDCard->Actual_Block_Device + " " + SDCard->Mount_Point;
	else if (sdcard_fs == "exfat")
		Command = "exfat-fuse -o nonempty,big_writes,max_read=131072,max_write=131072 " + SDCard->Actual_Block_Device + " " + SDCard->Mount_Point;
	TWFunc::Exec_Cmd(Command, result);
	if (Is_Mounted_By_Path(SDCard->Mount_Point)) {
		SDCard->Recreate_AndSec_Folder();
		DataManager::SetupTwrpFolder();
		// Rewrite settings - these will be gone after sdcard is partitioned
		DataManager::Flush();	
#ifdef TW_EXTERNAL_STORAGE_PATH
		DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
		if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
			DataManager::SetValue(TW_ZIP_LOCATION_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
		DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, "/sdcard");
		if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
			DataManager::SetValue(TW_ZIP_LOCATION_VAR, "/sdcard");
#endif
		Command = "umount " + SDCard->Mount_Point;
		TWFunc::Exec_Cmd(Command, result);
		Cache->Mount(false);
		if (TWFunc::Path_Exists("/cache/recovery/.twrps")) {
			LOGINFO("Removing copy of settings file from /cache/recovery.\n");
			string rm = "rm -f /cache/recovery/.twrps";
			system(rm.c_str());
		}
		Cache->UnMount(false);
	} else {
		gui_print("Unable to mount %s when trying to\n", SDCard->Mount_Point.c_str());
		gui_print("recreate certain folders & rewrite settings file.\n");
		gui_print("TWRP needs to reboot to handle this issue.\n");
	}

	// Create second primary partition
	if (sdext_size > 0) {
		gui_print("Creating 2nd primary partition [/sd-ext]...\n");
		if (sdext_fs == "nilfs2")
			Command = "parted -s " + Device + " mkpart primary " + sdcard_end + "MB " + sdext_end + "MB";
		else
			Command = "parted -s " + Device + " mkpartfs primary ext2 " + sdcard_end + "MB " + sdext_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		SDext->Change_FS_Type(sdext_fs);
		SDext->Can_Be_Mounted = true;
		strcat(boot_recovery, "/dev/block/mmcblk0p2:");
		strcat(boot_recovery, sdext_fs.c_str());
		strcat(boot_recovery, " ");
		if (TWFunc::Exec_Cmd(Command, result) != 0)
			reboot_needed = 1;
	}
	// Create third primary partition
	if ((sdext_size > 0) && (sdext2_size > 0)) {
		gui_print("Creating 3rd primary partition [/sdext2]...\n", sdext2_fs.c_str());
		if (sdext2_fs == "nilfs2")
			Command = "parted -s " + Device + " mkpart primary " + sdext_end + "MB " + sdext2_end + "MB";
		else
			Command = "parted -s " + Device + " mkpartfs primary ext2 " + sdext_end + "MB " + sdext2_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		SDext2->Change_FS_Type(sdext2_fs);
		SDext2->Can_Be_Mounted = true;
		strcat(boot_recovery, "/dev/block/mmcblk0p3:");
		strcat(boot_recovery, sdext2_fs.c_str());
		strcat(boot_recovery, " ");
		if (TWFunc::Exec_Cmd(Command, result) != 0)
			reboot_needed = 1;
	}
	// Create swap partition
	if (sdswap_size > 0) {
		if (sdext_size == 0) { // no ext partition
			gui_print("Creating 2nd primary partition [swap]...\n");
			SDext->Change_FS_Type("swap");
			SDext->Swap = true;
		} else { // 1st ext partition exists			
			if (sdext2_size == 0) { // no 2nd ext partition
				gui_print("Creating 3rd primary partition [swap]...\n");
				SDext2->Change_FS_Type("swap");
				SDext2->Swap = true;
			} else { // 2nd ext partition exists
				gui_print("Creating 4th primary partition [swap]...\n");
			}
		}
		Command = "parted -s " + Device + " mkpartfs primary linux-swap " + sdext2_end + "MB " + sdswap_end + "MB";
		LOGINFO("Command is: '%s'\n", Command.c_str());
		TWFunc::Exec_Cmd(Command, result);
	}

	if (reboot_needed) {
		gui_print("Device will reboot in 5s in order to\n");
		gui_print("complete the partitioning operation...\n");
		sleep(5);

		// Create and set bootloader_message
		struct bootloader_message boot;
		memset(&boot, 0, sizeof(boot));
		strcpy(boot.recovery, boot_recovery);
		if (TWFunc::set_bootloader_msg(&boot) == 0)
			TWFunc::tw_reboot(rb_recovery); // reboot into recovery
	}

	// If set, format second primary partition
	if (sdext_size > 0) {
		if (sdext_fs == "nilfs2")
			Command = "mkfs.nilfs2 " + SDext->Primary_Block_Device; //dev/block/mmcblk0p2";
		else
			Command = "mke2fs -t " + sdext_fs + " -m 0 " + SDext->Primary_Block_Device; //dev/block/mmcblk0p2";
		gui_print("Formatting %s as %s...\n", SDext->Primary_Block_Device.c_str(), sdext_fs.c_str());
		LOGINFO("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command, result) != 0)
			reboot_needed = 1;
		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
	} else
		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);

	// If set, format third primary partition
	if ((sdext_size > 0) && (sdext2_size > 0)) {
		if (sdext2_fs == "nilfs2")
			Command = "mkfs.nilfs2 " + SDext2->Primary_Block_Device; //dev/block/mmcblk0p3";
		else
			Command = "mke2fs -t " + sdext2_fs + " -m 0 " + SDext2->Primary_Block_Device; //dev/block/mmcblk0p3";
		gui_print("Formatting %s as %s...\n", SDext2->Primary_Block_Device.c_str(), sdext2_fs.c_str());
		LOGINFO("Formatting sdext2 after partitioning, command: '%s'\n", Command.c_str());
		if (TWFunc::Exec_Cmd(Command, result) != 0)
			reboot_needed = 1;
		DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
	} else if (sdext2_size == 0)
    		DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);

	Update_System_Details(true);

	// Run tune2fs to set user selected number of mounts
	if (sdext_size > 0) {
		Command = "tune2fs -c " + TWFunc::to_string(n_mounts) + " " + SDext->Primary_Block_Device; //dev/block/mmcblk0p2";
		TWFunc::Exec_Cmd(Command, result);
	}
	if ((sdext_size > 0) && (sdext2_size > 0)) {
		Command = "tune2fs -c " + TWFunc::to_string(n_mounts) + " " + SDext2->Primary_Block_Device; //dev/block/mmcblk0p3";
		TWFunc::Exec_Cmd(Command, result);
	}

	gui_print("Partitioning complete.\n\n");
	gui_print("=>Rebooting is advised before further actions<=\n");
	SD_Partitioning_Done_Once = 1;
	return true;
}

int TWPartitionManager::Format_SDCard(string cmd) {
	/* cmd examples:
	 *		"/dev/block/mmcblk0p1:vfat /dev/block/mmcblk0p2:nilfs2"
	 *		"/dev/block/mmcblk0p1:ntfs /dev/block/mmcblk0p2:ext4 /dev/block/mmcblk0p3:ext4"
	 */
	int ret = 0;
	if (cmd.empty())
		return ret;

	vector<string> split;
	split = TWFunc::split_string(cmd, ' ', true);
	string Command, result, mkfs_cmd, tune2fs_cmd;
	Command = "swapoff -a";
	TWFunc::Exec_Cmd(Command, result);
	TWFunc::Update_Log_File();

	LOGINFO("Finishing storage formatting...\n");
	if (split.size() > 0) {
		string::size_type i;
		string fs, blk;
		vector<string> part;
		for (i = 0; i < split.size(); i++) {
			TWPartition* ptn = NULL;
			part = TWFunc::split_string(split[i], ':', true);
			if (part.size() > 1) {
				tune2fs_cmd = "";
				blk = part[0];
				fs = part[1];
				ptn = Find_Partition_By_Block(blk);
				if (ptn != NULL) {
					ptn->UnMount(false);
					if (fs == "ext4") {
						mkfs_cmd = "mke2fs -t ext4 -m 0 " + blk;
						tune2fs_cmd = "tune2fs -c " + DataManager::GetStrValue("tw_num_of_mounts_for_fs_check") + " " + blk;
					}
	#ifdef TW_INCLUDE_NILFS2
					else if (fs == "nilfs2") {
						mkfs_cmd = "mkfs.nilfs2 " + blk;
						tune2fs_cmd = "tune2fs -c " + DataManager::GetStrValue("tw_num_of_mounts_for_fs_check") + " " + blk;
					}
	#endif
					else {
						LOGINFO("Unsupported file-system set for '%s'!\n", blk.c_str());
						continue;
					}
				} else
					continue;
				ret = TWFunc::Exec_Cmd(mkfs_cmd, result);
				ptn->Change_FS_Type(fs);
				if (ret == 0 && !tune2fs_cmd.empty())
					TWFunc::Exec_Cmd(tune2fs_cmd, result);
			}
		}
	}
	Update_System_Details(false);
	LOGINFO("Formatting complete.\n");
	return ret;
}

// Convert filesystem of EXT partition (ext2 - ext3 - ext4 - nilfs2)
int TWPartitionManager::FSConvert_SDEXT(string extpath) {
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGERR("Unable to locate device's storage.\n");
		return false;
	}
	unsigned long long free_space = SDCard->Free;

	TWPartition* SDext = Find_Partition_By_Path(extpath);
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	} else {
		LOGERR("Unable to locate EXT partition.\n");
		return false;
	}

	unsigned long long ext_size = TWFunc::Get_Folder_Size(extpath, true);
	string temp_dir, Command, result, ext_format, n_mounts;
	int rescue_ext, c_var, restore_needed = 0;
	DataManager::GetValue("tw_rescue_ext_contents", rescue_ext);	
	DataManager::GetValue("tw_num_of_mounts_for_fs_check", c_var);
	if (SDext->Mount_Point == "/sd-ext")
		DataManager::GetValue("tw_sdpart_file_system", ext_format);
	else if (SDext->Mount_Point == "/sdext2")
		DataManager::GetValue("tw_sdpart2_file_system", ext_format);
	else
		ext_format = SDext->Current_File_System;
	temp_dir = DataManager::GetCurrentStoragePath() + "/TWRP/temp";
	if (SDext->Current_File_System == ext_format) {
		gui_print("No need to change file system type.\n");
	} else {
		n_mounts = TWFunc::to_string(c_var);
		if (rescue_ext) {
			if (!Mount_Current_Storage(true))
				return false;
			LOGINFO("Checking sizes...\n");			
		    	if (free_space - (32 * 1024 * 1024) < ext_size) {
				LOGERR("Not enough free space on storage.\n");
				return false;
			}
			if (!TWFunc::Recursive_Mkdir(temp_dir))
				LOGERR("Failed to make temp folder.\n");
			gui_print("Saving %s's contents on storage...\n", extpath.c_str());
			Command = "busybox cp -a " + extpath + "/* " + temp_dir;
			if (TWFunc::Exec_Cmd(Command, result) == 0)
				restore_needed = 1;
		}
		if (ext_format == "nilfs2")
			Command = "mkfs.nilfs2 " + SDext->Primary_Block_Device;
		else	
			Command = "mke2fs -t " + ext_format + " -m 0 " + SDext->Primary_Block_Device;
		gui_print("Formatting %s as %s...\n", SDext->Primary_Block_Device.c_str(), ext_format.c_str());
		SDext->Change_FS_Type(ext_format);
		TWFunc::Exec_Cmd(Command, result);
		Command = "tune2fs -c " + n_mounts + " " + SDext->Primary_Block_Device;
		TWFunc::Exec_Cmd(Command, result);
		Update_System_Details(false);

		if (restore_needed) {
			SDext->Mount(true);
			gui_print("Restoring %s's contents from storage...\n", extpath.c_str());
			Command = "busybox cp -a " + temp_dir + "/* " + extpath;
			TWFunc::Exec_Cmd(Command, result);
			Command = "rm -rf " + temp_dir;
			TWFunc::Exec_Cmd(Command, result);
		}
		gui_print("EXT formatting completed.\n");
		Update_System_Details(true);
	}
	return true;
}

void TWPartitionManager::Get_Partition_List(string ListType, std::vector<PartitionList> *Partition_List) {
	std::vector<TWPartition*>::iterator iter;
	if (ListType == "mount") {
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Can_Be_Mounted && !(*iter)->Is_SubPartition) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Display_Name;
				part.Mount_Point = (*iter)->Mount_Point;
				part.selected = (*iter)->Is_Mounted();
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "storage") {
		char free_space[255];
		string Current_Storage = DataManager::GetCurrentStoragePath();
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Is_Storage) {
				struct PartitionList part;
				sprintf(free_space, "%llu", (*iter)->Free / 1024 / 1024);
				part.Display_Name = (*iter)->Storage_Name + " (";
				part.Display_Name += free_space;
				part.Display_Name += "MB)";
				part.Mount_Point = (*iter)->Storage_Path;
				if ((*iter)->Storage_Path == Current_Storage)
					part.selected = 1;
				else
					part.selected = 0;
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "backup") {
		char backup_size[255];
		unsigned long long Backup_Size;
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Can_Be_Backed_Up && !(*iter)->Is_SubPartition && (*iter)->Is_Present) {
				struct PartitionList part;
				Backup_Size = (*iter)->Backup_Size;
				if ((*iter)->Has_SubPartition) {
					std::vector<TWPartition*>::iterator subpart;

					for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
						if ((*subpart)->Is_SubPartition && (*subpart)->Can_Be_Backed_Up && (*subpart)->Is_Present && (*subpart)->SubPartition_Of == (*iter)->Mount_Point)
							Backup_Size += (*subpart)->Backup_Size;
					}
				}
				sprintf(backup_size, "%llu", Backup_Size / 1024 / 1024);
				part.Display_Name = (*iter)->Backup_Display_Name + " (";
				part.Display_Name += backup_size;
				part.Display_Name += "MB)";
				part.Mount_Point = (*iter)->Backup_Path;
				part.selected = 0;
				Partition_List->push_back(part);
			}
		}
	} else if (ListType == "restore") {
		string Restore_List, restore_path;
		TWPartition* restore_part = NULL;

		DataManager::GetValue("tw_restore_list", Restore_List);
		if (!Restore_List.empty()) {
			size_t start_pos = 0, end_pos = Restore_List.find(";", start_pos);
			while (end_pos != string::npos && start_pos < Restore_List.size()) {
				restore_path = Restore_List.substr(start_pos, end_pos - start_pos);
				if ((restore_part = Find_Partition_By_Path(restore_path)) != NULL && !restore_part->Is_SubPartition) {
					if (restore_part->Backup_Name == "recovery") {
						// Don't allow restore of recovery (causes problems on some devices)
					} else {
						struct PartitionList part;
						part.Display_Name = restore_part->Backup_Display_Name;
						part.Mount_Point = restore_part->Backup_Path;
						part.selected = 1;
						Partition_List->push_back(part);
					}
				} else {
					LOGERR("Unable to locate '%s' partition for restore.\n", restore_path.c_str());
				}
				start_pos = end_pos + 1;
				end_pos = Restore_List.find(";", start_pos);
			}
		}
	} else if (ListType == "wipe") {
		struct PartitionList dalvik;
		dalvik.Display_Name = "Dalvik Cache";
		dalvik.Mount_Point = "DALVIK";
		dalvik.selected = 0;
		Partition_List->push_back(dalvik);
		for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
			if ((*iter)->Wipe_Available_in_GUI && !(*iter)->Is_SubPartition) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Display_Name;
				part.Mount_Point = (*iter)->Mount_Point;
				part.selected = 0;
				Partition_List->push_back(part);
			}
			if ((*iter)->Has_Android_Secure) {
				struct PartitionList part;
				part.Display_Name = (*iter)->Backup_Display_Name;
				part.Mount_Point = (*iter)->Backup_Path;
				part.selected = 0;
				Partition_List->push_back(part);
			}
		}
	} else {
		LOGERR("Unknown list type '%s' requested for TWPartitionManager::Get_Partition_List\n", ListType.c_str());
	}
}
