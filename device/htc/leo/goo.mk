#
# Copyright (C) 2012 the cmhtcleo team
#
# GooManager specific stuff
#

PRODUCT_PROPERTY_OVERRIDES += \
        ro.goo.developerid=cmhtcleo \
        ro.goo.rom=cm9nightly \
        ro.goo.version=$(shell date +%s)

# include goo manager
PRODUCT_COPY_FILES += \
       device/htc/leo/prebuilt/GooManager.apk:system/app/GooManager.apk