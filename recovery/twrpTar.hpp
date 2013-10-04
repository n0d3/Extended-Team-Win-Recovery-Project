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

extern "C" {
        #include "libtar/libtar.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
struct TarListStruct {
	std::string fn;
	unsigned thread_id;
};

struct thread_data_struct {
	std::vector<TarListStruct> *TarList;
	unsigned thread_id;
};
#endif
class twrpTar {
	public:
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		twrpTar();
		virtual ~twrpTar();
#else
		int createTarGZFork();
#endif
		int createTarFork();
		int extractTarFork();
		int splitArchiveFork();
		int entryExists(string entry);
		void setexcl(string exclude);
                void setfn(string fn);
                void setdir(string dir);
		unsigned long long uncompressedSize();

	public:
		int has_data_media;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		int use_encryption;
		int userdata_encryption;
		int use_compression;
		int split_archives;
#endif

	private:
		int extract();
                int addFilesToExistingTar(vector <string> files, string tarFile);
		int createTar();
		int addFile(string fn, bool include_root);
		int closeTar();
		int create();
		int Split_Archive();
		int removeEOT(string tarFile);
		int extractTar();
		int tarDirs(bool include_root);
		int Generate_Multiple_Archives(string Path);
		string Strip_Root_Dir(string Path);
		int openTar();
		int Archive_File_Count;
		unsigned long long Archive_Current_Size;
		int Archive_Current_Type;
		int skip(char* name, char* type);


		TAR *t;
		FILE* p;
		int fd;
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		pid_t pigz_pid;
		pid_t oaes_pid;
#else
		FILE* rp;	
		int rfd;
#endif

		string tardir;
		string tarfn;
		string basefn;
		string tarexclude;
		vector<string> Excluded;

#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
		int Generate_TarList(string Path, std::vector<TarListStruct> *TarList, unsigned long long *Target_Size, unsigned *thread_id);
		static void* createList(void *cookie);
		static void* extractMulti(void *cookie);
		int tarList(bool include_root, std::vector<TarListStruct> *TarList, unsigned thread_id);
		std::vector<TarListStruct> *ItemList;
		unsigned thread_id;
#else
		int extractTGZ();
		int createTGZ();
#endif

}; 
