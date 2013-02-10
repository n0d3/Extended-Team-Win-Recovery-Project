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
#include <iomanip>
#include "variables.h"
#include "common.h"
#include "ui.h"
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"
#ifdef TW_INCLUDE_LIBTAR
	#include "twrpTar.hpp"
#else
	#include "makelist.hpp"
#endif

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
int TWPartitionManager::Process_Fstab(string Fstab_Filename, bool Display_Error) {
	FILE *fstabFile;
	char fstab_line[MAX_FSTAB_LINE_LENGTH];

	fstabFile = fopen(Fstab_Filename.c_str(), "rt");
	if (fstabFile == NULL) {
		LOGE("Critical Error: Unable to open fstab at '%s'.\n", Fstab_Filename.c_str());
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
#ifdef TW_EXTERNAL_STORAGE_PATH
			if (partition->Mount_Point == EXPAND(TW_EXTERNAL_STORAGE_PATH))
#else
			if (partition->Mount_Point == "/sdcard")
#endif
				DataManager::ReadSettingsFile(); // Why not try to read SettingsFile as soon as storage can be accessible!
		} else {
			delete partition;
		}
	}
	fclose(fstabFile);
	if (!Write_Fstab()) {
		if (Display_Error)
			LOGE("Error creating fstab\n");
		else
			LOGI("Error creating fstab\n");
	}
	Update_System_Details();
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
		LOGI("Can not open /etc/fstab.\n");
		return false;
	}
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
#ifdef TW_INCLUDE_NTFS_3G
			if ((*iter)->Current_File_System == "ntfs") {
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " ntfs-3g defaults 0 0\n";
			} else
#endif
#ifdef TW_INCLUDE_EXFAT
			if ((*iter)->Current_File_System == "exfat") { // not really sure...
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " exfat-fuse rw\n";
			} else
