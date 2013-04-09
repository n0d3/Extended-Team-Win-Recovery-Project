#ifndef TWCOMMON_HPP
#define TWCOMMON_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include "gui/gui.h"
#define LOGERR(...) gui_print("E:" __VA_ARGS__)
#define LOGINFO(...) fprintf(stdout, "I:" __VA_ARGS__)

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

#ifndef RAMFS_MAGIC
    #define RAMFS_MAGIC 0x858458f6
#endif
#ifndef YAFFS_MAGIC
    #define YAFFS_MAGIC 0x5941ff53
#endif

#ifdef __cplusplus
}
#endif

#endif  // TWCOMMON_HPP
