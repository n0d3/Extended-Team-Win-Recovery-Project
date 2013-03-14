#
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

#
# This is the device-specific product configuration for leo,
# configured with Europe-specific settings.
#

# Copy gsmconfig here for now as a workaround
# GSM APN list
PRODUCT_COPY_FILES += \
    vendor/cm/prebuilt/common/etc/apns-conf.xml:system/etc/apns-conf.xml

# GSM SPN overrides list
PRODUCT_COPY_FILES += \
    vendor/cm/prebuilt/common/etc/spn-conf.xml:system/etc/spn-conf.xml

# The gps config appropriate for this device
PRODUCT_COPY_FILES += \
     device/htc/leo/prebuilt/gps_eu.conf:system/etc/gps.conf


# The rest of the configuration is inherited from a generic config
$(call inherit-product, device/htc/leo/leo.mk)
