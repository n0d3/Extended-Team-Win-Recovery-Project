/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _VARIABLES_HEADER_
#define _VARIABLES_HEADER_

// Flag for hd2-specific options in the ui 
#define TW_HTC_LEO			"tw_htc_leo"

// Set to run the preboot.sh which edits the recovery.fstab according to bootloader
#define TW_BOOT_IS_MTD			"tw_boot_is_mtd"

// Exclude dalvik-cache and nativesd roms from backup
#define TW_SKIP_DALVIK			"tw_skip_dalvik"
#define TW_SKIP_NATIVESD	    	"tw_skip_nativesd"

// 2nd ext-partition
#define TW_SDEXT2_SIZE              	"tw_sdext2_size"
#define TW_HAS_SDEXT2_PARTITION    	"tw_has_sdext2_partition"
#define TW_USE_SDEXT2_PARTITION     	"tw_use_sdext2_partition"
#define TW_SDPART2_FILE_SYSTEM      	"tw_sdpart2_file_system"

// Screenshot
#define TW_SCREENSHOT_VAR	    	"tw_screenshot_var"

// NativeSD Manager
#define TW_SDBACKUPS_FOLDER_VAR     	"tw_sdbackups_folder"
#define TW_SD_BACKUP_RESTORE_SYSTEM 	"tw_sd_backup_restore_system"
#define TW_SD_BACKUP_RESTORE_DATA   	"tw_sd_backup_restore_data"
#define TW_SD_BACKUP_RESTORE_BOOT   	"tw_sd_backup_restore_boot"
#define TW_SD_USE_COMPRESSION_VAR      	"tw_sd_use_compression"
#define TW_SD_SKIP_MD5_CHECK_VAR       	"tw_sd_skip_md5_check"
#define TW_SD_SKIP_MD5_GENERATE_VAR    	"tw_sd_skip_md5_generate"
#define TW_SD_SKIP_DALVIK		"tw_sd_skip_dalvik"

// cLK's extra boot partition
#define TW_SDBOOT_PARTITION  	    	"tw_sdboot_partition"

// check restore size and adjust ptable if needed
#define TW_HANDLE_RESTORE_SIZE        	"tw_handle_restore_size"
#define TW_INCR_SIZE		 	"tw_incr_size"
#define TW_SKIP_SD_FREE_SZ_CHECK	"tw_skip_sd_free_sz_check"

// DataOnExt
#define TW_DATA_PATH		    	"tw_data_path"
#define TW_DATA_ON_EXT			"tw_data_on_ext"
#define TW_DATA_ON_EXT_CHECK	    	"tw_data_on_ext_check"
#define TW_RESTORE_IS_DATAONEXT         "tw_restore_is_dataonext"

// Theme file
#define TW_THEME_FOLDER_VAR		"tw_theme_folder"
#define TW_SEL_THEME_PATH		"tw_sel_theme_path"

// Scripts folder
#define TW_SCRIPTS_FOLDER_VAR     	"tw_scripts_folder"

// App folder
#define TW_APP_FOLDER_VAR     		"tw_app_folder"

// tune2fs -c
#define TW_NUM_OF_MOUNTS_FOR_FS_CHK 	"tw_num_of_mounts_for_fs_check"

// mkntfs -f or -q option
#define TW_MKNTFS_QUICK_FORMAT      	"tw_mkntfs_quick_format"

// Save contents before wiping
#define TW_RESCUE_EXT_CONTENTS      	"tw_rescue_ext_contents"

// Extra filesystems
#define TW_SHOW_NTFS                	"tw_show_ntfs"
#define TW_SHOW_EXFAT                	"tw_show_exfat"

// Root handling
#define TW_HANDLE_SU                	"tw_handle_su"

// Haptic feedback
#define TW_VIBRATE_AFTER_BUTTON_PRESS  	"tw_vibrate_after_button_press"
#define TW_BUTTON_FEEDBACK_DURATION_MS	"tw_button_feedback_duration_ms"
#define TW_VIBRATE_AFTER_BACKUP        	"tw_vibrate_after_backup"
#define TW_BACKUP_FEEDBACK_DURATION_MS	"tw_backup_feedback_duration_ms"
#define TW_VIBRATE_AFTER_RESTORE       	"tw_vibrate_after_restore"
#define TW_RESTORE_FEEDBACK_DURATION_MS	"tw_restore_feedback_duration_ms"
#define TW_VIBRATE_AFTER_INSTALL       	"tw_vibrate_after_install"
#define TW_INSTALL_FEEDBACK_DURATION_MS	"tw_install_feedback_duration_ms"
#define TW_VIBRATE_AFTER_PARTED        	"tw_vibrate_after_parted"
#define TW_PARTED_FEEDBACK_DURATION_MS	"tw_parted_feedback_duration_ms"
#define TW_VIBRATE_AFTER_SDBACKUP       "tw_vibrate_after_sdbackup"
#define TW_SDBACKUP_FEEDBACK_DURATION_MS "tw_sdbackup_feedback_duration_ms"
#define TW_VIBRATE_AFTER_SDRESTORE      "tw_vibrate_after_sdrestore"
#define TW_SDRESTORE_FEEDBACK_DURATION_MS "tw_sdrestore_feedback_duration_ms"

