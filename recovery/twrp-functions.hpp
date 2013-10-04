#ifndef _TWRPFUNCTIONS_HPP
#define _TWRPFUNCTIONS_HPP

#include <string>
#include <vector>
#include "bootloader.h"

using namespace std;

typedef enum
{
    rb_current = 0,
    rb_system,
    rb_recovery,
    rb_poweroff,
    rb_hot,
    rb_bootloader,     // May also be fastboot
    rb_download,
    rb_sboot,
    rb_tboot,
    rb_vboot,
    rb_wboot,
    rb_xboot,
    rb_yboot,
    rb_zboot,
} RebootCommand;

typedef enum
{
    button_pressed,
    backup_completed,
    restore_completed,
    install_completed,
    parted_completed,
    sdbackup_completed,
    sdrestore_completed,
} FeedbackReason;

// Partition class
class TWFunc {
	public:
		// Trims any trailing folders or filenames from the path, also adds a leading / if not present
		static string Get_Root_Path(string Path);
		// Trims everything after the last / in the string
		static string Get_Path(string Path);
		// Trims the path off of a filename
		static string Get_Filename(string Path);
		// Installs HTC Dumlock
		static void install_htc_dumlock(void);
		// Restores the backup of boot from HTC Dumlock
		static void htc_dumlock_restore_original_boot(void);
		// Reflashes the current recovery to boot
		static void htc_dumlock_reflash_recovery_to_boot(void);
		// Recursively makes the entire path
		static int Recursive_Mkdir(string Path);
		// Gets the size of a folder and all of its subfolders using dirent and stat
		static unsigned long long Get_Folder_Size(const string& Path, bool Display_Error);
		// Returns true if the path exists
		static bool Path_Exists(string Path);
		// Updates text for display in the GUI, e.g. Backing up %partition name%
		static void GUI_Operation_Text(string Read_Value, string Default_Text);
		// Same as above but includes partition name
		static void GUI_Operation_Text(string Read_Value, string Partition_Name, string Default_Text);
		// Returns the size of a file
		static unsigned long Get_File_Size(string Path);
		// Prepares the device for rebooting
		static int tw_reboot(RebootCommand command);
		// checks for the existence of a script, chmods it to 755, then runs it
		static void check_and_run_script(const char* script_file, const char* display_name);
		//execute a command and return the result as a string by reference
		static int Exec_Cmd(string cmd, string &result);
		//execute a command
		static int Exec_Cmd(const string& cmd);
		//recursively remove a directory
		static int removeDir(const string path, bool removeParent);
		//copy file from src to dst with mode permissions
		static int copy_file(string src, string dst, int mode);
		// Returns a dirent dt_type value using stat instead of dirent
		static unsigned int Get_D_Type_From_Stat(string Path);
		// read from file
		static int read_file(string fn, string& results);
		static int read_file_line_by_line(string fn, vector<string>& lines, bool skip_empty);
		//write from file
		static int write_file(string fn, string& line);
		// Return a diff for 2 times
		static timespec timespec_diff(timespec& start, timespec& end);
		//drop linux cache memory
		static int drop_caches(string drop);
		// check perms and owner of su binary in various locations
		static int Check_su_Perms(void);
		// sets proper permissions for su binaries and superuser apk
		static bool Fix_su_Perms(void);
		// chmod function that converts a 4 char string into st_mode automatically
		static int tw_chmod(const string& fn, const string& mode);
		// Installs su binary and apk and sets proper permissions
		static bool Install_SuperSU(void);
		static void Update_Intent_File(string Intent);
		static void Copy_Log(string Source, string Destination);
		static void Update_Log_File(void);
		static int get_bootloader_msg(struct bootloader_message *out);
		static int set_bootloader_msg(const struct bootloader_message *in);
		static bool loadTheme();
		static bool reloadTheme();
		static string getUIxml(int rotation);
		static int Get_File_Type(string fn);
		static int Try_Decrypting_File(string fn, string password);
		static bool Try_Decrypting_Backup(string Restore_Path, string password);
		static int Wait_For_Child(pid_t pid, int *status, string Child_Name);

		// Extended functions
		static void Update_Rotation_File(int r);
		static int Check_Rotation_File();
		static int CheckFor_ValidIMG(string mtdName);
		static unsigned long RoundUpSize(unsigned long sz, unsigned long multiple);
		static void apply_system_tweaks(int charge_mode);
		static void toggle_keybacklight(unsigned int usec);
		static void screen_off(void);
		static void power_save(void);
		static void power_restore(int charge_mode);
		static bool replace_string(string& str, const string& search_str, const string& replace_str);
		static string to_string(int number);
		static string Find_File_On_Storage(string Filename);
		static int Take_Screenshot(void);
		static int Vibrate(FeedbackReason reason);
		static int SubDir_Check(string Dir, string subDir1, string subDir2, string subDir3, string subDir4, string subDir5, int min);
		static vector<string> split_string(const string &in, char del, bool skip_empty);
		static int Split_NandroidMD5(string File);
		static int cat_file(string fn, int print_line_length);
		// Used in restoring tar
		static int TarExtract(string tarfn, string tardir);
		static int TarEntryExists(string tarfn, string entry);
};

extern int Log_Offset;

#endif // _TWRPFUNCTIONS_HPP
