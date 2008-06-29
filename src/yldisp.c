/****************************************************************************
 *
 *  File: yldisp.c
 *
 *  Copyright (C) 2006, 2007  Thomas Reitmayr <treitmayr@yahoo.com>
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include "yldisp.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

/*****************************************************************/

#define NANOSEC 1000000000

const char *YLDISP_DRIVER_BASEDIR = "/sys/bus/usb/drivers/yealink/";
const char *YLDISP_EVENT_LINKNAME = "input:event";
const char *YLDISP_INPUT_BASEDIR = "/dev/";

typedef enum { YL_COUNTER_OFF, YL_COUNTER_DATE, YL_COUNTER_DURATION }
  yl_counter_state_t;

typedef struct yldisp_data_s {
  char *path_sysfs;
  char *path_event;
  char *path_buf;
  
  pthread_t blink_thread;
  pthread_t counter_thread;
  pthread_t ringvol_thread;
  
  pthread_cond_t *blink_cond;
  pthread_mutex_t *blink_mutex;
  
  pthread_cond_t *count_cond;
  pthread_mutex_t *count_mutex;
  
  unsigned int blink_on_time;
  unsigned int blink_off_time;
  
  yl_counter_state_t counter_state;
} yldisp_data_t;


yldisp_data_t yldisp_data;

/*****************************************************************/
/* forward declarations */

void *blink_proc(void *arg);
void *counter_proc(void *arg);

/*****************************************************************/

void yldisp_init() {
  DIR *driver_dir;
  struct dirent *driver_dirent;
  char *symlink;
  struct stat event_stat;
  
  yldisp_data.path_sysfs = NULL;
  yldisp_data.path_event = NULL;
  yldisp_data.path_buf   = NULL;
  yldisp_data.counter_state = YL_COUNTER_OFF;
  
  yldisp_data.blink_cond = malloc(sizeof(pthread_cond_t));
  yldisp_data.blink_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_cond_init(yldisp_data.blink_cond, NULL);
  pthread_mutex_init(yldisp_data.blink_mutex, NULL);
  pthread_mutex_lock(yldisp_data.blink_mutex);

  yldisp_data.count_cond = malloc(sizeof(pthread_cond_t));
  yldisp_data.count_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_cond_init(yldisp_data.count_cond, NULL);
  pthread_mutex_init(yldisp_data.count_mutex, NULL);
  pthread_mutex_lock(yldisp_data.count_mutex);

  driver_dir = opendir(YLDISP_DRIVER_BASEDIR);
  if (!driver_dir) {
    perror(YLDISP_DRIVER_BASEDIR);
    fprintf(stderr, "Please connect your handset first!\n");
    exit(1);
  }
  
  symlink = NULL;
  while ((driver_dirent = readdir(driver_dir)) && !symlink) {
    if (driver_dirent->d_name[0] >= '0' &&
        driver_dirent->d_name[0] <= '9') {
      /* assume we found the link when filename starts with a number */
      int plen = strlen(YLDISP_DRIVER_BASEDIR) +
                 strlen(driver_dirent->d_name) + 5;
      symlink = malloc(plen);
      if (!symlink) {
        perror("malloc");
        exit(1);
      }
      strcpy(symlink, YLDISP_DRIVER_BASEDIR);
      strcat(symlink, driver_dirent->d_name);
      strcat(symlink, "/");
      yldisp_data.path_sysfs = symlink;
      if (!yldisp_data.path_sysfs) {
        perror("__FILE__/__LINE__: strdup");
        abort();
      }
      yldisp_data.path_buf = malloc(plen + 20);
      if (!yldisp_data.path_buf) {
        perror("__FILE__/__LINE__: strdup");
        abort();
      }
      /*printf("path_sysfs = %s\n", symlink);*/
    }
  }
  
  closedir(driver_dir);
  
  if (!yldisp_data.path_sysfs) {
    fprintf(stderr, "Could not find device link in directory %s!\n",
            YLDISP_DRIVER_BASEDIR);
    exit(1);
  }
  
  driver_dir = opendir(symlink);
  if (!driver_dir) {
    perror(symlink);
    exit(1);
  }
  
  symlink = NULL;
  while ((driver_dirent = readdir(driver_dir)) && !symlink) {
    if (strncmp(driver_dirent->d_name,
                YLDISP_EVENT_LINKNAME,
                strlen(YLDISP_EVENT_LINKNAME)) == 0) {
      char *colon;
      strcpy(yldisp_data.path_buf, YLDISP_INPUT_BASEDIR);
      strcat(yldisp_data.path_buf, driver_dirent->d_name);
      colon = strrchr(yldisp_data.path_buf, ':');
      *colon = '/';    /* replace colon with slash */
      /*printf("path_buf = %s\n", yldisp_data.path_buf);*/
      yldisp_data.path_event = strdup(yldisp_data.path_buf);
      if (!yldisp_data.path_event) {
        perror("strdup");
        abort();
      }
      symlink = yldisp_data.path_event;
    }
  }
  
  closedir(driver_dir);
  
  if (!yldisp_data.path_event) {
    fprintf(stderr, "Could not find event link in directory %s!\n",
            yldisp_data.path_sysfs);
    exit(1);
  }
  
  if (stat(yldisp_data.path_event, &event_stat)) {
    perror(yldisp_data.path_event);
    abort();
  }
  if (!S_ISCHR(event_stat.st_mode)) {
    fprintf(stderr, "Error: %s is no character device\n",
            yldisp_data.path_event);
    abort();
  }
  
  pthread_create(&(yldisp_data.blink_thread), NULL, blink_proc, &yldisp_data);
  pthread_create(&(yldisp_data.counter_thread), NULL, counter_proc, &yldisp_data);
  
  usleep(100000);
  
  yldisp_hide_all();
}

