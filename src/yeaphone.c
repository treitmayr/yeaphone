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
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include "ylsysfs.h"
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
int ylcontrol_started = 0;


struct cmdline_options {
  char *uniq;
  int wait_for_device;
  int verbose;
};
static struct cmdline_options cmdline_opts = {
  uniq: NULL,
  wait_for_device: 0,
  verbose: 0
};

void parse_args(int argc, char **argv) {
  int c;
  int opt_index;
  static struct option long_options[] = {
    {"help", 0, 0, 'h'},
    {"id", 1, 0, 0},
    {"wait", 2, 0, 1},
    {"verbose", 0, 0, 'v'},
    {0, 0, 0, 0}
  };

  while ((c = getopt_long(argc, argv, "vhw", long_options, &opt_index)) >= 0) {
    switch (c) {
    case 0:
      cmdline_opts.uniq = strdup(optarg);
      break;
    case 1: 
      cmdline_opts.wait_for_device = (optarg) ? atoi(optarg) : 10;
      break;
    case 'w': 
      cmdline_opts.wait_for_device = 10;
      break;
    case 'v':
      cmdline_opts.verbose = 1;
      break;
    default:
      printf("Usage: yeaphone [options]\n");
      printf("\t--id=<id>\tAttach to the device with an ID <id>.\n");
      printf("\t--wait=[<sec>]\tCheck for the handset every <sec> seconds.\n");
      printf("\t-w\t\tCheck for the handset every 10 seconds.\n");
      printf("\t--verbose|-v\tShow debug messages.\n");
      printf("\t--help|-h\tPrint this help message.\n");
      exit(1);
    }
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


void terminate(int signum)
{
  if (ylcontrol_started) {
    puts("\ngraceful exit requested...");
    stop_ylcontrol();
  }
  else {
    signal(signum, SIG_DFL);
    raise(signum);
  }
}


int main(int argc, char **argv) {
  int ret;

  parse_args(argc, argv);
  read_config();
  
  /* graceful exit handler */
  signal(SIGTERM, &terminate);
  signal(SIGINT, &terminate);
  
  yp_ml_init();
  init_ylcontrol(mycode);
  ylcontrol_started = 0;

  while (1) {
    ret = ylsysfs_find_device(cmdline_opts.uniq);
    if (ret == -ENOENT) {
      if (cmdline_opts.wait_for_device) {
        printf("Please connect your handset, waiting...\n");
        sleep(cmdline_opts.wait_for_device);
        continue;
      }
      else {
        printf("No appropriate handset found, exiting...\n");
        break;
      }
    }
    else if (ret < 0) {
      break;
    }

    start_lpcontrol(1, NULL);
    start_ylcontrol();
    ylcontrol_started = 1;

    if (yp_ml_run() != 0)
      break;

    ylcontrol_started = 0;
    yldisp_clear();
  }

  return 0;
}

