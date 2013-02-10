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
#include <iostream>
#include <fstream>
#include <sstream>
#include "twrp-functions.hpp"
#include "partitions.hpp"
#include "common.h"
#include "data.hpp"
#include "bootloader.h"
#include "variables.h"
extern "C" {
	#include "minuitwrp/minui.h"
	#include "libcrecovery/common.h"
}

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

/*  Checks md5 for a path
    Return values:
        -1 : MD5 does not exist
        0 : Failed
        1 : Success */
int TWFunc::Check_MD5(string File) {
	int ret;
	string Command, DirPath, MD5_File, Sline, Filename, MD5_File_Filename, OK;
	char line[255];
	size_t pos;
	string result;

	MD5_File = File + ".md5";
	if (Path_Exists(MD5_File)) {
		DirPath = Get_Path(File);
		MD5_File = Get_Filename(MD5_File);
		Command = "cd '" + DirPath + "' && /sbin/busybox md5sum -c " + MD5_File;
		Exec_Cmd(Command, result);
		pos = result.find(":");
		if (pos != string::npos) {
			Filename = Get_Filename(File);
			MD5_File_Filename = result.substr(0, pos);
			OK = result.substr(pos + 2, result.size() - pos - 2);
			if (Filename == MD5_File_Filename && (OK == "OK" || OK == "OK\n")) {
				//MD5 is good, return 1
				ret = 1;
			} else {
				// MD5 is bad, return 0
				ret = 0;
			}
		} else {
			// MD5 is bad, return 0
			ret = 0;
		}
	} else {
		//No md5 file, return -1
		ret = -1;
	}

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

	ui_print("Installing HTC Dumlock to system...\n");
	copy_file("/res/htcd/htcdumlocksys", "/system/bin/htcdumlock", 0755);
	if (!Path_Exists("/system/bin/flash_image")) {
		ui_print("Installing flash_image...\n");
		copy_file("/res/htcd/flash_imagesys", "/system/bin/flash_image", 0755);
		need_libs = 1;
	} else
		ui_print("flash_image is already installed, skipping...\n");
	if (!Path_Exists("/system/bin/dump_image")) {
		ui_print("Installing dump_image...\n");
		copy_file("/res/htcd/dump_imagesys", "/system/bin/dump_image", 0755);
		need_libs = 1;
	} else
		ui_print("dump_image is already installed, skipping...\n");
	if (need_libs) {
		ui_print("Installing libs needed for flash_image and dump_image...\n");
		copy_file("/res/htcd/libbmlutils.so", "/system/lib/libbmlutils.so", 0755);
		copy_file("/res/htcd/libflashutils.so", "/system/lib/libflashutils.so", 0755);
		copy_file("/res/htcd/libmmcutils.so", "/system/lib/libmmcutils.so", 0755);
		copy_file("/res/htcd/libmtdutils.so", "/system/lib/libmtdutils.so", 0755);
	}
	ui_print("Installing HTC Dumlock app...\n");
	mkdir("/data/app", 0777);
	unlink("/data/app/com.teamwin.htcdumlock*");
	copy_file("/res/htcd/HTCDumlock.apk", "/data/app/com.teamwin.htcdumlock.apk", 0777);
	sync();
	ui_print("HTC Dumlock is installed.\n");
}

void TWFunc::htc_dumlock_restore_original_boot(void) {
	string status;
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;

	ui_print("Restoring original boot...\n");
	Exec_Cmd("htcdumlock restore", status);
	ui_print("Original boot restored.\n");
}

void TWFunc::htc_dumlock_reflash_recovery_to_boot(void) {
	string status;
	if (!PartitionManager.Mount_By_Path("/sdcard", true))
		return;
	ui_print("Reflashing recovery to boot...\n");
	Exec_Cmd("htcdumlock recovery noreboot", status);
	ui_print("Recovery is flashed to boot.\n");
}

int TWFunc::Recursive_Mkdir(string Path) {
	string pathCpy = Path;
	string wholePath;
	size_t pos = pathCpy.find("/", 2);

	while (pos != string::npos)
	{
		wholePath = pathCpy.substr(0, pos);
		if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST) {
			LOGE("Unable to create folder: %s  (errno=%d)\n", wholePath.c_str(), errno);
			return false;
		}

		pos = pathCpy.find("/", pos + 1);
	}
	if (mkdir(wholePath.c_str(), 0777) && errno != EEXIST)
		return false;
	return true;
}