/*****************************************************************/

void yldisp_uninit() {
  pthread_cancel(yldisp_data.blink_thread);
  pthread_join(yldisp_data.blink_thread, NULL);
  
  pthread_cancel(yldisp_data.counter_thread);
  pthread_join(yldisp_data.counter_thread, NULL);
  
  free(yldisp_data.blink_cond);
  free(yldisp_data.blink_mutex);
  free(yldisp_data.count_cond);
  free(yldisp_data.count_mutex);

  /* more to come */
  
  yldisp_hide_all();
}

/*****************************************************************/

int yld_wopen_control_file(yldisp_data_t *yld_ptr, char *control) {
  int fd;
  strcpy(yld_ptr->path_buf, yld_ptr->path_sysfs);
  strcat(yld_ptr->path_buf, control);
  fd = open(yld_ptr->path_buf, O_WRONLY);
  if (fd < 0) {
    perror(yld_ptr->path_buf);
    abort();
  }
  return fd;
}


int yld_write_control_file(yldisp_data_t *yld_ptr,
                           char *control,
                           char *line) {
  FILE *fp;
  int res;
  
  strcpy(yld_ptr->path_buf, yld_ptr->path_sysfs);
  strcat(yld_ptr->path_buf, control);
  
  fp = fopen(yld_ptr->path_buf, "w");
  if (!fp) {
    perror(yld_ptr->path_buf);
    return 0;
  }
  
  /*flockfile(fp);*/
  res = fputs(line, fp);
  if (res < 0)
    perror("fputs");
  /*funlockfile(fp);*/
  
  fclose(fp);
  
  return (res < 0) ? 0 : strlen(line);
}

/*****************************************************************/

