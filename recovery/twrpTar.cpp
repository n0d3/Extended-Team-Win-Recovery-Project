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

extern "C" {
	#include "libtar/libtar.h"
	#include "twrpTar.h"
	#include "tarWrite.h"
	#include "libcrecovery/common.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <dirent.h>
#include <sys/mman.h>
#include "twrpTar.hpp"
#include "twcommon.h"
#include "data.hpp"
#include "variables.h"
#include "twrp-functions.hpp"

using namespace std;

twrpTar::twrpTar(void) {
	use_encryption = 0;
	userdata_encryption = 0;
	use_compression = 0;
	split_archives = 0;
	has_data_media = 0;
	pigz_pid = 0;
	oaes_pid = 0;
}

twrpTar::~twrpTar(void) {
	// Do nothing
}

void twrpTar::setfn(string fn) {
	tarfn = fn;
}

void twrpTar::setdir(string dir) {
	tardir = dir;
}

void twrpTar::setexcl(string exclude) {
	tarexclude = exclude;
}

int twrpTar::createTarFork() {
	int status = 0;
	pid_t pid, rc_pid;
	if ((pid = fork()) == -1) {
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("create tar failed to fork.\n");
#endif
		return -1;
	}
	if (pid == 0) {
		// Child process
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		if (use_encryption || userdata_encryption) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("Using encryption\n");
	#endif
			DIR* d;
			struct dirent* de;
			unsigned long long regular_size = 0, encrypt_size = 0, target_size = 0, core_count = 1;
			unsigned enc_thread_id = 1, regular_thread_id = 0, i, start_thread_id = 1;
			int item_len, ret, thread_error = 0;
			std::vector<TarListStruct> RegularList;
			std::vector<TarListStruct> EncryptList;
			string FileName;
			struct TarListStruct TarItem;
			twrpTar reg, enc[9];
			struct stat st;
			pthread_t enc_thread[9];
			pthread_attr_t tattr;
			void *thread_return;

			core_count = sysconf(_SC_NPROCESSORS_CONF);
			if (core_count > 8)
				core_count = 8;
	#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("   Core Count      : %llu\n", core_count);
	#endif
			Archive_Current_Size = 0;

			d = opendir(tardir.c_str());
			if (d == NULL) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("error opening '%s'\n", tardir.c_str());
	#endif
				_exit(-1);
			}
			// Figure out the size of all data to be encrypted and create a list of unencrypted files
			while ((de = readdir(d)) != NULL) {
				FileName = tardir + "/";
				FileName += de->d_name;
				if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
					continue; // Skip /data/media
				if (de->d_type == DT_BLK || de->d_type == DT_CHR)
					continue;
				if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strcmp(de->d_name, "lost+found") != 0) {
					item_len = strlen(de->d_name);
					if (userdata_encryption && ((item_len >= 3 && strncmp(de->d_name, "app", 3) == 0) || (item_len >= 6 && strncmp(de->d_name, "dalvik", 6) == 0))) {
						if (Generate_TarList(FileName, &RegularList, &target_size, &regular_thread_id) < 0) {
							LOGERR("Error in Generate_TarList with regular list!\n");
							closedir(d);
							_exit(-1);
						}
						regular_size += TWFunc::Get_Folder_Size(FileName, false);
					} else {
						encrypt_size += TWFunc::Get_Folder_Size(FileName, false);
					}
				} else if (de->d_type == DT_REG) {
					stat(FileName.c_str(), &st);
					encrypt_size += (unsigned long long)(st.st_size);
				}
			}
			closedir(d);

			target_size = encrypt_size / core_count;
			target_size++;
	#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("   Unencrypted size: %llu\n", regular_size);
			LOGINFO("   Encrypted size  : %llu\n", encrypt_size);
			LOGINFO("   Target size     : %llu\n", target_size);
	#endif
			if (!userdata_encryption) {
				enc_thread_id = 0;
				start_thread_id = 0;
				core_count--;
			}
			Archive_Current_Size = 0;

			d = opendir(tardir.c_str());
			if (d == NULL) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("error opening '%s'\n", tardir.c_str());
	#endif
				_exit(-1);
			}
			// Divide up the encrypted file list for threading
			while ((de = readdir(d)) != NULL) {
				FileName = tardir + "/";
				FileName += de->d_name;
				if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
					continue; // Skip /data/media
				if (de->d_type == DT_BLK || de->d_type == DT_CHR)
					continue;
				if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strcmp(de->d_name, "lost+found") != 0) {
					item_len = strlen(de->d_name);
					if (userdata_encryption && ((item_len >= 3 && strncmp(de->d_name, "app", 3) == 0) || (item_len >= 6 && strncmp(de->d_name, "dalvik", 6) == 0))) {
						// Do nothing, we added these to RegularList earlier
					} else {
						FileName = tardir + "/";
						FileName += de->d_name;
						if (Generate_TarList(FileName, &EncryptList, &target_size, &enc_thread_id) < 0) {
							LOGERR("Error in Generate_TarList with encrypted list!\n");
							closedir(d);
							_exit(-1);
						}
					}
				} else if (de->d_type == DT_REG || de->d_type == DT_LNK) {
					stat(FileName.c_str(), &st);
					if (de->d_type == DT_REG)
						Archive_Current_Size += (unsigned long long)(st.st_size);
					TarItem.fn = FileName;
					TarItem.thread_id = enc_thread_id;
					EncryptList.push_back(TarItem);
				}
			}
			closedir(d);
			if (enc_thread_id != core_count) {
				LOGERR("Error dividing up threads for encryption, %i threads for %i cores!\n", enc_thread_id, core_count);
				if (enc_thread_id > core_count)
					_exit(-1);
				else
					LOGERR("Continuining anyway.");
			}

			if (userdata_encryption) {
				// Create a backup of unencrypted data
				reg.setfn(tarfn);
				reg.ItemList = &RegularList;
				reg.thread_id = 0;
				reg.use_encryption = 0;
				reg.use_compression = use_compression;
				LOGINFO("Creating unencrypted backup...\n");
				if (createList((void*)&reg) != 0) {
					LOGERR("Error creating unencrypted backup.\n");
					_exit(-1);
				}
			}

			if (pthread_attr_init(&tattr)) {
				LOGERR("Unable to pthread_attr_init\n");
				_exit(-1);
			}
			if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE)) {
				LOGERR("Error setting pthread_attr_setdetachstate\n");
				_exit(-1);
			}
			if (pthread_attr_setscope(&tattr, PTHREAD_SCOPE_SYSTEM)) {
				LOGERR("Error setting pthread_attr_setscope\n");
				_exit(-1);
			}
			/*if (pthread_attr_setstacksize(&tattr, 524288)) {
				LOGERR("Error setting pthread_attr_setstacksize\n");
				_exit(-1);
			}*/

			// Create threads for the divided up encryption lists
			for (i = start_thread_id; i <= core_count; i++) {
				enc[i].setdir(tardir);
				enc[i].setfn(tarfn);
				enc[i].ItemList = &EncryptList;
				enc[i].thread_id = i;
				enc[i].use_encryption = use_encryption;
				enc[i].use_compression = use_compression;
	#ifdef TAR_DEBUG_VERBOSE
				LOGINFO("Start encryption thread %i\n", i);
	#endif
				ret = pthread_create(&enc_thread[i], &tattr, createList, (void*)&enc[i]);
				if (ret) {
					LOGINFO("Unable to create %i thread for encryption! %i\nContinuing in same thread (backup will be slower).", i, ret);
					if (createList((void*)&enc[i]) != 0) {
						LOGERR("Error creating encrypted backup %i.\n", i);
						_exit(-1);
					} else {
						enc[i].thread_id = i + 1;
					}
				}
				usleep(100000); // Need a short delay before starting the next thread or the threads will never finish for some reason.
			}
			if (pthread_attr_destroy(&tattr)) {
				LOGERR("Failed to pthread_attr_destroy\n");
			}
			for (i = start_thread_id; i <= core_count; i++) {
				if (enc[i].thread_id == i) {
					if (pthread_join(enc_thread[i], &thread_return)) {
						LOGERR("Error joining thread %i\n", i);
						_exit(-1);
					} else {
						LOGINFO("Joined thread %i.\n", i);
						ret = (int)thread_return;
						if (ret != 0) {
							thread_error = 1;
							LOGERR("Thread %i returned an error %i.\n", i, ret);
							_exit(-1);
						}
					}
				} else {
					LOGINFO("Skipping joining thread %i because of pthread failure.\n", i);
				}
			}
			if (thread_error) {
				LOGERR("Error returned by one or more threads.\n");
				_exit(-1);
			}
	#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("Finished encrypted backup.\n");
	#endif
			_exit(0);
		} else {
#endif
			if (create() != 0)
				_exit(-1);
			else
				_exit(0);
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		}
#endif
	} else {
		if (TWFunc::Wait_For_Child(pid, &status, "createTarFork()") != 0)
			return -1;
	}
	return 0;
}

