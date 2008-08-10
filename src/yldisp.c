/****************************************************************************
 *
 *  File: yldisp.c
 *
 *  Copyright (C) 2006 - 2008  Thomas Reitmayr <treitmayr@devbase.at>
 *
 ****************************************************************************
 *
 *  This file is part of Yeaphone.
 *
 *  Yeaphone is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include "yldisp.h"
#include "ylsysfs.h"
#include "ypmainloop.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

/*****************************************************************/

#define YLDISP_BLINK_ID     20
#define YLDISP_DATETIME_ID  21
#define YLDISP_MINRING_ID   22

typedef struct yldisp_data yldisp_data;
struct yldisp_data {
  unsigned int blink_on_time;
  unsigned int blink_off_time;
  int blink_reschedule;
  
  time_t counter_base;
  int wait_date_after_count;
  
  int ring_off_delayed;
};

static yldisp_data module_data;

/*****************************************************************/

void yldisp_clear()
{
  yp_ml_remove_event(-1, YLDISP_BLINK_ID);
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  
  /* more to come, eg. free */
  
  yldisp_hide_all();
}

/*****************************************************************/

static void led_on_callback(int id, int group, void *private_data) {
  (void) private_data;
  
  if (module_data.blink_reschedule) {
    module_data.blink_reschedule = 0;
    yp_ml_schedule_periodic_timer(YLDISP_BLINK_ID,
                                  module_data.blink_on_time + module_data.blink_off_time,
                                  0, led_on_callback, NULL);
  }
  ylsysfs_write_control_file((ylsysfs_get_led_inverted()) ? "hide_icon" : "show_icon",
                             "LED");
}

static void led_off_callback(int id, int group, void *private_data) {
  (void) private_data;
  
  if (module_data.blink_reschedule) {
    yp_ml_schedule_timer(YLDISP_BLINK_ID, module_data.blink_on_time,
                         led_on_callback, NULL);
  }
  ylsysfs_write_control_file((ylsysfs_get_led_inverted()) ? "show_icon" : "hide_icon",
                             "LED");
}

void yldisp_led_blink(unsigned int on_time, unsigned int off_time) {
  yp_ml_remove_event(-1, YLDISP_BLINK_ID);
  
  module_data.blink_on_time = on_time;
  module_data.blink_off_time = off_time;
  module_data.blink_reschedule = 0;
  
  if (on_time > 0) {
    /* turn on LED */
    led_on_callback(0, 0, NULL);
    
    if (off_time > 0) {
      module_data.blink_reschedule = 1;
      yp_ml_schedule_periodic_timer(YLDISP_BLINK_ID, (on_time + off_time),
                                    1, led_off_callback, NULL);
    }
  }
  else {
    /* turn off LED */
    led_off_callback(0, 0, NULL);
  }
}

void yldisp_led_off() {
  yldisp_led_blink(0, 1);
}

void yldisp_led_on() {
  yldisp_led_blink(1, 0);
}

/*****************************************************************/

static void show_date_callback(int id, int group, void *private_data) {
  time_t t;
  struct tm *tms;
  char line1[18];
  char line2[10];

  (void) private_data;

  t = time(NULL);
  tms = localtime(&t);
  
  strcpy(line2, "\t\t       ");
  line2[tms->tm_wday + 2] = '.';
  ylsysfs_write_control_file("line2", line2);

  sprintf(line1, "%2d.%2d.%2d.%02d\t\t\t %02d",
          tms->tm_mon + 1, tms->tm_mday,
          tms->tm_hour, tms->tm_min, tms->tm_sec);
  ylsysfs_write_control_file("line1", line1);
}

static void delayed_date_callback(int id, int group, void *private_data) {
  module_data.wait_date_after_count = 0;
  yldisp_show_date();
}

void yldisp_show_date() {
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  
  if (module_data.wait_date_after_count) {
    yp_ml_schedule_timer(YLDISP_DATETIME_ID, 5000,
                         delayed_date_callback, NULL);
  }
  else {
    show_date_callback(0, 0, NULL);
    yp_ml_schedule_periodic_timer(YLDISP_DATETIME_ID, 1000,
                                  1, show_date_callback, NULL);
  }
}


static void show_counter_callback(int id, int group, void *private_data) {
  time_t diff;
  char line1[18];
  int h,m,s;

  (void) private_data;

  diff = time(NULL) - module_data.counter_base;
  h = m = 0;
  s = diff % 60;
  if (diff >= 60) {
    m = ((diff - s) / 60);
    if (m >= 60) {
      h = m / 60;
      m -= h * 60;
    }
  }
  sprintf(line1, "      %2d.%02d\t\t\t %02d", h, m, s);
  ylsysfs_write_control_file("line1", line1);
  ylsysfs_write_control_file("line2", "\t\t       ");
}

void yldisp_show_counter() {
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  module_data.wait_date_after_count = 1;
  module_data.counter_base = time(NULL);
  show_counter_callback(0, 0, NULL);
}

void yldisp_start_counter() {
  yldisp_show_counter();
  yp_ml_schedule_periodic_timer(YLDISP_DATETIME_ID, 1000,
                                1, show_counter_callback, NULL);
}


void yldisp_stop_counter() {
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
}

/*****************************************************************/

void set_yldisp_call_type(yl_call_type_t ct) {
  char line1[14];
  
  strcpy(line1, "\t\t\t\t\t\t\t\t\t\t\t  ");
  if (ct == YL_CALL_IN) {
    line1[11] = '.';
  }
  else if (ct == YL_CALL_OUT) {
    line1[12] = '.';
  }
  
  ylsysfs_write_control_file("line1", line1);
}


