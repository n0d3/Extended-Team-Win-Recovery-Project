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

#ifndef __TWRP_Partition_Manager
#define __TWRP_Partition_Manager

#include <vector>
#include <string>

#define MAX_FSTAB_LINE_LENGTH 2048

using namespace std;

struct PartitionList {
	std::string Display_Name;
	std::string Mount_Point;
	unsigned int selected;
};

// Partition class
class TWPartition {
	public:
		enum Backup_Method_enum {
			NONE = 0,
			FILES = 1,
			DD = 2,
			FLASH_UTILS = 3,
		};

	public:
		TWPartition();
		virtual ~TWPartition();

	public:
		// Checks mount to see if the partition is currently mounted
		bool Is_Mounted();
		// Mounts the partition if it is not mounted
		bool Mount(bool Display_Error);
		// Unmounts the partition if it is mounted
		bool UnMount(bool Display_Error);
		// Wipes the partition
		bool Wipe(string New_File_System);
		// Wipes the partition
		bool Wipe();
		// Wipes android secure
		bool Wipe_AndSec();
		// Backs up the partition to the folder specified
		int Backup(string backup_folder);
		// Checks MD5 of a backup
		bool Check_MD5(string restore_folder);
		// Restores the partition using the backup folder provided
		bool Restore(string restore_folder);
		// Returns a string of the backup method for human readable output
		string Backup_Method_By_Name();
		// Decrypts the partition, return 0 for failure and -1 for success
		bool Decrypt(string Password);
		// Ignores wipe commands for /data/media devices and formats the original block device
		bool Wipe_Encryption();
		// Checks the fs type using blkid, does not do anything on MTD / yaffs2 because this crashes on some devices
		bool Check_FS_Type();
		// Updates size information
		bool Update_Size(bool Display_Error);
		// Recreates the /data/media folder
		void Recreate_Media_Folder();

		// Extended functions
		unsigned int FS_Type_Via_statfs();
		// When formatting card's partitions to different fs
		void Change_FS_Type(string type);
		void Change_Restore_Display_Name(string name);
		int Setup_Extra_Boot(string name, string mtd_num);

	public:
		// Indicates if the partition is a swap partition
		bool Swap;
		// Indicates if the partition is currently present as a block device
		bool Is_Present;
		// Overall size of the partition
		unsigned long long Size;
		// Overall free space
		unsigned long long Free;
		// Backup size -- may be different than used space especially when /data/media is present
		unsigned long long Backup_Size;
		// Current file system
		string Current_File_System;
		// Actual block device (one of primary, alternate, or decrypted)
		string Actual_Block_Device;
		// Name of the partition for MTD devices
		string MTD_Name;
		// dev/mtd/mtd# for MTD devices
		string MTD_Dev;
		// Letter used in ORS
		string ORS_Mark;

	// Extended functions
		// Size of dalvik-cache folder (used when calculating the backup size if we skip dalvik during backup)
		unsigned long long Dalvik_Cache_Size;
		// Size of all detected NativeSD Roms (used when calculating the backup size if we skip NativeSD Roms during sd-ext backup)
		unsigned long long NativeSD_Size;
		// tar '--exclude=' args (for skipping NativeSD Roms during sd-ext backup)
		string Tar_exclude;
		// Initial data_path for DataOnExt method
		string Path_For_DataOnExt;
		// For restoring a CWM-backup file type						  
		bool Use_unyaffs_To_Restore;
		// For skipping a partition if size check returns error					  
		bool Skip_From_Restore;
	
	private:
		// Processes a fstab line
		bool Process_Fstab_Line(string Line, bool Display_Error);
		// Determines the correct block device and stores it in Actual_Block_Device
		void Find_Actual_Block_Device();

