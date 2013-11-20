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

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

extern "C"
{
#include "../twcommon.h"
#include "../minuitwrp/minui.h"
#ifdef HAVE_SELINUX
#include "../minzip/Zip.h"
#else
#include "../minzipold/Zip.h"
#endif
#include <pixelflinger/pixelflinger.h>
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../variables.h"
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#ifndef TW_NO_SCREEN_TIMEOUT
#include "blanktimer.hpp"
#endif

const static int CURTAIN_FADE = 32;

using namespace rapidxml;

// Global values
static gr_surface gCurtain = NULL;
static int gGuiInitialized = 0;
static int gGuiConsoleRunning = 0;
static int gGuiConsoleTerminate = 0;
static int gForceRender = 0;

static int gNoAnimation = 1;
static int gGuiInputRunning = 0;
#ifndef TW_NO_SCREEN_TIMEOUT
blanktimer blankTimer;
#endif

// Needed by pages.cpp too
int gGuiRunning = 0;

// Needed for offmode-charging
static int offmode_charge = 0;
int key_pressed = 0;

static int gRecorder = -1;

enum RenderStates
{
  RENDER_NORMAL    = 0x00,
  RENDER_FORCE     = 0x01,
  RENDER_DISABLE   = 0x02,
};

static int gRenderState = RENDER_NORMAL;
static pthread_mutex_t gRenderStateMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutexattr_t gRenderStateAttr;

extern "C" void gr_write_frame_to_file(int fd);

void flip(void)
{
	if (gRecorder != -1)
	{
		timespec time;
		clock_gettime(CLOCK_MONOTONIC, &time);
		write(gRecorder, &time, sizeof(timespec));
		gr_write_frame_to_file(gRecorder);
	}
	gr_flip();
}

void rapidxml::parse_error_handler(const char *what, void *where)
{
	fprintf(stderr, "Parser error: %s\n", what);
	fprintf(stderr, "  Start of string: %s\n",(char *) where);
	LOGERR("Error parsing XML file.\n");
	//abort();
}

static void curtainSet()
{
	gr_color(0, 0, 0, 255);
	gr_fill(0, 0, gr_fb_width(), gr_fb_height());
	gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain), gr_get_height(gCurtain), 0, 0);
	gr_flip();
}

static void curtainRaise(gr_surface surface)
{
	int sy = 0;
	int h = gr_get_height(gCurtain) - 1;
	int w = gr_get_width(gCurtain);
	int fy = 1;

	int msw = gr_get_width(surface);
	int msh = gr_get_height(surface);
	int CURTAIN_RATE = msh / 30;

	if (gNoAnimation == 0)
	{
		for (; h > 0; h -= CURTAIN_RATE, sy += CURTAIN_RATE, fy += CURTAIN_RATE)
		{
			gr_blit(surface, 0, 0, msw, msh, 0, 0);
			gr_blit(gCurtain, 0, sy, w, h, 0, 0);
			gr_flip();
		}
	}
	gr_blit(surface, 0, 0, msw, msh, 0, 0);
	flip();
}

void curtainClose()
{
#if 0
	int w = gr_get_width(gCurtain);
	int h = 1;
	int sy = gr_get_height(gCurtain) - 1;
	int fbh = gr_fb_height();
	int CURTAIN_RATE = fbh / 30;

	if (gNoAnimation == 0)
	{
		for (; h < fbh; h += CURTAIN_RATE, sy -= CURTAIN_RATE)
		{
			gr_blit(gCurtain, 0, sy, w, h, 0, 0);
			gr_flip();
		}
		gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain),
		gr_get_height(gCurtain), 0, 0);
		gr_flip();

		if (gRecorder != -1)
			close(gRecorder);

		int fade;
		for (fade = 16; fade < 255; fade += CURTAIN_FADE)
		{
			gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain),
			gr_get_height(gCurtain), 0, 0);
			gr_color(0, 0, 0, fade);
			gr_fill(0, 0, gr_fb_width(), gr_fb_height());
			gr_flip();
		}
		gr_color(0, 0, 0, 255);
		gr_fill(0, 0, gr_fb_width(), gr_fb_height());
		gr_flip();
	}
#else
	gr_blit(gCurtain, 0, 0, gr_get_width(gCurtain), gr_get_height(gCurtain), 0, 0);
	gr_flip();
#endif
}

