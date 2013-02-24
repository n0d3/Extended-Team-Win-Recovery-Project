/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <map>
#include <fstream>
#include <sstream>

#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "gui/blanktimer.hpp"

#ifdef TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
	#include "cutils/properties.h"
#endif

extern "C"
{
	#include "common.h"
	#include "data.h"
	#include "gui/pages.h"
	#include "mtdutils/mtdutils.h"

	void gui_notifyVarChange(const char *name, const char* value);
}

#define FILE_VERSION	0x00010001

using namespace std;

map<string, DataManager::TStrIntPair>   DataManager::mValues;
map<string, string>			DataManager::mConstValues;
string					DataManager::mBackingFile;
int					DataManager::mInitialized = 0;
int                                     DataManager::SettingsFileRead = 0;
int                                     DataManager::BLDR = -1;
int                                     DataManager::pause = -1;

extern blanktimer blankTimer;

// Device ID functions
void DataManager::sanitize_device_id(char* device_id) {
	const char* whitelist ="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-._";
	char str[50];
	char* c = str;

	strcpy(str, device_id);
	memset(device_id, 0, sizeof(device_id));
	while (*c) {
		if (strchr(whitelist, *c))
			strncat(device_id, c, 1);
		c++;
	}
	return;
}

#define CMDLINE_SERIALNO	"androidboot.serialno="
#define CMDLINE_SERIALNO_LEN	(strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO	"Serial"
#define CPUINFO_SERIALNO_LEN	(strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE	"Hardware"
#define CPUINFO_HARDWARE_LEN	(strlen(CPUINFO_HARDWARE))
#define TW_FORCE_CPUINFO_FOR_DEVICE_ID
void DataManager::get_device_id(void) {
	FILE *fp;
	char line[2048];
	char hardware_id[32], device_id[64];
	char* token;

	// Assign a blank device_id to start with
	device_id[0] = 0;

#ifndef TW_FORCE_CPUINFO_FOR_DEVICE_ID
	#ifdef TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
	// Now we'll use product_model_hardwareid as device id
	char model_id[PROPERTY_VALUE_MAX];
	property_get("ro.product.model", model_id, "error");
	if (strcmp(model_id,"error") != 0) {
		LOGI("=> product model: '%s'\n", model_id);
		// Replace spaces with underscores
		for(int i = 0; i < strlen(model_id); i++) {
			if(model_id[i] == ' ')
			model_id[i] = '_';
		}
		strcpy(device_id, model_id);
		if (hardware_id[0] != 0) {
			strcat(device_id, "_");
			strcat(device_id, hardware_id);
		}
		sanitize_device_id((char *)device_id);
		mConstValues.insert(make_pair("device_id", device_id));
		LOGI("=> using device id: '%s'\n", device_id);
		return;
	}
	#endif
	// Try the cmdline to see if the serial number was supplied
	fp = fopen("/proc/cmdline", "rt");
	if (fp != NULL) {
		// First step, read the line. For cmdline, it's one long line
		fgets(line, sizeof(line), fp);
		fclose(fp);

		// Now, let's tokenize the string
		token = strtok(line, " ");

		// Let's walk through the line, looking for the CMDLINE_SERIALNO token
		while (token)
		{
			// We don't need to verify the length of token, because if it's too short, it will mismatch CMDLINE_SERIALNO at the NULL
			if (memcmp(token, CMDLINE_SERIALNO, CMDLINE_SERIALNO_LEN) == 0)
			{
				// We found the serial number!
				strcpy(device_id, token + CMDLINE_SERIALNO_LEN);
				sanitize_device_id((char *)device_id);
				mConstValues.insert(make_pair("device_id", device_id));
				return;
			}
			token = strtok(NULL, " ");
		}
	}
#endif
	// Now we'll try cpuinfo for a serial number
	fp = fopen("/proc/cpuinfo", "rt");
	if (fp != NULL)	{
		while (fgets(line, sizeof(line), fp) != NULL) { // First step, read the line.
			if (memcmp(line, CPUINFO_SERIALNO, CPUINFO_SERIALNO_LEN) == 0)  // check the beginning of the line for "Serial"
			{
				// We found the serial number!
				token = line + CPUINFO_SERIALNO_LEN; // skip past "Serial"
				while ((*token > 0 && *token <= 32 ) || *token == ':') token++; // skip over all spaces and the colon
				if (*token != 0) {
					token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
						memset(device_id, 0, sizeof(device_id));
						strncpy(device_id, token, strlen(token) - 1);
					} else {
						strcpy(device_id, token);
					}
					LOGI("=> Serial from cpuinfo: '%s'\n", device_id);
				}
			} else 
			// We're also going to look for the hardware line in cpuinfo and save it for later in case we don't find the device ID
			if (memcmp(line, CPUINFO_HARDWARE, CPUINFO_HARDWARE_LEN) == 0) {
				// We found the hardware ID
				token = line + CPUINFO_HARDWARE_LEN; // skip past "Hardware"
				while ((*token > 0 && *token <= 32 ) || *token == ':')  token++; // skip over all spaces and the colon
				if (*token != 0) {
					token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
						memset(hardware_id, 0, sizeof(hardware_id));
						strncpy(hardware_id, token, strlen(token) - 1);
					} else {
						strcpy(hardware_id, token);
					}
					LOGI("=> Hardware ID from cpuinfo: '%s'\n", hardware_id);
				}
			}
		}
		fclose(fp);
		}

	if (hardware_id[0] != 0) {
		LOGW("=> Using Hardware ID for Device ID: '%s'\n", hardware_id);
		strcpy(device_id, hardware_id);
		sanitize_device_id((char *)device_id);
		mConstValues.insert(make_pair("tw_device_id", device_id));
		return;
	}
	if (strcmp(device_id, "0000000000000000") == 0) {
		strcpy(device_id, "UDev");
		LOGE("=> Device ID is all zeros, using '%s'.", device_id);
		mConstValues.insert(make_pair("tw_device_id", device_id));
		return;
	}
	LOGW("=> Using Serial for Device ID: '%s'\n", device_id);
	sanitize_device_id((char *)device_id);
	mConstValues.insert(make_pair("tw_device_id", device_id));
	return;
}

#define OFFMODE_CHARGING	"androidboot.mode=offmode_charging"
#define OFFMODE_CHARGING_LEN	(strlen(OFFMODE_CHARGING))
int DataManager::Pause_For_Battery_Charge() {
	if (pause == -1) {
		FILE *fp = fopen("/proc/cmdline", "rt");
		if (fp != NULL) {
			pause = 0;
			char line[2048];
			fgets(line, sizeof(line), fp);
			fclose(fp);
			char* token = strtok(line, " ");
			while (token) {
				if (memcmp(token, OFFMODE_CHARGING, OFFMODE_CHARGING_LEN) == 0)	{	
					pause = 1;
					break;
				}
				token = strtok(NULL, " ");
			}
		}
	}
	return pause;
}

