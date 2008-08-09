/****************************************************************************
 *
 *  File: ylsysfs.c
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
#include "ylsysfs.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

/*****************************************************************/

static const char *YLSYSFS_DRIVER_BASEDIR = "/sys/bus/usb/drivers/yealink/";
static const char *YLSYSFS_INPUT_BASE = "/dev/input/event";

typedef struct ylsysfs_data ylsysfs_data;
struct ylsysfs_data {
  char *path_sysfs;
  char *path_event;
  char *path_buf;
  
  int alsa_card;
  
  ylsysfs_model model;
  int led_inverted;
};

static ylsysfs_data module_data = {
  path_sysfs: NULL,
  path_event: NULL,
  path_buf:   NULL,
  alsa_card:  -1,
  model:      YL_MODEL_UNKNOWN
};

/*****************************************************************/
/* forward declarations */

static void determine_model();

/*****************************************************************/

typedef int (*cmp_dirent) (const char *dirname, void *priv);

static char *find_dirent(const char *dirname, cmp_dirent compare, void *priv)
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

static char *get_num_ptr(char *s)
{
  /* find first occurance of a digit */
  char *cptr = s;
  while (*cptr && !isdigit(*cptr))
    cptr++;
  return (*cptr) ? cptr : NULL;
}

/*****************************************************************/

static int cmp_devlink(const char *dirname, void *priv)
{
  (void) priv;
  return (dirname && dirname[0] >= '0' && dirname[0] <= '9');
}

static int cmp_eventlink(const char *dirname, void *priv)
{
  (void) priv;
  return (dirname &&
          ((!strncmp(dirname, "event", 5) && isdigit(dirname[5])) ||
           (!strncmp(dirname, "input:event", 11) && isdigit(dirname[11]))));
}

static int cmp_inputdir(const char *dirname, void *priv)
{
  char *s = (char *) priv;
  return (dirname && !strncmp(dirname, s, strlen(s)) &&
          isdigit(dirname[strlen(s)]));
}

static int cmp_dir(const char *dirname, void *priv)
{
  return (dirname && !strcmp(dirname, (char *) priv));
}

static int cmp_pcmlink(const char *dirname, void *priv)
{
  unsigned card;
  int ret;
  ret = sscanf(dirname, "sound:pcmC%uD0[cp]", &card);
  if ((ret > 0) && priv)
    ((ylsysfs_data *)priv)->alsa_card = card;
  return (ret > 0);
}

/*****************************************************************/

static int check_input_dir(const char *idir, const char *uniq)
{
  char *symlink, *dirname, *evnum;
  int plen;
  struct stat event_stat;
  int ret = 0;

  if (module_data.path_event) {
    free(module_data.path_event);
    module_data.path_event = NULL;
  }
  if (module_data.path_sysfs) {
    free(module_data.path_sysfs);
    module_data.path_sysfs = NULL;
  }
  symlink = NULL;

  plen = strlen(YLSYSFS_DRIVER_BASEDIR) + strlen(idir) + 10;
  module_data.path_sysfs = malloc(plen);
  if (!module_data.path_sysfs) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  strcpy(module_data.path_sysfs, YLSYSFS_DRIVER_BASEDIR);
  strcat(module_data.path_sysfs, idir);
  strcat(module_data.path_sysfs, "/");

  /* allocate buffer for sysfs interface path */
  module_data.path_buf = malloc(plen + 50);
  if (!module_data.path_buf) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }

  /* allocate some buffer we can work with */
  symlink = malloc(plen + 50);
  if (!symlink) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  evnum = NULL;

  /* first get the 'input:inputX' link to find the 'uniq' file */
  strcpy(symlink, module_data.path_sysfs);
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
        while ((len > 0) && (uniq_str[len - 1] <= ' '))
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

  module_data.path_event = malloc(strlen(YLSYSFS_INPUT_BASE) +
                                  strlen(evnum) + 4);
  if (!module_data.path_event) {
    perror("__FILE__/__LINE__: malloc");
    ret = -ENOMEM;
    goto free_and_leave;
  }
  strcpy(module_data.path_event, YLSYSFS_INPUT_BASE);
  strcat(module_data.path_event, evnum);
  free(dirname);

  printf("path_sysfs = %s\n", module_data.path_sysfs);
  printf("path_event = %s\n", module_data.path_event);

  if (stat(module_data.path_event, &event_stat)) {
    perror(module_data.path_event);
    ret = (errno > 0) ? -errno : -ENOENT;
    goto free_and_leave;
  }
  if (!S_ISCHR(event_stat.st_mode)) {
    fprintf(stderr, "Error: %s is no character device\n",
            module_data.path_event);
    ret = -ENOENT;
    goto free_and_leave;
  }

  free(symlink);
  return 1;

free_and_leave:
  if (module_data.path_event) {
    free(module_data.path_event);
    module_data.path_event = NULL;
  }
  if (module_data.path_sysfs) {
    free(module_data.path_sysfs);
    module_data.path_sysfs = NULL;
  }
  if (symlink)
    free(symlink);
  return ret;
}

/*****************************************************************/

static int find_input_dir(const char *uniq)
{
  DIR *basedir_handle;
  struct dirent *basedirent;
  int ret = -ENOENT;
  
  basedir_handle = opendir(YLSYSFS_DRIVER_BASEDIR);
  if (!basedir_handle) {
    fprintf(stderr, "Please connect your handset first (driver not loaded)!\n");
    return (errno > 0) ? -errno : -ENOENT;
  }

  while ((basedirent = readdir(basedir_handle))) {
    if (!cmp_devlink(basedirent->d_name, NULL))
      continue;
    ret = check_input_dir(basedirent->d_name, uniq);
    if (ret != 0)
      break;
  }
  closedir(basedir_handle);
  if (ret == 0) {
    fprintf(stderr, "Please connect your handset ");
    if (uniq && *uniq)
      fprintf(stderr, "with ID \"%s\" ", uniq);
    fprintf(stderr, "first!\n");
  }
  return (ret > 0) ? 0: ret;
}

