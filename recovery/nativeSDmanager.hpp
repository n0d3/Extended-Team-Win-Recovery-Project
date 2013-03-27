#ifndef __TWRP_NativeSD_Manager
#define __TWRP_NativeSD_Manager

#include <string>

using namespace std;

class TWNativeSDManager {
	public:                              
		// Backup NativeSD Rom
		int Backup(string RomPath);                              
		// Restore NativeSD Rom
		int Restore(string RomPath);  
		// Copy NativeSD Rom's zImage and initrd.gz to /sdcard/NativeSD in order to load that rom after a reboot [not for cLK]
		int Prep_Rom_To_Boot(string RomPath);                         
		// Delete NativeSD Rom
		int Delete(string RomPath);                              
		// Fix permissions on NativeSD Rom
		int Fix_Perm(string RomPath);                                
		// Wipe data on NativeSD Rom
		int Wipe_Data(string RomPath);                            
		// Wipe dalvik-cache on NativeSD Rom
		int Wipe_Dalvik(string RomPath);                          
		// Flash rom's kernel to selected boot partition [for cLK ONLY]
		int Kernel_Update(string ptn, string RomPath);                  
};

extern TWNativeSDManager NativeSDManager;

#endif // __TWRP_NativeSD_Manager