int twrpTar::extractTarFork() {
	int status = 0;
	pid_t pid, rc_pid;

	pid = fork();
	if (pid >= 0) // fork was successful
	{
		if (pid == 0) // child process
		{
			if (TWFunc::Path_Exists(tarfn)) {
#ifdef TAR_DEBUG_VERBOSE
				LOGINFO("Single archive\n");
#endif
				if (extract() != 0)
					_exit(-1);
				else
					_exit(0);
			} else {
#ifdef TAR_DEBUG_VERBOSE
				LOGINFO("Multiple archives\n");
#endif
				string temp;
				char actual_filename[255];
				twrpTar tars[9];
				pthread_t tar_thread[9];
				pthread_attr_t tattr;
				unsigned thread_count = 0, i, start_thread_id = 1, ret, thread_error = 0;
				void *thread_return;

				basefn = tarfn;
				temp = basefn + "%i%02i";
				tarfn += "000";
				if (!TWFunc::Path_Exists(tarfn)) {
					LOGERR("Unable to locate '%s' or '%s'\n", basefn.c_str(), tarfn.c_str());
					_exit(-1);
				}
				if (TWFunc::Get_File_Type(tarfn) != 2) {
#ifdef TAR_DEBUG_VERBOSE
					LOGINFO("First tar file '%s' not encrypted\n", tarfn.c_str());
#endif
					tars[0].basefn = basefn;
					tars[0].thread_id = 0;
					if (extractMulti((void*)&tars[0]) != 0) {
						LOGERR("Error extracting split archive.\n");
						_exit(-1);
					}
				} else {
					start_thread_id = 0;
				}
				// Start threading encrypted restores
				if (pthread_attr_init(&tattr)) {
					LOGERR("Unable to pthread_attr_init\n");
					_exit(-1);
				}
				if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE)) {
					LOGERR("Error setting pthread_attr_setdetachstate\n");
					_exit(-1);
				}
				if (pthread_attr_setscope(&tattr, PTHREAD_SCOPE_SYSTEM)) {
					LOGERR("Error setting pthread_attr_setscope\n");
					_exit(-1);
				}
				/*if (pthread_attr_setstacksize(&tattr, 524288)) {
					LOGERR("Error setting pthread_attr_setstacksize\n");
					_exit(-1);
				}*/
				for (i = start_thread_id; i < 9; i++) {
					sprintf(actual_filename, temp.c_str(), i, 0);
					if (TWFunc::Path_Exists(actual_filename)) {
						thread_count++;
						tars[i].basefn = basefn;
						tars[i].thread_id = i;
						LOGINFO("Creating extract thread ID %i\n", i);
						ret = pthread_create(&tar_thread[i], &tattr, extractMulti, (void*)&tars[i]);
						if (ret) {
							LOGINFO("Unable to create %i thread for extraction! %i\nContinuing in same thread (restore will be slower).", i, ret);
							if (extractMulti((void*)&tars[i]) != 0) {
								LOGERR("Error extracting backup in thread %i.\n", i);
								_exit(-1);
							} else {
								tars[i].thread_id = i + 1;
							}
						}
						usleep(100000); // Need a short delay before starting the next thread or the threads will never finish for some reason.
					} else {
						break;
					}
				}
				for (i = start_thread_id; i < thread_count + start_thread_id; i++) {
					if (tars[i].thread_id == i) {
						if (pthread_join(tar_thread[i], &thread_return)) {
							LOGERR("Error joining thread %i\n", i);
							_exit(-1);
						} else {
							LOGINFO("Joined thread %i.\n", i);
							ret = (int)thread_return;
							if (ret != 0) {
								thread_error = 1;
								LOGERR("Thread %i returned an error %i.\n", i, ret);
								_exit(-1);
							}
						}
					} else {
						LOGINFO("Skipping joining thread %i because of pthread failure.\n", i);
					}
				}
				if (thread_error) {
					LOGERR("Error returned by one or more threads.\n");
					_exit(-1);
				}
				LOGINFO("Finished encrypted backup.\n");
				_exit(0);
			}
		}
		else // parent process
		{
			if (TWFunc::Wait_For_Child(pid, &status, "extractTarFork()") != 0)
				return -1;
		}
	}
	else // fork has failed
	{
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("extract tar failed to fork.\n");
#endif
		return -1;
	}
	return 0;
}