yl_call_type_t get_yldisp_call_type() {
  return(0);
}


void set_yldisp_store_type(yl_store_type_t st) {
  char line1[15];
  
  strcpy(line1, "\t\t\t\t\t\t\t\t\t\t\t\t\t ");
  if (st == YL_STORE_ON) {
    line1[13] = '.';
  }
  ylsysfs_write_control_file("line1", line1);
}


yl_store_type_t get_yldisp_store_type() {
  return(0);
}

/*****************************************************************/

#define RINGTONE_MAXLEN 256
#define RING_DIR ".yeaphone/ringtone"
void set_yldisp_ringtone(char *ringname, unsigned char volume)
{
  int fd_in;
  char ringtone[RINGTONE_MAXLEN];
  int len = 0;
  char *ringfile;
  char *home;
  ylsysfs_model model;
  
  model = ylsysfs_get_model();

  if (model != YL_MODEL_P1K && model != YL_MODEL_P1KH)
    return;

  /* make sure the buzzer is turned off! */
  if (yp_ml_remove_event(-1, YLDISP_MINRING_ID) > 0) {
    ylsysfs_write_control_file("hide_icon", "RINGTONE");
    usleep(10000);   /* urgh! TODO: Get rid of the delay! */
  }
  /* ringname may be either a path relative to RINGDIR or an absolute path */
  home = getenv("HOME");
  if (home && (ringname[0] != '/')) {
    len = strlen(home) + strlen(RING_DIR) + strlen(ringname) + 3;
    ringfile = malloc(len);
    strcpy(ringfile, home);
    strcat(ringfile, "/"RING_DIR"/");
    strcat(ringfile, ringname);
  } else {
    ringfile = strdup(ringname);
  }

  /* read binary file (replacing first byte with volume)
  ** and write to ringtone control file
  ** TODO: track changes - if unchanged, don't set it again
  ** (write to current.ring file)
  */
  fd_in = open(ringfile, O_RDONLY);
  if (fd_in >= 0)
  {
    len = read(fd_in, ringtone, RINGTONE_MAXLEN);
    if (len > 4)
    {
      /* write volume (replace first byte) */
      ringtone[0] = volume;
      ylsysfs_write_control_file_buf("ringtone", ringtone, len);
    }
    else
    {
      fprintf(stderr, "too short ringfile %s (len=%d)\n", ringfile, len);
    }
    close(fd_in);
  }
  else
  {
    fprintf(stderr, "can't open ringfile %s\n", ringfile);
  }
  
  free(ringfile);
}


static void yldisp_minring_callback(int id, int group, void *private_data) {
  (void) private_data;
  if (module_data.ring_off_delayed) {
    ylsysfs_write_control_file("hide_icon", "RINGTONE");
    module_data.ring_off_delayed = 0;
  }
}

void set_yldisp_ringer(yl_ringer_state_t rs, int minring) {
  ylsysfs_model model;
  
  model = ylsysfs_get_model();

  switch (rs) {
    case YL_RINGER_ON:
      if (yp_ml_remove_event(-1, YLDISP_MINRING_ID) > 0) {
        ylsysfs_write_control_file("hide_icon",
                 (model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
        usleep(10000);   /* urgh! TODO: Get rid of the delay! */
      }
      module_data.ring_off_delayed = 0;
      yp_ml_schedule_timer(YLDISP_MINRING_ID, minring,
                           yldisp_minring_callback, NULL);
      ylsysfs_write_control_file("show_icon",
               (model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      break;
    case YL_RINGER_OFF_DELAYED:
      if (yp_ml_count_events(-1, YLDISP_MINRING_ID) > 0)
        module_data.ring_off_delayed = 1;
      else
        ylsysfs_write_control_file("hide_icon",
                 (model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      break;
    case YL_RINGER_OFF:
      ylsysfs_write_control_file("hide_icon",
               (model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      yp_ml_remove_event(-1, YLDISP_MINRING_ID);
      module_data.ring_off_delayed = 0;
      break;
  }
}

yl_ringer_state_t get_yldisp_ringer() {
  return(0);
}

void yldisp_ringer_vol_up() {
}

void yldisp_ringer_vol_down() {
}

/*****************************************************************/

void set_yldisp_text(char *text) {
  ylsysfs_write_control_file("line3", text);
}

char *get_yldisp_text() {
  return(NULL);
}

/*****************************************************************/

void set_yldisp_pstn_mode(int enabled)
{
  ylsysfs_model model = ylsysfs_get_model();
  if (model == YL_MODEL_B2K || model == YL_MODEL_B3G)
    ylsysfs_write_control_file((enabled) ? "show_icon" : "hide_icon", "PSTN");
}

/*****************************************************************/

void set_yldisp_dial_tone(int enabled)
{
  ylsysfs_model model = ylsysfs_get_model();
  if (model == YL_MODEL_B2K || model == YL_MODEL_B3G || model == YL_MODEL_P4K)
    ylsysfs_write_control_file((enabled) ? "show_icon" : "hide_icon", "DIALTONE");
}

/*****************************************************************/

void yldisp_hide_all() {
  set_yldisp_ringer(YL_RINGER_OFF, 0);
  yldisp_led_off();
  yldisp_stop_counter();
  ylsysfs_write_control_file("line1", "                 ");
  ylsysfs_write_control_file("line2", "         ");
  ylsysfs_write_control_file("line3", "            ");
  set_yldisp_pstn_mode(1);
  set_yldisp_dial_tone(0);
}

/*****************************************************************/