// Tweaks
#define TW_SET_DROP_CACHES_AT_BOOT	"tw_set_drop_caches_at_boot"
#define TW_DROP_CACHES			"tw_drop_caches"
#define TW_SET_IO_SCHED_AT_BOOT		"tw_set_io_sched_at_boot"
#define TW_IO_SCHED			"tw_io_sched"
#define TW_SET_CPU_F_AT_BOOT		"tw_set_cpu_f_at_boot"
#define TW_MAX_CPU_F			"tw_max_cpu_f"
#define TW_MIN_CPU_F			"tw_min_cpu_f"
#define TW_SET_CPU_GOV_AT_BOOT		"tw_set_cpu_gov_at_boot"
#define TW_CPU_GOV			"tw_cpu_gov"

// Default definitions
#define TW_VERSION_STR			"2.5.0.4"

#define TW_USE_COMPRESSION_VAR      	"tw_use_compression"
#define TW_SKIP_MD5_CHECK_VAR       	"tw_skip_md5_check"
#define TW_SKIP_MD5_GENERATE_VAR    	"tw_skip_md5_generate"
#define TW_SIGNED_ZIP_VERIFY_VAR    	"tw_signed_zip_verify"

#define TW_FILENAME                 	"tw_filename"
#define TW_ZIP_INDEX                	"tw_zip_index"
#define TW_ZIP_QUEUE_COUNT       	"tw_zip_queue_count"

#define MAX_BACKUP_NAME_LEN 64
#define TW_BACKUP_TEXT              	"tw_backup_text"
#define TW_BACKUP_NAME		        "tw_backup_name"
#define TW_BACKUP_AVG_IMG_RATE      	"tw_backup_avg_img_rate"
#define TW_BACKUP_AVG_FILE_RATE     	"tw_backup_avg_file_rate"
#define TW_BACKUP_AVG_FILE_COMP_RATE    "tw_backup_avg_file_comp_rate"
#define TW_BACKUP_SYSTEM_SIZE       	"tw_backup_system_size"

#define TW_STORAGE_FREE_SIZE        	"tw_storage_free_size"
#define TW_GENERATE_MD5_TEXT        	"tw_generate_md5_text"

#define TW_RESTORE_TEXT             	"tw_restore_text"
#define TW_RESTORE_BOOT_VAR         	"tw_restore_boot"
#define TW_RESTORE_AVG_IMG_RATE     	"tw_restore_avg_img_rate"
#define TW_RESTORE_AVG_FILE_RATE    	"tw_restore_avg_file_rate"
#define TW_RESTORE_AVG_FILE_COMP_RATE   "tw_restore_avg_file_comp_rate"
#define TW_RESTORE_FILE_DATE        	"tw_restore_file_date"
#define TW_VERIFY_MD5_TEXT          	"tw_verify_md5_text"
#define TW_UPDATE_SYSTEM_DETAILS_TEXT 	"tw_update_system_details_text"

#define TW_SHOW_SPAM_VAR            	"tw_show_spam"
#define TW_COLOR_THEME_VAR          	"tw_color_theme"
#define TW_VERSION_VAR              	"tw_version"
#define TW_SORT_FILES_BY_DATE_VAR   	"tw_sort_files_by_date"
#define TW_GUI_SORT_ORDER           	"tw_gui_sort_order"
#define TW_ZIP_LOCATION_VAR         	"tw_zip_location"
#define TW_ZIP_INTERNAL_VAR         	"tw_zip_internal"
#define TW_ZIP_EXTERNAL_VAR         	"tw_zip_external"
#define TW_REBOOT_AFTER_FLASH_VAR   	"tw_reboot_after_flash_option"
#define TW_TIME_ZONE_VAR            	"tw_time_zone"
#define TW_DISP_TIME_ZONE_VAR          	"tw_display_time_zone"
#define TW_RM_RF_VAR                	"tw_rm_rf"

#define TW_BACKUPS_FOLDER_VAR       	"tw_backups_folder"

#define TW_SDEXT_SIZE               	"tw_sdext_size"
#define TW_SWAP_SIZE                	"tw_swap_size"
#define TW_SDPART_FILE_SYSTEM       	"tw_sdpart_file_system"
#define TW_TIME_ZONE_GUISEL         	"tw_time_zone_guisel"
#define TW_TIME_ZONE_GUIOFFSET      	"tw_time_zone_guioffset"
#define TW_TIME_ZONE_GUIDST         	"tw_time_zone_guidst"

