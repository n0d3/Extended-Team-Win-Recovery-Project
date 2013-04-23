/*
 * Kokotas: WIP... nativeSDmanager.cpp
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
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
#include "partitions.hpp"
#include "data.hpp"
#include "twrp-functions.hpp"
#include "fixPermissions.hpp"
#include "twrpDigest.hpp"
#include "nativeSDmanager.hpp"
#include "twrpTar.hpp"

int TWNativeSDManager::Backup(string RomPath) {
	int z, inc_system, inc_data, inc_boot, do_md5, skip_md5_gen;
	string extpath, Rom_Name, Backup_Folder, Backup_Name, Full_Backup_Path;
	unsigned long long total_bytes = 0, free_space = 0, remaining_bytes = 0;

	TWPartition* storage = NULL;

	struct tm *t;
	time_t seconds, rStart, rStop;
	time(&rStart);
    	t = localtime(&seconds);

	PartitionManager.Update_System_Details(false);

	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";

	if (!PartitionManager.Mount_Current_Storage(true))
		return false;

	TWPartition* sdext = PartitionManager.Find_Partition_By_Path(extpath);
	if (!sdext->Mount(false))
		return false;
	
	char timestamp[255];
	sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);
	Rom_Name = RomPath.substr(8, RomPath.size() - 1);
	Backup_Name = Rom_Name + "@" + timestamp;
	DataManager::GetValue(TW_SDBACKUPS_FOLDER_VAR, Backup_Folder);
	DataManager::GetValue(TW_SD_SKIP_MD5_GENERATE_VAR, skip_md5_gen);
	if (skip_md5_gen > 0)
		do_md5 = false;
	else
		do_md5 = true;
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_SYSTEM, inc_system);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_DATA, inc_data);
	DataManager::GetValue(TW_SD_BACKUP_RESTORE_BOOT, inc_boot);
	Full_Backup_Path = Backup_Folder + "/" + Backup_Name + "/";

	gui_print("\n[NATIVESD BACKUP STARTED]\n");
    	gui_print(" * Backup Folder:\n   %s\n @ %s\n", Backup_Name.c_str(), Backup_Folder.c_str());
	if (!TWFunc::Recursive_Mkdir(Full_Backup_Path)) {
		LOGERR("Failed to make backup folder.\n");
		return false;
	}

	LOGINFO("Calculating backup details...\n");
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
    	gui_print(" * Total size of all data: %lluMB\n", total_bytes / 1024 / 1024);
	storage = PartitionManager.Find_Partition_By_Path(DataManager::GetCurrentStoragePath());
	if (storage != NULL) {
		free_space = storage->Free;
		gui_print(" * Available space: %lluMB\n", free_space / 1024 / 1024);
	} else {
		LOGERR("Unable to locate storage device.\n");
		return false;
	}
	if (free_space - (32 * 1024 * 1024) < total_bytes) {
		// We require an extra 32MB just in case
		LOGERR("Not enough free space on storage.\n");
		return false;
	}

	DataManager::SetProgress(0.0);

	char back_name[255], split_index[5];
	string Full_FileName, Command, Tar_Excl = "";
	int backup_time, use_compression, index, backup_count;
	unsigned long long total_bsize = 0, file_size, file_bps;
	unsigned long total_time, remain_time, section_time, file_time = 1;
	float pos;

	DataManager::GetValue(TW_SD_USE_COMPRESSION_VAR, use_compression);
	if (use_compression)
		DataManager::GetValue(TW_BACKUP_AVG_FILE_COMP_RATE, file_bps);
	else
		DataManager::GetValue(TW_BACKUP_AVG_FILE_RATE, file_bps);

	total_time = total_bytes / (unsigned long)file_bps;
	
	if (inc_system && system_backup_size > 0) {
		time_t start, stop;
		time(&start);
		gui_print("Backing up %s's system...\n", Rom_Name.c_str());
		string SYS_Backup_FileName = "system.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		DataManager::SetProgress(pos);
		section_time = system_backup_size / file_bps;
		pos = section_time / (float) total_time;
		DataManager::ShowProgress(pos, section_time);
		if (system_backup_size > MAX_ARCHIVE_SIZE) {
			// This backup needs to be split into multiple archives
			gui_print("Breaking backup file into multiple archives...\n");
			sprintf(back_name, "%s/system", RomPath.c_str());
			twrpTar tar;
			tar.setexcl("");
			tar.setdir(back_name);
			tar.setfn(Full_FileName);
			backup_count = tar.splitArchiveFork();
			if (backup_count == -1) {
				LOGERR("Error tarring split files!\n");
				return false;
			}
		} else {
			Full_FileName = Full_Backup_Path + SYS_Backup_FileName;
			twrpTar tar;
			tar.setexcl("");
			tar.setdir(extpath + "/" + Rom_Name + "/system");
			tar.setfn(Full_FileName);
			if (use_compression) {
				if (tar.createTarGZFork() != 0)
					return -1;
				string gzname = Full_FileName + ".gz";
				rename(gzname.c_str(), Full_FileName.c_str());
			} else {
				if (tar.createTarFork() != 0)
					return -1;
			}
			if (TWFunc::Get_File_Size(Full_FileName) == 0) {
				LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
				return false;
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGINFO("System Backup time: %d\n", backup_time);
		remaining_bytes -= system_backup_size;
		file_time += backup_time;
		if (do_md5)
			PartitionManager.Make_MD5(true, Full_Backup_Path, SYS_Backup_FileName);
	}
	if (inc_data && data_backup_size > 0) {
		time_t start, stop;
		time(&start);
		int skip_dalvik;
		DataManager::GetValue(TW_SD_SKIP_DALVIK, skip_dalvik);
		if (skip_dalvik)
			Tar_Excl = "dalvik-cache";
		gui_print("Backing up %s's data...\n", Rom_Name.c_str());
		string DATA_Backup_FileName = "data.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		DataManager::SetProgress(pos);
		section_time = data_backup_size / file_bps;
		pos = section_time / (float) total_time;
		DataManager::ShowProgress(pos, section_time);
		if (data_backup_size > MAX_ARCHIVE_SIZE) {
			// This backup needs to be split into multiple archives
			gui_print("Breaking backup file into multiple archives...\n");
			sprintf(back_name, "%s/data", RomPath.c_str());
			twrpTar tar;
			tar.setexcl(Tar_Excl);
			tar.setdir(back_name);
			tar.setfn(Full_FileName);
			backup_count = tar.splitArchiveFork();
			if (backup_count == -1) {
				LOGERR("Error tarring split files!\n");
				return false;
			}
		} else {
			Full_FileName = Full_Backup_Path + DATA_Backup_FileName;
			twrpTar tar;
			tar.setexcl(Tar_Excl);
			tar.setdir(extpath + "/" + Rom_Name + "/data");
			tar.setfn(Full_FileName);
			if (use_compression) {
				if (tar.createTarGZFork() != 0)
					return -1;
				string gzname = Full_FileName + ".gz";
				rename(gzname.c_str(), Full_FileName.c_str());
			} else {
				if (tar.createTarFork() != 0)
					return -1;
			}
			if (TWFunc::Get_File_Size(Full_FileName) == 0) {
				LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
				return false;
			}
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGINFO("Data Backup time: %d\n", backup_time);
		remaining_bytes -= data_backup_size;
		file_time += backup_time;
		if (do_md5)
			PartitionManager.Make_MD5(true, Full_Backup_Path, DATA_Backup_FileName);
	}
	if (inc_boot && boot_backup_size > 0) {
		time_t start, stop;
		time(&start);
		gui_print("Backing up %s's boot...\n", Rom_Name.c_str());
		string BOOT_Backup_FileName = "boot.tar";
		remain_time = remaining_bytes / (unsigned long)file_bps;
		pos = (total_time - remain_time) / (float) total_time;
		DataManager::SetProgress(pos);
		section_time = boot_backup_size / file_bps;
		pos = section_time / (float) total_time;
		DataManager::ShowProgress(pos, section_time);
		Full_FileName = Full_Backup_Path + BOOT_Backup_FileName;
		twrpTar tar;
		tar.setexcl("");
		tar.setdir("/sdcard/NativeSD/" + Rom_Name);
		tar.setfn(Full_FileName);
		if (use_compression) {
			if (tar.createTarGZFork() != 0)
				return -1;
			string gzname = Full_FileName + ".gz";
			rename(gzname.c_str(), Full_FileName.c_str());
		} else {
			if (tar.createTarFork() != 0)
				return -1;
		}
		if (TWFunc::Get_File_Size(Full_FileName) == 0) {
			LOGERR("Backup file size for '%s' is 0 bytes.\n", Full_FileName.c_str());
			return false;
		}
		time(&stop);
		backup_time = (int) difftime(stop, start);
		LOGINFO("Boot Backup time: %d\n", backup_time);
		remaining_bytes -= boot_backup_size;
		file_time += backup_time;
		if (do_md5)
			PartitionManager.Make_MD5(true, Full_Backup_Path, BOOT_Backup_FileName);
	}
	
	// Average BPS
	if (file_time == 0)
		file_time = 1;
	file_bps = total_bytes / (int)file_time;	

	unsigned long long prev_file_bps;
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
	gui_print("[%llu MB TOTAL BACKED UP]\n", actual_backup_size);
	PartitionManager.Update_System_Details(true);
	PartitionManager.UnMount_Main_Partitions();
	time(&rStop);
	gui_print("[NATIVESD BACKUP COMPLETED in %d sec]\n\n",(int)difftime(rStop,rStart));
    	return true;
}

int TWNativeSDManager::Restore(string RomPath) {
	int z, inc_system, inc_data, inc_boot, skip_check_md5, check, partition_count = 0;
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
		LOGERR("Unable to find Rom's name.\n");
	}
	Rom_Name = Rom_Backup.substr(0, at);
	Rom_Restore_Path = extpath + "/" + Rom_Name;
	Sys_Restore_Path = extpath + "/" + Rom_Name + "/system";
	Data_Restore_Path = extpath + "/" + Rom_Name + "/data";
	Boot_Restore_Path = "/sdcard/NativeSD/" + Rom_Name;
	
	gui_print("\n[NATIVESD RESTORE STARTED]\n\n");
	gui_print(" * Restore folder:\n   %s\n @ '%s'\n", Rom_Backup.c_str(), Backup_Folder.c_str());

	if (!PartitionManager.Mount_Current_Storage(true))
		return false;

	DataManager::GetValue(TW_SD_SKIP_MD5_CHECK_VAR, skip_check_md5);
	if (skip_check_md5 > 0)
		gui_print("Skipping MD5 check based on user setting.\n");
	else {
		// Check MD5 files first before restoring to ensure that all of them match before starting a restore
		TWFunc::GUI_Operation_Text(TW_VERIFY_MD5_TEXT, "Verifying MD5");
		gui_print("Verifying MD5...\n");

		string md5file, Full_Filename;
		char split_filename[512];
		int index = 0;
		twrpDigest md5sum;
		
		if (inc_system) {
			Full_Filename = RomPath + "/system.tar";
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
					LOGERR("MD5 failed to match on '%s'.\n", split_filename);
					return false;
				} else
					return true;
			}
		}
		if (inc_data) {
			Full_Filename = RomPath + "/data.tar";
			if (!TWFunc::Path_Exists(Full_Filename)) {
				// This is a split archive, we presume
				sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
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
					LOGERR("MD5 failed to match on '%s'.\n", split_filename);
					return false;
				} else
					return true;
			}
		}
		if (inc_system) {
			Full_Filename = RomPath + "/boot.tar";
			if (TWFunc::Path_Exists(Full_Filename)) {
				// Single file archive
				md5file = Full_Filename + ".md5";
				if (!TWFunc::Path_Exists(md5file)) {
					LOGERR("No md5 file found for '%s'.\n", Full_Filename.c_str());
					LOGERR("Please select 'Skip MD5 verification' to restore.\n");
					return false;
				}
				md5sum.setfn(Full_Filename);
				if (md5sum.verify_md5digest() != 0) {
					LOGERR("MD5 failed to match on '%s'.\n", split_filename);
					return false;
				} else
					return true;
			}
		}

		gui_print("Done verifying MD5.\n");
	}

	string Full_FileName, Command;
	int index = 0;
	char split_index[5];
	
	Command = "cd " + extpath + " && busybox mkdir "+ Rom_Name;
	system(Command.c_str());
	if (inc_system) {
		gui_print(" * Restoring %s's system...\n", Rom_Name.c_str());
		if (TWFunc::Path_Exists(Sys_Restore_Path)) {			
			system(("rm -rf " + Sys_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/system.tar";
		if (!TWFunc::Path_Exists(Full_FileName)) {
			// Backup is multiple archives
			LOGINFO("Backup is multiple archives.\n");
			sprintf(split_index, "%03i", index);
			Full_FileName += split_index;
			while (TWFunc::Path_Exists(Full_FileName)) {
				gui_print("Restoring archive %i...\n", index + 1);
				twrpTar tar;
				tar.setdir("/");
				tar.setfn(Full_FileName);
				if (tar.extractTarFork() != 0)
					return false;
				index++;
				sprintf(split_index, "%03i", index);
				Full_FileName = Full_Backup_Path + "/system.tar" + split_index;
			}
			if (index == 0) {
				LOGERR("Error locating restore file:\n'%s'\n", Full_FileName.c_str());
				return false;
			}
		} else {
			twrpTar tar;
			tar.setdir(extpath);
			tar.setfn(Full_FileName);
			if (tar.extractTarFork() != 0)
				return false;
		}
	}
	if (inc_data) {
		gui_print(" * Restoring %s's data...\n", Rom_Name.c_str());
		if (TWFunc::Path_Exists(Data_Restore_Path)) {			
			system(("rm -rf " + Data_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/data.tar";
		if (!TWFunc::Path_Exists(Full_FileName)) {
			// Backup is multiple archives
			index = 0;
			LOGINFO("Backup is multiple archives.\n");
			sprintf(split_index, "%03i", index);
			Full_FileName += split_index;
			while (TWFunc::Path_Exists(Full_FileName)) {
				gui_print("Restoring archive %i...\n", index + 1);
				twrpTar tar;
				tar.setdir("/");
				tar.setfn(Full_FileName);
				if (tar.extractTarFork() != 0)
					return false;
				index++;
				sprintf(split_index, "%03i", index);
				Full_FileName = Full_Backup_Path + "/data.tar" + split_index;
			}
			if (index == 0) {
				LOGERR("Error locating restore file:\n'%s'\n", Full_FileName.c_str());
				return false;
			}
		} else {
			twrpTar tar;
			tar.setdir(extpath);
			tar.setfn(Full_FileName);
			if (tar.extractTarFork() != 0)
				return false;
		}
	}
	if (inc_boot) {
		gui_print(" * Restoring %s's boot...\n", Rom_Name.c_str());
		if (!TWFunc::Path_Exists(Boot_Restore_Path)) {
			if (!TWFunc::Recursive_Mkdir(Boot_Restore_Path)) {
				LOGERR("Failed to make boot folder.\n");
			}
		} else {
			system(("rm -rf " + Boot_Restore_Path).c_str());
		}
		Full_FileName = Full_Backup_Path + "/boot.tar";
		twrpTar tar;
		tar.setdir(Boot_Restore_Path);
		tar.setfn(Full_FileName);
		if (tar.extractTarFork() != 0)
			return false;
		tar.setdir("/sdcard/NativeSD");
		tar.setfn(Full_FileName);
		if (tar.extractTarFork() != 0)
			return false;
	}
	TWFunc::GUI_Operation_Text(TW_UPDATE_SYSTEM_DETAILS_TEXT, "Updating System Details");
	PartitionManager.Update_System_Details(true);
	PartitionManager.UnMount_Main_Partitions();
	time(&rStop);
	gui_print("[NATIVESD RESTORE COMPLETED IN %d sec]\n\n",(int)difftime(rStop,rStart));
	return true;
}

int TWNativeSDManager::Prep_Rom_To_Boot(string RomPath) {
	string Command, result;

	Command = "cp -f " + RomPath + "/zImage /sdcard/NativeSD/zImage";
	if (TWFunc::Exec_Cmd(Command, result) != 0)
		return false;
	Command = "cp -f " + RomPath + "/initrd.gz /sdcard/NativeSD/initrd.gz";
	if (TWFunc::Exec_Cmd(Command, result) != 0)
		return false;

	return true;
}

int TWNativeSDManager::Delete(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = PartitionManager.Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat(RomPath.c_str(), &st) == 0) {
                		if (system(("rm -rf " + RomPath).c_str()) == 0) {
					system(("rm -rf /sdcard/NativeSD/" + RomPath.substr(8, RomPath.size() - 1)).c_str());
        	    			system("rm -f /sdcard/NativeSD/initrd.gz");
        	    			system("rm -f /sdcard/NativeSD/zImage");
        	    			gui_print("Selected NativeSD Rom deleted.\n");
					return true;
				}
    	    		}
        	}
	}
	
	return false;
}

int TWNativeSDManager::Kernel_Update(string ptn, string RomPath) {
	string Rom_Name, mkbootimg, eraseimg, flashimg, clean;
	Rom_Name = RomPath.substr(17, RomPath.size() - 1);

	gui_print("\n[NATIVESD KERNEL RESTORE STARTED]\n\n");
	gui_print("Building img-file...\n");
	mkbootimg = "mkbootimg --kernel " + RomPath + "/zImage --ramdisk " + RomPath + "/initrd.gz --cmdline \"rel_path=\"" + Rom_Name + " --base 0x11800000 --output /sdcard/" + ptn + ".img";
	if (system(mkbootimg.c_str()) != 0)
		return false;
	eraseimg = "erase_image " + ptn;
	if (system(eraseimg.c_str()) != 0)
		return false;
	gui_print("Flashing img-file...\n");
	flashimg = "flash_image " + ptn + " /sdcard/" + ptn + ".img";
	if (system(flashimg.c_str()) != 0)
		return false;
	clean = "rm -f /sdcard/" + ptn + ".img";
	system(clean.c_str());
	gui_print("\n[NATIVESD KERNEL RESTORE COMPLETED]\n\n");
	
	return true;
}

int TWNativeSDManager::Fix_Perm(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = PartitionManager.Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (TWFunc::Path_Exists(RomPath)) {
			gui_print("\n[NATIVESD PERM-FIX STARTED]\n\n");
			if (PartitionManager.Is_Mounted_By_Path("/system"))
				PartitionManager.UnMount_By_Path("/system", false);
			sleep(2);
			if (PartitionManager.Is_Mounted_By_Path("/data"))
				PartitionManager.UnMount_By_Path("/data", false);
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
			gui_print("\n[NATIVESD PERM-FIX COMPLETED]\n\n");
			return true;
		}
	}

	return false;
}

int TWNativeSDManager::Wipe_Data(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = PartitionManager.Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat((RomPath + "/data").c_str(), &st) == 0) {
				system(("rm -rf " + RomPath + "/data/*").c_str());
				system(("rm -rf " + RomPath + "/data/.*").c_str());
                		gui_print("NativeSD Rom's data wiped.\n");
				return true;
    	    		}
        	}
	}
	
	return false;
}

int TWNativeSDManager::Wipe_Dalvik(string RomPath) {
	struct stat st;
	int z;
	string extpath;
	DataManager::GetValue(TW_USE_SDEXT2_PARTITION, z);
	if (z == 0)
		extpath = "/sd-ext";
	else
		extpath = "/sdext2";
	TWPartition* sdext = PartitionManager.Find_Partition_By_Path(extpath);
	if (sdext != NULL) {
		if (sdext->Is_Present && sdext->Mount(false)) {
			if (stat((RomPath + "/data/dalvik-cache").c_str(), &st) == 0) {
                		if (system(("rm -rf " + RomPath + "/data/dalvik-cache/*").c_str()) == 0) {
        	    			gui_print("NativeSD Rom's dalvik-cache wiped.\n");
					return true;
				}
    	    		}
        	}
	}
	
	return false;
}

