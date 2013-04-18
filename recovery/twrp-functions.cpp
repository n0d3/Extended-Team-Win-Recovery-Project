#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#ifdef ANDROID_RB_POWEROFF
	#include "cutils/android_reboot.h"
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "data.hpp"
#include "variables.h"
#include "bootloader.h"

extern "C" {
	#include "minuitwrp/minui.h"
	#include "libcrecovery/common.h"
}

#define TW_DEFAULT_POWER_MODE	0
#define TW_POWER_SAVE_MODE	1
unsigned cpu_settings = 0;
#define TW_SCRN_OFF		0
unsigned screen_state = 1;

/* Execute a command */
int TWFunc::Exec_Cmd(string cmd, string &result) {
	FILE* exec;
	char buffer[130];
	int ret = 0;
	exec = __popen(cmd.c_str(), "r");
	if (!exec) return -1;
	while(!feof(exec)) {
		memset(&buffer, 0, sizeof(buffer));
		if (fgets(buffer, 128, exec) != NULL) {
			buffer[128] = '\n';
			buffer[129] = NULL;
			result += buffer;
		}
	}
	ret = __pclose(exec);
	return ret;
}

// Returns "file.name" from a full /path/to/file.name
string TWFunc::Get_Filename(string Path) {
	size_t pos = Path.find_last_of("/");
	if (pos != string::npos) {
		string Filename;
		Filename = Path.substr(pos + 1, Path.size() - pos - 1);
		return Filename;
	} else
		return Path;
}

// Returns "/path/to/" from a full /path/to/file.name
string TWFunc::Get_Path(string Path) {
	size_t pos = Path.find_last_of("/");
	if (pos != string::npos) {
		string Pathonly;
		Pathonly = Path.substr(0, pos + 1);
		return Pathonly;
	} else
		return Path;
}

// Returns "/path" from a full /path/to/file.name
string TWFunc::Get_Root_Path(string Path) {
	string Local_Path = Path;

	// Make sure that we have a leading slash
	if (Local_Path.substr(0, 1) != "/")
		Local_Path = "/" + Local_Path;

	// Trim the path to get the root path only
	size_t position = Local_Path.find("/", 2);
	if (position != string::npos) {
		Local_Path.resize(position);
	}
	return Local_Path;
}

void TWFunc::install_htc_dumlock(void) {
	int need_libs = 0;

	if (!PartitionManager.Mount_By_Path("/system", true))
		return;

	if (!PartitionManager.Mount_By_Path("/data", true))
		return;

	gui_print("Installing HTC Dumlock to system...\n");
	copy_file("/res/htcd/htcdumlocksys", "/system/bin/htcdumlock", 0755);
	if (!Path_Exists("/system/bin/flash_image")) {
		gui_print("Installing flash_image...\n");
		copy_file("/res/htcd/flash_imagesys", "/system/bin/flash_image", 0755);
		need_libs = 1;
	} else
		gui_print("flash_image is already installed, skipping...\n");
	if (!Path_Exists("/system/bin/dump_image")) {
		gui_print("Installing dump_image...\n");
		copy_file("/res/htcd/dump_imagesys", "/system/bin/dump_image", 0755);
		need_libs = 1;
	} else
		gui_print("dump_image is already installed, skipping...\n");
	if (need_libs) {
		gui_print("Installing libs needed for flash_image and dump_image...\n");
		copy_file("/res/htcd/libbmlutils.so", "/system/lib/libbmlutils.so", 0755);
		copy_file("/res/htcd/libflashutils.so", "/system/lib/libflashutils.so", 0755);
		copy_file("/res/htcd/libmmcutils.so", "/system/lib/libmmcutils.so", 0755);
		copy_file("/res/htcd/libmtdutils.so", "/system/lib/libmtdutils.so", 0755);
	}
	gui_print("Installing HTC Dumlock app...\n");
	mkdir("/data/app", 0777);
	unlink("/data/app/com.teamwin.htcdumlock*");
	copy_file("/res/htcd/HTCDumlock.apk", "/data/app/com.teamwin.htcdumlock.apk", 0777);
	sync();
	gui_print("HTC Dumlock is installed.\n");
}

void TWFunc::htc_dumlock_restore_original_boot(void) {
	string status;
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	gui_print("Restoring original boot...\n");
	Exec_Cmd("htcdumlock restore", status);
	gui_print("Original boot restored.\n");
}

