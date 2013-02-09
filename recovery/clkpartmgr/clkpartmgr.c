/*
 * Kokotas: clkpartmgr.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/reboot.h>

#include "../bootloader.h"
#include "../mtdutils/mtdutils.h"

static unsigned partition_size(char* mtd_name) {
	size_t total_size;
	const MtdPartition *part = mtd_find_partition_by_name(mtd_name);
	if (part == NULL || mtd_partition_info(part, &total_size, NULL, NULL))
        	total_size = 0;
	else
		total_size /= 1048576;

	return total_size;
}

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

static int set_bootloader_msg(const struct bootloader_message *in) {
	size_t write_size;
	const MtdPartition *part = mtd_find_partition_by_name("misc");
	if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
        	printf("  Can't find misc\n");
        	return -1;
	}

	MtdReadContext *read = mtd_read_partition(part);
	if (read == NULL) {
        	printf("  Can't open misc\n(%s)\n", strerror(errno));
        	return -1;
	}

    	ssize_t size = write_size * MISC_PAGES;
	char data[size];
	ssize_t r = mtd_read_data(read, data, size);
	mtd_read_close(read);
	if (r != size) {
		printf("  Can't read misc\n(%s)\n", strerror(errno));
		return -1;
	}

	memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

	MtdWriteContext *write = mtd_write_partition(part);
	if (write == NULL) {
        	printf("  Can't open misc\n(%s)\n", strerror(errno));
        	return -1;
	}
	if (mtd_write_data(write, data, size) != size) {
        	printf("  Can't write misc\n(%s)\n", strerror(errno));
        	mtd_write_close(write);
        	return -1;
	}
	if (mtd_write_close(write)) {
        	printf("  Can't finish misc\n(%s)\n", strerror(errno));
        	return -1;
	}

    	printf("  Bootloader message set!\n");
	return 0;
}

int main(int argc, char** argv) {
	int i, arg_error = 0, write_msg = 0, restore = 0, update = 0;
	char buff[130], mtd_name[32], boot_recovery[1024], tmp[896], cmd_arg[64], parts[64];
	char* tmp_buff;
	unsigned min_ptn_size, ptn_size;
	mtd_scan_partitions();
	const MtdPartition *part = mtd_find_partition_by_name("lk");
	if (part == NULL) {
        	printf("  cLK bootloader was not detected.\n");
        	return -1;
	}
	if (argc < 2)
		arg_error = 1;
	else {
		for (i=1; i<argc; i++) {
			if (strpbrk(argv[i], ":") != NULL) {
				strcpy(buff, argv[i]);
				tmp_buff = strtok(buff, ":");
				strcpy(mtd_name, tmp_buff);
				tmp_buff = strtok(NULL, ":");
				min_ptn_size = atoi(tmp_buff);
				printf("  Checking %s's size...\n", mtd_name);
				ptn_size = partition_size(mtd_name);
				if (ptn_size > 0 && ptn_size < min_ptn_size) {
					write_msg++;
					printf("  %s's size will be increased\n    from %iMB to %iMB\n",
						mtd_name, (int)ptn_size, (int)min_ptn_size);
					strcat(tmp, argv[i]);
					strcat(tmp, "\n");
				} else {
					printf("  No change to %s's size.\n", mtd_name);
				}
			} else if (strpbrk(argv[i], "=") != NULL) {
				strcpy(buff, argv[i]);
				tmp_buff = strtok(buff, "=");
				strcpy(cmd_arg, tmp_buff);
				if (strcmp(cmd_arg, "--restore") == 0) {
					if (restore == 0 && update == 0) {
						restore = 1;
						printf("  Passing msg: '%s'\n", argv[i]);
						strcpy(boot_recovery, argv[i]);
					} else
						continue;
				} else if (strcmp(cmd_arg, "--update_package") == 0) {
					if (update == 0 && restore == 0) {
						update = 1;
						printf("  Passing msg: '%s'\n", argv[i]);
						strcpy(boot_recovery, argv[i]);
					} else
						continue;
				} else if (strcmp(cmd_arg, "--partitions") == 0) {
					if (update == 0) {
						strcat(parts, "|");
						tmp_buff = strtok(NULL, "=");
						printf("  Passing partitions: '%s'\n", tmp_buff);
						strcat(parts, tmp_buff);
					} else
						arg_error = 1;
				} else
					printf("  Skipping unknown msg: '%s'\n", argv[i]);
			}
		}		
	}
	if (arg_error) {
		printf("Usage: clkpartmgr [OPTION...] [ptn-name:min-size]\n");
		printf("\n Required arguments:\n");
		printf("  ptn-name:min-size     \tSet partition's minimum size to check.\n");
		printf("                        \tIf the real size is less than minimum,\n");
		printf("                        \tit will be increased later by cLK.\n\n");
		printf(" Optional arguments:\n");
		printf("  --restore=restore-name\tSet the name of the backup folder\n");
		printf("                        \tthat will be restored upon recovery start.\n");
		printf("  --partitions=BCADSEXM \tSet which parts of the backup to restore:\n");
		printf("                        \tM -> skip md5 check\n");
		printf("                        \tB -> boot\n");
		printf("                        \tC -> cache\n");
		printf("                        \tA -> android_secure\n");
		printf("                        \tD -> data\n");
		printf("                        \tS -> system\n");
		printf("                        \tE -> sd-ext\n");
		printf("                        \tX -> sdext2\n\n");
		printf("  --update_package=zip-name\tSet the (full)name of the zip package\n");
		printf("                           \tthat will be installed upon recovery start.\n");
		return 0;
	} else {
		if (write_msg) {
			if (restore == 1)
				strcat(boot_recovery, parts);
			strcat(boot_recovery, "\n");
			strcat(boot_recovery, tmp);	
			//printf("  boot.recovery: \n[%s]\n", boot_recovery);		
			struct bootloader_message boot;
			memset(&boot, 0, sizeof(boot));
			strcpy(boot.recovery, boot_recovery);
			if (set_bootloader_msg(&boot) == 0) {
				__reboot(LINUX_REBOOT_MAGIC1,
					 LINUX_REBOOT_MAGIC2,
					 LINUX_REBOOT_CMD_RESTART2,
					 (void*) "oem-8");
				return 0;
			} else
				return -1;
		} else {
			printf("  No need to change ptable.");
			return 1;
		}
	}

	return 0;
}