int twrpTar::splitArchiveFork() {
	int status = 0;
	pid_t pid, rc_pid;

	pid = fork();
	if (pid >= 0) // fork was successful
	{
		if (pid == 0) // child process
		{
			if (Split_Archive() <= 0)
				_exit(-1);
			else
				_exit(0);
		}
		else // parent process
		{
			if (TWFunc::Wait_For_Child(pid, &status, "splitArchiveFork()") != 0)
				return -1;
		}
	}
	else // fork has failed
	{
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("split archive failed to fork.\n");
#endif
		return -1;
	}
	return 0;
}

int twrpTar::Generate_TarList(string Path, std::vector<TarListStruct> *TarList, unsigned long long *Target_Size, unsigned *thread_id) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	string FileName;
	struct TarListStruct TarItem;

	if (has_data_media == 1 && Path.size() >= 11 && strncmp(Path.c_str(), "/data/media", 11) == 0)
		return 0; // Skip /data/media

	d = opendir(Path.c_str());
	if (d == NULL) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("error opening '%s' -- error: %s\n", Path.c_str(), strerror(errno));
#endif
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		// Skip excluded stuff
		char* type = NULL;
#ifdef TAR_DEBUG_VERBOSE
		if (de->d_type == DT_DIR)
			type = (char*)"(dir) ";
		else if (de->d_type == DT_REG)
			type = (char*)"(reg) ";
		else if (de->d_type == DT_LNK)
			type = (char*)"(link) ";
#endif
		if (skip(de->d_name, type))
			continue;

		FileName = Path + "/";
		FileName += de->d_name;
		if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
			continue; // Skip /data/media
		if (de->d_type == DT_BLK || de->d_type == DT_CHR)
			continue;
		TarItem.fn = FileName;
		TarItem.thread_id = *thread_id;
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strcmp(de->d_name, "lost+found") != 0) {
			TarList->push_back(TarItem);
			if (Generate_TarList(FileName, TarList, Target_Size, thread_id) < 0)
				return -1;
		} else if (de->d_type == DT_REG || de->d_type == DT_LNK) {
			stat(FileName.c_str(), &st);
			TarList->push_back(TarItem);
			if (de->d_type == DT_REG)
				Archive_Current_Size += st.st_size;
			if (Archive_Current_Size != 0 && *Target_Size != 0 && Archive_Current_Size > *Target_Size) {
				*thread_id = *thread_id + 1;
				Archive_Current_Size = 0;
			}
		}
	}
	closedir(d);
	return 0;
}

int twrpTar::Generate_Multiple_Archives(string Path) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	string FileName;
	char actual_filename[255];

	if (has_data_media == 1 && Path.size() >= 11 && strncmp(Path.c_str(), "/data/media", 11) == 0)
		return 0; // Skip /data/media
#ifdef TAR_DEBUG_VERBOSE
	LOGINFO("Path: '%s', archive filename: '%s'\n", Path.c_str(), tarfn.c_str());
#endif
	d = opendir(Path.c_str());
	if (d == NULL) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("error opening '%s' -- error: %s\n", Path.c_str(), strerror(errno));
#endif
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		// Skip excluded stuff
		char* type = NULL;
#ifndef TAR_DEBUG_SUPPRESS
		if (de->d_type == DT_DIR)
			type = (char*)"(dir) ";
		else if (de->d_type == DT_REG)
			type = (char*)"(reg) ";
		else if (de->d_type == DT_LNK)
			type = (char*)"(link) ";
