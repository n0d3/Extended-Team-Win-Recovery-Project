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

using namespace std;
#include "rapidxml.hpp"
using namespace rapidxml;
extern "C" {
	#include "../minzip/Zip.h"
	#include "../minuitwrp/minui.h"
	#include "../common.h"
	#include "../recovery_ui.h"
}
#include <string>
#include <vector>
#include <map>
#include "resources.hpp"
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <pixelflinger/pixelflinger.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <sstream>
#include "pages.hpp"
#include "blanktimer.hpp"
#include "../twrp-functions.hpp"
#include "../variables.h"
#include "../data.hpp"

blanktimer::blanktimer(void) {
	dimmed = 0;
	blanked = 0;
	conblank = 1;
	orig_brightness = getBrightness();
}

void blanktimer::setTime(int newtime) {
	if (sleepTimer != newtime) {
		LOGI("Screen timeout changed to %i sec.\n", newtime);
		sleepTimer = newtime;
	}
}

int blanktimer::setTimerThread(void) {
	pthread_mutexattr_settype(&timerattr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init(&timermutex, &timerattr);
	pthread_mutexattr_settype(&blankattr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init(&blankmutex, &blankattr);

	pthread_t thread;
	ThreadPtr blankptr = &blanktimer::setClockTimer;
	PThreadPtr p = *(PThreadPtr*)&blankptr;
	pthread_create(&thread, NULL, p, this);
	return 0;
}

void blanktimer::setBlank(int blank) {
	pthread_mutex_trylock(&blankmutex);
	conblank = blank;
	pthread_mutex_unlock(&blankmutex);
}

int blanktimer::getBlank(void) {
	return conblank;
}

void blanktimer::setTimer(void) {
	pthread_mutex_trylock(&timermutex);
	clock_gettime(CLOCK_MONOTONIC, &btimer);
	pthread_mutex_unlock(&timermutex);
}

timespec blanktimer::getTimer(void) {
	return btimer;
}

int blanktimer::setClockTimer(void) {
	//LOGI("Screen-off timer enabled.\n");
	timespec curTime, diff;
	while (conblank) {
		usleep(980000);
		clock_gettime(CLOCK_MONOTONIC, &curTime);
		diff = TWFunc::timespec_diff(btimer, curTime);
		if (sleepTimer > 2 && diff.tv_sec > (sleepTimer - 2) && !dimmed) {
			dimmed = 1;
			blanked = 0;
			orig_brightness = getBrightness();
			setBrightness(5);
		}
		if (sleepTimer && diff.tv_sec > sleepTimer && dimmed) {
			dimmed = 0;
			blanked = 1;
			gr_fb_blank(1);
			setBrightness(0);
			PageManager::ChangeOverlay("lock");
		}
	}	
	//LOGI("Screen-off timer disabled.\n");
	dimmed = 0;
	blanked = 0;
	pthread_mutex_destroy(&timermutex);
	pthread_mutex_destroy(&blankmutex);
	pthread_exit(NULL);
	return 0;
}

int blanktimer::getBrightness(void) {
	return DataManager::GetIntValue("tw_brightness");
}

int blanktimer::setBrightness(int brightness) {
	string brightness_path = EXPAND(TW_BRIGHTNESS_PATH);
	string bstring = TWFunc::to_string(brightness);
	if ((TWFunc::write_file(brightness_path, bstring)) != 0)
		return -1;
	return 0;
}

void blanktimer::resetTimerAndUnblank(void) {
	setTimer();
	if (blanked) {
		blanked = 0;
		gr_fb_blank(0);
		gui_forceRender();
		setBrightness(orig_brightness);
	} else if (dimmed) {
		dimmed = 0;
		setBrightness(orig_brightness);
	}
}