void TWFunc::htc_dumlock_reflash_recovery_to_boot(void) {
	string status;
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;
	gui_print("Reflashing recovery to boot...\n");
	Exec_Cmd("htcdumlock recovery noreboot", status);
	gui_print("Recovery is flashed to boot.\n");
}

int TWFunc::Recursive_Mkdir(string Path) {
	string pathCpy = Path;
	string wholePath;
	size_t pos = pathCpy.find("/", 2);

	while (pos != string::npos)
	{
		wholePath = pathCpy.substr(0, pos);
		if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST) {
			LOGERR("Unable to create folder: %s  (errno=%d)\n", wholePath.c_str(), errno);
			return false;
		}

		pos = pathCpy.find("/", pos + 1);
	}
	if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST)
		return false;
	return true;
}

unsigned long long TWFunc::Get_Folder_Size(const string& Path, bool Display_Error) {
	DIR* d = opendir(Path.c_str());
	if (d == NULL) {
		if (Display_Error) {
			LOGERR("[Get_Folder_Size()] error opening '%s'\n", Path.c_str());
			LOGERR("error: %s\n", strerror(errno));
		} else {
			LOGINFO("[Get_Folder_Size()] error opening '%s'\n", Path.c_str());
			LOGINFO("error: %s\n", strerror(errno));
		}
		return 0;
	}

	struct dirent* de;
	struct stat st;
	unsigned long long dusize = 0;
	unsigned long long dutemp = 0;
	while ((de = readdir(d)) != NULL) {
		if (de->d_type == DT_DIR) {
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			dutemp = Get_Folder_Size((Path + "/" + de->d_name), Display_Error);
			dusize += dutemp;
			dutemp = 0;
		} else if (de->d_type == DT_REG) {
			stat((Path + "/" + de->d_name).c_str(), &st);
			dusize += (unsigned long long)(st.st_size);
		}
	}
	closedir(d);
	return dusize;
}

bool TWFunc::Path_Exists(string Path) {
	// Check to see if the Path exists
	struct stat st;
	if (stat(Path.c_str(), &st) != 0)
		return false;
	else
		return true;
}

void TWFunc::GUI_Operation_Text(string Read_Value, string Default_Text) {
	string Display_Text;

	DataManager::GetValue(Read_Value, Display_Text);
	if (Display_Text.empty())
		Display_Text = Default_Text;

	DataManager::SetValue("tw_operation", Display_Text);
	DataManager::SetValue("tw_partition", "");
}

void TWFunc::GUI_Operation_Text(string Read_Value, string Partition_Name, string Default_Text) {
	string Display_Text;

	DataManager::GetValue(Read_Value, Display_Text);
	if (Display_Text.empty())
		Display_Text = Default_Text;

	DataManager::SetValue("tw_operation", Display_Text);
	DataManager::SetValue("tw_partition", Partition_Name);
}

unsigned long TWFunc::Get_File_Size(string Path) {
	struct stat st;

	if (stat(Path.c_str(), &st) != 0)
		return 0;
	return st.st_size;
}

unsigned long TWFunc::RoundUpSize(unsigned long sz, unsigned long multiple) {
	if (multiple == 0)
		return sz;

	unsigned long remainder = sz % multiple;
	if (remainder == 0)
		return sz;
	return (sz + multiple - remainder);
}

void TWFunc::Copy_Log(string Source, string Destination) {
	FILE *destination_log = fopen(Destination.c_str(), "a");
	if (destination_log == NULL) {
		LOGERR("TWFunc::Copy_Log -- Can't open destination log file: '%s'\n", Destination.c_str());
	} else {
		FILE *source_log = fopen(Source.c_str(), "r");
		if (source_log != NULL) {
			fseek(source_log, Log_Offset, SEEK_SET);
			char buffer[4096];
			while (fgets(buffer, sizeof(buffer), source_log))
				fputs(buffer, destination_log); // Buffered write of log file
			Log_Offset = ftell(source_log);
			fflush(source_log);
			fclose(source_log);
		}
		fflush(destination_log);
		fclose(destination_log);
	}
}