#endif
		if (skip(de->d_name, type))
			continue;

		FileName = Path + "/";
		FileName += de->d_name;
		if (has_data_media == 1 && FileName.size() >= 11 && strncmp(FileName.c_str(), "/data/media", 11) == 0)
			continue; // Skip /data/media
		if (de->d_type == DT_BLK || de->d_type == DT_CHR)
			continue;
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0 && strcmp(de->d_name, "lost+found") != 0)
		{
			unsigned long long folder_size = TWFunc::Get_Folder_Size(FileName, false);
			if (Archive_Current_Size + folder_size > MAX_ARCHIVE_SIZE) {
#ifdef TAR_DEBUG_VERBOSE
				LOGINFO("Calling Generate_Multiple_Archives\n");
#endif
				if (Generate_Multiple_Archives(FileName) < 0)
					return -1;
			} else {
				//FileName += "/";
#ifndef TAR_DEBUG_SUPPRESS
				LOGINFO("Adding folder '%s'\n", FileName.c_str());
#endif
				tardir = FileName;
				if (tarDirs(true) < 0)
					return -1;
				Archive_Current_Size += folder_size;
			}
		}
		else if (de->d_type == DT_REG || de->d_type == DT_LNK)
		{
			stat(FileName.c_str(), &st);
			if (de->d_type != DT_LNK) {
				if (Archive_Current_Size != 0 && Archive_Current_Size + st.st_size > MAX_ARCHIVE_SIZE) {
#ifdef TAR_DEBUG_VERBOSE
					LOGINFO("Closing tar '%s', ", tarfn.c_str());
#endif
					closeTar();
					if (TWFunc::Get_File_Size(tarfn) == 0) {
						LOGERR("Backup file size for '%s' is 0 bytes.\n", tarfn.c_str());
						return -1;
					}
					Archive_File_Count++;
					if (Archive_File_Count > 999) {
						LOGERR("Archive count is too large!\n");
						return -1;
					}
					string temp = basefn + "%03i";
					sprintf(actual_filename, temp.c_str(), Archive_File_Count);
					tarfn = actual_filename;
					Archive_Current_Size = 0;
					LOGINFO("Creating tar '%s'\n", tarfn.c_str());
					gui_print("Creating archive %i...\n", Archive_File_Count + 1);
					if (createTar() != 0)
						return -1;
				}
			}
#ifndef TAR_DEBUG_SUPPRESS
			LOGINFO("Adding file: '%s'... ", FileName.c_str());
#endif
			if (addFile(FileName, true) < 0)
				return -1;
			if (de->d_type != DT_LNK)
				Archive_Current_Size += st.st_size;
#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("added successfully, archive size: %llu\n", Archive_Current_Size);
#endif
			if (de->d_type != DT_LNK && st.st_size > 2147483648LL)
				LOGERR("There is a file that is larger than 2GB in the file system\n'%s'\nThis file may not restore properly\n", FileName.c_str());
		}
	}
	closedir(d);
	return 0;
}

int twrpTar::Split_Archive()
{
	string temp = tarfn + "%03i";
	char actual_filename[255];

	basefn = tarfn;
	Archive_File_Count = 0;
	Archive_Current_Size = 0;
	sprintf(actual_filename, temp.c_str(), Archive_File_Count);
	tarfn = actual_filename;
	if (!tarexclude.empty())
		Excluded = TWFunc::split_string(tarexclude, ' ', true);
	createTar();
	DataManager::GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	gui_print("Creating archive 1...\n");
	if (Generate_Multiple_Archives(tardir) < 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Error generating multiple archives\n");
#endif
		return -1;
	}
	closeTar();
	LOGINFO("Done, created %i archives.\n", (++Archive_File_Count));
	return (Archive_File_Count);
}

int twrpTar::extractTar() {
	char* charRootDir = (char*) tardir.c_str();
	if (openTar() == -1)
		return -1;
	if (tar_extract_all(t, charRootDir) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Unable to extract tar archive '%s'\n", tarfn.c_str());
#endif
		return -1;
	}
	if (tar_close(t) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Unable to close tar file\n");
#endif
		return -1;
	}
	return 0;
}

int twrpTar::extract() {
	Archive_Current_Type = TWFunc::Get_File_Type(tarfn);

	if (Archive_Current_Type == 0)
		LOGINFO("Extracting uncompressed tar\n");
	else if (Archive_Current_Type == 1)
		LOGINFO("Extracting gzipped tar\n");
	else if (Archive_Current_Type == 2) {
#ifdef TW_EXCLUDE_ENCRYPTED_BACKUPS
		LOGERR("Encrypted backup support NOT supported!\n");
		return -1;
#else
		string Password = DataManager::GetStrValue("tw_restore_password");
		int ret = TWFunc::Try_Decrypting_File(tarfn, Password);
		if (ret <= 1) {
			LOGERR("Failed to decrypt %s file '%s'\n", (ret == 1 ? "(Not tar format)" : "(tar)"), tarfn.c_str());
			return -1;
		} else if (ret == 3) {
			LOGINFO("Extracting encrypted and compressed tar.\n");
			Archive_Current_Type = 3;
		} else
			LOGINFO("Extracting encrypted tar.\n");
#endif
	}

	return extractTar();
}

