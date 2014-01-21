#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
	int res = -1;
	char fstab_contents[1024];
	FILE *rf; 
	rf = fopen("/proc/cmdline", "r");
	if (rf == NULL){
		printf("Could not open '/proc/cmdline'");
		strcpy(fstab_contents, "/cache\t\tyaffs2\t\tcache\n/misc\t\tmtd\t\tmisc\n/recovery\t\tmtd\t\trecovery\n/sdcard\t\tvfat\t\t/dev/block/mmcblk0p1\t\t/dev/block/mmcblk0\n/data\t\tyaffs2\t\tuserdata\n/boot\t\tmtd\t\tboot\n/system\t\tyaffs2\t\tsystem\n/sd-ext\t\tauto\t\t/dev/block/mmcblk0p2\n/sdext2\t\tauto\t\t/dev/block/mmcblk0p3");
	}
	char cmdline[1024];
	fgets(cmdline, 1024, rf);
	fclose(rf);

	char substring[4] = "clk=";
	if (strstr(cmdline, substring) == NULL) {
		// Not detected
		res = 0;
		strcpy(fstab_contents, "/cache\t\tyaffs2\t\tcache\n/misc\t\tmtd\t\tmisc\n/recovery\t\tmtd\t\trecovery\n/sdcard\t\tvfat\t\t/dev/block/mmcblk0p1\t\t/dev/block/mmcblk0\n/data\t\tyaffs2\t\tuserdata\n/boot\t\tyaffs2\t\tboot\n/system\t\tyaffs2\t\tsystem\n/sd-ext\t\tauto\t\t/dev/block/mmcblk0p2\n/sdext2\t\tauto\t\t/dev/block/mmcblk0p3");
	} else {
		// cLK detected
		res = 1;
		strcpy(fstab_contents, "/cache\t\tyaffs2\t\tcache\n/misc\t\tmtd\t\tmisc\n/recovery\t\tmtd\t\trecovery\n/sdcard\t\tvfat\t\t/dev/block/mmcblk0p1\t\t/dev/block/mmcblk0\n/data\t\tyaffs2\t\tuserdata\n/boot\t\tmtd\t\tboot\n/system\t\tyaffs2\t\tsystem\n/sd-ext\t\tauto\t\t/dev/block/mmcblk0p2\n/sdext2\t\tauto\t\t/dev/block/mmcblk0p3");
	}
	FILE* wf;
        wf = fopen("/etc/recovery.fstab", "w");
        if (wf == NULL) {
               printf("Cannot create '/etc/recovery.fstab'") ;
               return -1;
        }
	fprintf(wf, "%s\n", fstab_contents);
	fclose (wf);

	return res;
}