static void * input_thread(void *cookie)
{
    int drag = 0;
    static int touch_and_hold = 0, dontwait = 0, touch_repeat = 0, x = 0, y = 0, lshift = 0, rshift = 0, key_repeat = 0, power_key = 0;
    static struct timeval touchStart;
    HardwareKeyboard kb;
    DataManager::GetValue(TW_POWER_BUTTON, power_key);

    for (;;) {
        // wait for the next event
        struct input_event ev;
        int state = 0, ret = 0;

	ret = ev_get(&ev, dontwait);

	if (ret < 0) {
	    struct timeval curTime;
	    gettimeofday(&curTime, NULL);
	    long mtime, seconds, useconds;

	    seconds  = curTime.tv_sec  - touchStart.tv_sec;
	    useconds = curTime.tv_usec - touchStart.tv_usec;

	    mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
	    if (touch_and_hold && mtime > 500) {
		touch_and_hold = 0;
		touch_repeat = 1;
		gettimeofday(&touchStart, NULL);
#ifdef _EVENT_LOGGING
                LOGERR("TOUCH_HOLD: %d,%d\n", x, y);
#endif
		PageManager::NotifyTouch(TOUCH_HOLD, x, y);
	    } else if (touch_repeat && mtime > 100) {
#ifdef _EVENT_LOGGING
                LOGERR("TOUCH_REPEAT: %d,%d\n", x, y);
#endif
		gettimeofday(&touchStart, NULL);
		PageManager::NotifyTouch(TOUCH_REPEAT, x, y);
	    } else if (key_repeat == 1 && mtime > 500) {
#ifdef _EVENT_LOGGING
                LOGERR("KEY_HOLD: %d,%d\n", x, y);
#endif
		gettimeofday(&touchStart, NULL);
		key_repeat = 2;
		kb.KeyRepeat();
	    } else if (key_repeat == 2 && mtime > 100) {
#ifdef _EVENT_LOGGING
                LOGERR("KEY_REPEAT: %d,%d\n", x, y);
#endif
		gettimeofday(&touchStart, NULL);
		kb.KeyRepeat();
	    }
	} else if (ev.type == EV_ABS) {

            x = ev.value >> 16;
            y = ev.value & 0xFFFF;

            if (ev.code == 0) {
                if (state == 0) {
#ifdef _EVENT_LOGGING
                    LOGERR("TOUCH_RELEASE: %d,%d\n", x, y);
#endif
                    PageManager::NotifyTouch(TOUCH_RELEASE, x, y);
		    touch_and_hold = 0;
		    touch_repeat = 0;
		    if (!key_repeat)
			dontwait = 0;
                }
                state = 0;
                drag = 0;
            } else {
                if (!drag) {
#ifdef _EVENT_LOGGING
                    LOGERR("TOUCH_START: %d,%d\n", x, y);
#endif
                    if (PageManager::NotifyTouch(TOUCH_START, x, y) > 0)
                        state = 1;
                    drag = 1;
		    touch_and_hold = 1;
		    dontwait = 1;
		    key_repeat = 0;
		    gettimeofday(&touchStart, NULL);
                } else {
                    if (state == 0) {
#ifdef _EVENT_LOGGING
                        LOGERR("TOUCH_DRAG: %d,%d\n", x, y);
#endif
                        if (PageManager::NotifyTouch(TOUCH_DRAG, x, y) > 0)
                            state = 1;
			key_repeat = 0;
                    }
                }
            }
        } else if (ev.type == EV_KEY) {
            // Handle key-press here
#ifdef _EVENT_LOGGING
            LOGERR("TOUCH_KEY: %d\n", ev.code);
#endif
	    if (ev.value != 0) {
		// This is a key press
		if (kb.KeyDown(ev.code)) {
		    key_repeat = 1;
		    touch_and_hold = 0;
		    touch_repeat = 0;
		    dontwait = 1;
		    gettimeofday(&touchStart, NULL);
		} else {
		    key_repeat = 0;
		    touch_and_hold = 0;
		    touch_repeat = 0;
		    dontwait = 0;
		}
	   } else {
		// This is a key release
		kb.KeyUp(ev.code);
		key_repeat = 0;
		touch_and_hold = 0;
		touch_repeat = 0;
		dontwait = 0;
		if (!offmode_charge) {
		    TWFunc::Vibrate(button_pressed);
#ifndef TW_NO_SCREEN_TIMEOUT
		    blankTimer.resetTimerAndUnblank();
#endif
#ifdef _EVENT_LOGGING
		    LOGERR("Screen unblank & Timer reset.\n");
#endif
		} else if (ev.code == power_key || ev.code == 102 || ev.code == 114
			|| ev.code == 114 || ev.code == 139 || ev.code == 158
			|| ev.code == 231) {
#ifdef _EVENT_LOGGING
		    	LOGERR("Hard-Key[%d] wakes up device.\n", ev.code);
#endif
		    	key_pressed = 1;
		}
	    }
    	}
    }
    return NULL;
}