int twrpTar::tarDirs(bool include_root) {
	DIR* d;
	string mainfolder = tardir + "/", subfolder;
	char buf[PATH_MAX];
	char excl[1024];

	d = opendir(tardir.c_str());
	if (d != NULL) {		
		if (!tarexclude.empty()) {
			strcpy(excl, tarexclude.c_str());
			Excluded = TWFunc::split_string(tarexclude, ' ', true);
		}
		struct dirent* de;
		while ((de = readdir(d)) != NULL) {
#ifdef RECOVERY_SDCARD_ON_DATA
			if ((tardir == "/data" || tardir == "/data/") && strcmp(de->d_name, "media") == 0) continue;
#endif
			if (de->d_type == DT_BLK || de->d_type == DT_CHR || strcmp(de->d_name, "..") == 0 || strcmp(de->d_name, "lost+found") == 0)
				continue;

			char* type = NULL;
#ifndef TAR_DEBUG_SUPPRESS
			if (de->d_type == DT_DIR)
				type = (char*)"(dir) ";
			else if (de->d_type == DT_REG)
				type = (char*)"(reg) ";
			else if (de->d_type == DT_LNK)
				type = (char*)"(link) ";
#endif
			// Skip excluded stuff
			if (skip(de->d_name, type))
				continue;

			subfolder = mainfolder;
			if (strcmp(de->d_name, ".") != 0) {
				subfolder += de->d_name;
			} else {
#ifndef TAR_DEBUG_SUPPRESS
				LOGINFO("Adding %s: '%s'\n", (type == NULL ? "" : type), subfolder.c_str());
#endif
				if (addFile(subfolder, include_root) != 0)
					return -1;
				continue;
			}
#ifndef TAR_DEBUG_SUPPRESS
			LOGINFO("Adding %s: '%s'\n", type, subfolder.c_str());
#endif
			strcpy(buf, subfolder.c_str());
			if (de->d_type == DT_DIR) {
				char charTarPath[PATH_MAX];
				if (include_root) {
					charTarPath[0] = '0';
				} else {
					string temp = Strip_Root_Dir(buf);
					strcpy(charTarPath, temp.c_str());
				}
				if (tar_append_tree(t, buf, charTarPath, excl) != 0) {
#ifdef TAR_DEBUG_VERBOSE
					LOGERR("Error appending '%s' to tar archive '%s'\n", buf, tarfn.c_str());
#endif
					return -1;
				}
			} else if (tardir != "/" && (de->d_type == DT_REG || de->d_type == DT_LNK)) {
				if (addFile(buf, include_root) != 0)
					return -1;
			}
			fflush(NULL);
		}
		closedir(d);
	}
	return 0;
}

int twrpTar::tarList(bool include_root, std::vector<TarListStruct> *TarList, unsigned thread_id) {
	struct stat st;
	char buf[PATH_MAX];
	int list_size = TarList->size(), i = 0, archive_count = 0;
	string temp;
	char actual_filename[PATH_MAX];

	basefn = tarfn;
	temp = basefn + "%i%02i";
	sprintf(actual_filename, temp.c_str(), thread_id, archive_count);
	tarfn = actual_filename;
	if (createTar() != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Error creating tar '%s' for thread %i\n", tarfn.c_str(), thread_id);
#endif
		return -2;
	}
	Archive_Current_Size = 0;

	while (i < list_size) {
		if (TarList->at(i).thread_id == thread_id) {
			strcpy(buf, TarList->at(i).fn.c_str());
			stat(buf, &st);
			if (st.st_mode & S_IFREG) { // item is a regular file
				if (Archive_Current_Size + (unsigned long long)(st.st_size) > MAX_ARCHIVE_SIZE) {
					if (closeTar() != 0) {
#ifdef TAR_DEBUG_VERBOSE
						LOGERR("Error closing '%s' on thread %i\n", tarfn.c_str(), thread_id);
#endif
						return -3;
					}
					archive_count++;
					if (archive_count > 99) {
#ifdef TAR_DEBUG_VERBOSE
						LOGERR("Too many archives for thread %i\n", thread_id);
#endif
						return -4;
					}
					sprintf(actual_filename, temp.c_str(), thread_id, archive_count);
					tarfn = actual_filename;
					if (createTar() != 0) {
#ifdef TAR_DEBUG_VERBOSE
						LOGERR("Error creating tar '%s' for thread %i\n", tarfn.c_str(), thread_id);
#endif
						return -2;
					}
					Archive_Current_Size = 0;
				}
				Archive_Current_Size += (unsigned long long)(st.st_size);
			}
			if (addFile(buf, include_root) != 0) {
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Error adding file '%s' to '%s'\n", buf, tarfn.c_str());
#endif
				return -1;
			}
		}
		i++;
	}
	if (closeTar() != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Error closing '%s' on thread %i\n", tarfn.c_str(), thread_id);
#endif
		return -3;
	}
	return 0;
}

int twrpTar::create() {

	init_libtar_buffer(0);
	if (createTar() == -1)
		return -1;
	if (tarDirs(false) == -1)
		return -1;
	if (closeTar() == -1)
		return -1;
	free_libtar_buffer();
	return 0;
}

void* twrpTar::createList(void *cookie) {

	twrpTar* threadTar = (twrpTar*) cookie;
	if (threadTar->tarList(true, threadTar->ItemList, threadTar->thread_id) == -1) {
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("ERROR tarList for thread ID %i\n", threadTar->thread_id);
#endif
		return (void*)-2;
	}
#ifdef TAR_DEBUG_VERBOSE
	LOGINFO("Thread ID %i finished successfully.\n", threadTar->thread_id);
#endif
	return (void*)0;
}

void* twrpTar::extractMulti(void *cookie) {

	twrpTar* threadTar = (twrpTar*) cookie;
	int archive_count = 0;
	string temp = threadTar->basefn + "%i%02i";
	char actual_filename[255];
	sprintf(actual_filename, temp.c_str(), threadTar->thread_id, archive_count);
	while (TWFunc::Path_Exists(actual_filename)) {
		threadTar->tarfn = actual_filename;
		if (threadTar->extract() != 0) {
#ifdef TAR_DEBUG_VERBOSE
			LOGINFO("Error extracting '%s' in thread ID %i\n", actual_filename, threadTar->thread_id);
#endif
			return (void*)-2;
		}
		archive_count++;
		if (archive_count > 99)
			break;
		sprintf(actual_filename, temp.c_str(), threadTar->thread_id, archive_count);
	}
#ifdef TAR_DEBUG_VERBOSE
	LOGINFO("Thread ID %i finished successfully.\n", threadTar->thread_id);
#endif
	return (void*)0;
}