int DataManager::Detect_BLDR() {
	if (BLDR == -1) {
		string cmd = "grep -Fxq \"clk=\" /proc/cmdline";

		if (system(cmd.c_str()) == 0) {
			LOGI("=> Detected bootloader: cLK\n");
			BLDR = 1;
		} else {
			string cmd = "grep -Fxq \"rel_path=\" /proc/cmdline";
			if (system(cmd.c_str()) == 0) {
				LOGI("=> Detected bootloader: HARET\n");
				BLDR = 0;
			} else {
				LOGI("=> Detected bootloader: MAGLDR\n");
				BLDR = 2;
			}
		}
	}
		
	return BLDR;
}

void DataManager::get_boot_partitions(void) {
	int mtd_num = mtd_scan_partitions();
	LOGI("=> Scanned mtd partitions : %i\n", mtd_num);
	if (mtd_num > 7) {
		const MtdPartition* mtd1 = mtd_find_partition_by_name("sboot");
		if (mtd1 != NULL)
			SetValue("tw_has_sboot_ptn", 1);

		if (mtd_num > 8) {
			const MtdPartition* mtd2 = mtd_find_partition_by_name("tboot");
			if (mtd2 != NULL)
				SetValue("tw_has_tboot_ptn", 1);

			if (mtd_num > 9) {
				const MtdPartition* mtd3 = mtd_find_partition_by_name("vboot");
				if (mtd3 != NULL)
					SetValue("tw_has_vboot_ptn", 1);

				if (mtd_num > 10) {
					const MtdPartition* mtd4 = mtd_find_partition_by_name("wboot");
					if (mtd4 != NULL)
						SetValue("tw_has_wboot_ptn", 1);

					if (mtd_num > 11) {
						const MtdPartition* mtd5 = mtd_find_partition_by_name("xboot");
						if (mtd5 != NULL)
							SetValue("tw_has_xboot_ptn", 1);

						if (mtd_num > 12) {
							const MtdPartition* mtd6 = mtd_find_partition_by_name("yboot");
							if (mtd6 != NULL)
								SetValue("tw_has_yboot_ptn", 1);

							if (mtd_num > 13) {
								const MtdPartition* mtd7 = mtd_find_partition_by_name("zboot");
								if (mtd7 != NULL)
									SetValue("tw_has_zboot_ptn", 1);
							}
						}
					}
				}
			}
		}
	}
	return;
}

// TODO: Move this to partition.cpp
int DataManager::Wipe_MTD_By_Name(string ptnName) {
	ui_print("MTD Formatting \"%s\"\n", ptnName.c_str());

    	mtd_scan_partitions();
    	const MtdPartition* mtd = mtd_find_partition_by_name(ptnName.c_str());
    	if (mtd == NULL) {
        	LOGE("No mtd partition named '%s'", ptnName.c_str());
        	return false;
    	}

	string eraseimg = "erase_image " + ptnName;
	if (system(eraseimg.c_str()) != 0) {
		LOGE("Failed to format '%s'", ptnName.c_str());
		return false;
	}
	ui_print("Done.\n");
    	return true;
}

int DataManager::ResetDefaults() {
	string rm;
	if (PartitionManager.Mount_Settings_Storage(false)) {
		rm = "rm -f " + GetCurrentStoragePath() + "/TWRP/.twrps &> /dev/null";
		system(rm.c_str());
	}
	rm = "rm -f /cache/recovery/.version &> /dev/null";
	system(rm.c_str());

	mValues.clear();
	mConstValues.clear();
	SetDefaultValues();
	return 0;
}

int DataManager::LoadValues(const string filename) {
	if (!mInitialized)
		SetDefaultValues();

	// Save off the backing file for set operations
	mBackingFile = filename;

	// Read in the file, if possible
	FILE* in = fopen(filename.c_str(), "rb");
	if (!in)	return 0;

	int file_version;
	if (fread(&file_version, 1, sizeof(int), in) != sizeof(int))	goto error;
	if (file_version != FILE_VERSION)							   goto error;

	while (!feof(in))
	{
		string Name;
		string Value;
		unsigned short length;
		char array[512];

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)								goto error;
		if (fread(array, 1, length, in) != length)					goto error;
		Name = array;

		if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))	goto error;
		if (length >= 512)								goto error;
		if (fread(array, 1, length, in) != length)					goto error;
		Value = array;

		map<string, TStrIntPair>::iterator pos;

		pos = mValues.find(Name);
		if (pos != mValues.end())
		{
			pos->second.first = Value;
			pos->second.second = 1;
		}
		else
			mValues.insert(TNameValuePair(Name, TStrIntPair(Value, 1)));
	}
	fclose(in);
	SetBackupFolder(GetCurrentStoragePath());
	return 0;

error:
	// File version mismatch. Use defaults.
	fclose(in);
	SetBackupFolder(GetCurrentStoragePath());
	return -1;
}

int DataManager::Flush()
{
	return SaveValues();
}

int DataManager::SaveValues()
{
	if (mBackingFile.empty())
		return -1;

	string mount_path = GetSettingsStoragePath();
	PartitionManager.Mount_By_Path(mount_path.c_str(), 1);

	FILE* out = fopen(mBackingFile.c_str(), "wb");
	if (!out)
		return -1;

	int file_version = FILE_VERSION;
	fwrite(&file_version, 1, sizeof(int), out);

	map<string, TStrIntPair>::iterator iter;
	for (iter = mValues.begin(); iter != mValues.end(); ++iter)
	{
		// Save only the persisted data
		if (iter->second.second != 0)
		{
			unsigned short length = (unsigned short) iter->first.length() + 1;
			fwrite(&length, 1, sizeof(unsigned short), out);
			fwrite(iter->first.c_str(), 1, length, out);
			length = (unsigned short) iter->second.first.length() + 1;
			fwrite(&length, 1, sizeof(unsigned short), out);
			fwrite(iter->second.first.c_str(), 1, length, out);
		}
	}
	fclose(out);
	return 0;
}

int DataManager::GetValue(const string varName, string& value)
{
	string localStr = varName;

	if (!mInitialized)
		SetDefaultValues();

	// Strip off leading and trailing '%' if provided
	if (localStr.length() > 2 && localStr[0] == '%' && localStr[localStr.length()-1] == '%')
	{
		localStr.erase(0, 1);
		localStr.erase(localStr.length() - 1, 1);
	}

	// Handle magic values

	map<string, string>::iterator constPos;
	constPos = mConstValues.find(localStr);
	if (constPos != mConstValues.end())
	{
		value = constPos->second;
		return 0;
	}

	map<string, TStrIntPair>::iterator pos;
	pos = mValues.find(localStr);
	if (pos == mValues.end())
		return -1;

	value = pos->second.first;
	return 0;
}

