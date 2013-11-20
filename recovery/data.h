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

#ifndef _DATA_HEADER
#define _DATA_HEADER

int DataManager_ResetDefaults();
void DataManager_LoadDefaults();
int DataManager_LoadValues(const char* filename);
int DataManager_Flush();
const char* DataManager_GetStrValue(const char* varName);
const char* DataManager_GetCurrentStoragePath();
const char* DataManager_GetCurrentStorageMount();
const char* DataManager_GetSettingsStoragePath();
const char* DataManager_GetSettingsStorageMount();
int DataManager_GetIntValue(const char* varName);

int DataManager_SetStrValue(const char* varName, char* value);
int DataManager_SetIntValue(const char* varName, int value);
int DataManager_SetFloatValue(const char* varName, float value);

int DataManager_ToggleIntValue(const char* varName);

void DataManager_DumpValues();
void DataManager_ReadSettingsFile();
void DataManager_SetupTwrpFolder();
int DataManager_Detect_BLDR();
int DataManager_Pause_For_Battery_Charge();

#endif  // _DATA_HEADER