int twrpTar::addFilesToExistingTar(vector <string> files, string fn) {
	char* charTarFile = (char*) fn.c_str();

	if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) == -1)
		return -1;
	removeEOT(charTarFile);
	if (tar_open(&t, charTarFile, NULL, O_WRONLY | O_APPEND | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) == -1)
		return -1;
	for (unsigned int i = 0; i < files.size(); ++i) {
		char* file = (char*) files.at(i).c_str();
		if (tar_append_file(t, file, file) == -1)
			return -1;
	}
	if (tar_append_eof(t) == -1)
		return -1;
	if (tar_close(t) == -1)
		return -1;
	return 0;
}

int twrpTar::createTar() {
	char* charTarFile = (char*) tarfn.c_str();
	char* charRootDir = (char*) tardir.c_str();
	static tartype_t type = { open, close, read, write_tar };
	string Password;

	if (use_encryption && use_compression) {
#ifdef TW_EXCLUDE_ENCRYPTED_BACKUPS
		LOGINFO("Using encryption NOT supported...\n");
		return -1;
#else
		// Compressed and encrypted
		Archive_Current_Type = 3;
		LOGINFO("Using encryption and compression...\n");
		DataManager::GetValue("tw_backup_password", Password);
		int i, pipes[4];

		if (pipe(pipes) < 0 || pipe(pipes + 2) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("Error creating pipes\n");
	#endif
			return -1;
		}

		pigz_pid = fork();
		
		if (pigz_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("pigz fork() failed\n");
	#endif
			for (i = 0; i < 4; i++)
				close(pipes[i]); // close all
			return -1;
		} else if (pigz_pid == 0) {
			// pigz Child
			close(pipes[1]);
			close(pipes[2]);
			close(STDIN_FILENO);
			dup2(pipes[0], STDIN_FILENO);
			close(STDOUT_FILENO);
			dup2(pipes[3], STDOUT_FILENO);
			if (execlp("pigz", "pigz", "-", NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp pigz ERROR!\n");
	#endif
				close(pipes[0]);
				close(pipes[3]);
				_exit(-1);
			}
		} else {
			// Parent
			oaes_pid = fork();
		
			if (oaes_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("openaes fork() failed\n");
	#endif
				for (i = 0; i < 4; i++)
					close(pipes[i]); // close all
				return -1;
			} else if (oaes_pid == 0) {
				// openaes Child
				int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
				if (output_fd < 0) {
	#ifdef TAR_DEBUG_VERBOSE
					LOGERR("Failed to open '%s'\n", tarfn.c_str());
	#endif
					for (i = 0; i < 4; i++)
						close(pipes[i]); // close all
					return -1;
				}
				close(pipes[0]);
				close(pipes[1]);
				close(pipes[3]);
				close(STDIN_FILENO);
				dup2(pipes[2], STDIN_FILENO);
				close(STDOUT_FILENO);
				dup2(output_fd, STDOUT_FILENO);
				if (execlp("openaes", "openaes", "enc", "--key", Password.c_str(), NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
					LOGERR("execlp openaes ERROR!\n");
	#endif
					close(pipes[2]);
					close(output_fd);
					exit(-1);
				}
			} else {
				// Parent
				close(pipes[0]);
				close(pipes[2]);
				close(pipes[3]);
				fd = pipes[1];
				if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
					close(fd);
	#ifdef TAR_DEBUG_VERBOSE
					LOGERR("tar_fdopen failed\n");
	#endif
					return -1;
				}
				return 0;
			}
		}
#endif
	} else if (!use_encryption && use_compression) {
		// Compressed
		Archive_Current_Type = 1;
		LOGINFO("Creating gzipped archive...\n");
		int pigzfd[2];

		if (pipe(pigzfd) < 0) {
#ifdef TAR_DEBUG_VERBOSE
			LOGERR("Error creating pipe\n");
#endif
			return -1;
		}
		pigz_pid = fork();
		
		if (pigz_pid < 0) {
#ifdef TAR_DEBUG_VERBOSE
			LOGERR("fork() failed\n");
#endif
			close(pigzfd[0]);
			close(pigzfd[1]);
			return -1;
		} else if (pigz_pid == 0) {
			// Child
			close(pigzfd[1]);   // close unused output pipe
			int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
			if (output_fd < 0) {
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Failed to open '%s'\n", tarfn.c_str());
#endif
				close(pigzfd[0]);
				_exit(-1);
			}
			dup2(pigzfd[0], STDIN_FILENO); // remap stdin
			dup2(output_fd, STDOUT_FILENO); // remap stdout to output file
			if (execlp("pigz", "pigz", "-", NULL) < 0) {
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp pigz ERROR!\n");
#endif
				close(output_fd);
				close(pigzfd[0]);
				_exit(-1);
			}
		} else {
			// Parent
			close(pigzfd[0]); // close parent input
			fd = pigzfd[1];   // copy parent output
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
				close(fd);
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("tar_fdopen failed\n");
#endif
				return -1;
			}
		}
	} else if (use_encryption && !use_compression) {
#ifdef TW_EXCLUDE_ENCRYPTED_BACKUPS
		LOGINFO("Using encryption NOT supported...\n");
		return -1;
#else
		// Encrypted
		Archive_Current_Type = 2;
		LOGINFO("Using encryption...\n");
		DataManager::GetValue("tw_backup_password", Password);
		int oaesfd[2];
		pipe(oaesfd);
		oaes_pid = fork();
		
		if (oaes_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("fork() failed\n");
	#endif
			close(oaesfd[0]);
			close(oaesfd[1]);
			return -1;
		} else if (oaes_pid == 0) {
			// Child
			close(oaesfd[1]);   // close unused
			int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
			if (output_fd < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Failed to open '%s'\n", tarfn.c_str());
	#endif
				_exit(-1);
			}
			dup2(oaesfd[0], STDIN_FILENO); // remap stdin
			dup2(output_fd, STDOUT_FILENO); // remap stdout to output file
			if (execlp("openaes", "openaes", "enc", "--key", Password.c_str(), NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp openaes ERROR!\n");
	#endif
				close(output_fd);
				close(oaesfd[0]);
				_exit(-1);
			}
		} else {
			// Parent
			close(oaesfd[0]); // close parent input
			fd = oaesfd[1];   // copy parent output
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
				close(fd);
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("tar_fdopen failed\n");
	#endif
				return -1;
			}
			return 0;
		}
#endif
	} else {
		LOGINFO("Creating uncompressed archive...\n");
		// Not compressed or encrypted
		init_libtar_buffer(0);
		if (tar_open(&t, charTarFile, &type, O_WRONLY | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) == -1) {
#ifdef TAR_DEBUG_VERBOSE
			LOGERR("tar_open error opening '%s'\n", tarfn.c_str());
#endif
			return -1;
		}
	}
	return 0;
}