int DataManager::GetValue(const string varName, int& value)
{
	string data;

	if (GetValue(varName,data) != 0)
		return -1;

	value = atoi(data.c_str());
	return 0;
}

unsigned long long DataManager::GetValue(const string varName, unsigned long long& value)
{
    string data;

    if (GetValue(varName,data) != 0)
        return -1;

    value = strtoull(data.c_str(), NULL, 10);
    return 0;
}

// This is a dangerous function. It will create the value if it doesn't exist so it has a valid c_str
string& DataManager::GetValueRef(const string varName)
{
	if (!mInitialized)
		SetDefaultValues();

	map<string, string>::iterator constPos;
	constPos = mConstValues.find(varName);
	if (constPos != mConstValues.end())
		return constPos->second;

	map<string, TStrIntPair>::iterator pos;
	pos = mValues.find(varName);
	if (pos == mValues.end())
		pos = (mValues.insert(TNameValuePair(varName, TStrIntPair("", 0)))).first;

	return pos->second.first;
}

// This function will return an empty string if the value doesn't exist
string DataManager::GetStrValue(const string varName)
{
	string retVal;

	GetValue(varName, retVal);
	return retVal;
}

// This function will return 0 if the value doesn't exist
int DataManager::GetIntValue(const string varName)
{
	string retVal;

	GetValue(varName, retVal);
	return atoi(retVal.c_str());
}

int DataManager::SetValue(const string varName, string value, int persist /* = 0 */)
{
	if (!mInitialized)
		SetDefaultValues();

	// Don't allow empty values or numerical starting values
	if (varName.empty() || (varName[0] >= '0' && varName[0] <= '9'))
		return -1;

	map<string, string>::iterator constChk;
	constChk = mConstValues.find(varName);
	if (constChk != mConstValues.end())
		return -1;

	map<string, TStrIntPair>::iterator pos;
	pos = mValues.find(varName);
	if (pos == mValues.end())
		pos = (mValues.insert(TNameValuePair(varName, TStrIntPair(value, persist)))).first;
	else
		pos->second.first = value;

	if (pos->second.second != 0)
		SaveValues();
	if (varName == "tw_screen_timeout_secs") {
		if (pause == 0) blankTimer.setTime(atoi(value.c_str()));
	} else
		gui_notifyVarChange(varName.c_str(), value.c_str());
	return 0;
}

int DataManager::SetValue(const string varName, int value, int persist /* = 0 */)
{
	ostringstream valStr;
	valStr << value;
	if (varName == "tw_use_external_storage") {
		string str;

		if (GetIntValue(TW_HAS_DUAL_STORAGE) == 1) {
			if (value == 0) {
				str = GetStrValue(TW_INTERNAL_PATH);
			} else {
				str = GetStrValue(TW_EXTERNAL_PATH);
			}
		} else if (GetIntValue(TW_HAS_INTERNAL) == 1)
			str = GetStrValue(TW_INTERNAL_PATH);
		else
			str = GetStrValue(TW_EXTERNAL_PATH);

		SetBackupFolder(str);
	}
	return SetValue(varName, valStr.str(), persist);;
}

int DataManager::SetValue(const string varName, float value, int persist /* = 0 */)
{
	ostringstream valStr;
	valStr << value;
	return SetValue(varName, valStr.str(), persist);;
}

int DataManager::SetValue(const string varName, unsigned long long value, int persist /* = 0 */)
{
	ostringstream valStr;
    valStr << value;
    return SetValue(varName, valStr.str(), persist);;
}

void DataManager::DumpValues()
{
	map<string, TStrIntPair>::iterator iter;
	printf("\nData Manager dump (values with leading X are persisted):\n");
	for (iter = mValues.begin(); iter != mValues.end(); ++iter) {
		printf("%c %s=%s\n", iter->second.second ? 'X' : ' ', iter->first.c_str(), iter->second.first.c_str());
	}
	map<string, string>::iterator it;
    	for (it = mConstValues.begin(); it != mConstValues.end(); ++it) {
        	printf("X %s=%s\n", it->first.c_str(), it->second.c_str());
    	}
	printf("\n");
}

void DataManager::update_tz_environment_variables(void) {
	setenv("TZ", DataManager_GetStrValue(TW_TIME_ZONE_VAR), 1);
	tzset();
}

void DataManager::SetBackupFolder(string storage_path) {
	string backup_path, nativesdbackup_path, dev_id;

	if (storage_path.empty())
		backup_path = nativesdbackup_path = GetCurrentStoragePath();
	else
		backup_path = nativesdbackup_path = storage_path;

	GetValue("tw_device_id", dev_id);

	backup_path += ("/TWRP/BACKUPS/" + dev_id);
	nativesdbackup_path += ("/TWRP/NativeSD-BACKUPS/" + dev_id);
	SetValue(TW_BACKUPS_FOLDER_VAR, backup_path, 0);
	SetValue(TW_SDBACKUPS_FOLDER_VAR, nativesdbackup_path, 0);
}

void DataManager::SetAdditionalFolders(string storage_path) {
	string theme_path, scripts_path, app_path;

	if (storage_path.empty())
		theme_path = scripts_path = app_path = GetCurrentStoragePath();
	else
		theme_path = scripts_path = app_path = storage_path;

	theme_path += "/TWRP/theme";
	scripts_path += "/TWRP/scripts";
	app_path += "/TWRP/app";
	SetValue(TW_THEME_FOLDER_VAR, theme_path, 0);
	SetValue(TW_SCRIPTS_FOLDER_VAR, scripts_path, 0);
	SetValue(TW_APP_FOLDER_VAR, app_path, 0);
}

void DataManager::SetupTwrpFolder() {	
	if (!PartitionManager.Mount_Settings_Storage(false)) {
		usleep(500000);
		if (!PartitionManager.Mount_Settings_Storage(false))
			LOGI("Unable to mount %s when trying to setup TWRP folder.\n", DataManager_GetSettingsStorageMount());
	}

	if(GetStrValue(TW_BACKUPS_FOLDER_VAR).size() < 2
	|| GetStrValue(TW_SDBACKUPS_FOLDER_VAR).size() < 2)
		SetBackupFolder(GetCurrentStoragePath());
	if(GetStrValue(TW_THEME_FOLDER_VAR).size() < 2
	|| GetStrValue(TW_SCRIPTS_FOLDER_VAR).size() < 2
	|| GetStrValue(TW_APP_FOLDER_VAR).size() < 2)
		SetAdditionalFolders(GetCurrentStoragePath());

	string backup_path, nativesdbackup_path, theme_path, scripts_path, app_path;
	GetValue(TW_BACKUPS_FOLDER_VAR, backup_path);
	backup_path += "/";
	GetValue(TW_SDBACKUPS_FOLDER_VAR, nativesdbackup_path);
	nativesdbackup_path += "/";
	GetValue(TW_THEME_FOLDER_VAR, theme_path);
	theme_path += "/";
	GetValue(TW_SCRIPTS_FOLDER_VAR, scripts_path);
	scripts_path += "/";
	GetValue(TW_APP_FOLDER_VAR, app_path);
	app_path += "/";

	if (!TWFunc::Path_Exists(backup_path)) {
		if (!TWFunc::Recursive_Mkdir(backup_path))
			LOGI("Could not create '%s'\n", backup_path.c_str());
	}
	if (!TWFunc::Path_Exists(nativesdbackup_path)) {
		if (!TWFunc::Recursive_Mkdir(nativesdbackup_path))
			LOGI("Could not create '%s'\n", nativesdbackup_path.c_str());
	}
	if (!TWFunc::Path_Exists(theme_path)) {
		if (!TWFunc::Recursive_Mkdir(theme_path))
			LOGI("Could not create '%s'\n", theme_path.c_str());
	}
	if (!TWFunc::Path_Exists(scripts_path)) {
		if (!TWFunc::Recursive_Mkdir(scripts_path))
			LOGI("Could not create '%s'\n", scripts_path.c_str());
	}
	if (!TWFunc::Path_Exists(app_path)) {
		if (!TWFunc::Recursive_Mkdir(app_path))
			LOGI("Could not create '%s'\n", app_path.c_str());
	}
	LOGI("TWRP's folders created on /sdcard.\n");
}