void TWFunc::Update_Log_File(void) {
	// Copy logs to cache so the system can find out what happened.
	Copy_Log(TMP_LOG_FILE, "/cache/recovery/log");
	copy_file("/cache/recovery/log", "/cache/recovery/last_log", 600);
	chown("/cache/recovery/log", 1000, 1000);
	chmod("/cache/recovery/log", 0600);
	chmod("/cache/recovery/last_log", 0640);

	// Reset bootloader message
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part != NULL) {
		struct bootloader_message boot;
		memset(&boot, 0, sizeof(boot));
		if (Part->Current_File_System == "mtd") {
			if (set_bootloader_message_mtd_name(&boot, Part->MTD_Name.c_str()) != 0)
				LOGERR("Unable to set MTD bootloader message.\n");
		} else if (Part->Current_File_System == "emmc") {
			if (set_bootloader_message_block_name(&boot, Part->Actual_Block_Device.c_str()) != 0)
				LOGERR("Unable to set emmc bootloader message.\n");
		} else {
			LOGERR("Unknown file system for /misc: '%s'\n", Part->Current_File_System.c_str());
		}
	}

	if (!PartitionManager.Mount_By_Path("/cache", true) || (unlink("/cache/recovery/command") && errno != ENOENT)) {
		LOGINFO("Can't unlink %s\n", "/cache/recovery/command");
	}

	PartitionManager.UnMount_By_Path("/cache", true);
	sync();
}

void TWFunc::Update_Intent_File(string Intent) {
	if (PartitionManager.Mount_By_Path("/cache", false) && !Intent.empty()) {
		TWFunc::write_file("/cache/recovery/intent", Intent);
	}
}

int TWFunc::get_bootloader_msg(struct bootloader_message *out) {
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part == NULL) {
		LOGERR("Cannot load volume /misc!\n");
		return -1;
	}
	if (Part->Current_File_System == "mtd") {
		return get_bootloader_message_mtd_name(out, Part->MTD_Name.c_str());
	} else if (Part->Current_File_System == "emmc") {
		return get_bootloader_message_block_name(out, Part->Actual_Block_Device.c_str());
	}
	LOGERR("Unknown file system for /misc: '%s'\n", Part->Current_File_System.c_str());
	return -1;
}

int TWFunc::set_bootloader_msg(const struct bootloader_message *in) {
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part == NULL) {
		LOGERR("Cannot load volume /misc!\n");
		return -1;
	}
	if (Part->Current_File_System == "mtd") {
		return set_bootloader_message_mtd_name(in, Part->MTD_Name.c_str());
	} else if (Part->Current_File_System == "emmc") {
		return set_bootloader_message_block_name(in, Part->Actual_Block_Device.c_str());
	}
	LOGERR("Unknown file system for /misc: '%s'\n", Part->Current_File_System.c_str());
	return -1;
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::tw_reboot(RebootCommand command) {
	// Always force a sync before we reboot
	sync();
	string tmpbuf;

    	switch (command) {
    		case rb_hot:        		
			Exec_Cmd("busybox killall recovery", tmpbuf);
        		return 0;
    		case rb_current:
    		case rb_system:
				Update_Log_File();
				Update_Intent_File("s");
				sync();
			check_and_run_script("/sbin/rebootsystem.sh", "reboot system");
        		return reboot(RB_AUTOBOOT);
    		case rb_recovery:
			check_and_run_script("/sbin/rebootrecovery.sh", "reboot recovery");
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "recovery");
    		case rb_bootloader:
			check_and_run_script("/sbin/rebootbootloader.sh", "reboot bootloader");
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "bootloader");
    		case rb_poweroff:
			check_and_run_script("/sbin/poweroff.sh", "power off");
#ifdef ANDROID_RB_POWEROFF
			android_reboot(ANDROID_RB_POWEROFF, 0, 0);
#endif
        		return reboot(RB_POWER_OFF);
    		case rb_download:
			check_and_run_script("/sbin/rebootdownload.sh", "reboot download");
			return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "download");
    		case rb_sboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-1");
    		case rb_tboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-2");
    		case rb_vboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-3");
    		case rb_wboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-4");
    		case rb_xboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-5");
    		case rb_yboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-6");
    		case rb_zboot:
        		return __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, (void*) "oem-7");
    		default:
        		return -1;
    	}
    	return -1;
}

void TWFunc::check_and_run_script(const char* script_file, const char* display_name) {
	// Check for and run startup script if script exists
	struct stat st;
	string result;
	if (stat(script_file, &st) == 0) {
		gui_print("Running %s script...\n", display_name);
		chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		Exec_Cmd(script_file, result);
		gui_print("Finished running %s script.\n", display_name);
	}
}