int twrpTar::openTar() {
	char* charRootDir = (char*) tardir.c_str();
	char* charTarFile = (char*) tarfn.c_str();
	string Password;

#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	if (Archive_Current_Type == 3) {
	#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("Opening encrypted and compressed backup...\n");
	#endif
		DataManager::GetValue("tw_restore_password", Password);
		int i, pipes[4];

		if (pipe(pipes) < 0 || pipe(pipes + 2) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("Error creating pipes\n");
	#endif
			return -1;
		}

		oaes_pid = fork();
		
		if (oaes_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("pigz fork() failed\n");
	#endif
			for (i = 0; i < 4; i++)
				close(pipes[i]); // close all
			return -1;
		} else if (oaes_pid == 0) {
			// openaes Child
			close(pipes[0]); // Close pipes that are not used by this child
			close(pipes[2]);
			close(pipes[3]);
			int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
			if (input_fd < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Failed to open '%s'\n", tarfn.c_str());
	#endif
				close(pipes[1]);
				_exit(-1);
			}
			close(STDIN_FILENO);
			dup2(input_fd, STDIN_FILENO);
			close(STDOUT_FILENO);
			dup2(pipes[1], STDOUT_FILENO);
			if (execlp("openaes", "openaes", "dec", "--key", Password.c_str(), NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp openaes ERROR!\n");
	#endif
				close(input_fd);
				close(pipes[1]);
				_exit(-1);
			}
		} else {
			// Parent
			pigz_pid = fork();
		
			if (pigz_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("openaes fork() failed\n");
	#endif
				for (i = 0; i < 4; i++)
					close(pipes[i]); // close all
				return -1;
			} else if (pigz_pid == 0) {
				// pigz Child
				close(pipes[1]); // Close pipes not used by this child
				close(pipes[2]);
				close(STDIN_FILENO);
				dup2(pipes[0], STDIN_FILENO);
				close(STDOUT_FILENO);
				dup2(pipes[3], STDOUT_FILENO);
				if (execlp("pigz", "pigz", "-d", "-c", NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
					LOGERR("execlp pigz ERROR!\n");
	#endif
					close(pipes[0]);
					close(pipes[3]);
					_exit(-1);
				}
			} else {
				// Parent
				close(pipes[0]); // Close pipes not used by parent
				close(pipes[1]);
				close(pipes[3]);
				fd = pipes[2];
				if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
					close(fd);
	#ifdef TAR_DEBUG_VERBOSE
					LOGERR("tar_fdopen failed\n");
	#endif
					return -1;
				}
			}
		}
	} else if (Archive_Current_Type == 2) {
	#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("Opening encrypted backup...\n");
	#endif
		DataManager::GetValue("tw_restore_password", Password);
		int oaesfd[2];

		pipe(oaesfd);
		oaes_pid = fork();
		if (oaes_pid < 0) {
	#ifdef TAR_DEBUG_VERBOSE
			LOGERR("fork() failed\n");
	#endif
			close(oaesfd[0]);
			close(oaesfd[1]);
			return -1;
		} else if (oaes_pid == 0) {
			// Child
			close(oaesfd[0]); // Close unused pipe
			int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
			if (input_fd < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Failed to open '%s'\n", tarfn.c_str());
	#endif
				close(oaesfd[1]);
				_exit(-1);
			}
			close(STDIN_FILENO); // close stdin
			dup2(input_fd, STDIN_FILENO); // remap input fd to stdin
			close(STDOUT_FILENO); // close stdout
			dup2(oaesfd[1], STDOUT_FILENO); // remap stdout
			if (execlp("openaes", "openaes", "dec", "--key", Password.c_str(), NULL) < 0) {
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp openaes ERROR!\n");
	#endif
				close(input_fd);
				close(oaesfd[1]);
				_exit(-1);
			}
		} else {
			// Parent
			close(oaesfd[1]); // close parent output
			fd = oaesfd[0];   // copy parent input
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
				close(fd);
	#ifdef TAR_DEBUG_VERBOSE
				LOGERR("tar_fdopen failed\n");
	#endif
				return -1;
			}
		}
	}
#else
	if (Archive_Current_Type == 3 || Archive_Current_Type == 2) {
		LOGERR("Encrypted backup support NOT supported!\n");
		return -1;
	}