#endif
				Line = (*iter)->Actual_Block_Device + " " + (*iter)->Mount_Point + " " + (*iter)->Current_File_System + " rw\n";
			fputs(Line.c_str(), fp);
			// Handle subpartition tracking
			if ((*iter)->Is_SubPartition) {
				TWPartition* ParentPartition = Find_Partition_By_Path((*iter)->SubPartition_Of);
				if (ParentPartition)
					ParentPartition->Has_SubPartition = true;
				else
					LOGE("Unable to locate parent partition '%s' of '%s'\n", (*iter)->SubPartition_Of.c_str(), (*iter)->Mount_Point.c_str());
			}
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

	if (Part->Can_Be_Mounted) {
		printf("%s | %s | Size: %iMB Used: %iMB ", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb), (int)(Part->Used / mb));
		if (Part->Dalvik_Cache_Size > 0)
			printf("Dalvik-Cache: %iMB ", (int)(Part->Dalvik_Cache_Size / mb));
		if (Part->NativeSD_Size > 0)
			printf("NativeSD: %iMB ", (int)(Part->NativeSD_Size / mb));		
		printf("Free: %iMB Backup Size: %iMB", (int)(Part->Free / mb), (int)(Part->Backup_Size / mb));
		printf("\n   Flags: ");
		if (Part->Can_Be_Wiped)
			printf("Can_Be_Wiped ");
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
			printf("IsPresent ");
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
		printf("\n");
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
		if (!Part->Backup_Path.empty())
			printf("   Backup_Path: %s\n", Part->Backup_Path.c_str());
		if (!Part->Backup_FileName.empty())
			printf("   Backup_FileName: %s\n", Part->Backup_FileName.c_str());
		if (!Part->Storage_Path.empty())
			printf("   Storage_Path: %s\n", Part->Storage_Path.c_str());
		if (!Part->Current_File_System.empty())
			printf("   Current_File_System: %s\n", Part->Current_File_System.c_str());
		if (Part->Format_Block_Size != 0)
			printf("   Format_Block_Size: %i\n", Part->Format_Block_Size);
	} else {
		printf("%s | %s | Size: %iMB Backup Size: %iMB\n", Part->Mount_Point.c_str(), Part->Actual_Block_Device.c_str(), (int)(Part->Size / mb), (int)(Part->Backup_Size / mb));
	}
	if (Part->Size > 0) {
		if (!Part->Fstab_File_System.empty())
			printf("   Fstab_File_System: %s\n", Part->Fstab_File_System.c_str());
		if (!Part->Display_Name.empty())
			printf("   Display_Name: %s\n", Part->Display_Name.c_str());
		if (!Part->Backup_Name.empty())
			printf("   Backup_Name: %s\n", Part->Backup_Name.c_str());
		if (!Part->MTD_Name.empty())
			printf("   MTD_Name: %s\n", Part->MTD_Name.c_str());
		if (!Part->Primary_Block_Device.empty())
			printf("   Primary_Block_Device: %s\n", Part->Primary_Block_Device.c_str());
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
		LOGE("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGI("Mount: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
		LOGE("Mount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGI("Mount: Unable to find partition for block '%s'\n", Block.c_str());
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
		LOGE("Mount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGI("Mount: Unable to find partition for name '%s'\n", Name.c_str());
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
		LOGE("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
	} else {
		LOGI("UnMount: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
		LOGE("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
	else
		LOGI("UnMount: Unable to find partition for block '%s'\n", Block.c_str());
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
		LOGE("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	else
		LOGI("UnMount: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Path(string Path) {
	TWPartition* Part = Find_Partition_By_Path(Path);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for path '%s'\n", Path.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Block(string Block) {
	TWPartition* Part = Find_Partition_By_Block(Block);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for block '%s'\n", Block.c_str());
	return false;
}

int TWPartitionManager::Is_Mounted_By_Name(string Name) {
	TWPartition* Part = Find_Partition_By_Name(Name);

	if (Part)
		return Part->Is_Mounted();
	else
		LOGI("Is_Mounted: Unable to find partition for name '%s'\n", Name.c_str());
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
			LOGE("Backup name is too long.\n");
		return -2;
	}

	// Check each character
	strncpy(backup_name, Backup_Name.c_str(), copy_size);
	if (strcmp(backup_name, "0") == 0)
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
				LOGE("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Loc);
	strcpy(backup_loc, Backup_Loc.c_str());
	sprintf(tw_image_dir,"%s/%s/.", backup_loc, backup_name);
	if (TWFunc::Path_Exists(tw_image_dir)) {
		if (Display_Error)
			LOGE("A backup with this name already exists.\n");
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

	if (!generate_md5) 
		return true;

	TWFunc::GUI_Operation_Text(TW_GENERATE_MD5_TEXT, "Generating MD5");
	ui_print(" * Generating md5...\n");

	if (TWFunc::Path_Exists(Full_File)) {
		command = "cd '" + Backup_Folder + "' && md5sum " + Backup_Filename + " > " + Backup_Filename + ".md5";
		if (TWFunc::Exec_Cmd(command, result) == 0) {
			ui_print(" * MD5 Created.\n");
			return true;
		} else {
			ui_print(" * MD5 Error!\n");
			return false;
		}
	} else {
		char filename[512];
		int index = 0;
		sprintf(filename, "%s%03i", Full_File.c_str(), index);
		while (TWFunc::Path_Exists(filename) == true) {
			ostringstream intToStr;
			intToStr << index;
			ostringstream fn;
			fn << setw(3) << setfill('0') << intToStr.str();
			command = "cd '" + Backup_Folder + "' && md5sum " + Backup_Filename + fn.str() + " >"  + Backup_Filename + fn.str() + ".md5";
			if (TWFunc::Exec_Cmd(command, result) != 0) {
				ui_print(" * MD5 Error.\n");
				return false;
			}
			index++;
			sprintf(filename, "%s%03i", Full_File.c_str(), index);
		}
		if (index == 0) {
			LOGE("Backup file: '%s' not found!\n", filename);
			return false;
		}
		ui_print(" * MD5 Created.\n");
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
	int img_bps, file_bps;
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
	ui->SetProgress(pos);

	LOGI("Estimated Total time: %lu  Estimated remaining time: %lu\n", total_time, remain_time);

	// And get the time
	if (Part->Backup_Method == 1)
		section_time = Part->Backup_Size / file_bps;
	else
		section_time = Part->Backup_Size / img_bps;

	// Set the position
	pos = section_time / (float) total_time;
	ui->ShowProgress(pos, section_time);

	time(&start);

	if (Part->Backup(Backup_Folder)) {
		if (Part->Has_SubPartition) {
			std::vector<TWPartition*>::iterator subpart;

			for (subpart = Partitions.begin(); subpart != Partitions.end(); subpart++) {
				if ((*subpart)->Is_SubPartition && (*subpart)->SubPartition_Of == Part->Mount_Point) {
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
		LOGI("Partition Backup time: %d\n", backup_time);
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
	int check, do_md5, partition_count = 0, dataonext = 0;
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
	TWPartition* backup_sp1 = NULL;
	TWPartition* backup_sp2 = NULL;
	TWPartition* backup_sp3 = NULL;
	TWPartition* storage = NULL;
	std::vector<TWPartition*>::iterator subpart;
	struct tm *t;
	time_t start, stop, seconds, total_start, total_stop;
	seconds = time(0);
	t = localtime(&seconds);

	time(&total_start);

	Update_System_Details();

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_GENERATE_VAR, do_md5);
	if (do_md5 == 0)
		do_md5 = true;
	else
		do_md5 = false;

	DataManager::GetValue(TW_DATA_ON_EXT, dataonext);	

	DataManager::GetValue(TW_BACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	if (Backup_Name == "(Current Date)" || Backup_Name == "0" || Backup_Name.empty()) {
		char timestamp[255];
		sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
		Backup_Name = timestamp;
	}
	LOGI("Backup Name is: '%s'\n", Backup_Name.c_str());
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";
	LOGI("Full_Backup_Path is: '%s'\n", Full_Backup_Path.c_str());

	ui_print("\n[BACKUP STARTED]\n");
    	
	LOGI("Calculating backup details...\n");
	DataManager::GetValue(TW_BACKUP_SYSTEM_VAR, check);
	if (check) {
		backup_sys = Find_Partition_By_Path("/system");
		if (backup_sys != NULL) {
			partition_count++;
			if (backup_sys->Backup_Method == 1)
				file_bytes += backup_sys->Backup_Size;
			else
				img_bytes += backup_sys->Backup_Size;
		} else {
			LOGE("Unable to locate system partition.\n");
			DataManager::SetValue(TW_BACKUP_SYSTEM_VAR, 0);
		}
	}
	if (!dataonext)
		DataManager::GetValue(TW_BACKUP_DATA_VAR, check);
	else
		DataManager::GetValue(TW_BACKUP_NAND_DATA, check);
	if (check) {
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
			LOGE("Unable to locate data partition.\n");
			DataManager::SetValue(TW_BACKUP_DATA_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_CACHE_VAR, check);
	if (check) {
		backup_cache = Find_Partition_By_Path("/cache");
		if (backup_cache != NULL) {
			partition_count++;
			if (backup_cache->Backup_Method == 1)
				file_bytes += backup_cache->Backup_Size;
			else
				img_bytes += backup_cache->Backup_Size;
		} else {
			LOGE("Unable to locate cache partition.\n");
			DataManager::SetValue(TW_BACKUP_CACHE_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_RECOVERY_VAR, check);
	if (check) {
		backup_recovery = Find_Partition_By_Path("/recovery");
		if (backup_recovery != NULL) {
			partition_count++;
			if (backup_recovery->Backup_Method == 1)
				file_bytes += backup_recovery->Backup_Size;
			else
				img_bytes += backup_recovery->Backup_Size;
		} else {
			LOGE("Unable to locate recovery partition.\n");
			DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_BOOT_VAR, check);
	if (check) {
		backup_boot = Find_Partition_By_Path("/boot");
		if (backup_boot != NULL) {
			partition_count++;
			if (backup_boot->Backup_Method == 1)
				file_bytes += backup_boot->Backup_Size;
			else
				img_bytes += backup_boot->Backup_Size;
		} else {
			LOGE("Unable to locate boot partition.\n");
			DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_ANDSEC_VAR, check);
	if (check) {
		backup_andsec = Find_Partition_By_Path("/and-sec");
		if (backup_andsec != NULL) {
			partition_count++;
			if (backup_andsec->Backup_Method == 1)
				file_bytes += backup_andsec->Backup_Size;
			else
				img_bytes += backup_andsec->Backup_Size;
		} else {
			LOGE("Unable to locate android secure partition.\n");
			DataManager::SetValue(TW_BACKUP_ANDSEC_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_SDEXT_VAR, check);
	if (check) {
		backup_sdext = Find_Partition_By_Path("/sd-ext");
		if (backup_sdext != NULL) {
			partition_count++;
			if (backup_sdext->Backup_Method == 1)
				file_bytes += backup_sdext->Backup_Size;
			else
				img_bytes += backup_sdext->Backup_Size;
		} else {
			LOGE("Unable to locate sd-ext partition.\n");
			DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
		}
	}
	DataManager::GetValue(TW_BACKUP_SDEXT2_VAR, check);
	if (check) {
		backup_sdext2 = Find_Partition_By_Path("/sdext2");
		if (backup_sdext2 != NULL) {
			partition_count++;
			if (backup_sdext2->Backup_Method == 1)
				file_bytes += backup_sdext2->Backup_Size;
			else
				img_bytes += backup_sdext2->Backup_Size;
		} else {
			LOGE("Unable to locate sdext2 partition.\n");
			DataManager::SetValue(TW_BACKUP_SDEXT2_VAR, 0);
		}
	}
#ifdef SP1_NAME
	DataManager::GetValue(TW_BACKUP_SP1_VAR, check);
	if (check) {
		backup_sp1 = Find_Partition_By_Path(EXPAND(SP1_NAME));
		if (backup_sp1 != NULL) {
			partition_count++;
			if (backup_sp1->Backup_Method == 1)
				file_bytes += backup_sp1->Backup_Size;
			else
				img_bytes += backup_sp1->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP1_NAME));
			DataManager::SetValue(TW_BACKUP_SP1_VAR, 0);
		}
	}
#endif
#ifdef SP2_NAME
	DataManager::GetValue(TW_BACKUP_SP2_VAR, check);
	if (check) {
		backup_sp2 = Find_Partition_By_Path(EXPAND(SP2_NAME));
		if (backup_sp2 != NULL) {
			partition_count++;
			if (backup_sp2->Backup_Method == 1)
				file_bytes += backup_sp2->Backup_Size;
			else
				img_bytes += backup_sp2->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP2_NAME));
			DataManager::SetValue(TW_BACKUP_SP2_VAR, 0);
		}
	}
#endif
#ifdef SP3_NAME
	DataManager::GetValue(TW_BACKUP_SP3_VAR, check);
	if (check) {
		backup_sp3 = Find_Partition_By_Path(EXPAND(SP3_NAME));
		if (backup_sp3 != NULL) {
			partition_count++;
			if (backup_sp3->Backup_Method == 1)
				file_bytes += backup_sp3->Backup_Size;
			else
				img_bytes += backup_sp3->Backup_Size;
		} else {
			LOGE("Unable to locate %s partition.\n", EXPAND(SP3_NAME));
			DataManager::SetValue(TW_BACKUP_SP3_VAR, 0);
		}
	}
#endif

	if (partition_count == 0) {
		ui_print("No partitions selected for backup.\n");
		return false;
	}
	total_bytes = file_bytes + img_bytes;
	ui_print(" * Total number of partitions to back up: %d\n", partition_count);
    	ui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		ui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
	} else {
		LOGE("Unable to locate storage device.\n");
		return false;
	}
	if (free_space - (32 * 1024 * 1024) < total_bytes) {
		// We require an extra 32MB just in case
		LOGE("Not enough free space on storage.\n");
		return false;
	}
	img_bytes_remaining = img_bytes;
    	file_bytes_remaining = file_bytes;
	ui_print(" * Backup Folder:%s\n @ %s\n", Backup_Name.c_str(), Backup_Folder.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGE("Failed to make backup folder.\n");
		return false;
	}

	ui->SetProgress(0.0);

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
	if (!Backup_Partition(backup_sp1, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sp2, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;
	if (!Backup_Partition(backup_sp3, Full_Backup_Path, do_md5, &img_bytes_remaining, &file_bytes_remaining, &img_time, &file_time, &img_bytes, &file_bytes))
		return false;

	// Average BPS
	if (img_time == 0)
		img_time = 1;
	if (file_time == 0)
		file_time = 1;
	int img_bps = (int)img_bytes / (int)img_time;
	int file_bps = (int)file_bytes / (int)file_time;

	ui_print("Average backup rate for file systems: %lu MB/sec\n", (file_bps / (1024 * 1024)));
	ui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

	time(&total_stop);
	int total_time = (int) difftime(total_stop, total_start);
	unsigned long long actual_backup_size = TWFunc::Get_Folder_Size(Full_Backup_Path, true);
	actual_backup_size /= (1024LLU * 1024LLU);

	int prev_img_bps, prev_file_bps, use_compression;
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

	ui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details();
	UnMount_Main_Partitions();
	ui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
	string backup_log = Full_Backup_Path + "recovery.log";
	TWFunc::copy_file("/tmp/recovery.log", backup_log, 0644);
    	return true;
}

bool TWPartitionManager::Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count) {
	time_t Start, Stop;
	time(&Start);
	ui->ShowProgress(1.0 / (float)partition_count, 150);
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
	ui_print("[%s done (%d seconds)]\n\n", Part->Display_Name.c_str(), (int)difftime(Stop, Start));
	return true;
}

int TWPartitionManager::Run_Restore(string Restore_Name) {
	int hserr, check_md5, check, partition_count = 0;
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
	// Needed for any size checks
	string Full_FileName, part_size, parts, Command, data_size;
	int dataonext, dalvik_found_on_data = 1, dalvik_found_on_sdext = 1, dalvik_found_on_sdext2 = 1;
	unsigned long long min_size, dt_size;
	
	ui_print("\n[RESTORE STARTED]\n\n");
	ui_print(" * Restore folder:%s\n @ %s\n", TWFunc::Get_Filename(Restore_Name).c_str(), TWFunc::Get_Path(Restore_Name).c_str());
	if (!Mount_Current_Storage(true))
		return false;

	// Handle size errors?
	DataManager::GetValue("tw_handle_restore_size", hserr);
	if (hserr > 0)
		LOGI("TWRP will adjust partitions' size if needed.\n");
	
	DataManager::GetValue(TW_RESTORE_IS_DATAONEXT, dataonext);
	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check_md5);
	DataManager::GetValue(TW_RESTORE_SYSTEM_VAR, check);
	if (check > 0) {
		restore_sys = Find_Partition_By_Path("/system");
		if (restore_sys == NULL)
			LOGE("Unable to locate system partition.\n");
		else {
			if (hserr > 0) {
				LOGI("Comparing %s's size to %s's size...\n", restore_sys->Backup_FileName.c_str(), restore_sys->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sys->Backup_FileName;				
				if (restore_sys->Use_unyaffs_To_Restore) {
					// This is a yaffs2.img
					min_size += TWFunc::Get_File_Size(Full_FileName);
				} else {
					// This is an archive
					min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
				}
				// Now compare the size of the partition and the minimum required size
				if (restore_sys->Size >= min_size) {
					LOGI("%s's size is adequate to restore %s.\n", restore_sys->Backup_Name.c_str(), restore_sys->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGI("Size mismatch for %s.\n", restore_sys->Backup_Name.c_str());
					// cLK can deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						part_size += " system:" + TWFunc::to_string((int)(min_size/1048576LLU));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_data = NULL;
						LOGE("Size of 'system' partition is less than needed.\n");
						LOGE("Skipping 'system' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "S";
			} else
				partition_count++;
		}
	}
	DataManager::GetValue(TW_RESTORE_DATA_VAR, check);
	if (check > 0) {
		restore_data = Find_Partition_By_Path("/data");
		if (restore_data == NULL)
			LOGE("Unable to locate data partition.\n");
		else {
			if (hserr > 0) {
				LOGI("Comparing %s's size to %s's size...\n", restore_data->Backup_FileName.c_str(), restore_data->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_data->Backup_FileName;				
				if (restore_data->Use_unyaffs_To_Restore) {
					// This is a yaffs2.img
					min_size += TWFunc::Get_File_Size(Full_FileName);
				} else {
					// This is an archive
					min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
					dt_size = min_size;
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!dataonext && !TWFunc::Tar_Entry_Exists(Full_FileName, "dalvik-cache", 0))
						dalvik_found_on_data = 0;
				}
				// Now compare the size of the partition and the minimum required size
				if (restore_data->Size >= min_size) {
					LOGI("%s's size is adequate to restore %s.\n", restore_data->Backup_Name.c_str(), restore_data->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGI("Size mismatch for %s.\n", restore_data->Backup_Name.c_str());
					// cLK can deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						data_size = " data:" + TWFunc::to_string((int)(min_size/1048576LLU));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_data = NULL;
						LOGE("Size of 'data' partition is less than needed.\n");
						LOGE("Skipping 'data' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "D";
			} else
				partition_count++;
		}
	}
	DataManager::GetValue(TW_RESTORE_CACHE_VAR, check);
	if (check > 0) {
		restore_cache = Find_Partition_By_Path("/cache");
		if (restore_cache == NULL)
			LOGE("Unable to locate cache partition.\n");
		else {
			partition_count++;
			parts += "C";
		}
	}
	DataManager::GetValue(TW_RESTORE_BOOT_VAR, check);
	if (check > 0) {
		restore_boot = Find_Partition_By_Path("/boot");
		if (restore_boot == NULL)
			LOGE("Unable to locate boot partition.\n");
		else {
			if (hserr > 0) {
				LOGI("Comparing %s's size to %s's size...\n", restore_boot->Backup_FileName.c_str(), restore_boot->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_boot->Backup_FileName;
				if (restore_boot->Backup_FileName.find("mtd") != string::npos) {
					// This is a dumped img
					min_size += TWFunc::Get_File_Size(Full_FileName);
				} else {
					// This is an archive
					min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
				}
				// Now compare the size of the partition and the minimum required size
				if (restore_boot->Size >= min_size) {
					LOGI("%s's size is adequate to restore %s.\n", restore_boot->Backup_Name.c_str(), restore_boot->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGI("Size mismatch for %s.\n", restore_boot->Backup_Name.c_str());
					// cLK can help us deal with this case
					// TWRP just needs to call later clkpartmgr
					if (DataManager::Detect_BLDR() == 1) {
						part_size += " boot:" + TWFunc::to_string((int)(min_size/1048576LLU));
					} else if (DataManager::Detect_BLDR() == 2) {
						restore_boot = NULL;
						LOGE("Size of 'boot' partition is less than needed.\n");
						LOGE("Skipping 'boot' from restoring process.\n");
					} else if (DataManager::Detect_BLDR() == 0)
						partition_count++;
				}
				parts += "B";
			} else
				partition_count++;
		}
	}
	DataManager::GetValue(TW_RESTORE_ANDSEC_VAR, check);
	if (check > 0) {
		restore_andsec = Find_Partition_By_Path("/and-sec");
		if (restore_andsec == NULL)
			LOGE("Unable to locate android secure partition.\n");
		else {
			partition_count++;
			parts += "A";
		}
	}
	DataManager::GetValue(TW_RESTORE_SDEXT_VAR, check);
	if (check > 0) {
		restore_sdext = Find_Partition_By_Path("/sd-ext");
		if (restore_sdext == NULL)
			LOGE("Unable to locate sd-ext partition.\n");
		else {
			if (hserr > 0) {
				LOGI("Comparing %s's size to %s's size...\n", restore_sdext->Backup_FileName.c_str(), restore_sdext->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sdext->Backup_FileName;
				if (!TWFunc::Path_Exists(Full_FileName)) {
					int index = 0;
					char split_index[5];
					sprintf(split_index, "%03i", index);
					Full_FileName += split_index;
					while (TWFunc::Path_Exists(Full_FileName)) {
						ui_print("Getting size of archive %i...\n", index+1);
						min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
						index++;		
						sprintf(split_index, "%03i", index);
						Full_FileName = Restore_Name + "/" + restore_sys->Backup_FileName + split_index;
					}
					if (index == 0)
						LOGE("Error locating restore file: '%s'\n", Full_FileName.c_str());
				} else {
					min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!TWFunc::Tar_Entry_Exists(Full_FileName, "dalvik-cache", 0))
						dalvik_found_on_sdext = 0;
				}
				// Now compare the size of the partition and the minimum required size
				if (restore_sdext->Size >= min_size) {
					LOGI("%s's size is adequate to restore %s.\n", restore_sdext->Backup_Name.c_str(), restore_sdext->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGI("Size mismatch for %s.\n", restore_sdext->Backup_Name.c_str());
					// TODO: TWRP could repartition card in this case
					//	 but better just inform that sd-ext partition is too small for this backup to be restored
					restore_sdext = NULL;
					LOGE("Size of 'sd-ext' partition is less than needed.\n");
					LOGE("Skipping 'sd-ext' from restoring process.\n");
				}
				parts += "E";
			} else
				partition_count++;
		}
	}
	DataManager::GetValue(TW_RESTORE_SDEXT2_VAR, check);
	if (check > 0) {
		restore_sdext2 = Find_Partition_By_Path("/sdext2");
		if (restore_sdext2 == NULL)
			LOGE("Unable to locate sdext2 partition.\n");
		else {
			if (hserr > 0) {
				LOGI("Comparing %s's size to %s's size...\n", restore_sdext2->Backup_FileName.c_str(), restore_sdext2->Backup_Name.c_str());
				min_size = 0;
				Full_FileName = Restore_Name + "/" + restore_sdext2->Backup_FileName;
				if (!TWFunc::Path_Exists(Full_FileName)) {
					int index = 0;
					char split_index[5];
					sprintf(split_index, "%03i", index);
					Full_FileName += split_index;
					while (TWFunc::Path_Exists(Full_FileName)) {
						ui_print("Checking archive %i...\n", index+1);
						min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
						index++;		
						sprintf(split_index, "%03i", index);
						Full_FileName = Restore_Name + "/" + restore_sys->Backup_FileName + split_index;
					}
					if (index == 0)
						LOGE("Error locating restore file: '%s'\n", Full_FileName.c_str());
				} else {
					min_size += TWFunc::Get_Archive_Uncompressed_Size(Full_FileName);
					// check if the archive contains dalvik-cache.
					// If not we must add enough space for it
					if (!TWFunc::Tar_Entry_Exists(Full_FileName, "dalvik-cache", 0))
						dalvik_found_on_sdext2 = 0;
				}
				// Now compare the size of the partition and the minimum required size
				if (restore_sdext2->Size >= min_size) {
					LOGI("%s's size is adequate to restore %s.\n", restore_sdext2->Backup_Name.c_str(), restore_sdext2->Backup_FileName.c_str());
					partition_count++;
				} else {
					LOGI("Size mismatch for %s.\n", restore_sdext2->Backup_Name.c_str());
					// TODO: TWRP could repartition card in this case
					//	 but better just inform that sdext2 partition is too small for this backup to be restored
					restore_sdext = NULL;
					LOGE("Size of 'sdext2' partition is less than needed.\n");
					LOGE("Skipping 'sdext2' from restoring process.\n");
				}
				parts += "X";
			} else
				partition_count++;
		}
	}
#ifdef SP1_NAME
	DataManager::GetValue(TW_RESTORE_SP1_VAR, check);
	if (check > 0) {
		restore_sp1 = Find_Partition_By_Path(EXPAND(SP1_NAME));
		if (restore_sp1 == NULL)
			LOGE("Unable to locate %s partition.\n", EXPAND(SP1_NAME));
		else {
			// TODO: Size check for SP1
			partition_count++;
			parts += "1";
		}
	}
#endif
#ifdef SP2_NAME
	DataManager::GetValue(TW_RESTORE_SP2_VAR, check);
	if (check > 0) {
		restore_sp2 = Find_Partition_By_Path(EXPAND(SP2_NAME));
		if (restore_sp2 == NULL)
			LOGE("Unable to locate %s partition.\n", EXPAND(SP2_NAME));
		else {
			// TODO: Size check for SP2
			partition_count++;
			parts += "2";
		}
	}
#endif
#ifdef SP3_NAME
	DataManager::GetValue(TW_RESTORE_SP3_VAR, check);
	if (check > 0) {
		restore_sp3 = Find_Partition_By_Path(EXPAND(SP3_NAME));
		if (restore_sp3 == NULL)
			LOGE("Unable to locate %s partition.\n", EXPAND(SP3_NAME));
		else {
			// TODO: Size check for SP3
			partition_count++;
			parts += "3";
		}
	}
#endif
	
	if (hserr > 0 && DataManager::Detect_BLDR() == 1) {
		if (!data_size.empty()) {
			// data backup is to be restored, BUT no dalvik-cache was detected inside
			if (restore_data != NULL && !dalvik_found_on_data) {
				// Either sdext backup is to be restored, BUT no dalvik-cache was detected inside
				if((restore_sdext != NULL && !dalvik_found_on_sdext)
				/* or sdext2 backup is to be restored, BUT no dalvik-cache was detected inside	  */
				|| (restore_sdext2 != NULL && !dalvik_found_on_sdext2)) {
					// increase by 40% data's new size just to be safe
					data_size = " data:" + TWFunc::to_string((int)(1.4*dt_size/1048576LLU));
				}
			}
			part_size += data_size;
		}
		// create clkpartmgr's command
		Command = "clkpartmgr --restore=" + TWFunc::Get_Filename(Restore_Name); // folder's name
		if (check_md5 <= 0)
			parts += "M"; // arg for skipping md5 check
		if (!parts.empty())
			Command += (" --partitions=" + parts); // which parts to restore
		if (!part_size.empty()) {
			Command += part_size; // the part of command that holds which partitions are going to be resized
			LOGI("Command: %s\n", Command.c_str());
			// notify user
			ui_print("The backup you selected to restore, probably\n");
			ui_print("requires at least one larger partition.\n");
			ui_print("'clkpartmgr' will be executed in order to check\n");
			ui_print("if we need to enter cLK and adjust the PTABLE.\n");
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
		LOGE("No partitions selected for restore.\n");
		return false;
	}

	if (check_md5 > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		ui_print("Verifying MD5...\n");
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
		if (restore_sp1 != NULL && !restore_sp1->Check_MD5(Restore_Name))
			return false;
		if (restore_sp2 != NULL && !restore_sp2->Check_MD5(Restore_Name))
			return false;
		if (restore_sp3 != NULL && !restore_sp3->Check_MD5(Restore_Name))
			return false;
		ui_print("Done verifying MD5.\n");
	} else
		ui_print("Skipping MD5 check based on user setting.\n");

	ui_print("Restoring %i partitions...\n", partition_count);
	ui->SetProgress(0.0);
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
	if (restore_sp1 != NULL && !Restore_Partition(restore_sp1, Restore_Name, partition_count))
		return false;
	if (restore_sp2 != NULL && !Restore_Partition(restore_sp2, Restore_Name, partition_count))
		return false;
	if (restore_sp3 != NULL && !Restore_Partition(restore_sp3, Restore_Name, partition_count))
		return false;

	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	ui_print("[RESTORE COMPLETED IN %d SECONDS]\n\n",(int)difftime(rStop,rStart));
	// hserr will be negative if we are restoring via OpenRecoveryScript
	// reset to 1, the pre-restore selected value
	if (hserr < 0)
		DataManager::SetValue("tw_handle_restore_size", 1);
	return true;
}

void TWPartitionManager::Set_Restore_Files(string Restore_Name) {
	// Start with the default values
	int tw_restore_system = -1;
	int tw_restore_data = -1;
	int tw_restore_cache = -1;
	int tw_restore_recovery = -1;
	int tw_restore_boot = -1;
	int tw_restore_andsec = -1;
	int tw_restore_sdext = -1;
	int tw_restore_sdext2 = -1;
	int tw_restore_sp1 = -1;
	int tw_restore_sp2 = -1;
	int tw_restore_sp3 = -1;
	int tw_restore_is_dataonext = 0;
	bool get_date = true;
	bool split_archive = false;

	DIR* d;
	d = opendir(Restore_Name.c_str());
	if (d == NULL) {
		LOGE("Error opening '%s'\n", Restore_Name.c_str());
		return;
	}

	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
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
				LOGE("Unable to get required partition's info from '%s'.\n", de->d_name);
				continue;
			}
			extn = dname.substr((last_occur + 1), (dname.size() - last_occur - 1));
			
			if (extn.size() > 3) {
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
				LOGE(" Unable to locate partition by backup name: '%s'\n", label);
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

			LOGI("*extn = %s\n", extn.c_str());
			LOGI("*Backup_FileName = %s\n", Part->Backup_FileName.c_str());
			LOGI("*Backup_Path = %s\n", Part->Backup_Path.c_str());

			// Now, we just need to find the correct label
			if (Part->Backup_Path == "/boot") {
				// HD2's boot partition depends on which bootloader we have
				if((fstype == "mtd" && DataManager::Detect_BLDR() == 1/*cLK*/)
				|| (fstype == "yaffs2" && DataManager::Detect_BLDR() == 2/*MAGLDR*/))
					tw_restore_boot = 1;
				else {
					// We are trying to restore an incompatible backup
					// Set all values to -1 and break
					tw_restore_system = -1;
					tw_restore_data = -1;
					tw_restore_cache = -1;
					tw_restore_recovery = -1;
					tw_restore_boot = -1;
					tw_restore_andsec = -1;
					tw_restore_sdext = -1;
					tw_restore_sdext2 = -1;
					tw_restore_sp1 = -1;
					tw_restore_sp2 = -1;
					tw_restore_sp3 = -1;
					tw_restore_is_dataonext = 0;
					break;
				}
			}
			else if (Part->Backup_Path == "/system")
				tw_restore_system = 1;
			else if (Part->Backup_Path == "/data")
				tw_restore_data = 1;
			else if (Part->Backup_Path == "/cache")
				tw_restore_cache = 1;
			else if (Part->Backup_Path == "/recovery")
				tw_restore_recovery = 1;
			else if (Part->Backup_Path == "/and-sec")
				tw_restore_andsec = 1;
			else if (Part->Backup_Path == "/sd-ext")
				tw_restore_sdext = 1;
			else if (Part->Backup_Path == "/sdext2")
				tw_restore_sdext2 = 1;
#ifdef SP1_NAME
			if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP1_NAME)))
				tw_restore_sp1 = 1;
#endif
#ifdef SP2_NAME
			if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP2_NAME)))
				tw_restore_sp2 = 1;
#endif
#ifdef SP3_NAME
			if (Part->Backup_Path == TWFunc::Get_Root_Path(EXPAND(SP3_NAME)))
				tw_restore_sp3 = 1;
#endif
		}
	}
	closedir(d);

	// Set the final values
	DataManager::SetValue(TW_RESTORE_SYSTEM_VAR, tw_restore_system);
	DataManager::SetValue(TW_RESTORE_DATA_VAR, tw_restore_data);
	DataManager::SetValue(TW_RESTORE_CACHE_VAR, tw_restore_cache);
	DataManager::SetValue(TW_RESTORE_RECOVERY_VAR, tw_restore_recovery);
	DataManager::SetValue(TW_RESTORE_BOOT_VAR, tw_restore_boot);
	DataManager::SetValue(TW_RESTORE_ANDSEC_VAR, tw_restore_andsec);
	DataManager::SetValue(TW_RESTORE_SDEXT_VAR, tw_restore_sdext);
	DataManager::SetValue(TW_RESTORE_SDEXT2_VAR, tw_restore_sdext2);
	DataManager::SetValue(TW_RESTORE_SP1_VAR, tw_restore_sp1);
	DataManager::SetValue(TW_RESTORE_SP2_VAR, tw_restore_sp2);
	DataManager::SetValue(TW_RESTORE_SP3_VAR, tw_restore_sp3);
	DataManager::SetValue(TW_RESTORE_IS_DATAONEXT, tw_restore_is_dataonext);

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
		LOGE("Wipe: Unable to find partition for path '%s'\n", Local_Path.c_str());
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
	LOGE("Wipe: Unable to find partition for block '%s'\n", Block.c_str());
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
	LOGE("Wipe: Unable to find partition for name '%s'\n", Name.c_str());
	return false;
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

	ui_print("\nWiping Dalvik Cache Directories...\n");
	// cache
	TWPartition* Cache = Find_Partition_By_Path("/cache");
	if (Cache != NULL) {
		if (Cache->Dalvik_Cache_Size > 0) {
			if (Cache->Mount(true))
				TWFunc::removeDir("/cache/dalvik-cache", false);
			ui_print("Cleaned: /cache/dalvik-cache...\n");
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
						ui_print("Cleaned: DalvikOnNand...\n");
					}
				} else {
					TWFunc::removeDir("/data/dalvik-cache", false);
					ui_print("Cleaned: /data/dalvik-cache...\n");
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
					ui_print("Cleaned: %s...\n", dalvik_pth.c_str());
					SDext->Dalvik_Cache_Size = 0;
				}
			}
		}
	}
	ui_print("Dalvik Cache Directories Wipe Complete!\n\n");

	return true;
}

int TWPartitionManager::Wipe_Rotate_Data(void) {
	if (!Mount_By_Path("/data", true))
		return false;

	unlink("/data/misc/akmd*");
	unlink("/data/misc/rild*");
	ui_print("Rotation data wiped.\n");
	return true;
}

int TWPartitionManager::Wipe_Battery_Stats(void) {
	struct stat st;

	if (!Mount_By_Path("/data", true))
		return false;

	if (0 != stat("/data/system/batterystats.bin", &st)) {
		ui_print("No Battery Stats Found. No Need To Wipe.\n");
	} else {
		remove("/data/system/batterystats.bin");
		ui_print("Cleared battery stats.\n");
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
		LOGE("No android secure partitions found.\n");
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
		LOGE("Unable to locate /data.\n");
		return false;
	}
	return false;
}

int TWPartitionManager::Wipe_Media_From_Data(void) {
	TWPartition* dat = Find_Partition_By_Path("/data");

	if (dat != NULL) {
		if (!dat->Has_Data_Media) {
			LOGE("This device does not have /data/media\n");
			return false;
		}
		if (!dat->Mount(true))
			return false;

		ui_print("Wiping internal storage -- /data/media...\n");
		TWFunc::removeDir("/data/media", false);
		if (mkdir("/data/media", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0)
			return -1;
		if (dat->Has_Data_Media) {
			dat->Recreate_Media_Folder();
		}
		return true;
	} else {
		LOGE("Unable to locate /data.\n");
		return false;
	}
	return false;
}

void TWPartitionManager::Refresh_Sizes(void) {
	Update_System_Details();
	return;
}

void TWPartitionManager::Update_System_Details(void) {
	std::vector<TWPartition*>::iterator iter;
	int data_size = 0;

	ui_print("Updating partition details...\n");
	for (iter = Partitions.begin(); iter != Partitions.end(); iter++) {
		if ((*iter)->Can_Be_Mounted) {
			(*iter)->Update_Size(true);
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
					DataManager::SetValue(TW_HAS_ANDROID_SECURE, 0);
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
					DataManager::SetValue(TW_HAS_BOOT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_BOOT_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_BOOT_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/recovery") {
				int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				DataManager::SetValue(TW_BACKUP_RECOVERY_SIZE, backup_display_size);
				if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_RECOVERY_VAR, 0);
				} else
					DataManager::SetValue(TW_HAS_RECOVERY_PARTITION, 1);
			} else if ((*iter)->Mount_Point == "/data") {
				data_size += (int)((*iter)->Backup_Size / 1048576LLU);
			} else if ((*iter)->Mount_Point == "/sd-ext") {
				//int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				//DataManager::SetValue(TW_BACKUP_SDEXT_SIZE, backup_display_size);
				//if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_SDEXT_VAR, 0);
				//} else {
				//	DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1); 
				//}
			} else if ((*iter)->Mount_Point == "/sdext2") {
				//int backup_display_size = (int)((*iter)->Backup_Size / 1048576LLU);
				//DataManager::SetValue(TW_BACKUP_SDEXT2_SIZE, backup_display_size);
				//if ((*iter)->Backup_Size == 0) {
					DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
					DataManager::SetValue(TW_BACKUP_SDEXT2_VAR, 0);
				//} else {
				//	DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
				//}
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
						LOGE("Unable to mount internal storage.\n");
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
						LOGE("Unable to locate internal storage partition.\n");
						DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
					}
				}
			} else {
				// No dual storage and unable to mount storage, error!
				LOGE("Unable to mount storage.\n");
				DataManager::SetValue(TW_STORAGE_FREE_SIZE, 0);
			}
		} else {
			DataManager::SetValue(TW_STORAGE_FREE_SIZE, (int)(FreeStorage->Free / 1048576LLU));
		}
	} else {
		LOGI("Unable to find storage partition '%s'.\n", current_storage_path.c_str());
	}
	if (!Write_Fstab())
		LOGE("Error creating fstab\n");
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
		LOGE("Failed to decrypt data.\n");
		return -1;
	}

	if(efs)
		efs->UnMount(false);

	property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "error");
	if (strcmp(crypto_blkdev, "error") == 0) {
		LOGE("Error retrieving decrypted data block device.\n");
	} else {
		TWPartition* dat = Find_Partition_By_Path("/data");
		if (dat != NULL) {
			DataManager::SetValue(TW_DATA_BLK_DEVICE, dat->Primary_Block_Device);
			DataManager::SetValue(TW_IS_DECRYPTED, 1);
			dat->Is_Decrypted = true;
			dat->Decrypted_Block_Device = crypto_blkdev;
			dat->Setup_File_System(false);
			ui_print("Data successfully decrypted, new block device: '%s'\n", crypto_blkdev);

#ifdef CRYPTO_SD_FS_TYPE
			char crypto_blkdev_sd[255];
			property_get("ro.crypto.sd_fs_crypto_blkdev", crypto_blkdev_sd, "error");
			if (strcmp(crypto_blkdev_sd, "error") == 0) {
				LOGE("Error retrieving decrypted data block device.\n");
			} else if(TWPartition* emmc = Find_Partition_By_Path(EXPAND(TW_INTERNAL_STORAGE_PATH))){
				emmc->Is_Decrypted = true;
				emmc->Decrypted_Block_Device = crypto_blkdev_sd;
				emmc->Setup_File_System(false);
				ui_print("Internal SD successfully decrypted, new block device: '%s'\n", crypto_blkdev_sd);
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
			Update_System_Details();
			UnMount_Main_Partitions();
		} else
			LOGE("Unable to locate data partition.\n");
	}
	return 0;
#else
	LOGE("No crypto support was compiled into this build.\n");
	return -1;
#endif
	return 1;
}

int TWPartitionManager::Fix_Permissions(void) {
	int result = 0, sdext_mounted = 0;
	int dataonext, data_size = 0;
	Update_System_Details();

	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (SDext->Is_Present) {
			if (SDext->Mount(true))
				sdext_mounted == 1;
		}
	}
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
	ui_print("Done.\n\n");
	return result;
}

int TWPartitionManager::Open_Lun_File(string Partition_Path, string Lun_File) {
	int fd;
	TWPartition* Part = Find_Partition_By_Path(Partition_Path);

	if (Part == NULL) {
		LOGE("Unable to locate volume information for USB storage mode.");
		return false;
	}
	if (!Part->UnMount(true))
		return false;

	if ((fd = open(Lun_File.c_str(), O_WRONLY)) < 0) {
		LOGE("Unable to open ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
		return false;
	}

	if (write(fd, Part->Actual_Block_Device.c_str(), Part->Actual_Block_Device.size()) < 0) {
		LOGE("Unable to write to ums lunfile '%s': (%s)\n", Lun_File.c_str(), strerror(errno));
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

	DataManager::GetValue(TW_HAS_DUAL_STORAGE, has_dual);
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (has_dual == 1 && has_data_media == 0) {
		string Lun_File_str = CUSTOM_LUN_FILE;
		size_t found = Lun_File_str.find("%");
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
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		return Open_Lun_File(ext_path, lun_file);
	}
	return true;
}

int TWPartitionManager::usb_storage_disable(void) {
	int fd, index;
	char lun_file[255];

	for (index=0; index<2; index++) {
		sprintf(lun_file, CUSTOM_LUN_FILE, index);

		if ((fd = open(lun_file, O_WRONLY)) < 0) {
			Mount_All_Storage();
			Update_System_Details();
			if (index == 0) {
				LOGE("Unable to open ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		char ch = 0;
		if (write(fd, &ch, 1) < 0) {
			close(fd);
			Mount_All_Storage();
			Update_System_Details();
			if (index == 0) {
				LOGE("Unable to write to ums lunfile '%s': (%s)", lun_file, strerror(errno));
				return false;
			} else
				return true;
		}

		close(fd);
	}
	Mount_All_Storage();
	Update_System_Details();
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
	LOGI("Unmounting main partitions...\n");

	TWPartition* Boot_Partition = Find_Partition_By_Path("/boot");

	UnMount_By_Path("/system", true);
#ifndef RECOVERY_SDCARD_ON_DATA
	UnMount_By_Path("/data", true);
#endif
	if (Boot_Partition != NULL && Boot_Partition->Can_Be_Mounted)
		Boot_Partition->UnMount(true);
}

int TWPartitionManager::Check_SDCard(void) {	
	ui_print("Checking SD Card's partitions...\n");
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGE("Unable to locate SD Card.\n");
		return false;
	}
	if (!SDCard->UnMount(true))
		return false;

	string Command, sd_format;
	sd_format = SDCard->Current_File_System;
	if (sd_format == "vfat" && TWFunc::Path_Exists("/sbin/dosfsck")) {
			ui_print("Checking vfat filesystem @/sdcard...\n");	
			Command = "dosfsck -avw " + SDCard->Primary_Block_Device;
			LOGI("Command is: '%s'\n", Command.c_str());
			system(Command.c_str());
	}
#ifdef TW_INCLUDE_MKNTFS
	else if (sd_format == "ntfs" && TWFunc::Path_Exists("/sbin/ntfsck")) {
		ui_print("Checking ntfs filesystem @/sdcard...\n");
		Command = "ntfsck " + SDCard->Primary_Block_Device;
		LOGI("Command is: '%s'\n", Command.c_str());
		system(Command.c_str());
	} 
#endif
#ifdef TW_INCLUDE_EXFAT
	else if (sd_format == "exfat" && TWFunc::Path_Exists("/sbin/exfatfsck")) {
			ui_print("Checking vfat filesystem @/sdcard...\n");	
			Command = "exfatfsck -v " + SDCard->Primary_Block_Device;
			LOGI("Command is: '%s'\n", Command.c_str());
			system(Command.c_str());
	}
#endif
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (SDext->Size != 0 && SDext->Current_File_System != "swap") {
			if (!SDext->UnMount(true))
				return false;
			ui_print("Checking %s filesystem @/sd-ext...\n", SDext->Current_File_System.c_str());
			Command = "e2fsck -f -p -v " + SDext->Primary_Block_Device;
			LOGI("Command is: '%s'\n", Command.c_str());
			system(Command.c_str());
		}
	}
	
	TWPartition* SDext2 = Find_Partition_By_Path("/sdext2");
	if (SDext2 != NULL) {
		if (SDext2->Size != 0 && SDext2->Current_File_System != "swap") {
			if (!SDext2->UnMount(true))
				return false;
			ui_print("Checking %s filesystem @/sdext2...\n", SDext2->Current_File_System.c_str());
			Command = "e2fsck -f -p -v " + SDext2->Primary_Block_Device;
			LOGI("Command is: '%s'\n", Command.c_str());
			system(Command.c_str());
		}
	}
	SDCard->Mount(true);
	ui_print("SD Card's partition(s) check complete.\n");
	return true;
}

int TWPartitionManager::Partition_SDCard(void) {
	char mkdir_path[255], line[512];
	string Command, Device, fat_str, ext_str, ext2_str, swap_str, start_loc, end_loc, ext_format, ext2_format, sd_path, tmpdevice, n_mounts, sd_format;
	int ext, ext2, swap, total_size = 0, fat_size, c_var;
	FILE* fp;

	ui_print("Partitioning SD Card...\n");
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGE("Unable to locate device to partition.\n");
		return false;
	}
	if (!SDCard->UnMount(true))
		return false;
	TWPartition* SDext = Find_Partition_By_Path("/sd-ext");
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	}
	TWPartition* SDext2 = Find_Partition_By_Path("/sdext2");
	if (SDext2 != NULL) {
		if (!SDext2->UnMount(true))
			return false;
	}
	system("umount \"$SWAPPATH\"");
	Device = SDCard->Actual_Block_Device;
	// Just use the root block device
	Device.resize(strlen("/dev/block/mmcblkX"));

	// Find the size of the block device:
	fp = fopen("/proc/partitions", "rt");
	if (fp == NULL) {
		LOGE("Unable to open /proc/partitions\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		unsigned long major, minor, blocks;
		char device[512];
		char tmpString[64];

		if (strlen(line) < 7 || line[0] == 'm')	 continue;
		sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);

		tmpdevice = "/dev/block/";
		tmpdevice += device;
		if (tmpdevice == Device) {
			// Adjust block size to byte size
			total_size = (int)(blocks * 1024ULL  / 1000000LLU);
			break;
		}
	}
	fclose(fp);

	DataManager::GetValue("tw_num_of_mounts_for_fs_check", c_var);
	DataManager::GetValue("tw_sdext_size", ext);
	DataManager::GetValue("tw_sdext2_size", ext2);
	DataManager::GetValue("tw_swap_size", swap);
	DataManager::GetValue("tw_sdcard_file_system", sd_format);
	DataManager::GetValue("tw_sdpart_file_system", ext_format);
	DataManager::GetValue("tw_sdpart2_file_system", ext2_format);
	if (ext == 0)
		ext2 = 0;
	fat_size = total_size - ext - ext2 - swap;
	LOGI("sd card block device is '%s', sdcard size is: %iMB, fat size: %iMB, ext size: %iMB, ext system: '%s', 2nd ext size: %iMB, 2nd ext system: '%s', swap size: %iMB\n",
		Device.c_str(), total_size, fat_size, ext, ext_format.c_str(), ext2, ext2_format.c_str(), swap);
	n_mounts = TWFunc::to_string(c_var);
	fat_str = TWFunc::to_string(fat_size);
	ext_str = TWFunc::to_string(fat_size + ext);
	ext2_str = TWFunc::to_string(fat_size + ext + ext2);
	swap_str = TWFunc::to_string(fat_size + ext + ext2 + swap);
	if (ext + ext2 + swap > total_size) {
		LOGE("EXT + Swap size is larger than sdcard size.\n");
		return false;
	}
	ui_print("Removing partition table...\n");
	Command = "parted -s " + Device + " mklabel msdos";
	LOGI("Command is: '%s'\n", Command.c_str());
	if (system(Command.c_str()) != 0) {
		LOGE("Unable to remove partition table.\n");
		Update_System_Details();
		return false;
	}

	if (sd_format == "vfat") {	
		ui_print("Creating FAT32 partition...\n");
		Command = "parted " + Device + " mkpartfs primary fat32 0 " + fat_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create FAT32 partition.\n");
			return false;
		}	
	}
#ifdef TW_INCLUDE_MKNTFS
	else if (sd_format == "ntfs") {
		ui_print("Creating raw partition...\n");
		Command = "parted " + Device + " mkpart primary 0 " + fat_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create NTFS partition.\n");
			return false;
		}
	}  
#endif
#ifdef TW_INCLUDE_EXFAT
	else if (sd_format == "exfat") {
		ui_print("Creating raw partition...\n");
		Command = "parted " + Device + " mkpart primary 0 " + fat_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create EXFAT partition.\n");
			return false;
		}
	} 
#endif

	if (ext > 0) {
		ui_print("Creating EXT partition...\n");
		if (ext_format == "nilfs2") {
			Command = "parted " + Device + " mkpart primary " + fat_str + "MB " + ext_str + "MB";
		} else if (ext_format == "reiserfs") {
			Command = "parted " + Device + " mkpart primary " + fat_str + "MB " + ext_str + "MB";
		} else {
			Command = "parted " + Device + " mkpartfs primary ext2 " + fat_str + "MB " + ext_str + "MB";
		}
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create EXT partition.\n");
			Update_System_Details();
			return false;
		}
		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
	}
	if ((ext > 0) && (ext2 > 0)) {
		ui_print("Creating 2nd EXT partition...\n");
		if (ext2_format == "nilfs2") {
			Command = "parted " + Device + " mkpart primary " + ext_str + "MB " + ext2_str + "MB";
		} else if (ext2_format == "reiserfs") {
			Command = "parted " + Device + " mkpart primary " + ext_str + "MB " + ext2_str + "MB";
		} else {
			Command = "parted " + Device + " mkpartfs primary ext2 " + ext_str + "MB " + ext2_str + "MB";
		}
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create 2nd EXT partition.\n");
			Update_System_Details();
			return false;
		}
		DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 1);
	}
	if (swap > 0) {
		ui_print("Creating swap partition...\n");
		Command = "parted " + Device + " mkpartfs primary linux-swap " + ext2_str + "MB " + swap_str + "MB";
		LOGI("Command is: '%s'\n", Command.c_str());
		if (system(Command.c_str()) != 0) {
			LOGE("Unable to create swap partition.\n");
			Update_System_Details();
			return false;
		}
	}
#ifdef TW_INCLUDE_MKNTFS
	if (sd_format == "ntfs") {
		ui_print("Formatting /dev/block/mmcblk0p1 as NTFS...\n");
		Command = "mkntfs /dev/block/mmcblk0p1";
		LOGI("Command is: '%s'\n", Command.c_str());
		system(Command.c_str());
		SDCard->Change_FS_Type(sd_format);
		Command = "sdhandler.sh " + sd_format;
		//LOGI("Command is: '%s'\n", Command.c_str());
		system(Command.c_str());
	}
#endif
#ifdef TW_INCLUDE_EXFAT
	if (sd_format == "exfat") {
		ui_print("Formatting /dev/block/mmcblk0p1 as EXFAT...\n");
		Command = "mkexfatfs /dev/block/mmcblk0p1";
		LOGI("Command is: '%s'\n", Command.c_str());
		system(Command.c_str());
		SDCard->Change_FS_Type(sd_format);
		Command = "sdhandler.sh " + sd_format;
		//LOGI("Command is: '%s'\n", Command.c_str());
		system(Command.c_str());
	}
#endif
	// recreate TWRP folder and rewrite settings - these will be gone after sdcard is partitioned
#ifdef TW_EXTERNAL_STORAGE_PATH
	Mount_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH), 1);
	DataManager::GetValue(TW_EXTERNAL_PATH, sd_path);
	memset(mkdir_path, 0, sizeof(mkdir_path));
	sprintf(mkdir_path, "%s/TWRP", sd_path.c_str());
#else
	Mount_By_Path("/sdcard", 1);
	strcpy(mkdir_path, "/sdcard/TWRP");
#endif
	mkdir(mkdir_path, 0777);
	DataManager::SetupTwrpFolder();
	DataManager::Flush();
	SDCard->Recreate_AndSec_Folder();
#ifdef TW_EXTERNAL_STORAGE_PATH
	DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
	if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
		DataManager::SetValue(TW_ZIP_LOCATION_VAR, EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	DataManager::SetValue(TW_ZIP_EXTERNAL_VAR, "/sdcard");
	if (DataManager::GetIntValue(TW_USE_EXTERNAL_STORAGE) == 1)
		DataManager::SetValue(TW_ZIP_LOCATION_VAR, "/sdcard");
#endif
	if (ext > 0) {
		if (SDext == NULL) {
			LOGE("Unable to locate sd-ext partition.\n");
			return false;
		}
		if (ext_format == "nilfs2") {
			Command = "mkfs.nilfs2 " + SDext->Primary_Block_Device;
		} else if (ext_format == "reiserfs") {
			Command = "mkfs.reiser " + SDext->Primary_Block_Device;
		} else {
			Command = "mke2fs -t " + ext_format + " -m 0 " + SDext->Primary_Block_Device;
		}		
		ui_print("Formatting %s as %s...\n", SDext->Primary_Block_Device.c_str(), ext_format.c_str());
		SDext->Change_FS_Type(ext_format);
		LOGI("Formatting sd-ext after partitioning, command: '%s'\n", Command.c_str());
		system(Command.c_str());
		system(("tune2fs -c " + n_mounts + " " + SDext->Primary_Block_Device).c_str());
	}
	if ((ext > 0) && (ext2 > 0)) {
		if (ext2_format == "nilfs2") {
			Command = "mkfs.nilfs2 /dev/block/mmcblk0p3";// SDext2->Actual_Block_Device
		} else if (ext2_format == "reiserfs") {
			Command = "mkfs.reiser /dev/block/mmcblk0p3";// SDext2->Actual_Block_Device
		} else {
			Command = "mke2fs -t " + ext2_format + " -m 0 /dev/block/mmcblk0p3";// SDext2->Actual_Block_Device
		}		
		ui_print("Formatting /dev/block/mmcblk0p3 as %s...\n", ext2_format.c_str());
		SDext2->Change_FS_Type(ext2_format);
		LOGI("Formatting sdext2 after partitioning, command: '%s'\n", Command.c_str());
		system(Command.c_str());
		system(("tune2fs -c " + n_mounts + " /dev/block/mmcblk0p3").c_str());
	}

	if (ext == 0) {
    		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 0);
	} else {
    		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
		Command = "exthandler.sh " + ext_format;
		system(Command.c_str());
	}
	if (ext2 == 0) {
    		DataManager::SetValue(TW_HAS_SDEXT2_PARTITION, 0);
		Command = "exthandler2.sh";
		system(Command.c_str());
	} else {
    		DataManager::SetValue(TW_HAS_SDEXT_PARTITION, 1);
		Command = "exthandler2.sh " + ext2_format;
		system(Command.c_str());		
	}
	Update_System_Details();

	ui_print("Partitioning complete.\n");
	return true;
}

// Convert filesystem of EXT partition (ext2 - ext3 - ext4 - nilfs2)
int TWPartitionManager::FSConvert_SDEXT(string extpath) {
#ifdef TW_EXTERNAL_STORAGE_PATH
	TWPartition* SDCard = Find_Partition_By_Path(EXPAND(TW_EXTERNAL_STORAGE_PATH));
#else
	TWPartition* SDCard = Find_Partition_By_Path("/sdcard");
#endif
	if (SDCard == NULL) {
		LOGE("Unable to locate device's SD-card.\n");
		return false;
	}
	unsigned long long free_space = SDCard->Free;

	TWPartition* SDext = Find_Partition_By_Path(extpath);
	if (SDext != NULL) {
		if (!SDext->UnMount(true))
			return false;
	} else {
		LOGE("Unable to locate EXT partition.\n");
		return false;
	}
	unsigned long long ext_size = TWFunc::Get_Folder_Size(extpath, true);
	string Command, ext_format, n_mounts;
	int rescue_ext, c_var, restore_needed = 0;
	DataManager::GetValue("tw_rescue_ext_contents", rescue_ext);	
	DataManager::GetValue("tw_num_of_mounts_for_fs_check", c_var);
	DataManager::GetValue("tw_sdpart_file_system", ext_format);
	if (SDext->Current_File_System == ext_format) {
		ui_print("No need to change file system type.\n");
	} else {
		n_mounts = TWFunc::to_string(c_var);
		if (rescue_ext) {
			if (!Mount_Current_Storage(true))
				return false;
			LOGI("Checking sizes...\n");			
		    	if (free_space - (32 * 1024 * 1024) < ext_size) {
				LOGE("Not enough free space on storage.\n");
				return false;
			}
			if (!TWFunc::Recursive_Mkdir("/sdcard/TWRP/temp"))
				LOGE("Failed to make temp folder.\n");
			ui_print("Saving %s's contents on /sdcard...\n", extpath.c_str());
			Command = "busybox cp -a " + extpath + "/* /sdcard/TWRP/temp";
			if (system(Command.c_str()) == 0)
				restore_needed = 1;
		}
		if (ext_format == "nilfs2") {
			Command = "mkfs.nilfs2 " + SDext->Actual_Block_Device;
		} else if (ext_format == "reiserfs") {
			Command = "mkfs.reiser " + SDext->Actual_Block_Device;
		} else {
			/* if(SDext->Current_File_System == "ext2" && ext_format == "ext3") {
				Command = "tune2fs -j " + SDext->Actual_Block_Device;
			} else if(SDext->Current_File_System == "ext2" && ext_format == "ext4") {
				Command = "tune2fs -O extents,uninit_bg,dir_index,has_journal " + SDext->Actual_Block_Device;
			} else if(SDext->Current_File_System == "ext3" && ext_format == "ext4") {
				Command = "tune2fs -O extents,uninit_bg,dir_index " + SDext->Actual_Block_Device;
			} else */	
				Command = "mke2fs -t " + ext_format + " -m 0 " + SDext->Actual_Block_Device;
		}
		ui_print("Formatting %s as %s...\n", SDext->Actual_Block_Device.c_str(), ext_format.c_str());
		SDext->Change_FS_Type(ext_format);
		system(Command.c_str());
		system(("tune2fs -c " + n_mounts + " " + SDext->Actual_Block_Device).c_str());
		Update_System_Details();
		// Set the fs_type in /etc/recovery.fstab & /etc/fstab to the real type instead of "auto".
		if (extpath == "/sd-ext")
			Command = "exthandler.sh " + ext_format;
		else 
			Command = "exthandler2.sh " + ext_format;
		system(Command.c_str());

		if (restore_needed) {
			SDext->Mount(true);
			ui_print("Restoring %s's contents from /sdcard...\n", extpath.c_str());
			Command = "busybox cp -a /sdcard/TWRP/temp/* " + extpath;
			system(Command.c_str());
			system("rm -rf /sdcard/TWRP/temp");
		}
		ui_print("EXT formatting completed.\n");
		Update_System_Details();
	}
	return true;
}

int TWPartitionManager::NativeSD_Backup(string RomPath) {
	int z, inc_system, inc_data, inc_boot, do_md5;
	string extpath, Rom_Name, Backup_Folder, Backup_Name, Full_Backup_Path;
	unsigned long long total_bytes = 0, free_space = 0, remaining_bytes = 0;

	TWPartition* storage = NULL;

	struct tm *t;
	time_t seconds, rStart, rStop;
	time(&rStart);
    	t = localtime(&seconds);

	Update_System_Details();

	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";

	if (!Mount_Current_Storage(true))
		return false;

	TWPartition* sdext = Find_Partition_By_Path(extpath);
	if (!sdext->Mount(false))
		return false;
	
	char timestamp[255];
	sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
	Rom_Name = RomPath.substr(8, RomPath.size() - 1);
	Backup_Name = Rom_Name + "@" + timestamp;
	DataManager::GetValue(TW_SDBACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_SKIP_MD5_GENERATE_VAR, do_md5);
	if (do_md5 == 0)
		do_md5 = true;
	else
		do_md5 = false;
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_SYSTEM, inc_system);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_DATA, inc_data);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_BOOT, inc_boot);
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";

	ui_print("\n[NATIVESD BACKUP STARTED]\n");
    	ui_print(" * Backup Folder:\n   %s\n @ %s\n", Backup_Name.c_str(), Backup_Folder.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGE("Failed to make backup folder.\n");
		return false;
	}

	LOGI("Calculating backup details...\n");
	unsigned long long system_backup_size = 0;
	if (TWFunc::Path_Exists(RomPath + "/system"))
		system_backup_size = TWFunc::Get_Folder_Size(RomPath + "/system", true);
    	unsigned long long data_backup_size = 0;
	if (TWFunc::Path_Exists(RomPath + "/data"))
		data_backup_size = TWFunc::Get_Folder_Size(RomPath + "/data", true);
    	unsigned long long boot_backup_size = 0;
	if (TWFunc::Path_Exists("/sdcard/NativeSD/" + RomPath.substr(8, RomPath.size() - 1)))
		boot_backup_size = TWFunc::Get_Folder_Size("/sdcard/NativeSD/" + RomPath.substr(8, RomPath.size() - 1), true);

	total_bytes = system_backup_size + data_backup_size + boot_backup_size;
	remaining_bytes = total_bytes;
    	ui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		ui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
	} else {
		LOGE("Unable to locate storage device.\n");
		return false;
	}
	if (free_space - (32 * 1024 * 1024) < total_bytes) {
		// We require an extra 32MB just in case
		LOGE("Not enough free space on storage.\n");
		return false;
	}

	ui->SetProgress(0.0);

	char back_name[255], split_index[5];
	string Full_FileName, Command, Tar_Args = "", Tar_Excl = "";
	int backup_time, use_compression, index, backup_count, file_bps;
	unsigned long long total_bsize = 0, file_size;
	unsigned long total_time, remain_time, section_time, file_time = 1;
	float pos;

	DataManager::GetValue(TW_USE_COMPRESSION_VAR, use_compression);
	if (use_compression) {
		Tar_Args += "-czv";
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	} else {
		Tar_Args += "-cv";
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);
	}

	total_time = total_bytes / (unsigned long)file_bps;
	
	if (inc_system && system_backup_size > 0) {
		time_t start, stop;
		time(&start);
		ui_print("Backing up %s's system...\n", Rom_Name.c_str());
		string SYS_Backup_FileName = "system.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		ui->SetProgress(pos);
		section_time = system_backup_size / file_bps;
		pos = section_time / (float) total_time;
		ui->ShowProgress(pos, section_time);
		if (system_backup_size > MAX_ARCHIVE_SIZE) {
			// This backup needs to be split into multiple archives
			ui_print("Breaking backup file into multiple archives...\nGenerating file lists\n");
			sprintf(back_name, "%s/system", RomPath.c_str());
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
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
				Full_FileName = Full_Backup_Path + SYS_Backup_FileName + split_index;
				Command = "tar "+ Tar_Args + " -f '" + Full_FileName + "' -T /tmp/list/filelist" + split_index;
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
			Full_FileName = Full_Backup_Path + SYS_Backup_FileName;
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
			tar.setdir(extpath + "/" + Rom_Name + "/system");
			tar.setfn(Full_FileName);
			if (use_compression) {
				if (tar.createTarGZThread() != 0)
					return -1;
				string gzname = Full_FileName + ".gz";
				rename(gzname.c_str(), Full_FileName.c_str());
			} else {
				if (tar.createTarThread() != 0)
					return -1;
			}
#else
			Command = "cd " + extpath + " && tar " + Tar_Args + " -f '" + Full_FileName + "' " + Rom_Name + "/system";
			LOGI("Backup command: '%s'\n", Command.c_str());
			system(Command.c_str());
#endif
			if (TWFunc::Get_File_Size(Full_FileName) == 0) {
				LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
				return false;
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGI("System Backup time: %d\n", backup_time);
		remaining_bytes -= system_backup_size;
		file_time += backup_time;
		if (do_md5)
			Make_MD5(true, Full_Backup_Path, SYS_Backup_FileName);
	}
	if (inc_data && data_backup_size > 0) {
		time_t start, stop;
		time(&start);
		int skip_dalvik;
		DataManager::GetValue(TW_SKIP_DALVIK, skip_dalvik);
		if (skip_dalvik)
			Tar_Excl = " --exclude='dalvik-cache' --exclude='dalvik-cache/*'";
		ui_print("Backing up %s's data...\n", Rom_Name.c_str());
		string DATA_Backup_FileName = "data.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		ui->SetProgress(pos);
		section_time = data_backup_size / file_bps;
		pos = section_time / (float) total_time;
		ui->ShowProgress(pos, section_time);
		if (data_backup_size > MAX_ARCHIVE_SIZE) {
			// This backup needs to be split into multiple archives
			ui_print("Breaking backup file into multiple archives...\nGenerating file lists\n");
			sprintf(back_name, "%s/data", RomPath.c_str());
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
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
				Full_FileName = Full_Backup_Path + DATA_Backup_FileName + split_index;
				Command = "tar "+ Tar_Args + Tar_Excl + " -f '" + Full_FileName + "' -T /tmp/list/filelist" + split_index;
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
			Full_FileName = Full_Backup_Path + DATA_Backup_FileName;
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
			tar.setdir(extpath + "/" + Rom_Name + "/data");
			tar.setfn(Full_FileName);
			if (use_compression) {
				if (tar.createTarGZThread() != 0)
					return -1;
				string gzname = Full_FileName + ".gz";
				rename(gzname.c_str(), Full_FileName.c_str());
			} else {
				if (tar.createTarThread() != 0)
					return -1;
			}
#else
			Command = "cd " + extpath + " && tar " + Tar_Args + Tar_Excl + " -f '" + Full_FileName + "' " + Rom_Name + "/data";
			LOGI("Backup command: '%s'\n", Command.c_str());
			system(Command.c_str());
#endif
			if (TWFunc::Get_File_Size(Full_FileName) == 0) {
				LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
				return false;
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGI("Data Backup time: %d\n", backup_time);
		remaining_bytes -= data_backup_size;
		file_time += backup_time;
		if (do_md5)
			Make_MD5(true, Full_Backup_Path, DATA_Backup_FileName);
	}
	if (inc_boot && boot_backup_size > 0) {
		time_t start, stop;
		time(&start);
		ui_print("Backing up %s's boot...\n", Rom_Name.c_str());
		string BOOT_Backup_FileName = "boot.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		ui->SetProgress(pos);
		section_time = boot_backup_size / file_bps;
		pos = section_time / (float) total_time;
		ui->ShowProgress(pos, section_time);
		Full_FileName = Full_Backup_Path + BOOT_Backup_FileName;
#ifdef TW_INCLUDE_LIBTAR
		twrpTar tar;
		tar.setdir("/sdcard/NativeSD/" + Rom_Name);
		tar.setfn(Full_FileName);
		if (use_compression) {
			if (tar.createTarGZThread() != 0)
				return -1;
			string gzname = Full_FileName + ".gz";
			rename(gzname.c_str(), Full_FileName.c_str());
		} else {
			if (tar.createTarThread() != 0)
				return -1;
		}
#else
		Command = "cd /sdcard/NativeSD && tar " + Tar_Args + " -f '" + Full_FileName + "' " + Rom_Name;
		LOGI("Backup command: '%s'\n", Command.c_str());
		system(Command.c_str());
#endif
		if (TWFunc::Get_File_Size(Full_FileName) == 0) {
			LOGE("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
			return false;
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGI("Boot Backup time: %d\n", backup_time);
		remaining_bytes -= boot_backup_size;
		file_time += backup_time;
		if (do_md5)
			Make_MD5(true, Full_Backup_Path, BOOT_Backup_FileName);
	}
	
	// Average BPS
	if (file_time == 0)
		file_time = 1;
	file_bps = (int)total_bytes / (int)file_time;	

	int prev_file_bps;
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, prev_file_bps);
	else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, prev_file_bps);
	file_bps += (prev_file_bps * 4);
	file_bps /= 5;

	if (use_compression)
		DataManager::SetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::SetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	unsigned long long actual_backup_size = TWFunc::Get_Folder_Size(Full_Backup_Path, true);
    	actual_backup_size /= (1024LLU * 1024LLU);
	ui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	ui_print("[NATIVESD BACKUP COMPLETED in %d sec]\n\n",(int)difftime(rStop,rStart));
    	return true;
}

int TWPartitionManager::NativeSD_Restore(string RomPath) {
	int z, inc_system, inc_data, inc_boot, check_md5, check, partition_count = 0;
	size_t at;
	string extpath, dev_id, Full_Backup_Path, Rom_Restore_Path, Sys_Restore_Path, Data_Restore_Path, Boot_Restore_Path, Rom_Backup, Rom_Name, Backup_Folder;
	time_t rStart, rStop;
	time(&rStart);

	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	
	DataManager::GetValue(TW_SDBACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_SYSTEM, inc_system);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_DATA, inc_data);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_BOOT, inc_boot);
	Rom_Backup = TWFunc::Get_Filename(RomPath);
	Full_Backup_Path = RomPath;//Backup_Folder + "/" + Rom_Backup;
	// Parse backup filename to extract the rom's name
	at = Rom_Backup.find("@");
	if (at == string::npos) {
		LOGE("Unable to find Rom's name.\n");
	}
	Rom_Name = Rom_Backup.substr(0, at);
	Rom_Restore_Path = extpath + "/" + Rom_Name;
	Sys_Restore_Path = extpath + "/" + Rom_Name + "/system";
	Data_Restore_Path = extpath + "/" + Rom_Name + "/data";
	Boot_Restore_Path = "/sdcard/NativeSD/" + Rom_Name;
	
	ui_print("\n[NATIVESD RESTORE STARTED]\n\n");
	ui_print(" * Restore folder:\n   %s\n @ '%s'\n", Rom_Backup.c_str(), Backup_Folder.c_str());

	if (!Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SKIP_MD5_CHECK_VAR, check_md5);
	if (check_md5 > 0) {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		ui_print("Verifying MD5...\n");

		string Full_Filename;
		char split_filename[512];
		int index = 0;
		
		if (inc_system) {
			Full_Filename = RomPath + "/system.tar";
			if (!TWFunc::Path_Exists(Full_Filename)) {
				// This is a split archive, we presume
				sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
				while (index < 1000 && TWFunc::Path_Exists(split_filename)) {
					if (TWFunc::Check_MD5(split_filename) == 0) {
						LOGE("MD5 failed to match on '%s'.\n", split_filename);
					}
					index++;
					sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
				}
			} else {
				// Single file archive
				if (TWFunc::Check_MD5(Full_Filename) == 0)
					LOGE("MD5 failed to match on '%s'.\n", Full_Filename.c_str());
			}
		}
		if (inc_data) {
			Full_Filename = RomPath + "/data.tar";
			if (!TWFunc::Path_Exists(Full_Filename)) {
				// This is a split archive, we presume
				sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
				while (index < 1000 && TWFunc::Path_Exists(split_filename)) {
					if (TWFunc::Check_MD5(split_filename) == 0) {
						LOGE("MD5 failed to match on '%s'.\n", split_filename);
					}
					index++;
					sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
				}
			} else {
				// Single file archive
				if (TWFunc::Check_MD5(Full_Filename) == 0)
					LOGE("MD5 failed to match on '%s'.\n", Full_Filename.c_str());
			}
		}
		if (inc_system) {
			Full_Filename = RomPath + "/boot.tar";
			if (TWFunc::Path_Exists(Full_Filename)) {
				// Single file archive
				if (TWFunc::Check_MD5(Full_Filename) == 0)
					LOGE("MD5 failed to match on '%s'.\n", Full_Filename.c_str());
			}
		}

		ui_print("Done verifying MD5.\n");
	} else
		ui_print("Skipping MD5 check based on user setting.\n");

	string Full_FileName, Command;
	int index = 0;
	char split_index[5];
	
	Command = "cd " + extpath + " && busybox mkdir "+ Rom_Name;
	system(Command.c_str());
	if (inc_system) {
		ui_print(" * Restoring %s's system...\n", Rom_Name.c_str());
		if (TWFunc::Path_Exists(Sys_Restore_Path)) {			
			system(("rm -rf " + Sys_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/system.tar";
		if (!TWFunc::Path_Exists(Full_FileName)) {
			// Backup is multiple archives
			LOGI("Backup is multiple archives.\n");
			sprintf(split_index, "%03i", index);
			Full_FileName += split_index;
			while (TWFunc::Path_Exists(Full_FileName)) {
				ui_print("Restoring archive %i...\n", index + 1);
#ifdef TW_INCLUDE_LIBTAR
				twrpTar tar;
				tar.setdir("/");
				tar.setfn(Full_FileName);
				if (tar.extractTarThread() != 0)
					return false;
#else
				Command = "cd / && tar -xvf '" + Full_FileName + "'";
				LOGI("Restore command: '%s'\n", Command.c_str());
				system(Command.c_str());	
#endif
				index++;
				sprintf(split_index, "%03i", index);
				Full_FileName = Full_Backup_Path + "/system.tar" + split_index;
			}
			if (index == 0) {
				LOGE("Error locating restore file:\n'%s'\n", Full_FileName.c_str());
				return false;
			}
		} else {
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
			tar.setdir(extpath);
			tar.setfn(Full_FileName);
			if (tar.extractTarThread() != 0)
				return false;
#else
			Command = "cd " + extpath + " && tar -xvf '" + Full_FileName + "'";
			LOGI("Restore command: '%s'\n", Command.c_str());
			system(Command.c_str());	
#endif
		}
	}
	if (inc_data) {
		ui_print(" * Restoring %s's data...\n", Rom_Name.c_str());
		if (TWFunc::Path_Exists(Data_Restore_Path)) {			
			system(("rm -rf " + Data_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/data.tar";
		if (!TWFunc::Path_Exists(Full_FileName)) {
			// Backup is multiple archives
			index = 0;
			LOGI("Backup is multiple archives.\n");
			sprintf(split_index, "%03i", index);
			Full_FileName += split_index;
			while (TWFunc::Path_Exists(Full_FileName)) {
				ui_print("Restoring archive %i...\n", index + 1);
#ifdef TW_INCLUDE_LIBTAR
				twrpTar tar;
				tar.setdir("/");
				tar.setfn(Full_FileName);
				if (tar.extractTarThread() != 0)
					return false;
#else
				Command = "cd / && tar -xvf '" + Full_FileName + "'";
				LOGI("Restore command: '%s'\n", Command.c_str());
				system(Command.c_str());	
#endif
				index++;
				sprintf(split_index, "%03i", index);
				Full_FileName = Full_Backup_Path + "/data.tar" + split_index;
			}
			if (index == 0) {
				LOGE("Error locating restore file:\n'%s'\n", Full_FileName.c_str());
				return false;
			}
		} else {
#ifdef TW_INCLUDE_LIBTAR
			twrpTar tar;
			tar.setdir(extpath);
			tar.setfn(Full_FileName);
			if (tar.extractTarThread() != 0)
				return false;
#else
			Command = "cd " + extpath + " && tar -xvf '" + Full_FileName + "'";
			LOGI("Restore command: '%s'\n", Command.c_str());
			system(Command.c_str());	
#endif
		}
	}
	if (inc_boot) {
		ui_print(" * Restoring %s's boot...\n", Rom_Name.c_str());
		if (!TWFunc::Path_Exists(Boot_Restore_Path)) {
			if (!TWFunc::Recursive_Mkdir(Boot_Restore_Path)) {
				LOGE("Failed to make boot folder.\n");
			}
		} else {
			system(("rm -rf " + Boot_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/boot.tar";
#ifdef TW_INCLUDE_LIBTAR
		twrpTar tar;
		tar.setdir(Boot_Restore_Path);
		tar.setfn(Full_FileName);
		if (tar.extractTarThread() != 0)
			return false;
#else
		Command = "cd " + Boot_Restore_Path + " && tar -xvf '" + Full_FileName + "'";
		LOGI("Restore command: '%s'\n", Command.c_str());
		system(Command.c_str());	
#endif
#ifdef TW_INCLUDE_LIBTAR
		twrpTar tar;
		tar.setdir("/sdcard/NativeSD");
		tar.setfn(Full_FileName);
		if (tar.extractTarThread() != 0)
			return false;
#else
		Command = "cd /sdcard/NativeSD && tar -xvf '" + Full_FileName + "'";
		LOGI("Restore command: '%s'\n", Command.c_str());
		system(Command.c_str());	
#endif
	}
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	Update_System_Details();
	UnMount_Main_Partitions();
	time(&rStop);
	ui_print("[NATIVESD RESTORE COMPLETED IN %d sec]\n\n",(int)difftime(rStop,rStart));
	return true;
}

int TWPartitionManager::NativeSD_Delete(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat(RomPath.c_str(), &st) == 0) {
                		if (system(("rm -rf " + RomPath).c_str()) == 0) {
					system(("rm -rf /sdcard/NativeSD/" + RomPath.substr(8, RomPath.size() - 1)).c_str());
        	    			system("rm -f /sdcard/NativeSD/initrd.gz");
        	    			system("rm -f /sdcard/NativeSD/zImage");
        	    			ui_print("Selected NativeSD Rom deleted.\n");
					return true;
				}
    	    		}
        	}
	}
	
	return false;
}

int TWPartitionManager::NativeSD_Kernel(string ptn, string RomPath) {
	string Rom_Name, mkbootimg, eraseimg, flashimg, clean;
	Rom_Name = RomPath.substr(17, RomPath.size() - 1);

	ui_print("\n[NATIVESD KERNEL RESTORE STARTED]\n\n");
	ui_print("Building img-file...\n");
	mkbootimg = "mkbootimg --kernel " + RomPath + "/zImage --ramdisk " + RomPath + "/initrd.gz --cmdline \"rel_path=\"" + Rom_Name + " --base 0x11800000 --output /sdcard/" + ptn + ".img";
	if (system(mkbootimg.c_str()) != 0)
		return false;
	eraseimg = "erase_image " + ptn;
	if (system(eraseimg.c_str()) != 0)
		return false;
	ui_print("Flashing img-file...\n");
	flashimg = "flash_image " + ptn + " /sdcard/" + ptn + ".img";
	if (system(flashimg.c_str()) != 0)
		return false;
	clean = "rm -f /sdcard/" + ptn + ".img";
	system(clean.c_str());
	ui_print("\n[NATIVESD KERNEL RESTORE COMPLETED]\n\n");
	
	return true;
}

int TWPartitionManager::NativeSD_Perm(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (TWFunc::Path_Exists(RomPath)) {
			ui_print("\n[NATIVESD PERM-FIX STARTED]\n\n");
			if (Is_Mounted_By_Path("/system"))
				UnMount_By_Path("/system", false);
			sleep(2);
			if (Is_Mounted_By_Path("/data"))
				UnMount_By_Path("/data", false);
			sleep(2);
			system(("busybox mount -o bind " + RomPath + "/system /system").c_str());
			system(("busybox mount -o bind " + RomPath + "/data /data").c_str());
			sleep(2);
			fixPermissions perms;
			perms.fixPerms(true, true);
			sleep(2);
			system("umount /system");
			sleep(2);
			system("umount /data");
			ui_print("\n[NATIVESD PERM-FIX COMPLETED]\n\n");
			return true;
		}
	}

	return false;
}

int TWPartitionManager::NativeSD_WipeData(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat((RomPath + "/data").c_str(), &st) == 0) {
				system(("rm -rf " + RomPath + "/data/*").c_str());
				system(("rm -rf " + RomPath + "/data/.*").c_str());
                		ui_print("NativeSD Rom's data wiped.\n");
				return true;
    	    		}
        	}
	}
	
	return false;
}

int TWPartitionManager::NativeSD_WipeDalvik(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat((RomPath + "/data/dalvik-cache").c_str(), &st) == 0) {
                		if (system(("rm -rf " + RomPath + "/data/dalvik-cache/*").c_str()) == 0) {
        	    			ui_print("NativeSD Rom's dalvik-cache wiped.\n");
					return true;
				}
    	    		}
        	}
	}
	
	return false;
}