int TWFunc::removeDir(const string path, bool skipParent) {
	DIR *d = opendir(path.c_str());
	int r = 0;
	string new_path;

	if (d == NULL) {
		LOGERR("[removeDir()] Error opening '%s'\n", path.c_str());
		return -1;
	}

	if (d) {
		struct dirent *p;
		while (!r && (p = readdir(d))) {
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;
			new_path = path + "/";
			new_path.append(p->d_name);
			if (p->d_type == DT_DIR) {
				r = removeDir(new_path, true);
				if (!r) {
					if (p->d_type == DT_DIR) 
						r = rmdir(new_path.c_str());
					else
						LOGINFO("Unable to removeDir '%s': %s\n", new_path.c_str(), strerror(errno));
				}
			} else if (p->d_type == DT_REG || p->d_type == DT_LNK || p->d_type == DT_FIFO || p->d_type == DT_SOCK) {
				r = unlink(new_path.c_str());
				if (r != 0)
					LOGINFO("Unable to unlink '%s'\n", new_path.c_str());
			}
		}
		closedir(d);

		if (!r) { 
			if (skipParent)
				return 0;
			else
				r = rmdir(path.c_str());
		}
	}
	return r;
}

int TWFunc::copy_file(string src, string dst, int mode) {
	LOGINFO("Copying file %s to %s\n", src.c_str(), dst.c_str());
	ifstream srcfile(src.c_str(), ios::binary);
	ofstream dstfile(dst.c_str(), ios::binary);
	dstfile << srcfile.rdbuf();
	srcfile.close();
	dstfile.close();
	if (chmod(dst.c_str(), mode) != 0)
		return -1;
	return 0;
}

unsigned int TWFunc::Get_D_Type_From_Stat(string Path) {
	struct stat st;

	stat(Path.c_str(), &st);
	if (st.st_mode & S_IFDIR)
		return DT_DIR;
	else if (st.st_mode & S_IFBLK)
		return DT_BLK;
	else if (st.st_mode & S_IFCHR)
		return DT_CHR;
	else if (st.st_mode & S_IFIFO)
		return DT_FIFO;
	else if (st.st_mode & S_IFLNK)
		return DT_LNK;
	else if (st.st_mode & S_IFREG)
		return DT_REG;
	else if (st.st_mode & S_IFSOCK)
		return DT_SOCK;
	return DT_UNKNOWN;
}

int TWFunc::read_file(string fn, string& results) {
	ifstream file;
	file.open(fn.c_str(), ios::in);
	if (file.is_open()) {
		file >> results;
		file.close();
		return 0;
	}
	LOGINFO("Cannot find file %s\n", fn.c_str());
	return -1;
}

int TWFunc::read_file_line_by_line(string fn, vector<string>& lines, bool skip_empty) {
	if (fn.empty())
		return 0;

	ifstream file;
	file.open(fn.c_str(), ios::in);
	if (file.is_open()) {
		string line;
		while (getline(file, line)) {
			if (line.empty() && skip_empty)
				continue;
			lines.push_back(line);
		}
		file.close();
		return 1;
	}
	LOGINFO("Cannot find file %s\n", fn.c_str());
	return 0;
}

int TWFunc::write_file(string fn, string& line) {
	FILE *file;
	file = fopen(fn.c_str(), "w");
	if (file != NULL) {
		fwrite(line.c_str(), line.size(), 1, file);
		fclose(file);
		return 0;
	}
	LOGINFO("Cannot find file %s\n", fn.c_str());
	return -1;
}

