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
const char *YLDISP_INPUT_BASE = "/dev/input/event";

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
  
  pthread_mutex_t *file_mutex;
  
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

int exist_dir(const char *dirname) {
  DIR *dir_handle;
  int result = 0;
  
  dir_handle = opendir(dirname);
  if (dir_handle) {
    closedir(dir_handle);
    result = 1;
  }
  return result;
}

/*****************************************************************/

typedef int (*cmp_dirent) (const char *dirname, void *priv);

char *find_dirent(const char *dirname, cmp_dirent compare, void *priv) {
  DIR *dir_handle;
  struct dirent *dirent;
  char *result = NULL;
  
  dir_handle = opendir(dirname);
  if (!dir_handle) {
    return NULL;
  }
  while (!result && (dirent = readdir(dir_handle))) {
    if (compare(dirent->d_name, priv)) {
      result = strdup(dirent->d_name);
      if (!result) {
        perror("__FILE__/__LINE__: strdup");
        abort();
      }
    }
  }
  closedir(dir_handle);
  return result;
}

/*****************************************************************/

char *get_num_ptr(char *s) {
  /* old link to input class directory found, now deprecated */
  char *cptr = s;
  while (*cptr && !isdigit(*cptr))
    cptr++;
  return (*cptr) ? cptr : NULL;
}

/*****************************************************************/

int cmp_devlink(const char *dirname, void *priv) {
  (void) priv;
  return (dirname && dirname[0] >= '0' && dirname[0] <= '9');
}

int cmp_eventlink(const char *dirname, void *priv) {
  (void) priv;
  return (dirname &&
          ((!strncmp(dirname, "event", 5) && isdigit(dirname[5])) ||
           (!strncmp(dirname, "input:event", 11) && isdigit(dirname[11]))));
}

int cmp_inputdir(const char *dirname, void *priv) {
  char *s = (char *) priv;
  return (dirname && !strncmp(dirname, s, strlen(s)) &&
          isdigit(dirname[strlen(s)]));
}

