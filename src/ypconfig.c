/****************************************************************************
 *
 *  File: ypconfig.c
 *
 *  Copyright (C) 2006  Thomas Reitmayr <treitmayr@yahoo.com>
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
#include <string.h>
#include "ypconfig.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif


#define MAX_LINE_LEN 256

typedef struct yc_dll_s {
  char *key;
  char *val;
  struct yc_dll_s *next;
} yc_dll_t;


static char *ypconfig_fname = NULL;
static yc_dll_t *yl_dll_root = NULL;


void yc_dll_destroy() {
  yc_dll_t *current;
  yc_dll_t *next;
  
  current = yl_dll_root;
  while (current) {
    next = current->next;
    free(current);
    current = next;
  }
}


int ypconfig_read(const char *fname) {
  FILE *fp;
  char *linebuf;
  char *key;
  char *val;
  yc_dll_t **current;
  int num = 0;
  
  if (fname) {
    /* use the supplied file name */
    if (ypconfig_fname)
      free(ypconfig_fname);
    ypconfig_fname = strdup(fname);
  }
  else {
    /* try to use a previously specified file name */
    if (!ypconfig_fname)
      return -1;
  }
  
  fp = fopen(ypconfig_fname, "r");
  if (!fp) {
    perror(ypconfig_fname);
    return -1;
  }
  
  /* deallocate any existing dll-nodes */
  yc_dll_destroy();
  
  linebuf = malloc(MAX_LINE_LEN);
  current = &yl_dll_root;
  
  while (fgets(linebuf, MAX_LINE_LEN, fp)) {
    char *cp;
    int cnt;
    
    /* remove trailing white space */
    if (*linebuf) {
      cp = linebuf + strlen(linebuf) - 1;
      while (*cp <= ' ')
        *cp-- = '\0';
    }
    
    /* get the key part */
    key = linebuf;
    while (*key && *key <= ' ')
      key++;
    if (!*key)
      continue;
    
    cp = key + 1;
    while (*cp && *cp > ' ')
      cp++;
    
    val = (cp) ? cp+1 : cp;
    *cp = '\0';
    
    /* get the value part */
    /* TODO: handle escaped characters, eg. \" */
    while (*val && (*val <= ' ' || *val == '='))
      val++;
    
    /* check if it is a quoted string */
    cnt = strlen(val) - 1;
    if ((cnt > 0) &&
        ((val[0] == '"' && val[cnt] == '"') ||
         (val[0] == '\'' && val[cnt] == '\''))) {
      /* remove the quotes */
      val[cnt] = '\0';
      val++;
    }
    
    /* add to linked list */
    *current = malloc(sizeof(yc_dll_t));
    (*current)->key = strdup(key);
    (*current)->val = strdup(val);
    (*current)->next = NULL;
    current = &((*current)->next);
    num++;
  }
  
  fclose(fp);
  free(linebuf);
  
  /* return number of read pairs */
  return num;
}


char *ypconfig_get_value(const char *key) {
  yc_dll_t **current;
  current = &yl_dll_root;
  
  while (*current) {
    if (!strcmp((*current)->key, key))
      return (*current)->val;
    current = &((*current)->next);
  }
  return NULL;
}


void ypconfig_set_pair(const char *key, const char *value) {
  yc_dll_t **current;
  current = &yl_dll_root;
  
  while (*current) {
    if (!strcmp((*current)->key, key)) {
      /* key already exists */
      if ((*current)->val)
        free((*current)->val);
      (*current)->val = strdup(value);
      return;
    }
    current = &((*current)->next);
  }
  
  /* this is a new key */
  *current = malloc(sizeof(yc_dll_t));
  (*current)->key = strdup(key);
  (*current)->val = strdup(value);
  (*current)->next = NULL;
}


int ypconfig_write(char *fname) {
  FILE *fp;
  yc_dll_t **current;
  char *val;
  int num = 0;
  
  if (fname) {
    /* use the supplied file name */
    if (ypconfig_fname)
      free(ypconfig_fname);
    ypconfig_fname = strdup(fname);
  }
  else {
    /* try to use a previously specified file name */
    if (!ypconfig_fname)
      return -1;
  }
  
  fp = fopen(ypconfig_fname, "w");
  if (!fp) {
    perror(ypconfig_fname);
    return -1;
  }
  
  current = &yl_dll_root;
  
  while (*current) {
    fputs((*current)->key, fp);
    val = (*current)->val;
    if ((val[0] == '=') || strchr(val, ' ') || strchr(val, '\t')) {
      fputs("\t\"", fp);
      fputs(val, fp);
      fputs("\"\n", fp);
    }
    else {
      fputs("\t", fp);
      fputs(val, fp);
      fputs("\n", fp);
    }
    current = &((*current)->next);
    num++;
  }
  
  fclose(fp);
  
  /* return number of written pairs */
  return num;
}