void *blink_proc(void *arg) {
  yldisp_data_t *yld_ptr = arg;
  struct timespec abstime, on_time, off_time;
  struct timespec *inctime;
  int turn_on = 1;
  int init = 0;
  
  while (1) {
    if (yld_ptr->blink_on_time == 0 && yld_ptr->blink_off_time == 0) {
      /* no blinking requested yet */
      pthread_cond_wait(yld_ptr->blink_cond, yld_ptr->blink_mutex);
      init = 1;
    }
    
    if (init) {
      abstime.tv_sec = time(NULL);
      abstime.tv_nsec = 0;
      on_time.tv_sec = yld_ptr->blink_on_time / 1000;
      on_time.tv_nsec = (yld_ptr->blink_on_time - (on_time.tv_sec * 1000)) * 1000000L;
      off_time.tv_sec = yld_ptr->blink_off_time / 1000;
      off_time.tv_nsec = (yld_ptr->blink_off_time - (off_time.tv_sec * 1000)) * 1000000L;
      turn_on = 1;
      init = 0;
    }
    
    inctime = (turn_on) ? &on_time : &off_time;
    
    if (inctime->tv_sec > 0 || inctime->tv_nsec > 0) {
      yld_write_control_file(yld_ptr,
                             (turn_on) ? "hide_icon" : "show_icon",
                             "LED");
      abstime.tv_sec += inctime->tv_sec;
      abstime.tv_nsec += inctime->tv_nsec;
      if (abstime.tv_nsec >= NANOSEC) {
        abstime.tv_nsec -= NANOSEC;
        abstime.tv_sec++;
      }
      if (abstime.tv_sec + 2 < time(NULL)) {
        abstime.tv_sec = time(NULL);
        abstime.tv_nsec = 0;
      }
      if (pthread_cond_timedwait(yld_ptr->blink_cond,
                                 yld_ptr->blink_mutex,
                                 &abstime) != ETIMEDOUT) {
        init = 1;
      }
    }
    
    turn_on = !turn_on;
  }
  
  return(NULL);
}


void yldisp_led_blink(unsigned int on_time, unsigned int off_time) {
  pthread_mutex_lock(yldisp_data.blink_mutex);
  yldisp_data.blink_on_time = on_time;
  yldisp_data.blink_off_time = off_time;
  pthread_mutex_unlock(yldisp_data.blink_mutex);
  
  pthread_cond_signal(yldisp_data.blink_cond);
}


void yldisp_led_off() {
  pthread_mutex_lock(yldisp_data.blink_mutex);
  yldisp_data.blink_on_time = 0;
  yldisp_data.blink_off_time = 100000;
  pthread_mutex_unlock(yldisp_data.blink_mutex);
  
  pthread_cond_signal(yldisp_data.blink_cond);
}


void yldisp_led_on() {
  pthread_mutex_lock(yldisp_data.blink_mutex);
  yldisp_data.blink_on_time = 100000;
  yldisp_data.blink_off_time = 0;
  pthread_mutex_unlock(yldisp_data.blink_mutex);
  
  pthread_cond_signal(yldisp_data.blink_cond);
}

/*****************************************************************/