void yldisp_init() {
  DIR *driver_dir;
  struct dirent *driver_dirent;
  char *symlink;
  char *dirname;
  int plen;
  char *evnum;
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

  yldisp_data.file_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(yldisp_data.file_mutex, NULL);

  dirname = find_dirent(YLDISP_DRIVER_BASEDIR, cmp_devlink, NULL);
  if (!dirname) {
    fprintf(stderr, "Please connect your handset first!\n");
    abort();
  }
  plen = strlen(YLDISP_DRIVER_BASEDIR) + strlen(dirname) + 10;
  yldisp_data.path_sysfs = malloc(plen);
  if (!yldisp_data.path_sysfs) {
    perror("__FILE__/__LINE__: malloc");
    abort();
  }
  strcpy(yldisp_data.path_sysfs, YLDISP_DRIVER_BASEDIR);
  strcat(yldisp_data.path_sysfs, dirname);
  strcat(yldisp_data.path_sysfs, "/");
  free(dirname);
  printf("path_sysfs = %s\n", yldisp_data.path_sysfs);
  
  /* allocate buffer for sysfs interface path */
  yldisp_data.path_buf = malloc(plen + 20);
  if (!yldisp_data.path_buf) {
    perror("__FILE__/__LINE__: malloc");
    abort();
  }
  strcpy(yldisp_data.path_buf, yldisp_data.path_sysfs);
  
  evnum = NULL;
  symlink = malloc(plen + 50);
  if (!symlink) {
    perror("__FILE__/__LINE__: malloc");
    abort();
  }
  strcpy(symlink, yldisp_data.path_sysfs);
  dirname = find_dirent(symlink, cmp_eventlink, NULL);
  if (dirname) {
    evnum = get_num_ptr(dirname);
  }
  if (!evnum) {
    dirname = find_dirent(symlink, cmp_inputdir, "input:input");
    if (dirname) {
      strcat(symlink, dirname);
      free(dirname);
      dirname = find_dirent(symlink, cmp_eventlink, NULL);
      if (dirname) {
        evnum = get_num_ptr(dirname);
      }
    }
  }
  if (!evnum) {
    strcat(symlink, "input/");
    dirname = find_dirent(symlink, cmp_inputdir, "input");
    if (dirname) {
      strcat(symlink, dirname);
      free(dirname);
      dirname = find_dirent(symlink, cmp_eventlink, NULL);
      if (dirname) {
        evnum = get_num_ptr(dirname);
      }
    }
  }
  if (evnum) {
    yldisp_data.path_event = malloc(strlen(YLDISP_INPUT_BASE) +
                                    strlen(evnum) + 4);
    strcpy(yldisp_data.path_event, YLDISP_INPUT_BASE);
    strcat(yldisp_data.path_event, evnum);
    free(dirname);
    printf("path_event = %s\n", yldisp_data.path_event);
  }
  else {
    fprintf(stderr, "Could not find the input event interface via %s!\n",
            yldisp_data.path_sysfs);
    abort();
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
  if (yldisp_data.file_mutex) {
    free(yldisp_data.file_mutex);
    yldisp_data.file_mutex = NULL;
  }

  /* more to come, eg. free */
  
  yldisp_hide_all();
}

/*****************************************************************/

int yld_write_control_file_bin(yldisp_data_t *yld_ptr,
                               char *control,
                               char *buf,
                               int size) {
  FILE *fp;
  int res;
  
  if (yld_ptr->file_mutex) {
    if (pthread_mutex_lock(yld_ptr->file_mutex) != 0) {
      perror("__FILE__/__LINE__: pthread_mutex_lock");
      abort();
    }
  }
  strcpy(yld_ptr->path_buf, yld_ptr->path_sysfs);
  strcat(yld_ptr->path_buf, control);
  
  fp = fopen(yld_ptr->path_buf, "wb");
  if (fp) {
    res = fwrite(buf, 1, size, fp);
    if (res < size)
      perror(yld_ptr->path_buf);
    fclose(fp);
  }
  else {
    perror(yld_ptr->path_buf);
    res = -1;
  }
  
  if (yld_ptr->file_mutex) {
    if (pthread_mutex_unlock(yld_ptr->file_mutex) != 0) {
      perror("__FILE__/__LINE__: pthread_mutex_lock");
      abort();
    }
  }

  return (res < 0) ? 0 : res;
}

/*****************************************************************/

int yld_write_control_file(yldisp_data_t *yld_ptr,
                           char *control,
                           char *line) {
  FILE *fp;
  int res;
  
  if (yld_ptr->file_mutex) {
    if (pthread_mutex_lock(yld_ptr->file_mutex) != 0) {
      perror("__FILE__/__LINE__: pthread_mutex_lock");
      abort();
    }
  }
  strcpy(yld_ptr->path_buf, yld_ptr->path_sysfs);
  strcat(yld_ptr->path_buf, control);
  
  fp = fopen(yld_ptr->path_buf, "w");
  if (fp) {
    res = fputs(line, fp);
    if (res < 0)
      perror(yld_ptr->path_buf);
    fclose(fp);
  }
  else {
    perror(yld_ptr->path_buf);
    res = -1;
  }
  
  if (yld_ptr->file_mutex) {
    if (pthread_mutex_unlock(yld_ptr->file_mutex) != 0) {
      perror("__FILE__/__LINE__: pthread_mutex_lock");
      abort();
    }
  }

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
      yld_write_control_file(yld_ptr, "line1", "           \t\t\t   ");
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


#define RINGTONE_MAXLEN 256
#define RING_DIR ".yeaphone/ringtone"
void set_yldisp_set_ringtone(char *ringname, unsigned char volume)
{
  int fd_in;
  unsigned char ringtone[RINGTONE_MAXLEN];
  int len = 0;
  char *ringfile;
  char *home;

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
      yld_write_control_file_bin(&yldisp_data, "ringtone", ringtone, len);
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