	private:
		// Indicates that the partition can be mounted
		bool Can_Be_Mounted;
		// Indicates that the partition can be wiped
		bool Can_Be_Wiped;
		// Indicates that the partition will show up in the backup list
		bool Can_Be_Backed_Up;
		// Indicates that the partition will always be formatted w/ "rm -rf *"
		bool Use_Rm_Rf;
		// Indicates that this partition is wiped during a factory reset
		bool Wipe_During_Factory_Reset;
		// Indicates that the wipe can be user initiated in the GUI system
		bool Wipe_Available_in_GUI;
		// Indicates that this partition is a sub-partition of another partition (e.g. datadata is a sub-partition of data)
		bool Is_SubPartition;
		// Indicates that this partition has a sub-partition
		bool Has_SubPartition;
		// Indicates which partition is the parent partition of this partition (e.g. /data is the parent partition of /datadata)
		string SubPartition_Of;
		// Symlink path (e.g. /data/media)
		string Symlink_Path;
		// /sdcard could be the symlink mount point for /data/media
		string Symlink_Mount_Point;
		// Mount point for this partition (e.g. /system or /data)
		string Mount_Point;
		// Path for backup
		string Backup_Path;
		// Block device (e.g. /dev/block/mmcblk1p1)
		string Primary_Block_Device;
		// Alternate block device (e.g. /dev/block/mmcblk1)
		string Alternate_Block_Device;
		// Decrypted block device available after decryption
		string Decrypted_Block_Device;
		// Indicates if this partition is removable -- affects how often we check overall size, if present, etc.
		bool Removable;
		// Used by make_ext4fs to leave free space at the end of the partition block for things like a crypto footer
		int Length;
		// Overall used space
		unsigned long long Used;
		// This partition might be encrypted, affects error handling, can only be true if crypto support is compiled in
		bool Can_Be_Encrypted;
		// This partition is thought to be encrypted -- it wouldn't mount for some reason, only avialble with crypto support
		bool Is_Encrypted;
		// This partition has successfully been decrypted
		bool Is_Decrypted;
		// Display name for the GUI
		string Display_Name;
		// Backup name -- used for backup filenames
		string Backup_Name;
		// Name displayed in the partition list for backup selection
		string Backup_Display_Name;
		// Name displayed in the partition list for restore selection
		string Restore_Display_Name;
		// Name displayed in the partition list for storage selection
		string Storage_Name;
		// Actual backup filename
		string Backup_FileName;
		// Method used for backup
		Backup_Method_enum Backup_Method;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		// Indicates if this item can be encrypted during backup
		bool Can_Encrypt_Backup;
		// Indicates if we will use userdata encryption splitting on an encrypted backup
		bool Use_Userdata_Encryption;
#endif
		// Indicates presence of /data/media, may affect wiping and backup methods
		bool Has_Data_Media;
		// Indicates the presence of .android_secure on this partition
		bool Has_Android_Secure;
		// Indicates if this partition is used for storage for backup, restore, and installing zips
		bool Is_Storage;
		// Indicates that this storage partition is the location of the .twrps settings file and the location that is used for custom themes
		bool Is_Settings_Storage;
		// Indicates the path to the storage -- root indicates mount point, media/ indicates e.g. /data/media
		string Storage_Path;
		// File system from the recovery.fstab	
		string Fstab_File_System;
		// Block size for formatting
		int Format_Block_Size;
		// File system flags from recovery.fstab
		int Mount_Flags;                                                          
		// File system options from recovery.fstab
		string Mount_Options;  
		// Ignore blkid results due to superblocks lying to us on certain devices / partitions
		bool Ignore_Blkid;
		// Retains the .layout_version file during a wipe (needed on devices like Sony Xperia T where /data and /data/media are separate partitions)
		bool Retain_Layout_Version;
	#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
		// Have to store the encryption password to remount
		string EcryptFS_Password;
	#endif