timespec TWFunc::timespec_diff(timespec& start, timespec& end) {
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
	        temp.tv_sec = end.tv_sec-start.tv_sec;
	        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

// Dropping the cache will make the available RAM go up, but the CPU load will go up too.
// It may have a positive effect after heavy jobs (backup/restore, install, partitioning)[?]
int TWFunc::drop_caches(void) {
	int do_drop = DataManager::GetIntValue(TW_DROP_CACHES);
	if (do_drop) {
		sync();
		string caches = "/proc/sys/vm/drop_caches";
		/*
		    free pagecache:			string drop = "1\n";
		    free dentries & inodes:		string drop = "2\n";
		    free pagecache, dentries & inodes:	string drop = "3\n";
		*/
		string drop = "3\n";
		if (write_file(caches, drop) != 0)
			return -1;
	}
	return 0;
}

// Screen off
void TWFunc::screen_off(void) {
	if (screen_state != TW_SCRN_OFF) {
		screen_state = TW_SCRN_OFF;
		string lcd_brightness;
		string off = "0\n";
		string brightness_file = EXPAND(TW_BRIGHTNESS_PATH);
#ifndef TW_NO_SCREEN_BLANK
		gr_fb_blank(1);
#endif
		write_file(brightness_file, off);
		LOGINFO("Screen turned off to save power.\n");
	}
}

// Powersave cpu settings
void TWFunc::power_save(void) {
	if (cpu_settings != TW_POWER_SAVE_MODE) {
		cpu_settings = TW_POWER_SAVE_MODE;
		string powersave = "powersave\n";
		string cpu_governor = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
		write_file(cpu_governor, powersave);
		string low_power_freq = "245000\n";
		string cpu_max_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
		write_file(cpu_max_freq, low_power_freq);
		string cpu_min_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq";
		write_file(cpu_min_freq, low_power_freq);
		LOGINFO("Powersave cpu settings loaded.\n");
		sync();
	}
}

// Restore default cpu settings
void TWFunc::power_restore(int charge_mode) {
	if (cpu_settings != TW_DEFAULT_POWER_MODE && charge_mode == 0) {
		cpu_settings = TW_DEFAULT_POWER_MODE;
		int set_cpu_gov = 0, set_cpu_f = 0;
		string default_gov, max_power_freq, low_power_freq;
		string cpu_governor = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
		string cpu_max_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
		string cpu_min_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq";

		set_cpu_gov = DataManager::GetIntValue(TW_SET_CPU_GOV_AT_BOOT);
		if (set_cpu_gov)
			default_gov = DataManager::GetStrValue(TW_CPU_GOV) + "\n";
		else
			default_gov = "performance\n";

		set_cpu_f = DataManager::GetIntValue(TW_SET_CPU_F_AT_BOOT);
		if (set_cpu_f) {
			max_power_freq = DataManager::GetStrValue(TW_MAX_CPU_F) + "\n";
			low_power_freq = DataManager::GetStrValue(TW_MIN_CPU_F) + "\n";
		} else {
			max_power_freq = "998400\n";
			low_power_freq = "245000\n";
		}

		write_file(cpu_governor, default_gov);
		write_file(cpu_max_freq, max_power_freq);
		write_file(cpu_min_freq, low_power_freq);
		LOGINFO("Default cpu settings loaded.\n");
		sync();
	}
}

void TWFunc::apply_system_tweaks(int charge_mode) {
	if (charge_mode == 0) {
		int set_drop_caches = 0, set_io_sched = 0, set_cpu_gov = 0, set_cpu_f = 0;
		string default_io_sched, default_gov, max_power_freq, low_power_freq, cmd, res;
		
		set_io_sched = DataManager::GetIntValue(TW_SET_IO_SCHED_AT_BOOT);
		set_cpu_gov = DataManager::GetIntValue(TW_SET_CPU_GOV_AT_BOOT);
		set_cpu_f = DataManager::GetIntValue(TW_SET_CPU_F_AT_BOOT);
		set_drop_caches = DataManager::GetIntValue(TW_SET_DROP_CACHES_AT_BOOT);
		if (set_io_sched || set_cpu_gov || set_cpu_f || set_drop_caches)
			LOGINFO("Applying system tweaks...\n");
		if (set_io_sched) {
			string io_scheduler = "/sys/block/mmcblk0/queue/scheduler";
			default_io_sched = DataManager::GetStrValue(TW_IO_SCHED);
			cmd = "echo \"" + default_io_sched + "\" > " + io_scheduler;
			Exec_Cmd(cmd, res);
			LOGINFO("I/O Scheduler: %s\n", default_io_sched.c_str());
		}
		if (set_cpu_gov) {
			string cpu_governor = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
			default_gov = DataManager::GetStrValue(TW_CPU_GOV) + "\n";
			write_file(cpu_governor, default_gov);
			LOGINFO("CPU Governor: %s", default_gov.c_str());
		}
		if (set_cpu_f) {
			string cpu_max_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
			string cpu_min_freq = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq";
			max_power_freq = DataManager::GetStrValue(TW_MAX_CPU_F) + "\n";
			low_power_freq = DataManager::GetStrValue(TW_MIN_CPU_F) + "\n";
			write_file(cpu_max_freq, max_power_freq);
			write_file(cpu_min_freq, low_power_freq);
			LOGINFO("Max cpu freq: %s", max_power_freq.c_str());
			LOGINFO("Min cpu freq: %s", low_power_freq.c_str());
		}
		if (set_drop_caches) {
			DataManager::SetValue(TW_DROP_CACHES, 1);
			LOGINFO("Drop caches after backup/restore completion: true\n");
		} else {
			DataManager::SetValue(TW_DROP_CACHES, 0);
		}
		sync();
	}
}

bool TWFunc::replace_string(string& str, const string& search_str, const string& replace_str) {
	size_t start_pos = str.find(search_str);
	if (start_pos != string::npos) {
		str.replace(start_pos, search_str.length(), replace_str);
        	return true;
	}

	return false; // search_str not found
}

string TWFunc::to_string(int number) {
	stringstream ss;
	ss << number;

	return ss.str();
}

vector<string> TWFunc::split_string(const string &in, char del, bool skip_empty) {		
	vector<string> res;

	if (in.empty() || del == '\0')
		return res;

	string field;
	istringstream f(in);
	if (del == '\n') {
		while(getline(f, field)) {
			if (field.empty() && skip_empty)
				continue;
			res.push_back(field);
		}
	} else {
		while(getline(f, field, del)) {
			if (field.empty() && skip_empty)
				continue;
			res.push_back(field);
		}
	}
	return res;
}

int TWFunc::Split_NandroidMD5(string File) {
	int i, lines_read, ret = -1;
	vector<string> lines;
	lines_read = read_file_line_by_line(File, lines, true);
	if (lines_read && lines.size() > 0) {
		vector<string> line_parts;
		string backup_folder, MD5fileName, MD5fileContent, MD5fileFullPath, bak;

		backup_folder = Get_Path(File);
		for (i = 0; i < (int)lines.size(); i++) {
			line_parts = split_string(lines[i], ' ', true);
			if (line_parts.size() > 0) {
				MD5fileContent = lines[i];
				MD5fileName = line_parts[line_parts.size() - 1] + ".md5";
				MD5fileFullPath = backup_folder + MD5fileName;
				ret = write_file(MD5fileFullPath, MD5fileContent);
			}
		}
		// Rename nandroid.md5 to nandroid.md5.bak
		bak = File + ".bak";
		rename(File.c_str(), bak.c_str());
	}
	return ret;
}

// Returns full-path of Filename if found on storage
string TWFunc::Find_File_On_Storage(string Filename) {
	string Full_Path = Filename;
	string File_Name = Get_Filename(Filename);
	string Current_Storage_Path = DataManager::GetCurrentStoragePath();
	if (!PartitionManager.Is_Mounted_By_Path(Current_Storage_Path))
		PartitionManager.Mount_Current_Storage(false);

	if (File_Name.size() != 0) {
		LOGINFO("Scanning storage for %s...\n", File_Name.c_str());
		string result, cmd;
		cmd = "find " + Current_Storage_Path + " -type f -iname " + File_Name + " | sed 1q";
		TWFunc::Exec_Cmd(cmd, result);
		if (!result.empty()) {
			// Trim any '\n' from result
			size_t position = result.find("\n", 0);
			if (position != string::npos) {
				result.resize(position);
			}
			Full_Path = result;
		}
	}
	return Full_Path;
}

int TWFunc::Vibrate(FeedbackReason reason) {
	switch (reason) {
		case button_pressed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_BUTTON_PRESS)
			? vibrate(DataManager::GetIntValue(TW_BUTTON_FEEDBACK_DURATION_MS)) : 0);
			break;
		case backup_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_BACKUP)
			? vibrate(DataManager::GetIntValue(TW_BACKUP_FEEDBACK_DURATION_MS)) : 0);
			break;
		case restore_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_RESTORE)
			? vibrate(DataManager::GetIntValue(TW_RESTORE_FEEDBACK_DURATION_MS)) : 0);
			break;
		case install_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_INSTALL)
			? vibrate(DataManager::GetIntValue(TW_INSTALL_FEEDBACK_DURATION_MS)) : 0);
			break;
		case parted_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_PARTED)
			? vibrate(DataManager::GetIntValue(TW_PARTED_FEEDBACK_DURATION_MS)) : 0);
			break;
		case sdbackup_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_SDBACKUP)
			? vibrate(DataManager::GetIntValue(TW_SDBACKUP_FEEDBACK_DURATION_MS)) : 0);
			break;
		case sdrestore_completed:
			return (DataManager::GetIntValue(TW_VIBRATE_AFTER_SDRESTORE)
			? vibrate(DataManager::GetIntValue(TW_SDRESTORE_FEEDBACK_DURATION_MS)) : 0);
			break;
		default: // Just vibrate using the reason for the timeout
			return (1000 > (int)reason /* Dont't allow a duration more than 1s */
			? vibrate((int)reason) : 0);
			break;
	}
	return 0;
}