unsigned long long TWFunc::Get_Folder_Size(const string& Path, bool Display_Error) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	unsigned long long dusize = 0;
	unsigned long long dutemp = 0;

	d = opendir(Path.c_str());
	if (d == NULL)
	{
		LOGE("error opening '%s'\n", Path.c_str());
		LOGE("error: %s\n", strerror(errno));
		return 0;
	}

	while ((de = readdir(d)) != NULL)
	{
		if (de->d_type == DT_DIR && strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0)
		{
			dutemp = Get_Folder_Size((Path + "/" + de->d_name), Display_Error);
			dusize += dutemp;
			dutemp = 0;
		}
		else if (de->d_type == DT_REG)
		{
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

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";

// close a file, log an error if the error indicator is set
void TWFunc::check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

void TWFunc::copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
void TWFunc::twfinish_recovery(const char *send_intent) {
	// By this point, we're ready to return to the main system...
	if (send_intent != NULL) {
		FILE *fp = fopen_path(INTENT_FILE, "w");
		if (fp == NULL) {
		    LOGE("Can't open %s\n", INTENT_FILE);
		} else {
		    fputs(send_intent, fp);
		    check_and_fclose(fp, INTENT_FILE);
		}
	}

	// Copy logs to cache so the system can find out what happened.
	copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
	copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
	copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
	chmod(LOG_FILE, 0600);
	chown(LOG_FILE, 1000, 1000);   // system user
	chmod(LAST_LOG_FILE, 0640);
	chmod(LAST_INSTALL_FILE, 0644);

	// Reset to normal system boot so recovery won't cycle indefinitely.
	struct bootloader_message boot;
	memset(&boot, 0, sizeof(boot));
	set_bootloader_message(&boot);

	// Remove the command file, so recovery won't repeat indefinitely.
	if (!PartitionManager.Mount_By_Path("/system", true) || (unlink(COMMAND_FILE) && errno != ENOENT)) {
        	LOGW("Can't unlink %s\n", COMMAND_FILE);
	}

	PartitionManager.UnMount_By_Path("/cache", true);
	sync();  // For good measure.
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::tw_reboot(RebootCommand command)
{
	// Always force a sync before we reboot
    sync();

    	switch (command) {
    		case rb_current:
    		case rb_system:
        		twfinish_recovery("s");
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

void TWFunc::check_and_run_script(const char* script_file, const char* display_name)
{
	// Check for and run startup script if script exists
	struct stat st;
	string result;
	if (stat(script_file, &st) == 0) {
		ui_print("Running %s script...\n", display_name);
		chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		TWFunc::Exec_Cmd(script_file, result);
		ui_print("Finished running %s script.\n", display_name);
	}
}

int TWFunc::removeDir(const string path, bool skipParent) {
	DIR *d = opendir(path.c_str());
	int r = 0;
	string new_path;

	if (d == NULL) {
		LOGE("Error opening '%s'\n", path.c_str());
		return -1;
	}

	if (d) {
		struct dirent *p;
		while (!r && (p = readdir(d))) {
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;
			LOGI("checking :%s\n", p->d_name);
			new_path = path + "/";
			new_path.append(p->d_name);
			if (p->d_type == DT_DIR) {
				r = removeDir(new_path, true);
				if (!r) {
					if (p->d_type == DT_DIR) 
						r = rmdir(new_path.c_str());
					else
						LOGI("Unable to removeDir '%s': %s\n", new_path.c_str(), strerror(errno));
				}
			} else if (p->d_type == DT_REG || p->d_type == DT_LNK || p->d_type == DT_FIFO || p->d_type == DT_SOCK) {
				r = unlink(new_path.c_str());
				if (!r)
					LOGI("Unable to unlink '%s'\n", new_path.c_str());
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
	LOGI("Copying file %s to %s\n", src.c_str(), dst.c_str());
	ifstream srcfile(src.c_str(), ios::binary);
	ofstream dstfile(dst.c_str(), ios::binary);
	dstfile << srcfile.rdbuf();
	srcfile.close();
	dstfile.close();
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

bool TWFunc::replace_string(string str, const string search_str, const string replace_str) {
	size_t start_pos = str.find(search_str);
	if(start_pos != string::npos) {
		str.replace(start_pos, search_str.length(), replace_str);
        	return true;
	}

	return false; // search_str not found
}

string TWFunc::to_string(int number) {
/*
	char temp[255];
	string snumber;
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%i", number);
	snumber = temp;
	return snumber;
*/
	stringstream ss;
	ss << number;

	return ss.str();
}

// Check a tar file for a given entry
bool TWFunc::Tar_Entry_Exists(string tar_file, string entry, int level) {
	bool ret = false;
	string cmd, result, line;

	cmd = "tar -tf " + tar_file;
	if (level)
		cmd += " | sed " + to_string(level) + "q";
	TWFunc::Exec_Cmd(cmd, result);
	if (!result.empty()) {
		istringstream f(result);
		while (getline(f, line)) {
			if (line.find(entry) != string::npos) {
				ret = true;
				break;
			}
		}
	}

	return ret;
}

// Compressed or Uncompressed archive?
int TWFunc::Get_Archive_Type(string FilePath) {
        int type = 0;
        string::size_type i = 0;
        int firstbyte = 0, secondbyte = 0;
	char header[3];
        
        ifstream f;
        f.open(FilePath.c_str(), ios::in | ios::binary);
        f.get(header, 3);
        f.close();
        firstbyte = header[i] & 0xff;
        secondbyte = header[++i] & 0xff;

        if (firstbyte == 0x1f && secondbyte == 0x8b)		
		type = 1;
	else
		type = 0;

	return type;
}

// Total bytes of archive's content
unsigned long long TWFunc::Get_Archive_Uncompressed_Size(string FilePath) {
	int type = 0;
        unsigned long long total_size = 0;
	string Tar, Command, result, line;

	Tar = Get_Filename(FilePath);
	LOGI("%s is ", Tar.c_str());
	type = Get_Archive_Type(FilePath);
	if (type == 0) {
		LOGI("uncompressed archive.\n");
		Command = "tar tvf " + FilePath + " | sed 's! \\+! !g' | cut -f3 -d' '";
	} else {
		LOGI("compressed archive.\n");
		Command = "tar tzvf " + FilePath + " | sed 's! \\+! !g' | cut -f3 -d' '";
	}
	TWFunc::Exec_Cmd(Command, result);
	if (!result.empty()) {
		istringstream f(result);
		while (getline(f, line)) {
			total_size += atoi(line.c_str());
		}
	}
	LOGI("[Uncompressed size: %llu bytes]\n", total_size);
	// adding 5% just to be safe sounds like a good idea
	total_size *= 1.05;

	return total_size;
}

// Returns full-path of Filename if found on storage
string TWFunc::Find_File_On_Storage(string Filename) {
	string Full_Path = Filename;
	string File_Name = Get_Filename(Filename);
	string Current_Storage_Path = DataManager::GetCurrentStoragePath();
	if (!PartitionManager.Is_Mounted_By_Path(Current_Storage_Path))
		PartitionManager.Mount_Current_Storage(false);

	if (File_Name.size() != 0) {
		LOGI("Scanning storage for %s...\n", File_Name.c_str());
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

void TWFunc::Take_Screenshot(void) {
	// Where to store the screenshot
	string current_storage_path = DataManager::GetCurrentStoragePath();
	string twrp_dir = current_storage_path + "/TWRP";
	string scr_path = twrp_dir + "/screenshots";
	if (!Path_Exists(twrp_dir)) {
		if (!Recursive_Mkdir(twrp_dir)) {
			LOGI("Failed to create TWRP folder.\n");
			scr_path = current_storage_path;
		}
	} else {
		if (!Path_Exists(scr_path)) {
			if (system(("cd " + twrp_dir + " && mkdir screenshots").c_str()) != 0) {
				LOGI("Failed to make sub-folder for screenshots.\nRoot of %s will be used instead.\n", current_storage_path.c_str());
				scr_path = current_storage_path;
			}
		}
	}
	// Set proper filename (i.e. TWRPScr-001.png)
	string png_num = "";
	int png_inc = 0, count = 0;
	DIR* Dir = opendir(scr_path.c_str());
	if (Dir == NULL)
		LOGI("Unable to open %s\n", scr_path.c_str());
	else {
		struct dirent* DirEntry;
		while ((DirEntry = readdir(Dir)) != NULL) {
			string dname = DirEntry->d_name;
			if (!strcmp(DirEntry->d_name, ".") || !strcmp(DirEntry->d_name, ".."))
				continue;
			if (DirEntry->d_type == DT_REG) {
				count++;
				// Try to parse png's increasement before taking shot
				size_t first_mark = dname.find("-");
				if (first_mark == string::npos) {
					LOGI("Unable to find png's increasement (first mark).\n");
					break;
				}
				png_num = dname.substr(first_mark + 1, dname.size() - first_mark - 1);
				size_t last_period = png_num.find(".");
				if (last_period == string::npos) {
					LOGI("Unable to find png's increasement (last period).\n");
					break;
				}
				png_num.resize(last_period);
				png_inc = atoi(png_num.c_str());
				// If a screenshot was deleted use that missing incr.
				if (png_inc != count) {
					png_inc = count - 1;
					break;
				}
			}
		}
		closedir(Dir);
	}

	char temp[64];
	memset(temp, 0, sizeof(temp));
	sprintf(temp, "%03d", ++png_inc);
	png_num = temp;
	
    	FILE *fb_in = NULL;
    	fb_in = fopen("/dev/graphics/fb0", "r");
    	if (fb_in == NULL) {
        	LOGE("error: could not read framebuffer\n");
        	return;
    	}

	string png_full_pth = scr_path + "/TWRPScr-" + png_num + ".png";
	FILE *png = NULL;
    	png = fopen(png_full_pth.c_str(), "w");
    	if (!png) {
        	LOGE("error: writing *.png file\n");
        	return;
    	} else
		LOGI("Saving screenshot at %s\n", png_full_pth.c_str());

    	gr_screenshot(fb_in, png);
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