	private:
		// Process custom fstab flags
		bool Process_Flags(string Flags, bool Display_Error);
		// Process standard fstab fs flags
		bool Process_FS_Flags(string& Options, int Flags);
		// Checks to see if the file system given is considered a file system
		bool Is_File_System(string File_System);
		// Checks to see if the file system given is considered an image
		bool Is_Image(string File_System);
		// Sets defaults for a file system partition
		void Setup_File_System(bool Display_Error);
		// Sets defaults for an image partition
		void Setup_Image(bool Display_Error);
		// Sets up .android_secure settings
		void Setup_AndSec(void);
		// Checks the block device given and follows symlinks until it gets to the real block device
		void Find_Real_Block_Device(string& Block_Device, bool Display_Error);
		// Finds the partition size from /proc/partitions
		bool Find_Partition_Size();
		// Uses du to get sizes
		unsigned long long Get_Size_Via_du(string Path, bool Display_Error);
		// Formats as ext3 or ext2
		bool Wipe_EXT23(string File_System);
		// Formats using ext4, uses make_ext4fs when present
		bool Wipe_EXT4();
		// Formats as FAT if mkdosfs exits otherwise rm -rf wipe
		bool Wipe_FAT();
		// Formats as EXFAT
		bool Wipe_EXFAT();
		// Formats as yaffs2 for MTD memory types
		bool Wipe_YAFFS2();
		// Format using erase_image for MTD memory types
		bool Wipe_MTD();
		// Uses rm -rf to wipe
		bool Wipe_RMRF();
		// Uses mkfs.f2fs to wipe
		bool Wipe_F2FS();
		// Uses rm -rf to wipe but does not wipe /data/media
		bool Wipe_Data_Without_Wiping_Media();
		// Backs up using tar for file systems
		int Backup_Tar(string backup_folder);
		// Backs up using dd for emmc memory types
		int Backup_DD(string backup_folder);
		// Backs up using dump_image for MTD memory types
		int Backup_Dump_Image(string backup_folder);
		// Restore using tar for file systems
		bool Restore_Tar(string restore_folder, string Restore_File_System);
		// Restore using dd for emmc memory types
		bool Restore_DD(string restore_folder);
		// Restore using flash_image for MTD memory types
		bool Restore_Flash_Image(string restore_folder);
		// Get Partition size, used, and free space using statfs
		bool Get_Size_Via_statfs(bool Display_Error);
		// Get Partition size, used, and free space using df command
		bool Get_Size_Via_df(bool Display_Error);
		// Creates a directory if it doesn't already exist
		bool Make_Dir(string Path, bool Display_Error);
		// Finds the mtd block device based on the name from the fstab
		bool Find_MTD_Block_Device(string MTD_Name);
		// Recreates the .android_secure folder
		void Recreate_AndSec_Folder(void);
		// Tries multiple times with a half second delay to mount a device in case storage is slow to mount
		void Mount_Storage_Retry(void);

	// Extended functions
		void Recreate_Cache_Recovery_Folder(void);
		// Returns the partition size from /proc/partitions
		unsigned long long Get_Blk_Size(void);
		// Checks to see if the file system given is swap
		bool Is_Swap(string File_System);
		// Formats using nilfs2
		bool Wipe_NILFS2();
		// Formats using ntfs
		bool Wipe_NTFS();
		// Restore using unyaffs for cwm-backup file type
		bool Restore_Yaffs_Image(string restore_folder);
		// Check sd-ext partition for NativeSD Roms
		void CheckFor_NativeSD(void);
		// Check partition for dalvik-cache
		void CheckFor_Dalvik_Cache(void);
		// Check sd-ext partition if being used as /data
		int CheckFor_DataOnExt(void);
		// Recreates the data folder used for DataOnExt feature
		void Recreate_DataOnExt_Folder(void);
		void Check_BuildProp(void);
		string Alternate_Display_Name;

	friend class TWPartitionManager;
	friend class DataManager;
	friend class GUIPartitionList;
};

class TWPartitionManager {
	public:
		TWPartitionManager() {}
		~TWPartitionManager() {}

