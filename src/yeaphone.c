/****************************************************************************
 *
 *  File: yeaphone.c
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

#include <mcheck.h>

#include <stdlib.h>
#include <signal.h>
#include "yldisp.h"
#include "lpcontrol.h"
#include "ylcontrol.h"
#include "ypconfig.h"
#include "ypmainloop.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif


#define CONFIG_FILE ".yeaphonerc"

char *mycode = "43";   /* default to Austria ;) */


void parse_args(int argc, char **argv) {
  char *arg;
  
  while (argc--) {
    arg = *(argv++);
    if (!strcmp(arg, "-h") ||
        !strcmp(arg, "--help")) {
      printf("Usage: yeaphone\n");
      exit(1);
    }
    /*else
    if (!strncmp(arg, "--mycode=", 9)) {
      mycode = strdup(arg+9);
    }*/
  }
}


void read_config() {
  char *cfgfile;
  char *home;
  int len = 0;
  
  home = getenv("HOME");
  if (home) {
    len = strlen(home) + strlen(CONFIG_FILE) + 2;
    cfgfile = malloc(len);
    strcpy(cfgfile, home);
    strcat(cfgfile, "/"CONFIG_FILE);
  }
  else {
    cfgfile = strdup(CONFIG_FILE);
  }
  
  ypconfig_read(cfgfile);
  
  free(cfgfile);
}


void terminate(int signal)
{
  puts("\ngraceful exit requested...");
  stop_ylcontrol();
}


int main(int argc, char **argv) {
  parse_args(argc, argv);
  read_config();
  
  yp_ml_init();
  
  yldisp_init();
  init_ylcontrol(mycode);
  
  start_lpcontrol(1, NULL);
  start_ylcontrol();
  
  /* graceful exit handler */
  signal(SIGINT, &terminate);
  signal(SIGTERM, &terminate);
  
#ifdef MTRACE
  /* track down memory leaks !! */
  sleep(10);
  mtrace();
#endif
  
  yp_ml_run();
 
  return(0);
}

