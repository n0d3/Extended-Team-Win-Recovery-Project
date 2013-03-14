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