	public:
		// Parses the fstab and populates the partitions
		int Process_Fstab(string Fstab_Filename, bool Display_Error);
		// Creates /etc/fstab file that's used by the command line for mount commands
		int Write_Fstab();
		// Outputs partition information to the log
		void Output_Partition_Logging();
		// Mounts partition based on path (e.g. /system)
		int Mount_By_Path(string Path, bool Display_Error);
		// Mounts partition based on block device (e.g. /dev/block/mmcblk1p1)
		int Mount_By_Block(string Block, bool Display_Error);
		// Mounts partition based on display name (e.g. System)
		int Mount_By_Name(string Name, bool Display_Error);
		// Unmounts partition based on path
		int UnMount_By_Path(string Path, bool Display_Error);
		// Unmounts partition based on block device
		int UnMount_By_Block(string Block, bool Display_Error);
		// Unmounts partition based on display name
		int UnMount_By_Name(string Name, bool Display_Error);
		// Checks if partition is mounted based on path
		int Is_Mounted_By_Path(string Path);
		// Checks if partition is mounted based on block device
		int Is_Mounted_By_Block(string Block);
		// Checks if partition is mounted based on display name
		int Is_Mounted_By_Name(string Name);
		// Mounts the current storage location
		int Mount_Current_Storage(bool Display_Error);
		// Mounts the settings file storage location (usually internal)
		int Mount_Settings_Storage(bool Display_Error);
		// Returns a pointer to a partition based on path
		TWPartition* Find_Partition_By_Path(string Path);
		// Returns a pointer to a partition based on block device
		TWPartition* Find_Partition_By_Block(string Block);
		// Returns a pointer to a partition based on name
		TWPartition* Find_Partition_By_Name(string Name);
		// Checks the current backup name to ensure that it is valid
		int Check_Backup_Name(bool Display_Error);
		// Initiates a backup in the current storage
		bool Run_Backup();
		// Restores a backup
		int Run_Restore(string Restore_Name);
		// Used to gather a list of available backup partitions for the user to select for a restore
		void Set_Restore_Files(string Restore_Name);
		// Wipes a partition based on path
		int Wipe_By_Path(string Path);
		// Wipes a partition based on block device
		int Wipe_By_Block(string Block);
		// Wipes a partition based on display name
		int Wipe_By_Name(string Name);
		// Performs a factory reset
		int Factory_Reset();
		// Wipes dalvik cache
		int Wipe_Dalvik_Cache();
		// Wipes rotation data -- 
		int Wipe_Rotate_Data();
		// Wipe battery stats -- /data/system/batterystats.bin
		int Wipe_Battery_Stats();
		// Wipes android secure
		int Wipe_Android_Secure();
		// Really formats data on /data/media devices -- also removes encryption
		int Format_Data();
		// Removes and recreates the media folder on /data/media devices
		int Wipe_Media_From_Data();
		// Refreshes size data of partitions
		void Refresh_Sizes(bool Display_Msg);
		// Updates fstab, file systems, sizes, etc.
		void Update_System_Details(bool Display_Msg);
		// Attempt to decrypt any encrypted partitions
		int Decrypt_Device(string Password);
		// Enable USB storage mode
		int usb_storage_enable(void);
		// Disable USB storage mode
		int usb_storage_disable(void);
		// Mounts all storage locations
		void Mount_All_Storage(void);
		// Unmounts system and data if not data/media and boot if boot is mountable
		void UnMount_Main_Partitions(void);
		// Repartitions the sdcard
		int Partition_SDCard(void);
		int Format_SDCard(string cmd);
		int Fix_Permissions();
		// Generates an MD5 after a backup is made
		bool Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename);
		void Get_Partition_List(string ListType, std::vector<PartitionList> *Partition_List);
		// Creates a /cache/recovery/storage.fstab file with a list of all potential storage locations for app use
		void Output_Storage_Fstab();

	// Extended functions
		static int Fstab_Proc_Done;
		static int SD_Partitioning_Done_Once;
		// Wipe all partitions except /sdcard
		int Wipe_All_But_SDCARD(void);
		// Check filesystem of SD Card's partitions
		int Check_SDCard(void);
		// Set filesystem on ext
		int FSConvert_SDEXT(string extpath);
		void Process_Extra_Boot_Partitions(void);

	private:
		int Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5,
					unsigned long long* img_bytes_remaining,
					unsigned long long* file_bytes_remaining,
					unsigned long *img_time, unsigned long *file_time,
					unsigned long long *img_bytes, unsigned long long *file_bytes);
		bool Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count);
		void Output_Partition(TWPartition* Part);
		int Open_Lun_File(string Partition_Path, string Lun_File);

	private:
		// Vector list of all partitions
		std::vector<TWPartition*> Partitions;                                     
};

extern TWPartitionManager PartitionManager;

#endif // __TWRP_Partition_Manager