void DataManager::SetDefaultValues()
{
	string str, path;

	get_device_id();
	
	mInitialized = 1;

	mConstValues.insert(make_pair("true", "1"));
	mConstValues.insert(make_pair("false", "0"));

	mConstValues.insert(make_pair(TW_VERSION_VAR, TW_VERSION_STR));
// Extended-start
	mValues.insert(make_pair(TW_SKIP_DALVIK, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SKIP_NATIVESD, make_pair("0", 1)));

	mValues.insert(make_pair(TW_BACKUP_SDEXT2_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SDEXT2_SIZE, make_pair("0", 1)));
    	mValues.insert(make_pair(TW_SDEXT2_SIZE, make_pair("0", 1)));
    	mValues.insert(make_pair(TW_RESTORE_SDEXT2_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_HAS_SDEXT2_PARTITION, make_pair("0", 1)));
    	mValues.insert(make_pair(TW_USE_SDEXT2_PARTITION, make_pair("0", 1)));
    	mValues.insert(make_pair(TW_SDPART2_FILE_SYSTEM, make_pair("ext4", 1)));

    	mValues.insert(make_pair(TW_SCREENSHOT_VAR, make_pair("0", 1)));

	mValues.insert(make_pair(TW_SD_BACKUP_RESTORE_SYSTEM, make_pair("1", 1)));
    	mValues.insert(make_pair(TW_SD_BACKUP_RESTORE_DATA, make_pair("1", 1)));
    	mValues.insert(make_pair(TW_SD_BACKUP_RESTORE_BOOT, make_pair("1", 1)));

	// Bootloader check
	if (Detect_BLDR() == 1) // cLK detected
		get_boot_partitions();

	mValues.insert(make_pair(TW_SEL_THEME_PATH, make_pair("", 1)));
	mValues.insert(make_pair(TW_SDBOOT_PARTITION, make_pair("", 1)));
	mValues.insert(make_pair(TW_BACKUP_NAND_DATA, make_pair("0", 1)));
    	mValues.insert(make_pair(TW_DATA_PATH, make_pair("/sd-ext", 1)));
	mValues.insert(make_pair(TW_DATA_ON_EXT, make_pair("0", 1)));
	mValues.insert(make_pair(TW_DATA_ON_EXT_CHECK, make_pair("0", 1)));
	mValues.insert(make_pair(TW_RESTORE_IS_DATAONEXT, make_pair("0", 1)));

	mValues.insert(make_pair(TW_NUM_OF_MOUNTS_FOR_FS_CHK, make_pair("10", 1)));
	mValues.insert(make_pair(TW_INCR_SIZE, make_pair("40", 1)));

	mValues.insert(make_pair(TW_RESCUE_EXT_CONTENTS, make_pair("0", 1)));
	mValues.insert(make_pair(TW_HANDLE_RESTORE_SIZE, make_pair("0", 1)));
// Extended-End

#ifdef TW_INCLUDE_MKNTFS
	mConstValues.insert(make_pair(TW_SHOW_NTFS, "1"));
#else
	mConstValues.insert(make_pair(TW_SHOW_NTFS, "0"));
#endif

#ifdef TW_INCLUDE_EXFAT
	mConstValues.insert(make_pair(TW_SHOW_EXFAT, "1"));
#else
	mConstValues.insert(make_pair(TW_SHOW_EXFAT, "0"));
#endif

#ifdef BOARD_HAS_NO_REAL_SDCARD
	mConstValues.insert(make_pair(TW_ALLOW_PARTITION_SDCARD, "0"));
#else
	mConstValues.insert(make_pair(TW_ALLOW_PARTITION_SDCARD, "1"));
#endif

#ifdef TW_INCLUDE_DUMLOCK
	mConstValues.insert(make_pair(TW_SHOW_DUMLOCK, "1"));
#else
	mConstValues.insert(make_pair(TW_SHOW_DUMLOCK, "0"));
#endif

#ifdef TW_INTERNAL_STORAGE_PATH
	LOGI("=> Internal path defined: '%s'\n", EXPAND(TW_INTERNAL_STORAGE_PATH));
	mValues.insert(make_pair(TW_USE_EXTERNAL_STORAGE, make_pair("0", 1)));
	mConstValues.insert(make_pair(TW_HAS_INTERNAL, "1"));
	mValues.insert(make_pair(TW_INTERNAL_PATH, make_pair(EXPAND(TW_INTERNAL_STORAGE_PATH), 0)));
	mConstValues.insert(make_pair(TW_INTERNAL_LABEL, EXPAND(TW_INTERNAL_STORAGE_MOUNT_POINT)));
	path.clear();
	path = "/";
	path += EXPAND(TW_INTERNAL_STORAGE_MOUNT_POINT);
	mConstValues.insert(make_pair(TW_INTERNAL_MOUNT, path));
	#ifdef TW_EXTERNAL_STORAGE_PATH
		LOGI("=> External path defined: '%s'\n", EXPAND(TW_EXTERNAL_STORAGE_PATH));
		// Device has dual storage
		mConstValues.insert(make_pair(TW_HAS_DUAL_STORAGE, "1"));
		mConstValues.insert(make_pair(TW_HAS_EXTERNAL, "1"));
		mConstValues.insert(make_pair(TW_EXTERNAL_PATH, EXPAND(TW_EXTERNAL_STORAGE_PATH)));
		mConstValues.insert(make_pair(TW_EXTERNAL_LABEL, EXPAND(TW_EXTERNAL_STORAGE_MOUNT_POINT)));
		mValues.insert(make_pair(TW_ZIP_EXTERNAL_VAR, make_pair(EXPAND(TW_EXTERNAL_STORAGE_PATH), 1)));
		path.clear();
		path = "/";
		path += EXPAND(TW_EXTERNAL_STORAGE_MOUNT_POINT);
		mConstValues.insert(make_pair(TW_EXTERNAL_MOUNT, path));
		if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
			mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/emmc", 1)));
		} else {
			mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/sdcard", 1)));
		}
	#else
		LOGI("=> Just has internal storage.\n");
		// Just has internal storage
		mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/sdcard", 1)));
		mConstValues.insert(make_pair(TW_HAS_DUAL_STORAGE, "0"));
		mConstValues.insert(make_pair(TW_HAS_EXTERNAL, "0"));
		mConstValues.insert(make_pair(TW_EXTERNAL_PATH, "0"));
		mConstValues.insert(make_pair(TW_EXTERNAL_MOUNT, "0"));
		mConstValues.insert(make_pair(TW_EXTERNAL_LABEL, "0"));
	#endif