void TWFunc::Take_Screenshot(void) {
	// Where to store the screenshot
	string current_storage_path = DataManager::GetCurrentStoragePath();
	string twrp_dir = current_storage_path + "/TWRP";
	string scr_path = twrp_dir + "/screenshots";
	if (!Path_Exists(twrp_dir)) {
		if (!Recursive_Mkdir(twrp_dir)) {
			LOGINFO("Failed to create TWRP folder.\n");
			scr_path = current_storage_path;
		}
	} else {
		if (!Path_Exists(scr_path)) {
			if (system(("cd " + twrp_dir + " && mkdir screenshots").c_str()) != 0) {
				LOGINFO("Failed to make sub-folder for screenshots.\nRoot of %s will be used instead.\n", current_storage_path.c_str());
				scr_path = current_storage_path;
			}
		}
	}
	// Set proper filename (i.e. TWRPScr-001.bmp)
	string bmp_num = "";
	int bmp_inc = 0, count = 0;
	DIR* Dir = opendir(scr_path.c_str());
	if (Dir == NULL)
		LOGINFO("Unable to open %s\n", scr_path.c_str());
	else {
		struct dirent* DirEntry;
		while ((DirEntry = readdir(Dir)) != NULL) {
			string dname = DirEntry->d_name;
			if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
				continue;
			if (DirEntry->d_type == DT_REG) {
				// Try to parse bmp's increasement before taking shot
				size_t first_mark = dname.find("-");
				count++;
				//LOGINFO("[%i] File: %s\n", count, DirEntry->d_name);
				if (first_mark == string::npos) {
					//LOGINFO("Unable to find filename's increasement (first mark).\n");
					continue;
				}
				
				bmp_num = dname.substr(first_mark + 1, dname.size() - first_mark - 1);
				size_t last_period = bmp_num.find(".");
				if (last_period == string::npos) {
					//LOGINFO("Unable to find filename's increasement (last period).\n");
					continue;
				}
				bmp_num.resize(last_period);
				bmp_inc = atoi(bmp_num.c_str());
				// If a screenshot was deleted use that missing incr.
				if (bmp_inc != count) {
					bmp_inc = count - 1;
					break;
				}
			}
		}
		closedir(Dir);
	}

	char temp[64];
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%03d", ++bmp_inc);
	bmp_num = temp;
	string bmp_full_pth = scr_path + "/TWRPScr-" + bmp_num + ".bmp";	

    	if (gr_screenshot(bmp_full_pth.c_str()))
		LOGINFO("Saved screenshot at %s\n", bmp_full_pth.c_str());
}