#endif
	else if (Archive_Current_Type == 1) {
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("Opening as a gzip...\n");
#endif
		int pigzfd[2];
		pipe(pigzfd);

		pigz_pid = fork();
		if (pigz_pid < 0) {
#ifdef TAR_DEBUG_VERBOSE
			LOGERR("fork() failed\n");
#endif
			close(pigzfd[0]);
			close(pigzfd[1]);
			return -1;
		} else if (pigz_pid == 0) {
			// Child
			close(pigzfd[0]);
			int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
			if (input_fd < 0) {
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("Failed to open '%s'\n", tarfn.c_str());
#endif
				_exit(-1);
			}
			close(STDIN_FILENO); // close stdin
			dup2(input_fd, STDIN_FILENO); // remap input fd to stdin
			close(STDOUT_FILENO); // close stdout
			dup2(pigzfd[1], STDOUT_FILENO); // remap stdout
			if (execlp("pigz", "pigz", "-d", "-c", NULL) < 0) {
				close(pigzfd[1]);
				close(input_fd);
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("execlp openaes ERROR!\n");
#endif
				_exit(-1);
			}
		} else {
			// Parent
			close(pigzfd[1]); // close parent output
			fd = pigzfd[0];   // copy parent input
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
				close(fd);
#ifdef TAR_DEBUG_VERBOSE
				LOGERR("tar_fdopen failed\n");
#endif
				return -1;
			}
		}
	}  else if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Unable to open tar archive '%s'\n", charTarFile);
#endif
		return -1;
	}
	return 0;
}

string twrpTar::Strip_Root_Dir(string Path) {
	string stripped, temp;
	size_t slash;

	if (Path.substr(0, 1) == "/")
		temp = Path.substr(1, Path.size() - 1);
	else
		temp = Path;
	slash = temp.find("/");
	if (slash == string::npos)
		stripped = temp;
	else
		stripped = temp.substr(slash, temp.size() - slash);

	return stripped;
}

int twrpTar::addFile(string fn, bool include_root) {
	char* charTarFile = (char*) fn.c_str();
	if (include_root) {
		if (tar_append_file(t, charTarFile, NULL) == -1)
			return -1;
	} else {
		string temp = Strip_Root_Dir(fn);
		char* charTarPath = (char*) temp.c_str();
		if (tar_append_file(t, charTarFile, charTarPath) == -1)
			return -1;
	}
	return 0;
}

int twrpTar::closeTar() {
	flush_libtar_buffer(t->fd);
	if (tar_append_eof(t) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("tar_append_eof(): %s\n", strerror(errno));
#endif
		tar_close(t);
		return -1;
	}
	if (tar_close(t) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGERR("Unable to close tar archive: '%s'\n", tarfn.c_str());
#endif
		return -1;
	}
	if (Archive_Current_Type > 0) {
		close(fd);
		int status;
		if (pigz_pid > 0 && TWFunc::Wait_For_Child(pigz_pid, &status, "pigz") != 0)
			return -1;
		if (oaes_pid > 0 && TWFunc::Wait_For_Child(oaes_pid, &status, "openaes") != 0)
			return -1;
	}
	free_libtar_buffer();
	return 0;
}

int twrpTar::removeEOT(string tarFile) {
	char* charTarFile = (char*) tarFile.c_str();
	off_t tarFileEnd = 0;
	while (th_read(t) == 0) {
		if (TH_ISREG(t))
			tar_skip_regfile(t);
		tarFileEnd = lseek(t->fd, 0, SEEK_CUR);
	}
	if (tar_close(t) == -1)
		return -1;
	if (truncate(charTarFile, tarFileEnd) == -1)
		return -1;
	return 0;
}

int twrpTar::entryExists(string entry) {
#ifdef TAR_DEBUG_VERBOSE
	LOGINFO("Searching archive for entry: '%s'\n", entry.c_str());
#endif
	char* searchstr = (char*)entry.c_str();
	int ret;

	Archive_Current_Type = TWFunc::Get_File_Type(tarfn);
	if (openTar() == -1)
		ret = 0;
	else
		ret = tar_find(t, searchstr);

#ifdef TAR_DEBUG_VERBOSE
	if (ret)
		LOGINFO("Found matching entry.\n");
	else
		LOGINFO("No matching entry found.\n");
#endif

	if (tar_close(t) != 0) {
#ifdef TAR_DEBUG_VERBOSE
		LOGINFO("Unable to close tar file after searching for entry '%s'.\n", entry.c_str());
#endif
	}
	if (Archive_Current_Type > 0) {
		close(fd);
	}

	return ret;
}

int twrpTar::skip(char* name, char* type) {
	if (Excluded.size() > 0) {
		unsigned i;
		for (i = 0; i < Excluded.size(); i++) {
			if (strcmp(name, Excluded[i].c_str()) == 0) {
#ifndef TAR_DEBUG_SUPPRESS
				LOGINFO("Excluding %s: '%s'\n", (type == NULL ? "" : type), name);
#endif
				return 1;
			}
		}
	}
	return 0;
}

unsigned long long twrpTar::uncompressedSize() {
	int type = 0;
        unsigned long long total_size = 0;
	string Tar, Command, result;
	vector<string> split;

	Tar = TWFunc::Get_Filename(tarfn);
	type = TWFunc::Get_File_Type(tarfn);
	if (type == 0)
		total_size = TWFunc::Get_File_Size(tarfn);
	else {
		Command = "pigz -l " + tarfn;
		/* if we set Command = "pigz -l " + tarfn + " | sed '1d' | cut -f5 -d' '";
		   we get the uncompressed size at once. */
		TWFunc::Exec_Cmd(Command, result);
		if (!result.empty()) {
			/* Expected output:
				compressed   original reduced  name
				  95855838  179403776   -1.3%  data.yaffs2.win
						^
					     split[5]
			*/
			split = TWFunc::split_string(result, ' ', true);
			if (split.size() > 4)
				total_size = atoi(split[5].c_str());
		}
	}
	LOGINFO("%s's uncompressed size: %llu bytes\n", Tar.c_str(), total_size);

	return total_size;
}

extern "C" ssize_t write_tar(int fd, const void *buffer, size_t size) {
	return (ssize_t) write_libtar_buffer(fd, buffer, size);
}