#else
	#ifdef RECOVERY_SDCARD_ON_DATA
		#ifdef TW_EXTERNAL_STORAGE_PATH
			LOGI("=> Has /data/media + external storage in '%s'\n", EXPAND(TW_EXTERNAL_STORAGE_PATH));
			// Device has /data/media + external storage
			mConstValues.insert(make_pair(TW_HAS_DUAL_STORAGE, "1"));
		#else
			LOGI("=> Single storage only -- data/media.\n");
			// Device just has external storage
			mConstValues.insert(make_pair(TW_HAS_DUAL_STORAGE, "0"));
			mConstValues.insert(make_pair(TW_HAS_EXTERNAL, "0"));
		#endif
	#else
		LOGI("=> Single storage only.\n");
		// Device just has external storage
		mConstValues.insert(make_pair(TW_HAS_DUAL_STORAGE, "0"));
	#endif
	#ifdef RECOVERY_SDCARD_ON_DATA
		LOGI("=> Device has /data/media defined.\n");
		// Device has /data/media
		mConstValues.insert(make_pair(TW_USE_EXTERNAL_STORAGE, "0"));
		mConstValues.insert(make_pair(TW_HAS_INTERNAL, "1"));
		mValues.insert(make_pair(TW_INTERNAL_PATH, make_pair("/data/media", 0)));
		mConstValues.insert(make_pair(TW_INTERNAL_MOUNT, "/data"));
		mConstValues.insert(make_pair(TW_INTERNAL_LABEL, "data"));
		#ifdef TW_EXTERNAL_STORAGE_PATH
			if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
				mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/emmc", 1)));
			} else {
				mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/sdcard", 1)));
			}
		#else
			mValues.insert(make_pair(TW_ZIP_INTERNAL_VAR, make_pair("/sdcard", 1)));
		#endif
	#else
		LOGI("=> No internal storage defined.\n");
		// Device has no internal storage
		mConstValues.insert(make_pair(TW_USE_EXTERNAL_STORAGE, "1"));
		mConstValues.insert(make_pair(TW_HAS_INTERNAL, "0"));
		mValues.insert(make_pair(TW_INTERNAL_PATH, make_pair("0", 0)));
		mConstValues.insert(make_pair(TW_INTERNAL_MOUNT, "0"));
		mConstValues.insert(make_pair(TW_INTERNAL_LABEL, "0"));
	#endif
	#ifdef TW_EXTERNAL_STORAGE_PATH
		LOGI("=> Only external path defined: '%s'\n", EXPAND(TW_EXTERNAL_STORAGE_PATH));
		// External has custom definition
		mConstValues.insert(make_pair(TW_HAS_EXTERNAL, "1"));
		mConstValues.insert(make_pair(TW_EXTERNAL_PATH, EXPAND(TW_EXTERNAL_STORAGE_PATH)));
		mConstValues.insert(make_pair(TW_EXTERNAL_LABEL, EXPAND(TW_EXTERNAL_STORAGE_MOUNT_POINT)));
		mValues.insert(make_pair(TW_ZIP_EXTERNAL_VAR, make_pair(EXPAND(TW_EXTERNAL_STORAGE_PATH), 1)));
		path.clear();
		path = "/";
		path += EXPAND(TW_EXTERNAL_STORAGE_MOUNT_POINT);
		mConstValues.insert(make_pair(TW_EXTERNAL_MOUNT, path));
	#else
		#ifndef RECOVERY_SDCARD_ON_DATA
			LOGI("=> No storage defined, defaulting to /sdcard.\n");
			// Standard external definition
			mConstValues.insert(make_pair(TW_HAS_EXTERNAL, "1"));
			mConstValues.insert(make_pair(TW_EXTERNAL_PATH, "/sdcard"));
			mConstValues.insert(make_pair(TW_EXTERNAL_MOUNT, "/sdcard"));
			mConstValues.insert(make_pair(TW_EXTERNAL_LABEL, "sdcard"));
			mValues.insert(make_pair(TW_ZIP_EXTERNAL_VAR, make_pair("/sdcard", 1)));
		#endif
	#endif
#endif

#ifdef TW_DEFAULT_EXTERNAL_STORAGE
	SetValue(TW_USE_EXTERNAL_STORAGE, 1);
	LOGI("=> Defaulting to external storage.\n");
#endif

#ifdef RECOVERY_SDCARD_ON_DATA
	if (PartitionManager.Mount_By_Path("/data", false) && TWFunc::Path_Exists("/data/media/0"))
		SetValue(TW_INTERNAL_PATH, "/data/media/0");
#endif
	str = GetCurrentStoragePath();
#ifdef RECOVERY_SDCARD_ON_DATA
	#ifndef TW_EXTERNAL_STORAGE_PATH
		SetValue(TW_ZIP_LOCATION_VAR, "/sdcard", 1);
	#else
		if (strcmp(EXPAND(TW_EXTERNAL_STORAGE_PATH), "/sdcard") == 0) {
			SetValue(TW_ZIP_LOCATION_VAR, "/emmc", 1);
		} else {
			SetValue(TW_ZIP_LOCATION_VAR, "/sdcard", 1);
		}
	#endif
#else
	SetValue(TW_ZIP_LOCATION_VAR, str.c_str(), 1);
#endif

#ifdef SP1_DISPLAY_NAME
	if (strlen(EXPAND(SP1_DISPLAY_NAME)))	mConstValues.insert(make_pair(TW_SP1_PARTITION_NAME_VAR, EXPAND(SP1_DISPLAY_NAME)));
#else
	#ifdef SP1_NAME
		if (strlen(EXPAND(SP1_NAME)))	mConstValues.insert(make_pair(TW_SP1_PARTITION_NAME_VAR, EXPAND(SP1_NAME)));
	#endif
#endif
#ifdef SP2_DISPLAY_NAME
	if (strlen(EXPAND(SP2_DISPLAY_NAME)))	mConstValues.insert(make_pair(TW_SP2_PARTITION_NAME_VAR, EXPAND(SP2_DISPLAY_NAME)));
