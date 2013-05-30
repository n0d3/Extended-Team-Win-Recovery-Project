LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    gui.cpp \
    resources.cpp \
    pages.cpp \
    text.cpp \
    image.cpp \
    action.cpp \
    console.cpp \
    fill.cpp \
    button.cpp \
    checkbox.cpp \
    fileselector.cpp \
    progressbar.cpp \
    animation.cpp \
    conditional.cpp \
    slider.cpp \
    slidervalue.cpp \
    listbox.cpp \
    keyboard.cpp \
    input.cpp \
    blanktimer.cpp \
    partitionlist.cpp \
    ../minuitwrp/graphics.c

ifneq ($(TWRP_CUSTOM_KEYBOARD),)
  LOCAL_SRC_FILES += $(TWRP_CUSTOM_KEYBOARD)
else
  LOCAL_SRC_FILES += hardwarekeyboard.cpp
endif

LOCAL_MODULE := libguitwrp

ifeq ($(TARGET_DEVICE),leo)
	LOCAL_CFLAGS += -DTW_DEVICE_IS_HTC_LEO
endif

# Use this flag to create a build that simulates threaded actions like installing zips, backups, restores, and wipes for theme testing
#TWRP_SIMULATE_ACTIONS := true
ifeq ($(TWRP_SIMULATE_ACTIONS), true)
	LOCAL_CFLAGS += -D_SIMULATE_ACTIONS
endif

#TWRP_EVENT_LOGGING := true
ifeq ($(TWRP_EVENT_LOGGING), true)
	LOCAL_CFLAGS += -D_EVENT_LOGGING
endif

ifneq ($(RECOVERY_SDCARD_ON_DATA),)
	LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifneq ($(TW_EXTERNAL_STORAGE_PATH),)
	LOCAL_CFLAGS += -DTW_EXTERNAL_STORAGE_PATH=$(TW_EXTERNAL_STORAGE_PATH)
endif
ifneq ($(TW_BRIGHTNESS_PATH),)
	LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=$(TW_BRIGHTNESS_PATH)
endif
ifneq ($(TW_MAX_BRIGHTNESS),)
	LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
else
	LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=255
endif
ifneq ($(TW_NO_SCREEN_BLANK),)
	LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
endif
ifneq ($(LANDSCAPE_RESOLUTION),)
	LOCAL_CFLAGS += -DTW_HAS_LANDSCAPE
endif

TWRP_DEFAULT_UI_IS_PORTRAIT := true
ifneq ($(TW_DEFAULT_ROTATION),)
	ifeq ($(TW_DEFAULT_ROTATION), 0)
		TWRP_DEFAULT_UI_IS_PORTRAIT := true
	endif
	ifeq ($(TW_DEFAULT_ROTATION), 90)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(TW_DEFAULT_ROTATION), 180)
		TWRP_DEFAULT_UI_IS_PORTRAIT := true
	endif
	ifeq ($(TW_DEFAULT_ROTATION), 270)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	LOCAL_CFLAGS += -DTW_DEFAULT_ROTATION=$(TW_DEFAULT_ROTATION)
else
	ifeq ($(DEVICE_RESOLUTION), 2560x1600)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(DEVICE_RESOLUTION), 1920x1200)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(DEVICE_RESOLUTION), 1280x800)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(DEVICE_RESOLUTION), 1024x768)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(DEVICE_RESOLUTION), 1024x600)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(DEVICE_RESOLUTION), 800x480)
		TWRP_DEFAULT_UI_IS_PORTRAIT := false
	endif
	ifeq ($(TWRP_DEFAULT_UI_IS_PORTRAIT), true)
		LOCAL_CFLAGS += -DTW_DEFAULT_ROTATION=0
	else
		LOCAL_CFLAGS += -DTW_DEFAULT_ROTATION=90
	endif
endif

LOCAL_C_INCLUDES += bionic external/stlport/stlport $(commands_recovery_local_path)/gui/devices/$(DEVICE_RESOLUTION)

include $(BUILD_STATIC_LIBRARY)

# Transfer in the resources for the device
include $(CLEAR_VARS)
LOCAL_MODULE := twrp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/res

TWRP_RES_LOC := $(commands_recovery_local_path)/gui/devices
TWRP_RES_GEN := $(intermediates)/twrp

$(TWRP_RES_GEN):
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/fonts
	cp -fr $(TWRP_RES_LOC)/$(DEVICE_RESOLUTION)/res/fonts/* $(TARGET_RECOVERY_ROOT_OUT)/res/fonts/
ifneq ($(LANDSCAPE_RESOLUTION),)
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/portrait
	cp -fr $(TWRP_RES_LOC)/common/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/portrait/
	cp -fr $(TWRP_RES_LOC)/$(DEVICE_RESOLUTION)/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/portrait/
	cp -f $(TWRP_RES_LOC)/$(DEVICE_RESOLUTION)/res/ui.xml $(TARGET_RECOVERY_ROOT_OUT)/res/portrait.xml
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/landscape
	cp -fr $(TWRP_RES_LOC)/common/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/landscape/
	cp -fr $(TWRP_RES_LOC)/$(LANDSCAPE_RESOLUTION)/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/landscape/
	cp -f $(TWRP_RES_LOC)/$(LANDSCAPE_RESOLUTION)/res/ui.xml $(TARGET_RECOVERY_ROOT_OUT)/res/landscape.xml
else
ifeq ($(TWRP_DEFAULT_UI_IS_PORTRAIT), true)
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/portrait
	cp -fr $(TWRP_RES_LOC)/common/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/portrait/
	cp -fr $(TWRP_RES_LOC)/$(DEVICE_RESOLUTION)/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/portrait/
	cp -f $(TWRP_RES_LOC)/$(DEVICE_RESOLUTION)/res/ui.xml $(TARGET_RECOVERY_ROOT_OUT)/res/portrait.xml
else
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/landscape
	cp -fr $(TWRP_RES_LOC)/common/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/landscape/
	cp -fr $(TWRP_RES_LOC)/$(LANDSCAPE_RESOLUTION)/res/images/* $(TARGET_RECOVERY_ROOT_OUT)/res/landscape/
	cp -f $(TWRP_RES_LOC)/$(LANDSCAPE_RESOLUTION)/res/ui.xml $(TARGET_RECOVERY_ROOT_OUT)/res/landscape.xml
endif
endif
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin/
	ln -sf /sbin/busybox $(TARGET_RECOVERY_ROOT_OUT)/sbin/sh
	ln -sf /sbin/pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gzip
	ln -sf /sbin/unpigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gunzip

LOCAL_GENERATED_SOURCES := $(TWRP_RES_GEN)
LOCAL_SRC_FILES := twrp $(TWRP_RES_GEN)
include $(BUILD_PREBUILT)
