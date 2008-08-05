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
#include "ypmainloop.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

/*****************************************************************/

#define NANOSEC 1000000000L

#define YLDISP_BLINK_ID     20
#define YLDISP_DATETIME_ID  21
#define YLDISP_MINRING_ID   22

const char *YLDISP_DRIVER_BASEDIR = "/sys/bus/usb/drivers/yealink/";
const char *YLDISP_INPUT_BASE = "/dev/input/event";

typedef struct yldisp_data_s {
  char *path_sysfs;
  char *path_event;
  char *path_buf;
  
  int alsa_cardp;
  int alsa_cardc;
  
  yl_models_t model;
  int led_inverted;
  
  unsigned int blink_on_time;
  unsigned int blink_off_time;
  int blink_off_reschedule;
  
  time_t counter_base;
  int wait_date_after_count;
  
  int ring_off_delayed;
} yldisp_data_t;


yldisp_data_t yldisp_data;

/*****************************************************************/
/* forward declarations */

static void yldisp_determine_model();

/*****************************************************************/

int exist_dir(const char *dirname)
{
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

char *find_dirent(const char *dirname, cmp_dirent compare, void *priv)
{
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

char *get_num_ptr(char *s)
{
  /* old link to input class directory found, now deprecated */
  char *cptr = s;
  while (*cptr && !isdigit(*cptr))
    cptr++;
  return (*cptr) ? cptr : NULL;
}

/*****************************************************************/

int cmp_devlink(const char *dirname, void *priv)
{
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

int cmp_dir(const char *dirname, void *priv) {
  return (dirname && !strcmp(dirname, (char *) priv));
}

int cmp_pcmlink(const char *dirname, void *priv) {
  const char *devptr;
  char cardtype = ((char *)priv)[0];
  int len = strlen(dirname);
  if (len < 10)
    return 0;
  if (cardtype != dirname[len - 1])
    return 0;
  devptr = &dirname[strlen(dirname) - 3];
  return (!strncmp(dirname, "sound:pcmC", 10) &&
          (!strncmp(devptr, "D0p", 3) || !strncmp(devptr, "D0c", 3)));
}

/*****************************************************************/

static int check_input_dir(const char *idir, const char *uniq) {
  char *symlink, *dirname, *evnum;
  int plen;
  struct stat event_stat;
  int ret = 0;

  yldisp_data.path_sysfs = NULL;
  yldisp_data.path_event = NULL;
  yldisp_data.path_buf   = NULL;
  symlink = NULL;

  plen = strlen(YLDISP_DRIVER_BASEDIR) + strlen(idir) + 10;
  yldisp_data.path_sysfs = malloc(plen);
  if (!yldisp_data.path_sysfs) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  strcpy(yldisp_data.path_sysfs, YLDISP_DRIVER_BASEDIR);
  strcat(yldisp_data.path_sysfs, idir);
  strcat(yldisp_data.path_sysfs, "/");
  printf("path_sysfs = %s\n", yldisp_data.path_sysfs);
  
  /* allocate buffer for sysfs interface path */
  yldisp_data.path_buf = malloc(plen + 50);
  if (!yldisp_data.path_buf) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  strcpy(yldisp_data.path_buf, yldisp_data.path_sysfs);
  
  /* allocate some buffer we can work with */
  symlink = malloc(plen + 50);
  if (!symlink) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  evnum = NULL;

  /* first get the 'input:inputX' link to find the 'uniq' file */
  strcpy(symlink, yldisp_data.path_sysfs);
  dirname = find_dirent(symlink, cmp_inputdir, "input:input");
  if (dirname) {
    strcat(symlink, dirname);
    free(dirname);
    if (uniq && *uniq) {
      int match = 0;
      char uniq_str[40];
      int len;
      FILE *fp;
      
      strcat(symlink, "/uniq");
      fp = fopen(symlink, "r");
      if (fp) {
        len = fread(uniq_str, 1, sizeof(uniq_str), fp);
        fclose(fp);
        uniq_str[len] = '\0';
        if ((len > 0) && (uniq_str[len - 1] < ' '))
          uniq_str[--len] = '\0';       /* strip off \n */
        match = !strcmp(uniq_str, uniq);
        printf("device id \"%s\" ->%s match\n", uniq_str, (match) ? "" : " no");
      }
      if (!match) {
        ret = 0;
        goto free_and_leave;
      }
      symlink[strlen(symlink) - 5] = '\0';     /* remove "/uniq" */
    }
    dirname = find_dirent(symlink, cmp_eventlink, NULL);
    if (dirname) {
      evnum = get_num_ptr(dirname);
      if (!evnum) {
        free(dirname);
        fprintf(stderr, "Could not find the event number!\n");
        ret = -ENOENT;
        goto free_and_leave;
      }
    }
    else {
      fprintf(stderr, "Could not find the event link!\n");
      ret = -ENOENT;
      goto free_and_leave;
    }
  }
  else {
    fprintf(stderr, "Could not find the input:inputX!\n");
    ret = -ENOENT;
    goto free_and_leave;
  }
  
  yldisp_data.path_event = malloc(strlen(YLDISP_INPUT_BASE) +
                                  strlen(evnum) + 4);
  strcpy(yldisp_data.path_event, YLDISP_INPUT_BASE);
  strcat(yldisp_data.path_event, evnum);
  free(dirname);
  printf("path_event = %s\n", yldisp_data.path_event);

  if (stat(yldisp_data.path_event, &event_stat)) {
    perror(yldisp_data.path_event);
    ret = (errno > 0) ? -errno : -ENOENT;
    goto free_and_leave;
  }
  if (!S_ISCHR(event_stat.st_mode)) {
    fprintf(stderr, "Error: %s is no character device\n",
            yldisp_data.path_event);
    ret = -ENOENT;
    goto free_and_leave;
  }

  return 1;

free_and_leave:
  if (yldisp_data.path_event) {
    free(yldisp_data.path_event);
    yldisp_data.path_event = NULL;
  }
  if (yldisp_data.path_sysfs) {
    free(yldisp_data.path_sysfs);
    yldisp_data.path_sysfs = NULL;
  }
  if (yldisp_data.path_buf) {
    free(yldisp_data.path_buf);
    yldisp_data.path_buf = NULL;
  }
  if (symlink)
    free(symlink);
  return ret;
}


static int find_input_dir(const char *uniq) {
  DIR *basedir_handle;
  struct dirent *basedirent;
  int ret = -ENOENT;
  
  basedir_handle = opendir(YLDISP_DRIVER_BASEDIR);
  if (!basedir_handle) {
    fprintf(stderr, "Please connect your handset first (driver not loaded)!\n");
    return (errno > 0) ? -errno : -ENOENT;
  }

  while ((basedirent = readdir(basedir_handle))) {
    if (!cmp_devlink(basedirent->d_name, NULL))
      continue;
    ret = check_input_dir(basedirent->d_name, uniq);
    if (ret > 0)
      break;
    if (ret < 0)
      return ret;
  }
  closedir(basedir_handle);
  if (ret <= 0) {
    fprintf(stderr, "Please connect your handset ");
    if (uniq && *uniq)
      fprintf(stderr, "with ID \"%s\" ", uniq);
    fprintf(stderr, "first!\n");
  }
  return (ret > 0) ? 0: ret;
}


static int extract_card_number(char *cardstr)
{
  char *cardnum;
  /* cut off last 3 characters */
  cardstr[strlen(cardstr) - 3] = '\0';
  /* get the card number */
  cardnum = get_num_ptr(cardstr);
  if (!cardnum) {
    fprintf(stderr, "Could not determine ALSA card number from '%s'!\n",
            cardstr);
    return -ENOENT;
  }
  return atoi(cardnum);
}


static int find_alsa_card() {
  DIR *dir_handle;
  struct dirent *dirent;
  char *dirname;
  int found;

  yldisp_data.alsa_cardc = -1;     /* capture */
  yldisp_data.alsa_cardp = -1;     /* playback */
  
  if (!yldisp_data.path_sysfs)
    return -ENOENT;

  strcpy(yldisp_data.path_buf, yldisp_data.path_sysfs);
  strcat(yldisp_data.path_buf, "../");

  dir_handle = opendir(yldisp_data.path_buf);
  if (!dir_handle) {
    perror(yldisp_data.path_buf);
    return (errno > 0) ? -errno : -ENOENT;
  }

  found = 0;
  while (!found && (dirent = readdir(dir_handle))) {
    if (!cmp_devlink(dirent->d_name, NULL))
      continue;
    strcpy(yldisp_data.path_buf, yldisp_data.path_sysfs);
    strcat(yldisp_data.path_buf, "../");
    strcat(yldisp_data.path_buf, dirent->d_name);
    printf("%s\n", yldisp_data.path_buf);
    
    dirname = find_dirent(yldisp_data.path_buf, cmp_pcmlink, "p");
    if (dirname) {
      found = 1;
      yldisp_data.alsa_cardp = extract_card_number(dirname);
      free(dirname);
    }
    dirname = find_dirent(yldisp_data.path_buf, cmp_pcmlink, "c");
    if (dirname) {
      found = 1;
      yldisp_data.alsa_cardc = extract_card_number(dirname);
      free(dirname);
    }
  }
  closedir(dir_handle);
  
  if ((yldisp_data.alsa_cardc < 0) || (yldisp_data.alsa_cardp < 0)) {
    fprintf(stderr, "Could not find required sound cards!\n");
    return -ENOENT;
  }
  return 0;
}


void yldisp_get_alsa_cards(int *cardp, int *cardc)
{
  if (cardp)
    *cardp = yldisp_data.alsa_cardp;
  if (cardc)
    *cardc = yldisp_data.alsa_cardc;
}

/*****************************************************************/

int yldisp_init(const char *uniq) {
  int ret;
  
  if ((ret = find_input_dir(uniq)) != 0)
    return ret;
  if ((ret = find_alsa_card()) != 0);
    return ret;
  
  yldisp_determine_model();
  
  yldisp_hide_all();
}

/*****************************************************************/

void yldisp_uninit()
{
  yp_ml_remove_event(-1, YLDISP_BLINK_ID);
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  
  /* more to come, eg. free */
  
  yldisp_hide_all();
}

/*****************************************************************/

static int yld_write_control_file_buf(yldisp_data_t *yld_ptr,
                                      const char *control,
                                      const char *buf,
                                      int size)
{
  FILE *fp;
  int res;
  
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
  
  return res;
}

/*****************************************************************/

static inline int yld_write_control_file(yldisp_data_t *yld_ptr,
                                         const char *control,
                                         const char *line)
{
  return yld_write_control_file_buf(yld_ptr, control, line, strlen(line));
}

/*****************************************************************/

static int yld_read_control_file_buf(yldisp_data_t *yld_ptr,
                                     const char *control,
                                     char *buf,
                                     int size)
{
  FILE *fp;
  int res;
  
  strcpy(yld_ptr->path_buf, yld_ptr->path_sysfs);
  strcat(yld_ptr->path_buf, control);
  
  fp = fopen(yld_ptr->path_buf, "rb");
  if (fp) {
    res = fread(buf, 1, size, fp);
    if (res < 0)
      perror(yld_ptr->path_buf);
    fclose(fp);
  }
  else {
    perror(yld_ptr->path_buf);
    res = -1;
  }
  
  return res;
}

/*****************************************************************/

static inline int yld_read_control_file(yldisp_data_t *yld_ptr,
                                        const char *control,
                                        char *line,
                                        int size)
{
  int len = yld_read_control_file_buf(yld_ptr, control, line, size - 1);
  if (len >= 0)
    line[len] = '\0';
  return len;
}

/*****************************************************************/

const static char *model_strings[] = { "Unknown", "P1K", "P4K", "B2K", "P1KH" };

static void yldisp_determine_model()
{
  char model_str[50];
  int len;
  
  len = yld_read_control_file(&yldisp_data, "model",
                              model_str, sizeof(model_str));
  yldisp_data.led_inverted = ((len < 0) || (model_str[0] == ' ') ||
                                           (model_str[0] == '*'));
  if ((len < 0) || !strcmp(model_str, "P1K") || strstr(model_str, "*P1K"))
    yldisp_data.model = YL_MODEL_P1K;
  else
  if (!strcmp(model_str, "P1KH"))
    yldisp_data.model = YL_MODEL_P1KH;
  else
  if (!strcmp(model_str, "P4K") || strstr(model_str, "*P4K"))
    yldisp_data.model = YL_MODEL_P4K;
  else
  if (!strcmp(model_str, "B2K") || strstr(model_str, "*B2K"))
    yldisp_data.model = YL_MODEL_B2K;
  else
    yldisp_data.model = YL_MODEL_UNKNOWN;
  
  if (yldisp_data.model != YL_MODEL_UNKNOWN)
    printf("Detected handset Yealink USB-%s\n", model_strings[yldisp_data.model]);
  else
    printf("Unable to detect type of handset\n");
}

yl_models_t get_yldisp_model()
{
  return yldisp_data.model;
}

/*****************************************************************/

static void led_off_callback(int id, int group, void *private_data) {
  yldisp_data_t *yld_ptr = private_data;
  
  if (yld_ptr->blink_off_reschedule) {
    yld_ptr->blink_off_reschedule = 0;
    yp_ml_schedule_periodic_timer(YLDISP_BLINK_ID,
                                  yld_ptr->blink_on_time + yld_ptr->blink_off_time,
                                  0, led_off_callback, private_data);
  }
  yld_write_control_file(yld_ptr,
                         (yldisp_data.led_inverted) ? "show_icon" : "hide_icon",
                         "LED");
}

static void led_on_callback(int id, int group, void *private_data) {
  yldisp_data_t *yld_ptr = private_data;
  
  yld_write_control_file(yld_ptr,
                         (yldisp_data.led_inverted) ? "hide_icon" : "show_icon",
                         "LED");
}

void yldisp_led_blink(unsigned int on_time, unsigned int off_time) {
  yp_ml_remove_event(-1, YLDISP_BLINK_ID);
  
  yldisp_data.blink_on_time = on_time;
  yldisp_data.blink_off_time = off_time;
  
  if (on_time > 0) {
    /* turn on LED */
    led_on_callback(0, 0, &yldisp_data);
    
    if (off_time > 0) {
      yldisp_data.blink_off_reschedule = 1;
      yp_ml_schedule_timer(YLDISP_BLINK_ID, off_time,
                           led_off_callback, &yldisp_data);
      yp_ml_schedule_periodic_timer(YLDISP_BLINK_ID, (on_time + off_time),
                                    1, led_on_callback, &yldisp_data);
    }
  }
  else {
    /* turn off LED */
    yldisp_data.blink_off_reschedule = 0;
    led_off_callback(0, 0, &yldisp_data);
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
  yldisp_data_t *yld_ptr = private_data;
  time_t t;
  struct tm *tms;
  char line1[18];
  char line2[10];

  t = time(NULL);
  tms = localtime(&t);
  
  strcpy(line2, "\t\t       ");
  line2[tms->tm_wday + 2] = '.';
  yld_write_control_file(yld_ptr, "line2", line2);

  sprintf(line1, "%2d.%2d.%2d.%02d\t\t\t %02d",
          tms->tm_mon + 1, tms->tm_mday,
          tms->tm_hour, tms->tm_min, tms->tm_sec);
  yld_write_control_file(yld_ptr, "line1", line1);
}

static void delayed_date_callback(int id, int group, void *private_data) {
  yldisp_data.wait_date_after_count = 0;
  yldisp_show_date();
}

void yldisp_show_date() {
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  
  if (yldisp_data.wait_date_after_count) {
    yp_ml_schedule_timer(YLDISP_DATETIME_ID, 5000,
                         delayed_date_callback, &yldisp_data);
  }
  else {
    show_date_callback(0, 0, &yldisp_data);
    yp_ml_schedule_periodic_timer(YLDISP_DATETIME_ID, 1000,
                                  1, show_date_callback, &yldisp_data);
  }
}


static void show_counter_callback(int id, int group, void *private_data) {
  yldisp_data_t *yld_ptr = private_data;
  time_t diff;
  char line1[18];
  int h,m,s;

  diff = time(NULL) - yld_ptr->counter_base;
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
  yld_write_control_file(yld_ptr, "line1", line1);
  yld_write_control_file(yld_ptr, "line2", "\t\t       ");
}

void yldisp_start_counter() {
  yp_ml_remove_event(-1, YLDISP_DATETIME_ID);
  yldisp_data.wait_date_after_count = 1;
  yldisp_data.counter_base = time(NULL);
  show_date_callback(0, 0, &yldisp_data);
  yp_ml_schedule_periodic_timer(YLDISP_DATETIME_ID, 1000,
                                1, show_counter_callback, &yldisp_data);
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

  if ((yldisp_data.model == YL_MODEL_P4K) || (yldisp_data.model == YL_MODEL_B2K))
    return;

  /* make sure the buzzer is turned off! */
  if (yp_ml_remove_event(-1, YLDISP_MINRING_ID) > 0) {
    yld_write_control_file(&yldisp_data, "hide_icon", "RINGTONE");
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
      yld_write_control_file_buf(&yldisp_data, "ringtone", ringtone, len);
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


void yldisp_minring_callback(int id, int group, void *private_data) {
  yldisp_data_t *yld_ptr = private_data;
  if (yld_ptr->ring_off_delayed) {
    yld_write_control_file(yld_ptr, "hide_icon", "RINGTONE");
    yld_ptr->ring_off_delayed = 0;
  }
}

void set_yldisp_ringer(yl_ringer_state_t rs, int minring) {
  switch (rs) {
    case YL_RINGER_ON:
      if (yp_ml_remove_event(-1, YLDISP_MINRING_ID) > 0) {
        yld_write_control_file(&yldisp_data, "hide_icon",
                 (yldisp_data.model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
        usleep(10000);   /* urgh! TODO: Get rid of the delay! */
      }
      yldisp_data.ring_off_delayed = 0;
      yp_ml_schedule_timer(YLDISP_MINRING_ID, minring,
                           yldisp_minring_callback, &yldisp_data);
      yld_write_control_file(&yldisp_data, "show_icon",
               (yldisp_data.model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      break;
    case YL_RINGER_OFF_DELAYED:
      if (yp_ml_count_events(-1, YLDISP_MINRING_ID) > 0)
        yldisp_data.ring_off_delayed = 1;
      else
        yld_write_control_file(&yldisp_data, "hide_icon",
                 (yldisp_data.model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      break;
    case YL_RINGER_OFF:
      yld_write_control_file(&yldisp_data, "hide_icon",
               (yldisp_data.model == YL_MODEL_P4K) ? "SPEAKER" : "RINGTONE");
      yp_ml_remove_event(-1, YLDISP_MINRING_ID);
      yldisp_data.ring_off_delayed = 0;
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
  yld_write_control_file(&yldisp_data, "line3", text);
}

char *get_yldisp_text() {
  return(NULL);
}


void yldisp_hide_all() {
  set_yldisp_ringer(YL_RINGER_OFF, 0);
  yldisp_led_off();
  yldisp_stop_counter();
  yld_write_control_file(&yldisp_data, "line1", "                 ");
  yld_write_control_file(&yldisp_data, "line2", "         ");
  yld_write_control_file(&yldisp_data, "line3", "            ");
}

/*****************************************************************/

char *get_yldisp_sysfs_path() {
  return(yldisp_data.path_sysfs);
}


char *get_yldisp_event_path() {
  return(yldisp_data.path_event);
}