#else
	#ifdef SP2_NAME
		if (strlen(EXPAND(SP2_NAME)))	mConstValues.insert(make_pair(TW_SP2_PARTITION_NAME_VAR, EXPAND(SP2_NAME)));
	#endif
#endif
#ifdef SP3_DISPLAY_NAME
	if (strlen(EXPAND(SP3_DISPLAY_NAME)))	mConstValues.insert(make_pair(TW_SP3_PARTITION_NAME_VAR, EXPAND(SP3_DISPLAY_NAME)));
#else
	#ifdef SP3_NAME
		if (strlen(EXPAND(SP3_NAME)))	mConstValues.insert(make_pair(TW_SP3_PARTITION_NAME_VAR, EXPAND(SP3_NAME)));
	#endif
#endif

	mConstValues.insert(make_pair(TW_REBOOT_SYSTEM, "1"));
#ifdef TW_NO_REBOOT_RECOVERY
	mConstValues.insert(make_pair(TW_REBOOT_RECOVERY, "0"));
#else
	mConstValues.insert(make_pair(TW_REBOOT_RECOVERY, "1"));
#endif
	mConstValues.insert(make_pair(TW_REBOOT_POWEROFF, "1"));
#ifdef TW_NO_REBOOT_BOOTLOADER
	mConstValues.insert(make_pair(TW_REBOOT_BOOTLOADER, "0"));
#else
	mConstValues.insert(make_pair(TW_REBOOT_BOOTLOADER, "1"));
#endif
#ifdef RECOVERY_SDCARD_ON_DATA
	mConstValues.insert(make_pair(TW_HAS_DATA_MEDIA, "1"));
#else
	mConstValues.insert(make_pair(TW_HAS_DATA_MEDIA, "0"));
#endif
#ifdef TW_NO_BATT_PERCENT
	mConstValues.insert(make_pair(TW_NO_BATTERY_PERCENT, "1"));
#else
	mConstValues.insert(make_pair(TW_NO_BATTERY_PERCENT, "0"));
#endif
#ifdef TW_CUSTOM_POWER_BUTTON
	mConstValues.insert(make_pair(TW_POWER_BUTTON, EXPAND(TW_CUSTOM_POWER_BUTTON)));
#else
	mConstValues.insert(make_pair(TW_POWER_BUTTON, "0"));
#endif
#ifdef TW_ALWAYS_RMRF
	mConstValues.insert(make_pair(TW_RM_RF_VAR, "1"));
#endif
#ifdef TW_NEVER_UNMOUNT_SYSTEM
	mConstValues.insert(make_pair(TW_DONT_UNMOUNT_SYSTEM, "1"));
#else
	mConstValues.insert(make_pair(TW_DONT_UNMOUNT_SYSTEM, "0"));
#endif
#ifdef TW_NO_USB_STORAGE
	mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "0"));
#else
	char lun_file[255];
	string Lun_File_str = CUSTOM_LUN_FILE;
	size_t found = Lun_File_str.find("%");
	if (found != string::npos) {
		sprintf(lun_file, CUSTOM_LUN_FILE, 0);
		Lun_File_str = lun_file;
	}
	if (!TWFunc::Path_Exists(Lun_File_str)) {
		LOGI("=> Lun file '%s' does not exist, USB storage mode disabled\n", Lun_File_str.c_str());
		mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "0"));
	} else {
		LOGI("=> Lun file '%s'\n", Lun_File_str.c_str());
		mConstValues.insert(make_pair(TW_HAS_USB_STORAGE, "1"));
	}
#endif
#ifdef TW_INCLUDE_INJECTTWRP
	mConstValues.insert(make_pair(TW_HAS_INJECTTWRP, "1"));
	mValues.insert(make_pair(TW_INJECT_AFTER_ZIP, make_pair("1", 1)));
#else
	mConstValues.insert(make_pair(TW_HAS_INJECTTWRP, "0"));
	mValues.insert(make_pair(TW_INJECT_AFTER_ZIP, make_pair("0", 1)));
#endif
#ifdef TW_FLASH_FROM_STORAGE
	mConstValues.insert(make_pair(TW_FLASH_ZIP_IN_PLACE, "1"));
#endif
#ifdef TW_HAS_DOWNLOAD_MODE
	mConstValues.insert(make_pair(TW_DOWNLOAD_MODE, "1"));
#endif
#ifdef TW_INCLUDE_CRYPTO
	mConstValues.insert(make_pair(TW_HAS_CRYPTO, "1"));
	LOGI("Device has crypto support compiled into recovery.\n");
#endif
#ifdef TW_SDEXT_NO_EXT4
	mConstValues.insert(make_pair(TW_SDEXT_DISABLE_EXT4, "1"));
#else
	mConstValues.insert(make_pair(TW_SDEXT_DISABLE_EXT4, "0"));