/* Checks if a Directory has any of the subDirs we're looking for and
 * returns: 	 0 ~ for 0 subDirs
 *	  	-1 ~ for an invalid path
 *		 1 ~ for finding the minimum number of the subDirs we search
 */
int TWFunc::SubDir_Check(string Dir, string subDir1, string subDir2, string subDir3, string subDir4, string subDir5, int min) {
	int subDir_num = 0, wanted_subDir_num = 0;
	DIR* d = opendir(Dir.c_str());
	if (d == NULL) {
		return -1; // invalid Path
	} else {
		struct dirent* DirEntry;
		while ((DirEntry = readdir(d)) != NULL) {
			string dname = DirEntry->d_name;
			if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
				continue;
			if (DirEntry->d_type == DT_DIR) {
				subDir_num++;
				if (dname == subDir1 || dname == subDir2 || dname == subDir3 || dname == subDir4 || dname == subDir5)
					wanted_subDir_num++;
			}
		}
		closedir(d);
	}
	if (subDir_num == 0)
		return 0; // no subDirs at all
	else {
		if (wanted_subDir_num == 0)
			return 0; // none of the wanted subdirs was found
		else if (wanted_subDir_num >= min)
			return 1; // if the minimum number of subdirs were found
	}
	return 0;
}

int TWFunc::Check_su_Perms(void) {
	struct stat st;
	int ret = 0;

	if (!PartitionManager.Mount_By_Path("/system", false))
		return 0;

	// Check to ensure that perms are 6755 for all 3 file locations
	if (stat("/system/bin/su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret = 1;
		}
	}
	if (stat("/system/xbin/su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret += 2;
		}
	}
	if (stat("/system/bin/.ext/.su", &st) == 0) {
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) != (S_ISUID | S_ISGID | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || st.st_uid != 0 || st.st_gid != 0) {
			ret += 4;
		}
	}
	return ret;
}