#define TW_ACTION_BUSY              	"tw_busy"

#define TW_ALLOW_PARTITION_SDCARD   	"tw_allow_partition_sdcard"

#define TW_SCREEN_OFF               	"tw_screen_off"

#define TW_REBOOT_SYSTEM            	"tw_reboot_system"
#define TW_REBOOT_RECOVERY          	"tw_reboot_recovery"
#define TW_REBOOT_POWEROFF          	"tw_reboot_poweroff"
#define TW_REBOOT_BOOTLOADER        	"tw_reboot_bootloader"

#define TW_HAS_DUAL_STORAGE         	"tw_has_dual_storage"
#define TW_USE_EXTERNAL_STORAGE     	"tw_use_external_storage"
#define TW_HAS_INTERNAL             	"tw_has_internal"
#define TW_INTERNAL_PATH            	"tw_internal_path"         // /data/media or /internal
#define TW_INTERNAL_MOUNT           	"tw_internal_mount"        // /data or /internal
#define TW_INTERNAL_LABEL           	"tw_internal_label"        // data or internal
#define TW_HAS_EXTERNAL             	"tw_has_external"
#define TW_EXTERNAL_PATH            	"tw_external_path"         // /sdcard or /external/sdcard2
#define TW_EXTERNAL_MOUNT           	"tw_external_mount"        // /sdcard or /external
#define TW_EXTERNAL_LABEL           	"tw_external_label"        // sdcard or external

#define TW_HAS_DATA_MEDIA           	"tw_has_data_media"

#define TW_HAS_BOOT_PARTITION       	"tw_has_boot_partition"
#define TW_HAS_RECOVERY_PARTITION   	"tw_has_recovery_partition"
#define TW_HAS_ANDROID_SECURE       	"tw_has_android_secure"
#define TW_HAS_SDEXT_PARTITION      	"tw_has_sdext_partition"
#define TW_HAS_USB_STORAGE          	"tw_has_usb_storage"
#define TW_NO_BATTERY_PERCENT       	"tw_no_battery_percent"
#define TW_POWER_BUTTON             	"tw_power_button"
#define TW_SIMULATE_ACTIONS         	"tw_simulate_actions"
#define TW_SIMULATE_FAIL            	"tw_simulate_fail"
#define TW_DONT_UNMOUNT_SYSTEM      	"tw_dont_unmount_system"
// #define TW_ALWAYS_RMRF              	"tw_always_rmrf"

#define TW_SHOW_DUMLOCK             	"tw_show_dumlock"
#define TW_HAS_INJECTTWRP           	"tw_has_injecttwrp"
#define TW_INJECT_AFTER_ZIP         	"tw_inject_after_zip"
#define TW_HAS_DATADATA             	"tw_has_datadata"
#define TW_FLASH_ZIP_IN_PLACE       	"tw_flash_zip_in_place"
#define TW_MIN_SYSTEM_SIZE          	"50" // minimum system size to allow a reboot
#define TW_MIN_SYSTEM_VAR           	"tw_min_system"
#define TW_DOWNLOAD_MODE            	"tw_download_mode"
#define TW_IS_ENCRYPTED             	"tw_is_encrypted"
#define TW_IS_DECRYPTED             	"tw_is_decrypted"
#define TW_HAS_CRYPTO               	"tw_has_crypto"
#define TW_CRYPTO_PASSWORD          	"tw_crypto_password"
#define TW_DATA_BLK_DEVICE          	"tw_data_blk_device"  // Original block device - not decrypted
#define TW_SDEXT_DISABLE_EXT4       	"tw_sdext_disable_ext4"
#define TW_MILITARY_TIME		"tw_military_time"

// Also used:
//   tw_boot_is_mountable
//   tw_system_is_mountable
//   tw_data_is_mountable
//   tw_cache_is_mountable
//   tw_sdcext_is_mountable
//   tw_sdcint_is_mountable
//   tw_sd-ext_is_mountable

#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/devices/platform/usb_mass_storage/lun%d/file"
#endif

#ifndef TW_BRIGHTNESS_PATH
#define TW_BRIGHTNESS_PATH /nobrightness
#endif

// For OpenRecoveryScript
#define SCRIPT_FILE_CACHE "/cache/recovery/openrecoveryscript"
#define SCRIPT_FILE_TMP "/tmp/openrecoveryscript"
#define TMP_LOG_FILE "/tmp/recovery.log"

// Max archive size for tar backups before we split
//536870912LLU	= 512MB 
//1610612736LLU = 1.5GB
//4294967296LLU	= 4.0GB
#define MAX_ARCHIVE_SIZE 		1610612736LLU

#endif  // _VARIABLES_HEADER_