#endif

	mConstValues.insert(make_pair(TW_MIN_SYSTEM_VAR, TW_MIN_SYSTEM_SIZE));
	mValues.insert(make_pair(TW_BACKUP_NAME, make_pair("(Current Date)", 0)));
	mValues.insert(make_pair(TW_BACKUP_SYSTEM_VAR, make_pair("1", 1)));
	mValues.insert(make_pair(TW_BACKUP_DATA_VAR, make_pair("1", 1)));
	mValues.insert(make_pair(TW_BACKUP_BOOT_VAR, make_pair("1", 1)));
	mValues.insert(make_pair(TW_BACKUP_RECOVERY_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_CACHE_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SP1_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SP2_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SP3_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_ANDSEC_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SDEXT_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_BACKUP_SYSTEM_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_DATA_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_BOOT_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_RECOVERY_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_CACHE_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_ANDSEC_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_SDEXT_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_SP1_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_SP2_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_SP3_SIZE, make_pair("0", 0)));
	mValues.insert(make_pair(TW_STORAGE_FREE_SIZE, make_pair("0", 0)));
	
	mValues.insert(make_pair(TW_REBOOT_AFTER_FLASH_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SIGNED_ZIP_VERIFY_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_COLOR_THEME_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_USE_COMPRESSION_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SHOW_SPAM_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_VAR, make_pair("CST6CDT", 1)));
	mValues.insert(make_pair(TW_SORT_FILES_BY_DATE_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_GUI_SORT_ORDER, make_pair("1", 1)));
	mValues.insert(make_pair(TW_RM_RF_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SKIP_MD5_CHECK_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SKIP_MD5_GENERATE_VAR, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SDEXT_SIZE, make_pair("512", 1)));
	mValues.insert(make_pair(TW_SWAP_SIZE, make_pair("32", 1)));
	mValues.insert(make_pair(TW_SDPART_FILE_SYSTEM, make_pair("ext3", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUISEL, make_pair("CST6;CDT", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUIOFFSET, make_pair("0", 1)));
	mValues.insert(make_pair(TW_TIME_ZONE_GUIDST, make_pair("1", 1)));
	mValues.insert(make_pair(TW_ACTION_BUSY, make_pair("0", 0)));
	mValues.insert(make_pair(TW_BACKUP_AVG_IMG_RATE, make_pair("15000000", 1)));
	mValues.insert(make_pair(TW_BACKUP_AVG_FILE_RATE, make_pair("3000000", 1)));
	mValues.insert(make_pair(TW_BACKUP_AVG_FILE_COMP_RATE, make_pair("2000000", 1)));
	mValues.insert(make_pair(TW_RESTORE_AVG_IMG_RATE, make_pair("15000000", 1)));
	mValues.insert(make_pair(TW_RESTORE_AVG_FILE_RATE, make_pair("3000000", 1)));
	mValues.insert(make_pair(TW_RESTORE_AVG_FILE_COMP_RATE, make_pair("2000000", 1)));
	mValues.insert(make_pair("tw_wipe_cache", make_pair("0", 0)));
	mValues.insert(make_pair("tw_wipe_dalvik", make_pair("0", 0)));
	if (GetIntValue(TW_HAS_INTERNAL) == 1 && GetIntValue(TW_HAS_DATA_MEDIA) == 1 && GetIntValue(TW_HAS_EXTERNAL) == 0)
		SetValue(TW_HAS_USB_STORAGE, 0, 0);
	else
		SetValue(TW_HAS_USB_STORAGE, 1, 0);
	mValues.insert(make_pair(TW_ZIP_INDEX, make_pair("0", 0)));
	mValues.insert(make_pair(TW_ZIP_QUEUE_COUNT, make_pair("0", 0)));
	mValues.insert(make_pair(TW_FILENAME, make_pair("/sdcard", 0)));
	mValues.insert(make_pair(TW_SIMULATE_ACTIONS, make_pair("0", 1)));
	mValues.insert(make_pair(TW_SIMULATE_FAIL, make_pair("0", 1)));
	mValues.insert(make_pair(TW_IS_ENCRYPTED, make_pair("0", 0)));
	mValues.insert(make_pair(TW_IS_DECRYPTED, make_pair("0", 0)));
	mValues.insert(make_pair(TW_CRYPTO_PASSWORD, make_pair("0", 0)));
	mValues.insert(make_pair(TW_DATA_BLK_DEVICE, make_pair("0", 0)));
	mValues.insert(make_pair("tw_terminal_state", make_pair("0", 0)));
	mValues.insert(make_pair("tw_background_thread_running", make_pair("0", 0)));
	mValues.insert(make_pair(TW_RESTORE_FILE_DATE, make_pair("0", 0)));
	mValues.insert(make_pair("tw_screen_timeout_secs", make_pair("60", 1)));
	mValues.insert(make_pair("tw_gui_done", make_pair("0", 0)));
#ifdef TW_MAX_BRIGHTNESS
	if (strcmp(EXPAND(TW_BRIGHTNESS_PATH), "/nobrightness") != 0) {
		LOGI("=> Brightness file '%s'\n", EXPAND(TW_BRIGHTNESS_PATH));
		mConstValues.insert(make_pair("tw_has_brightnesss_file", "1"));
		mConstValues.insert(make_pair("tw_brightness_file", EXPAND(TW_BRIGHTNESS_PATH)));
		ostringstream val100, val25, val50, val75;
		int value = TW_MAX_BRIGHTNESS;
		val100 << value;
		mConstValues.insert(make_pair("tw_brightness_100", val100.str()));
		value = TW_MAX_BRIGHTNESS * 0.25;
		val25 << value;
		mConstValues.insert(make_pair("tw_brightness_25", val25.str()));
		value = TW_MAX_BRIGHTNESS * 0.5;
		val50 << value;
		mConstValues.insert(make_pair("tw_brightness_50", val50.str()));
		value = TW_MAX_BRIGHTNESS * 0.75;
		val75 << value;
		mConstValues.insert(make_pair("tw_brightness_75", val75.str()));
		mValues.insert(make_pair("tw_brightness", make_pair(val100.str(), 1)));
		mValues.insert(make_pair("tw_brightness_display", make_pair("100", 1)));
	} else {
		mConstValues.insert(make_pair("tw_has_brightnesss_file", "0"));
	}
#endif
}

void DataManager::Output_Version(void) {
	string Path;
	char version[255];

	if (!PartitionManager.Mount_By_Path("/cache", false)) {
		LOGI("Unable to mount 'cache' to write version number.\n");
		return;
	}
	if (!TWFunc::Path_Exists("/cache/recovery/.")) {
		LOGI("Recreating /cache/recovery folder.\n");
		if (mkdir("/cache/recovery", S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP) != 0) {
			LOGE("DataManager::Output_Version -- Unable to make /cache/recovery\n");
			return;
		}
	}
	Path = "/cache/recovery/.version";
	if (TWFunc::Path_Exists(Path)) {
		unlink(Path.c_str());
	}
	FILE *fp = fopen(Path.c_str(), "w");
	if (fp == NULL) {
		LOGE("Unable to open '%s'.\n", Path.c_str());
		return;
	}
	strcpy(version, TW_VERSION_STR);
	fwrite(version, sizeof(version[0]), strlen(version) / sizeof(version[0]), fp);
	fclose(fp);
	sync();
	LOGI("Version number saved to '%s'\n", Path.c_str());
}

void DataManager::ReadSettingsFile(void)
{
	if (SettingsFileRead)
		return;

	// Load up the values for TWRP - Sleep to let the card be ready
	char mkdir_path[255], settings_file[255];
	int is_enc, has_dual, use_ext, has_data_media, has_ext;

	GetValue(TW_IS_ENCRYPTED, is_enc);
	GetValue(TW_HAS_DATA_MEDIA, has_data_media);
	if (is_enc == 1 && has_data_media == 1) {
		LOGI("Cannot load settings -- encrypted.\n");
		return;
	}

	memset(mkdir_path, 0, sizeof(mkdir_path));
	memset(settings_file, 0, sizeof(settings_file));
	sprintf(mkdir_path, "%s/TWRP", DataManager_GetSettingsStoragePath());
	sprintf(settings_file, "%s/.twrps", mkdir_path);

	if (!PartitionManager.Mount_Settings_Storage(false))
	{
		usleep(500000);
		if (!PartitionManager.Mount_Settings_Storage(false))
			LOGE("Unable to mount %s when trying to read settings file.\n", DataManager_GetSettingsStorageMount());
	}

	mkdir(mkdir_path, 0777);

	LOGI("Attempt to load settings from settings file on /sdcard...\n");
	LoadValues(settings_file);
	Output_Version();
	GetValue(TW_HAS_DUAL_STORAGE, has_dual);
	GetValue(TW_USE_EXTERNAL_STORAGE, use_ext);
	GetValue(TW_HAS_EXTERNAL, has_ext);
	if (has_dual != 0 && use_ext == 1) {
		// Attempt to switch to using external storage
		if (!PartitionManager.Mount_Current_Storage(false)) {
			LOGE("Failed to mount external storage, using internal storage.\n");
			// Remount failed, default back to internal storage
			SetValue(TW_USE_EXTERNAL_STORAGE, 0);
			PartitionManager.Mount_Current_Storage(true);
			string int_zip_path;
			GetValue(TW_ZIP_INTERNAL_VAR, int_zip_path);
			SetValue(TW_USE_EXTERNAL_STORAGE, 0);
			SetValue(TW_ZIP_LOCATION_VAR, int_zip_path);
		}
	} else {
		PartitionManager.Mount_Current_Storage(true);
	}

	if (has_ext) {
		string ext_path;

		GetValue(TW_EXTERNAL_PATH, ext_path);
		PartitionManager.Mount_By_Path(ext_path, 0);
	}
	update_tz_environment_variables();
#ifdef TW_MAX_BRIGHTNESS
	if (pause == 0 && strcmp(EXPAND(TW_BRIGHTNESS_PATH), "/nobrightness") != 0) {
		string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
		string brightness_value = GetStrValue("tw_brightness");
		LOGI("writing %s to brightness\n", brightness_value.c_str());
		TWFunc::write_file(brightness_path, brightness_value);
	}
#endif
	SettingsFileRead = 1;
}

string DataManager::GetCurrentStoragePath(void)
{
	if (GetIntValue(TW_HAS_DUAL_STORAGE) == 1) {
		if (GetIntValue(TW_USE_EXTERNAL_STORAGE) == 0)
			return GetStrValue(TW_INTERNAL_PATH);
		else
			return GetStrValue(TW_EXTERNAL_PATH);
	} else if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetStrValue(TW_INTERNAL_PATH);
	else
		return GetStrValue(TW_EXTERNAL_PATH);
}

string& DataManager::CGetCurrentStoragePath()
{
	if (GetIntValue(TW_HAS_DUAL_STORAGE) == 1) {
		if (GetIntValue(TW_USE_EXTERNAL_STORAGE) == 0)
			return GetValueRef(TW_INTERNAL_PATH);
		else
			return GetValueRef(TW_EXTERNAL_PATH);
	} else if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetValueRef(TW_INTERNAL_PATH);
	else
		return GetValueRef(TW_EXTERNAL_PATH);
}

string DataManager::GetCurrentStorageMount(void)
{
	if (GetIntValue(TW_HAS_DUAL_STORAGE) == 1) {
		if (GetIntValue(TW_USE_EXTERNAL_STORAGE) == 0)
			return GetStrValue(TW_INTERNAL_MOUNT);
		else
			return GetStrValue(TW_EXTERNAL_MOUNT);
	} else if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetStrValue(TW_INTERNAL_MOUNT);
	else
		return GetStrValue(TW_EXTERNAL_MOUNT);
}

string& DataManager::CGetCurrentStorageMount()
{
	if (GetIntValue(TW_HAS_DUAL_STORAGE) == 1) {
		if (GetIntValue(TW_USE_EXTERNAL_STORAGE) == 0)
			return GetValueRef(TW_INTERNAL_MOUNT);
		else
			return GetValueRef(TW_EXTERNAL_MOUNT);
	} else if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetValueRef(TW_INTERNAL_MOUNT);
	else
		return GetValueRef(TW_EXTERNAL_MOUNT);
}

string DataManager::GetSettingsStoragePath(void)
{
	if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetStrValue(TW_INTERNAL_PATH);
	else
		return GetStrValue(TW_EXTERNAL_PATH);
}

string& DataManager::CGetSettingsStoragePath()
{
	if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetValueRef(TW_INTERNAL_PATH);
	else
		return GetValueRef(TW_EXTERNAL_PATH);
}

string DataManager::GetSettingsStorageMount(void)
{
	if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetStrValue(TW_INTERNAL_MOUNT);
	else
		return GetStrValue(TW_EXTERNAL_MOUNT);
}

string& DataManager::CGetSettingsStorageMount()
{
	if (GetIntValue(TW_HAS_INTERNAL) == 1)
		return GetValueRef(TW_INTERNAL_MOUNT);
	else
		return GetValueRef(TW_EXTERNAL_MOUNT);
}

extern "C" int DataManager_ResetDefaults()
{
	return DataManager::ResetDefaults();
}

extern "C" void DataManager_LoadDefaults()
{
	return DataManager::SetDefaultValues();
}

extern "C" int DataManager_LoadValues(const char* filename)
{
	return DataManager::LoadValues(filename);
}

extern "C" int DataManager_Flush()
{
	return DataManager::Flush();
}

extern "C" int DataManager_GetValue(const char* varName, char* value)
{
	int ret;
	string str;

	ret = DataManager::GetValue(varName, str);
	if (ret == 0)
		strcpy(value, str.c_str());
	return ret;
}

extern "C" const char* DataManager_GetStrValue(const char* varName)
{
	string& str = DataManager::GetValueRef(varName);
	return str.c_str();
}

extern "C" const char* DataManager_GetCurrentStoragePath(void)
{
	string& str = DataManager::CGetCurrentStoragePath();
	return str.c_str();
}

extern "C" const char* DataManager_GetSettingsStoragePath(void)
{
	string& str = DataManager::CGetSettingsStoragePath();
	return str.c_str();
}

extern "C" const char* DataManager_GetCurrentStorageMount(void)
{
	string& str = DataManager::CGetCurrentStorageMount();
	return str.c_str();
}

extern "C" const char* DataManager_GetSettingsStorageMount(void)
{
	string& str = DataManager::CGetSettingsStorageMount();
	return str.c_str();
}

extern "C" int DataManager_GetIntValue(const char* varName)
{
	return DataManager::GetIntValue(varName);
}

extern "C" int DataManager_SetStrValue(const char* varName, char* value)
{
	return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_SetIntValue(const char* varName, int value)
{
	return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_SetFloatValue(const char* varName, float value)
{
	return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_ToggleIntValue(const char* varName)
{
	if (DataManager::GetIntValue(varName))
		return DataManager::SetValue(varName, 0);
	else
		return DataManager::SetValue(varName, 1);
}

extern "C" void DataManager_DumpValues()
{
	return DataManager::DumpValues();
}

extern "C" void DataManager_ReadSettingsFile()
{
	return DataManager::ReadSettingsFile();
}

extern "C" void DataManager_SetupTwrpFolder()
{
	return DataManager::SetupTwrpFolder();
}

extern "C" int DataManager_Detect_BLDR()
{
	return DataManager::Detect_BLDR();
}

extern "C" int DataManager_Pause_For_Battery_Charge()
{
	return DataManager::Pause_For_Battery_Charge();
}