// This special function will return immediately the first time, but then
// always returns 1/30th of a second (or immediately if called later) from
// the last time it was called
static void loopTimer(void)
{
	static timespec lastCall;
	static int initialized = 0;

	if (!initialized)
	{
		clock_gettime(CLOCK_MONOTONIC, &lastCall);
		initialized = 1;
		return;
	}

	do
	{
		timespec curTime;
		clock_gettime(CLOCK_MONOTONIC, &curTime);

		timespec diff = TWFunc::timespec_diff(lastCall, curTime);

		// This is really 30 times per second
		if (diff.tv_sec || diff.tv_nsec > 33333333)
		{
			lastCall = curTime;
			return;
		}

		// We need to sleep some period time microseconds
		unsigned int sleepTime = 33333 -(diff.tv_nsec / 1000);
		usleep(sleepTime);
	} while (1);
}

static inline void doRenderIteration(void)
{
    loopTimer ();

    pthread_mutex_lock(&gRenderStateMutex);

    if(gRenderState & RENDER_DISABLE)
    {
        pthread_mutex_unlock(&gRenderStateMutex);
        return;
    }

    if(gRenderState == RENDER_NORMAL)
    {
        int ret = PageManager::Update();
        if(ret > 1)
            PageManager::Render();
        if(ret > 0)
            flip();
    }
    else if(gRenderState & RENDER_FORCE)
    {
        gRenderState &= ~(RENDER_FORCE);
        PageManager::Render ();
        flip ();
    }

    pthread_mutex_unlock(&gRenderStateMutex);
}

static void runPageLoop(const std::string& stopVar)
{
    // Raise the curtain
    if (gCurtain != NULL)
    {
        gr_surface surface;

        PageManager::Render();
        gr_get_surface(&surface);
        curtainRaise(surface);
        gr_free_surface(surface);
    }

    gGuiRunning = 1;

    DataManager::SetValue ("tw_loaded", 1);

    while(true)
    {
        doRenderIteration();

        if(DataManager::GetIntValue(stopVar) != 0)
            break;
    }

    gGuiRunning = 0;
}

static int runPages(void)
{
    runPageLoop("tw_gui_done");
    return 0;
}

static int runPage(const char* page_name)
{
    gui_changePage(page_name);
    runPageLoop("tw_gui_done");
    gui_changePage ("main");
    return 0;
}

int gui_forceRender(void)
{
    pthread_mutex_lock(&gRenderStateMutex);
    gRenderState |= RENDER_FORCE;
    pthread_mutex_unlock(&gRenderStateMutex);
    return 0;
}

int gui_setRenderEnabled(int enable)
{
    pthread_mutex_lock(&gRenderStateMutex);
    if (enable)
        gRenderState &= ~(RENDER_DISABLE);
    else
        gRenderState |= RENDER_DISABLE;
    pthread_mutex_unlock(&gRenderStateMutex);
    return 0;
}

int gui_changePage(std::string newPage)
{
    LOGINFO("Set page: '%s'\n", newPage.c_str());
    PageManager::ChangePage(newPage);
    gui_forceRender();
    return 0;
}

int gui_changeOverlay(std::string overlay)
{
    PageManager::ChangeOverlay(overlay);
    gui_forceRender();
    return 0;
}

int gui_changePackage(std::string newPackage)
{
    PageManager::SelectPackage(newPackage);
    gui_forceRender();
    return 0;
}

std::string gui_parse_text(string inText)
{
    // Copied from std::string GUIText::parseText(void)
    // This function parses text for DataManager values encompassed by %value% in the XML
    static int counter = 0;
    std::string str = inText;
    size_t pos = 0;
    size_t next = 0, end = 0;

    while (1)
    {
        next = str.find('%', pos);
        if (next == std::string::npos)
	    return str;
        end = str.find('%', next + 1);
        if (end == std::string::npos)
	    return str;

        // We have a block of data
        std::string var = str.substr(next + 1, (end - next) - 1);
        str.erase(next, (end - next) + 1);

        if (next + 1 == end)
        {
            str.insert(next, 1, '%');
        }
        else
        {
            std::string value;
            if (DataManager::GetValue(var, value) == 0)
                str.insert(next, value);
        }

        pos = next + 1;
    }
}