bool TWFunc::Fix_su_Perms(void) {
	if (!PartitionManager.Mount_By_Path("/system", true))
		return false;

	string file = "/system/bin/su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/xbin/su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/bin/.ext/.su";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "6755") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	file = "/system/app/Superuser.apk";
	if (TWFunc::Path_Exists(file)) {
		if (chown(file.c_str(), 0, 0) != 0) {
			LOGERR("Failed to chown '%s'\n", file.c_str());
			return false;
		}
		if (tw_chmod(file, "0644") != 0) {
			LOGERR("Failed to chmod '%s'\n", file.c_str());
			return false;
		}
	}
	sync();
	if (!PartitionManager.UnMount_By_Path("/system", true))
		return false;
	return true;
}

int TWFunc::tw_chmod(string fn, string mode) {
	long mask = 0;

	for ( std::string::size_type n = 0; n < mode.length(); ++n) {
		if (n == 0) {
			if (mode[n] == '0')
				continue;
			if (mode[n] == '1')
				mask |= S_ISVTX;
			if (mode[n] == '2')
				mask |= S_ISGID;
			if (mode[n] == '4')
				mask |= S_ISUID;
			if (mode[n] == '5') {
				mask |= S_ISVTX;
				mask |= S_ISUID;
			}
			if (mode[n] == '6') {
				mask |= S_ISGID;
				mask |= S_ISUID;
			}
			if (mode[n] == '7') {
				mask |= S_ISVTX;
				mask |= S_ISGID;
				mask |= S_ISUID;
			}
		}
		else if (n == 1) {
			if (mode[n] == '7') {
				mask |= S_IRWXU;
			}
			if (mode[n] == '6') {
				mask |= S_IRUSR;
				mask |= S_IWUSR;
			}
			if (mode[n] == '5') {
				mask |= S_IRUSR;
				mask |= S_IXUSR;
			}
			if (mode[n] == '4')
				mask |= S_IRUSR;
			if (mode[n] == '3') {
				mask |= S_IWUSR;
				mask |= S_IRUSR;
			}
			if (mode[n] == '2')
				mask |= S_IWUSR;
			if (mode[n] == '1')
				mask |= S_IXUSR;
		}
		else if (n == 2) {
			if (mode[n] == '7') {
				mask |= S_IRWXG;
			}
			if (mode[n] == '6') {
				mask |= S_IRGRP;
				mask |= S_IWGRP;
			}
			if (mode[n] == '5') {
				mask |= S_IRGRP;
				mask |= S_IXGRP;
			}
			if (mode[n] == '4')
				mask |= S_IRGRP;
			if (mode[n] == '3') {
				mask |= S_IWGRP;
				mask |= S_IXGRP;
			}
			if (mode[n] == '2')
				mask |= S_IWGRP;
			if (mode[n] == '1')
				mask |= S_IXGRP;
		}
		else if (n == 3) {
			if (mode[n] == '7') {
				mask |= S_IRWXO;
			}
			if (mode[n] == '6') {
				mask |= S_IROTH;
				mask |= S_IWOTH;
			}
			if (mode[n] == '5') {
				mask |= S_IROTH;
				mask |= S_IXOTH;
			}
			if (mode[n] == '4')
					mask |= S_IROTH;
			if (mode[n] == '3') {
				mask |= S_IWOTH;
				mask |= S_IXOTH;
			}
			if (mode[n] == '2')
				mask |= S_IWOTH;
			if (mode[n] == '1')
				mask |= S_IXOTH;
		}
	}

	if (chmod(fn.c_str(), mask) != 0) {
		LOGERR("Unable to chmod '%s' %l\n", fn.c_str(), mask);
		return -1;
	}

	return 0;
}

bool TWFunc::Install_SuperSU(void) {
	if (!PartitionManager.Mount_By_Path("/system", true))
		return false;

	if (copy_file("/supersu/su", "/system/xbin/su", 0755) != 0) {
		LOGERR("Failed to copy su binary to /system/bin\n");
		return false;
	}
	if (copy_file("/supersu/Superuser.apk", "/system/app/Superuser.apk", 0644) != 0) {
		LOGERR("Failed to copy Superuser app to /system/app\n");
		return false;
	}
	if (!Fix_su_Perms())
		return false;
	return true;
}
