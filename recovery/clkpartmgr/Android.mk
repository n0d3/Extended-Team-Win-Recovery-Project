LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ../mtdutils/mtdutils.c clkpartmgr.c
LOCAL_CFLAGS := -g -c -W
LOCAL_MODULE := clkpartmgr
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SHARED_LIBRARIES := libmtdutils libflashutils libmmcutils libbmlutils libcutils libc
include $(BUILD_EXECUTABLE)
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