extern "C" int gui_init()
{
    int fd;
    std::string alter_curtain;
    std::string default_curtain;

    gr_init();
    gr_set_rotation(TWFunc::Check_Rotation_File());
    gr_update_surface_dimensions();
    if (gr_get_rotation() % 180 == 0) {
	alter_curtain = "/tmp/portrait/curtain.jpg";
        default_curtain = "/res/portrait/curtain.jpg";
    } else {
	alter_curtain = "/tmp/landscape/curtain.jpg";
        default_curtain = "/res/landscape/curtain.jpg";
    }

    // First try to use the curtain.jpg extracted from the selected theme's ui.zip to /tmp during booting up (check init.rc & prerecoveryboot.sh)
    if (res_create_surface(alter_curtain.c_str(), &gCurtain)) {
	// If no curtain.jpg is found, try to use the built-in
	if (res_create_surface(default_curtain.c_str(), &gCurtain)) {
	    printf("Unable to locate 'curtain.jpg'\n");
	    return -1;
	}
    }

    curtainSet();
    ev_init();
    return 0;
}

extern "C" int gui_loadResources() {
/*
    unlink("/sdcard/video.last");
    rename("/sdcard/video.bin", "/sdcard/video.last");
    gRecorder = open("/sdcard/video.bin", O_CREAT | O_WRONLY);
*/
    pthread_mutexattr_settype(&gRenderStateAttr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&gRenderStateMutex, &gRenderStateAttr);

    if(!TWFunc::loadTheme())
	goto error;

    // Set the default package
    PageManager::SelectPackage("TWRP");

    gGuiInitialized = 1;
    return 0;

error:
    LOGERR("An internal error has occurred.\n");
    TWFunc::copy_file("/tmp/recovery.log", DataManager::GetSettingsStoragePath() + "/recovery.log", 0644);
    gGuiInitialized = 0;
    return -1;
}

static void *time_update_thread(void *cookie)
{
	char tmp[32];
	time_t now;
	struct tm *current;
	string current_time;
	int tw_military_time;

	for(;;) {
		now = time(0);
		current = localtime(&now);
		DataManager::GetValue(TW_MILITARY_TIME, tw_military_time);
		if (tw_military_time == 1)
			sprintf(tmp, "%02d:%02d:%02d", current->tm_hour, current->tm_min, current->tm_sec);
		else {
			if (current->tm_hour >= 12)
				sprintf(tmp, "%d:%02d PM", current->tm_hour == 12 ? 12 : current->tm_hour - 12, current->tm_min);
			else
				sprintf(tmp, "%d:%02d AM", current->tm_hour == 0 ? 12 : current->tm_hour, current->tm_min);
		}
		current_time = tmp;
		DataManager::SetValue("tw_time", current_time);
		usleep(990000);
	}
	
	return NULL;
}

static void *battery_thread(void *cookie)
{
	char tmp[16], cap_s[4];
	int blink = 0, bat_capacity = -1, usb_cable_connected = 1;
	string battery, battery_status, ac_connected, usb_connected;
	string ac_online = "/sys/class/power_supply/ac/online";
	string usb_online = "/sys/class/power_supply/usb/online";
	string status = "/sys/class/power_supply/battery/status";
	string solid_amber = "/sys/class/leds/amber/brightness";
	string solid_green = "/sys/class/leds/green/brightness";
	string on = "1\n";
	string off = "0\n";

	FILE *capacity = NULL;
	while ( (offmode_charge && !key_pressed && usb_cable_connected == 1)
	     || (!offmode_charge)) {
		capacity = fopen("/sys/class/power_supply/battery/capacity","rt");
		if (capacity){
			fgets(cap_s, 4, capacity);
			fclose(capacity);
			bat_capacity = atoi(cap_s);
			if (bat_capacity < 0)	bat_capacity = 0;
			if (bat_capacity > 100)	bat_capacity = 100;
		}
		sprintf(tmp, "%i%%", bat_capacity);
		battery = tmp;
		DataManager::SetValue("tw_battery", battery);
		usleep(800000);

		TWFunc::read_file(ac_online, ac_connected);
		TWFunc::read_file(usb_online, usb_connected);
		if (ac_connected == "0" && usb_connected == "0")
			usb_cable_connected = 0;
		else
			usb_cable_connected = 1;

		if (usb_cable_connected == 1) {
#ifdef TW_DEVICE_IS_HTC_LEO
			TWFunc::power_restore(offmode_charge);
#endif
			if (TWFunc::read_file(status, battery_status) == 0) {
				if (battery_status == "Full") {
					TWFunc::write_file(solid_amber, off);
					TWFunc::write_file(solid_green, on);	
				} else {
					TWFunc::write_file(solid_amber, on);
					TWFunc::write_file(solid_green, off);
				}
			}
		} else {
			TWFunc::write_file(solid_green, off);
			if (bat_capacity > 10) {
#ifdef TW_DEVICE_IS_HTC_LEO
				TWFunc::power_restore(offmode_charge);
#endif
				TWFunc::write_file(solid_amber, off);
			} else {
#ifdef TW_DEVICE_IS_HTC_LEO
				TWFunc::power_save();
#endif
				if (blink)
					TWFunc::write_file(solid_amber, on);
				else
					TWFunc::write_file(solid_amber, off);
				blink ^= 1;
			}
		}		
		usleep(1200000);
	}
	if (offmode_charge) {
		TWFunc::write_file(solid_amber, off);
		TWFunc::write_file(solid_green, off);
		if (key_pressed)
			TWFunc::tw_reboot(rb_system);
		else if (usb_cable_connected != 1)
			TWFunc::tw_reboot(rb_poweroff);
	}
	return NULL;
}