/*****************************************************************/

static int find_alsa_card()
{
  DIR *dir_handle;
  struct dirent *dirent;
  char *dirname;
  int found;

  module_data.alsa_card = -1;
  
  if (!module_data.path_sysfs)
    return -ENOENT;

  strcpy(module_data.path_buf, module_data.path_sysfs);
  strcat(module_data.path_buf, "../");

  dir_handle = opendir(module_data.path_buf);
  if (!dir_handle) {
    perror(module_data.path_buf);
    return (errno > 0) ? -errno : -ENOENT;
  }

  found = 0;
  while (!found && (dirent = readdir(dir_handle))) {
    if (!cmp_devlink(dirent->d_name, NULL))
      continue;
    strcpy(module_data.path_buf, module_data.path_sysfs);
    strcat(module_data.path_buf, "../");
    strcat(module_data.path_buf, dirent->d_name);
    
    dirname = find_dirent(module_data.path_buf, cmp_pcmlink, &module_data);
    if (dirname) {
      found = 1;
      free(dirname);
    }
  }
  closedir(dir_handle);
  
  if (module_data.alsa_card < 0) {
    fprintf(stderr, "Could not find required sound card!\n");
    return -ENOENT;
  }
  return 0;
}

/*****************************************************************/

const static char *model_strings[] = { "Unknown", "P1K", "P4K",
                                       "B2K", "B3G", "P1KH" };

static void determine_model()
{
  char model_str[50];
  int len;
  
  len = ylsysfs_read_control_file("model", model_str, sizeof(model_str));
  module_data.led_inverted = ((len < 0) || (model_str[0] == ' ') ||
                                           (model_str[0] == '*'));
  if ((len < 0) || !strcmp(model_str, "P1K") || strstr(model_str, "*P1K"))
    module_data.model = YL_MODEL_P1K;
  else
  if (!strcmp(model_str, "P1KH"))
    module_data.model = YL_MODEL_P1KH;
  else
  if (!strcmp(model_str, "P4K") || strstr(model_str, "*P4K"))
    module_data.model = YL_MODEL_P4K;
  else
  if (!strcmp(model_str, "B2K") || strstr(model_str, "*B2K"))
    module_data.model = YL_MODEL_B2K;
  else
  if (!strcmp(model_str, "B3G"))
    module_data.model = YL_MODEL_B3G;
  else
    module_data.model = YL_MODEL_UNKNOWN;
  
  if (module_data.model != YL_MODEL_UNKNOWN)
    printf("Detected handset Yealink USB-%s\n", model_strings[module_data.model]);
  else
    printf("Unable to detect type of handset\n");
}

/*****************************************************************/

int ylsysfs_find_device(const char *uniq)
{
  int ret;
  
  if ((ret = find_input_dir(uniq)) != 0)
    return ret;
  if ((ret = find_alsa_card()) != 0)
    return ret;
  
  determine_model();

  return 0;
}

/*****************************************************************/

int ylsysfs_write_control_file_buf(const char *control,
                                   const char *buf,
                                   int size)
{
  FILE *fp;
  int res;
  
  if (!module_data.path_buf || !module_data.path_sysfs)
    return -ENOENT;

  strcpy(module_data.path_buf, module_data.path_sysfs);
  strcat(module_data.path_buf, control);
  
  fp = fopen(module_data.path_buf, "wb");
  if (fp) {
    res = fwrite(buf, 1, size, fp);
    if (res < size)
      perror(module_data.path_buf);
    fclose(fp);
  }
  else {
    perror(module_data.path_buf);
    res = (errno > 0) ? -errno : -1;
  }
  
  return res;
}

/*****************************************************************/

int ylsysfs_write_control_file(const char *control,
                               const char *line)
{
  return ylsysfs_write_control_file_buf(control, line, strlen(line));
}

/*****************************************************************/

int ylsysfs_read_control_file_buf(const char *control,
                                  char *buf,
                                  int size)
{
  FILE *fp;
  int res;
  
  if (!module_data.path_buf || !module_data.path_sysfs)
    return -ENOENT;

  strcpy(module_data.path_buf, module_data.path_sysfs);
  strcat(module_data.path_buf, control);
  
  fp = fopen(module_data.path_buf, "rb");
  if (fp) {
    res = fread(buf, 1, size, fp);
    if (res < 0)
      perror(module_data.path_buf);
    fclose(fp);
  }
  else {
    perror(module_data.path_buf);
    res = (errno > 0) ? -errno : -1;
  }
  
  return res;
}

/*****************************************************************/

int ylsysfs_read_control_file(const char *control,
                              char *line,
                              int size)
{
  int len = ylsysfs_read_control_file_buf(control, line, size - 1);
  if (len >= 0)
    line[len] = '\0';
  return len;
}

/*****************************************************************/

const char *ylsysfs_get_sysfs_path()
{
  return module_data.path_sysfs;
}

/*****************************************************************/

const char *ylsysfs_get_event_path()
{
  return module_data.path_event;
}

/*****************************************************************/

int ylsysfs_get_led_inverted()
{
  return module_data.led_inverted;
}

/*****************************************************************/

int ylsysfs_get_alsa_card()
{
  return module_data.alsa_card;
}

/*****************************************************************/

ylsysfs_model ylsysfs_get_model()
{
  return module_data.model;
}

/*****************************************************************/