void *counter_proc(void *arg) {
  yldisp_data_t *yld_ptr = arg;
  struct timespec abstime, basetime;
  int init = 0;
  time_t t;
  struct tm *tms;
  time_t diff;
  int wday;
  char line1[18];
  char line2[10];

  /* prevent warnings */
  basetime.tv_sec = 0;
  basetime.tv_nsec = 0;
  wday = -1;
  
  /* the thread's main loop */
  while (1) {
    if (yld_ptr->counter_state == YL_COUNTER_OFF) {
      /* no counting requested yet */
      yld_write_control_file(&yldisp_data, "line1", "           \t\t\t   ");
      yld_write_control_file(yld_ptr, "line2", "\t\t       ");
      pthread_cond_wait(yld_ptr->count_cond, yld_ptr->count_mutex);
      init = 1;
    }
    
    if (init) {
      abstime.tv_sec = time(NULL);
      abstime.tv_nsec = 0;
      if (yld_ptr->counter_state == YL_COUNTER_DATE) {
        /* force update of the day of the week */
        wday = -1;
      }
      else
      if (yld_ptr->counter_state == YL_COUNTER_DURATION) {
        /* remember the time the counter was started */
        basetime.tv_sec = abstime.tv_sec;
        basetime.tv_nsec = 0;
        yld_write_control_file(yld_ptr, "line2", "\t\t       ");
      }
      init = 0;
    }
    
    if (yld_ptr->counter_state == YL_COUNTER_DATE) {
      if (abstime.tv_sec + 2 < time(NULL)) {
        /* catch up if we were suspended */
        abstime.tv_sec = time(NULL);
      }
      
      t = abstime.tv_sec;
      tms = localtime(&t);
      
      if (wday != tms->tm_wday) {
        strcpy(line2, "\t\t       ");
        wday = tms->tm_wday;
        line2[wday + 2] = '.';
        yld_write_control_file(yld_ptr, "line2", line2);
      }
      
      sprintf(line1, "%2d.%2d.%2d.%02d\t\t\t %02d",
              tms->tm_mon + 1, tms->tm_mday,
              tms->tm_hour, tms->tm_min, tms->tm_sec);
      yld_write_control_file(yld_ptr, "line1", line1);
      
      abstime.tv_sec++;
      if (pthread_cond_timedwait(yld_ptr->count_cond,
                                 yld_ptr->count_mutex,
                                 &abstime) != ETIMEDOUT) {
        init = 1;
      }
    }
    else
    if (yld_ptr->counter_state == YL_COUNTER_DURATION) {
      int h,m,s;
      h = m = s = 0;
      if (abstime.tv_sec + 2 < time(NULL)) {
        /* catch up if we were suspended */
        abstime.tv_sec = time(NULL);
      }
      diff = abstime.tv_sec - basetime.tv_sec;
      s = diff % 60;
      if (diff >= 60) {
        m = ((diff - s) / 60);
        if (m >= 60) {
          h = m / 60;
          m -= h * 60;
        }
      }
      sprintf(line1, "      %2d.%02d\t\t\t %02d", h, m, s);
      yld_write_control_file(yld_ptr, "line1", line1);
      
      abstime.tv_sec++;
      if (pthread_cond_timedwait(yld_ptr->count_cond,
                                 yld_ptr->count_mutex,
                                 &abstime) != ETIMEDOUT) {
        if (yld_ptr->counter_state == YL_COUNTER_DATE) {
          /* when changing from counter to date, insert a 3 sec delay */
          abstime.tv_sec += 3;
          pthread_cond_timedwait(yld_ptr->count_cond,
                                 yld_ptr->count_mutex,
                                 &abstime);
        }
        init = 1;
      }
    }
  }
  
  return(NULL);
}


void yldisp_show_date() {
  pthread_mutex_lock(yldisp_data.count_mutex);
  yldisp_data.counter_state = YL_COUNTER_DATE;
  pthread_mutex_unlock(yldisp_data.count_mutex);
  
  pthread_cond_signal(yldisp_data.count_cond);
}


void yldisp_start_counter() {
  pthread_mutex_lock(yldisp_data.count_mutex);
  yldisp_data.counter_state = YL_COUNTER_DURATION;
  pthread_mutex_unlock(yldisp_data.count_mutex);
  
  pthread_cond_signal(yldisp_data.count_cond);
}


void yldisp_stop_counter() {
  pthread_mutex_lock(yldisp_data.count_mutex);
  yldisp_data.counter_state = YL_COUNTER_OFF;
  pthread_mutex_unlock(yldisp_data.count_mutex);
  
  pthread_cond_signal(yldisp_data.count_cond);
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
  
  yld_write_control_file(&yldisp_data, "line1", line1);
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
  yld_write_control_file(&yldisp_data, "line1", line1);
}


yl_store_type_t get_yldisp_store_type() {
  return(0);
}


void set_yldisp_ringer(yl_ringer_state_t rs) {
  yld_write_control_file(&yldisp_data,
                         (rs == YL_RINGER_ON) ? "show_icon" : "hide_icon",
                         "RINGTONE");
}

yl_ringer_state_t get_yldisp_ringer() {
  return(0);
}

void yldisp_ringer_vol_up() {
}

void yldisp_ringer_vol_down() {
}

void set_yldisp_text(char *text) {
  yld_write_control_file(&yldisp_data, "line3", text);
}

char *get_yldisp_text() {
  return(NULL);
}


void yldisp_hide_all() {
  yldisp_led_off();
  yldisp_stop_counter();
  yld_write_control_file(&yldisp_data, "line1", "                 ");
  yld_write_control_file(&yldisp_data, "line2", "         ");
  yld_write_control_file(&yldisp_data, "line3", "            ");
}


char *get_yldisp_sysfs_path() {
  return(yldisp_data.path_sysfs);
}


char *get_yldisp_event_path() {
  return(yldisp_data.path_event);
}