extern "C" int gui_start()
{
    if (!gGuiInitialized)
    	return -1;

    offmode_charge = DataManager::Pause_For_Battery_Charge();

    gGuiConsoleTerminate = 1;
    while (gGuiConsoleRunning)
	loopTimer();

    // Set the default package
    PageManager::SelectPackage("TWRP");

    if (!gGuiInputRunning) {
#ifdef TW_DEVICE_IS_HTC_LEO
	// If set, apply tweaks
	TWFunc::apply_system_tweaks(offmode_charge);
#endif
	// Input handler
	pthread_t t;
	pthread_create(&t, NULL, input_thread, NULL);
	gGuiInputRunning = 1;
	// Time handler
	pthread_t t_update;
	pthread_create(&t_update, NULL, time_update_thread, NULL);
	// Battery charge handler
	pthread_t b_update;
	pthread_create(&b_update, NULL, battery_thread, NULL);
	if (offmode_charge) {
	    LOGINFO("Offmode-charging...\n");
	    TWFunc::screen_off();
	    TWFunc::power_save();	    
	    for(;;);
	} else {
	    // Screen brightness level set to the desired level
	    string brightness_value = DataManager::GetStrValue("tw_brightness");
	    string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
	    TWFunc::write_file(brightness_path, brightness_value);
#ifndef TW_NO_SCREEN_TIMEOUT
	    // Start the screen-off-timeout handler
	    blankTimer.setTimerThread();
	    // Set the desired timeout
	    blankTimer.setTime(DataManager::GetIntValue("tw_screen_timeout_secs"));
#endif
	}
    }

    return runPages();
}

extern "C" int gui_startPage(const char* page_name)
{
    if (!gGuiInitialized)
	return -1;

    gGuiConsoleTerminate = 1;
    while (gGuiConsoleRunning)
	loopTimer();

    // Set the default package
    PageManager::SelectPackage("TWRP");

    if (!gGuiInputRunning) {
        // Start by spinning off an input handler.
        pthread_t t;
        pthread_create(&t, NULL, input_thread, NULL);
        gGuiInputRunning = 1;
    }

    DataManager::SetValue("tw_page_done", 0);
    return runPage(page_name);
}

static void * console_thread(void *cookie)
{
	PageManager::SwitchToConsole();

    while (!gGuiConsoleTerminate)
        doRenderIteration();

    gGuiConsoleRunning = 0;
    return NULL;
}

extern "C" int gui_console_only()
{
    if (!gGuiInitialized)
	return -1;

    gGuiConsoleTerminate = 0;
    gGuiConsoleRunning = 1;

    // Start by spinning off an input handler.
    pthread_t t;
    pthread_create(&t, NULL, console_thread, NULL);

    return 0;
}

int gui_rotate(int rotation)
{
#ifndef TW_HAS_LANDSCAPE
    return 0;
#else
    if (gr_get_rotation() == rotation)
	return 0;
    LOGINFO("gui_rotate(): %d\n", rotation);

    std::string pagename = PageManager::GetCurrentPage();

    gr_freeze_fb(1);
    gui_setRenderEnabled(0);
    gr_set_rotation(rotation);
    TWFunc::Update_Rotation_File(rotation);

    bool reloaded = TWFunc::reloadTheme();

    // failed, try to rotate to 0
    if (!reloaded && rotation != 0)
    {
	gr_freeze_fb(0);
	return gui_rotate(0);
    }

    DataManager::SetValue(TW_ROTATION, rotation);

    gr_update_surface_dimensions();
    gr_freeze_fb(0);
    gui_setRenderEnabled(1);

    if (!pagename.empty())
	PageManager::ChangePage(pagename);

    gui_forceRender();
    return reloaded ? 0 : -1;
#endif
}
