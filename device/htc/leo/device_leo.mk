# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# This file is the build configuration for a full Android
# build for leo hardware. This cleanly combines a set of
# device-specific aspects (drivers) with a device-agnostic
# product configuration (apps).
#
# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Overlay
DEVICE_PACKAGE_OVERLAYS += device/htc/leo/overlay

# Packages
PRODUCT_PACKAGES += \
	sensors.htcleo \
	lights.htcleo \
 	gps.htcleo \
	leo-reference-ril

# Ramdisk
PRODUCT_COPY_FILES += \
	device/htc/leo/ramdisk/init.htcleo.rc:root/init.htcleo.rc \
	device/htc/leo/ramdisk/init.htcleo.usb.rc:root/init.htcleo.usb.rc \
	device/htc/leo/ramdisk/ueventd.htcleo.rc:root/ueventd.htcleo.rc \
	device/htc/leo/ramdisk/logo.rle:root/logo.rle \
	device/htc/leo/ramdisk/fstab.htcleo:root/fstab.htcleo

# GSM APN list
PRODUCT_COPY_FILES += \
    vendor/cm/prebuilt/common/etc/apns-conf.xml:system/etc/apns-conf.xml \
    vendor/cm/prebuilt/common/etc/spn-conf.xml:system/etc/spn-conf.xml

# GPS
PRODUCT_COPY_FILES += \
     device/htc/leo/configs/gps.conf:system/etc/gps.conf

# media config xml file
PRODUCT_COPY_FILES += \
	device/htc/leo/configs/media_profiles.xml:system/etc/media_profiles.xml

# Fix graphic crash
#PRODUCT_PROPERTY_OVERRIDES += \
#    debug.sf.hw=0

# Scripts
PRODUCT_COPY_FILES += \
	device/htc/leo/scripts/init.d/01modules:system/etc/init.d/01modules \
	device/htc/leo/scripts/init.d/02usb_tethering:system/etc/init.d/02usb_tethering \
	device/htc/leo/scripts/init.d/10mic_level:system/etc/init.d/10mic_level \

# Keylayouts
PRODUCT_COPY_FILES += \
	device/htc/leo/keylayout/htcleo-keypad.kl:system/usr/keylayout/htcleo-keypad.kl \
	device/htc/leo/keylayout/htcleo-keypad.kcm:system/usr/keychars/htcleo-keypad.kcm \
	device/htc/leo/keylayout/h2w_headset.kl:system/usr/keylayout/h2w_headset.kl \
	device/htc/leo/keylayout/htcleo-touchscreen.idc:system/usr/idc/htcleo-touchscreen.idc

# cLK
PRODUCT_COPY_FILES += \
	device/htc/leo/clk/default.prop:system/default.prop \
	device/htc/leo/clk/ppp:system/ppp \
	device/htc/leo/clk/etc/init.d/97ppp:system/etc/init.d/97ppp \
	device/htc/leo/clk/etc/ppp/active:system/etc/ppp/active \
	device/htc/leo/clk/etc/ppp/chap-secrets:system/etc/ppp/chap-secrets \
	device/htc/leo/clk/etc/ppp/ip-down:system/etc/ppp/ip-down \
	device/htc/leo/clk/etc/ppp/ip-up:system/etc/ppp/ip-up \
	device/htc/leo/clk/etc/ppp/options:system/etc/ppp/options \
	device/htc/leo/clk/etc/ppp/options.smd:system/etc/ppp/options.smd \
	device/htc/leo/clk/etc/ppp/pap-secrets:system/etc/ppp/pap-secrets \
	device/htc/leo/clk/etc/ppp/ppp-gprs.pid:system/etc/ppp/ppp-gprs.pid \
	device/htc/leo/clk/etc/ppp/resolv.conf:system/etc/ppp/resolv.conf

# Kernel modules
ifeq ($(BUILD_KERNEL),false)
PRODUCT_COPY_FILES += $(shell \
    find device/htc/leo/modules -name '*.ko' \
    | sed -r 's/^\/?(.*\/)([^/ ]+)$$/\1\2:system\/lib\/modules\/\2/' \
    | tr '\n' ' ')
endif

# Permissions
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml 

# Leo uses high-density artwork where available
PRODUCT_LOCALES += hdpi mdpi

# QSD8K Commomn Stuff
$(call inherit-product, device/htc/qsd8k-common/qsd8k.mk)

# Vendor
$(call inherit-product, vendor/htc/leo/leo-vendor.mk)

# Discard inherited values and use our own instead.
PRODUCT_NAME := full_leo
PRODUCT_DEVICE := leo
PRODUCT_MODEL := Full Android on leo
