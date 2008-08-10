/****************************************************************************
 *
 *  File: ylcontrol.c
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
#include <fcntl.h>
#include <ctype.h>
#include <assert.h>
#include <linux/input.h>

#include <linphone/linphonecore.h>
#include <osipparser2/osip_message.h>
#include "yldisp.h"
#include "ylsysfs.h"
#include "lpcontrol.h"
#include "ylcontrol.h"
#include "ypconfig.h"
#include "ypmainloop.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif


#define MAX_NUMBER_LEN 32


typedef struct ylcontrol_data_s {
  int evfd;
  
  int kshift;
  int pressed;
  
  int prep_store;
  int prep_recall;
  
  char dialnum[MAX_NUMBER_LEN];
  char callernum[MAX_NUMBER_LEN];
  char dialback[MAX_NUMBER_LEN];
  
  char *intl_access_code;
  char *country_code;
  char *natl_access_code;

  char *default_display;
  
  int hard_shutdown;
  int linphone_2_1_1_bug;
} ylcontrol_data_t;

ylcontrol_data_t ylcontrol_data;

/*****************************************************************/

void display_dialnum(char *num) {
  int len = (num) ? strlen(num) : 0;
  if (len < 12) {
    char buf[13];
    strcpy(buf, "            ");
    if (num)
      strncpy(buf, num, len);
    set_yldisp_text(buf);
  }
  else {
    set_yldisp_text(num + len - 12);
  }
}

/**********************************/

void extract_callernum(ylcontrol_data_t *ylc_ptr, const char *line) {
  int err;
  char *line1 = NULL;
  osip_from_t *url;
  char *num;
  char *ptr;
  int what;
  
  ylc_ptr->callernum[0] = '\0';
  
  if (line && line[0]) {
    osip_from_init(&url);
    err = osip_from_parse(url, line);
    what = (err < 0) ? 2 : 0;
    
    while ((what < 3) && !ylc_ptr->callernum[0]) {
      if (what == 2)
        line1 = strdup(line);
      
      num = (what == 0) ? url->url->username : 
            (what == 1) ? url->displayname : line1;
      what++;
      
      if (num && num[0]) {
        /*printf("trying %s\n", num);*/
        
        /* remove surrounding quotes */
        if (num[0] == '"' && num[strlen(num) - 1] == '"') {
          num[strlen(num) - 1] = '\0';
          num++;
        }
        
        /* first check for the country code */
        int intl = 0;
        if (num[0] == '+') {
          /* assume "+<country-code><area-code><local-number>" */
          intl = 1;
          num++;
        }
        else
        if (!strncmp(num, ylc_ptr->intl_access_code, strlen(ylc_ptr->intl_access_code))) {
          /* assume "<intl-access-code><country-code><area-code><local-number>" */
          intl = 1;
          num += strlen(ylc_ptr->intl_access_code);
        }
        else
        if (!strncmp(num, ylc_ptr->country_code, strlen(ylc_ptr->country_code))) {
          /* assume "<country-code><area-code><local-number>" */
          intl = 1;
        }

        /* check if 'num' consists of numbers and * # only */
        ptr = num;
        while (ptr && *ptr) {
          if (isalnum(*ptr) || ispunct(*ptr))
            ptr++;
          else
            ptr = NULL;
        }
        if (!ptr || !*num) {
          /* we found other characters -> skip this string */
          continue;
        }

        if (intl) {
          if (!strncmp(num, ylc_ptr->country_code, strlen(ylc_ptr->country_code))) {
            /* call from our own country */
            /* create "<natl-access-code><area-code><local-number>" */
            int left = MAX_NUMBER_LEN-1;
            num += strlen(ylc_ptr->country_code);
            strncat(ylc_ptr->callernum, ylc_ptr->natl_access_code, left);
            left -= strlen(ylc_ptr->natl_access_code);
            strncat(ylc_ptr->callernum, num, left);
          }
          else {
            /* call from a foreign country */
            /* create "<intl-access-code><country-code><area-code><local-number>" */
            int left = MAX_NUMBER_LEN-1;
            strncat(ylc_ptr->callernum, ylc_ptr->intl_access_code, left);
            left -= strlen(ylc_ptr->intl_access_code);
            strncat(ylc_ptr->callernum, num, left);
          }
        }
        else {
          strncat(ylc_ptr->callernum, num, MAX_NUMBER_LEN-1);
        }
      }
    }
    osip_from_free(url);
    if (line1)
      free(line1);
  }
  
  /*printf("callernum=%s\n", ylc_ptr->callernum);*/
}

/**********************************/

/* callerid to ringtone name */
static char *callerid2ringtone(const char *dialnum)
{
  char *calleridkey;
  char *keyprefix = "ringtone_";
  char *ringtone;
  
  calleridkey = malloc(strlen(keyprefix) + strlen(dialnum) + 1);
  strcpy(calleridkey, keyprefix);
  strcat(calleridkey, dialnum);
  ringtone = ypconfig_get_value(calleridkey);
  free(calleridkey);
  return ringtone;
}

static void load_custom_ringtone(const char *callernum) 
{
  char *ringtone, *ringtone_distinctive = NULL;
  
  if (callernum && strlen(callernum))
  {
    ringtone_distinctive = callerid2ringtone(callernum);
  }

  if (ringtone_distinctive)
    ringtone = ringtone_distinctive;
  else
    ringtone = callerid2ringtone("default");

  if (ringtone) {
    /* upload custom ringtone based on callerid */
    printf("setting ring tone to %s\n", ringtone);
    set_yldisp_ringtone(ringtone, 250);
  }
}

/***********************************/

/* callerid to minimum ring duration in [ms] */
static int callerid2minring(const char *dialnum)
{
  char *calleridkey;
  char *keyprefix = "minring_";
  char *minring_str;
  int minring = 0;
  
  calleridkey = malloc(strlen(keyprefix) + strlen(dialnum) + 1);
  strcpy(calleridkey, keyprefix);
  strcat(calleridkey, dialnum);
  minring_str = ypconfig_get_value(calleridkey);
  free(calleridkey);
  if (minring_str) {
    minring = atoi(minring_str);
    if (minring >= 0)
      minring *= 1000;
    else
      minring = 0;
  }
  else
    minring = -1;

  return minring;
}

static int get_custom_minring(const char *callernum)
{
  int minring;

  if (callernum && strlen(callernum))
    minring = callerid2minring(callernum);
  if (minring < 0)
    minring = callerid2minring("default");
  if (minring < 0)
    minring = 0;

  return minring;
}

/***********************************/

void handle_key(ylcontrol_data_t *ylc_ptr, int code, int value) {
  char c;
  gstate_t lpstate_power;
  gstate_t lpstate_call;
  gstate_t lpstate_reg;
  
  lpstate_power = gstate_get_state(GSTATE_GROUP_POWER);
  lpstate_call = gstate_get_state(GSTATE_GROUP_CALL);
  lpstate_reg = gstate_get_state(GSTATE_GROUP_REG);
  
  if (code == 42) {     /* left shift */
    ylc_ptr->kshift = value;
    ylc_ptr->pressed = -1;
  }
  else {
    ylc_ptr->pressed = (value) ? code : -1;
    if (value) {
      /*printf("key=%d\n", code);*/
      switch (code) {
        case 2:       /* '1'..'9' */
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:      /* '0' */
        case 55:      /* '*' */
        case 103:     /* UP */
          if (lpstate_power != GSTATE_POWER_ON)
            break;
          /* get the real character */
          c = (code == 55) ? '*' :
              (code == 4 && ylc_ptr->kshift) ? '#' :
              (code == 11) ? '0' : ('0' + code - 1);

          if (lpstate_call == GSTATE_CALL_IDLE &&
              lpstate_reg  == GSTATE_REG_OK) {
            int len = strlen(ylc_ptr->dialnum);
            
            if (code == 103) {
              /* store/recall (cursor up) */
              if ((len > 0) || ylc_ptr->dialback[0]) {
                /* prepare to store the currently displayed number */
                ylc_ptr->prep_store = 1;
                set_yldisp_store_type(YL_STORE_ON);
              }
              else {
                /* prepare to recall a number */
                ylc_ptr->prep_recall = 1;
                set_yldisp_text("  select    ");
              }
            }
            else
            if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
              if (ylc_ptr->prep_store) {
                /* store number */
                char *key;
                key = strdup("mem ");
                key[3] = c;
                ypconfig_set_pair(key, (len) ? ylc_ptr->dialnum : ylc_ptr->dialback);
                free(key);
                ypconfig_write(NULL);
                ylc_ptr->prep_store = 0;
                set_yldisp_store_type(YL_STORE_NONE);
              }
              else
              if (ylc_ptr->prep_recall) {
                /* recall number but do not dial yet */
                char *key;
                char *val;
                key = strdup("mem ");
                key[3] = c;
                val = ypconfig_get_value(key);
                if (val && *val) {
                  strncpy(ylc_ptr->dialback, val, MAX_NUMBER_LEN);
                }
                free(key);
                ylc_ptr->prep_recall = 0;
                display_dialnum(ylc_ptr->dialback);
              }
              else {
                /* we want to dial for an outgoing call */
                if (len + 1 < sizeof(ylc_ptr->dialnum)) {
                  ylc_ptr->dialnum[len + 1] = '\0';
                  ylc_ptr->dialnum[len] = c;
                  display_dialnum(ylc_ptr->dialnum);
                }
                ylc_ptr->dialback[0] = '\0';
              }
            }
            else {
              /* do not handle '*' for now ... */
            }
          }
          else
          if (lpstate_call == GSTATE_CALL_OUT_CONNECTED ||
              lpstate_call == GSTATE_CALL_IN_CONNECTED) {
            char buf[2];
            buf[0] = c;
            buf[1] = '\0';
            lpstates_submit_command(LPCOMMAND_DTMF, buf);
          }
          else
          if (lpstate_call == GSTATE_CALL_IN_INVITE &&
              c == '#') {
            set_yldisp_ringer(YL_RINGER_OFF, 0);
          }
          break;

        case 14:         /* KEY_BACKSPACE (C) */
          if (lpstate_power != GSTATE_POWER_ON)
            break;
          if (lpstate_call == GSTATE_CALL_IDLE &&
              lpstate_reg  == GSTATE_REG_OK) {
            int len = strlen(ylc_ptr->dialnum);
            if (ylc_ptr->prep_store) {
              ylc_ptr->prep_store = 0;
              set_yldisp_store_type(YL_STORE_NONE);
            }
            else {
              if (len > 0) {
                ylc_ptr->dialnum[len - 1] = '\0';
              }
              display_dialnum((ylc_ptr->dialnum[0]) ?
                              ylc_ptr->dialnum : ylc_ptr->default_display);
              ylc_ptr->dialback[0] = '\0';
              ylc_ptr->prep_recall = 0;
            }
          }
          break;

        case 28:         /* KEY_ENTER (pick up) */
          if (lpstate_power != GSTATE_POWER_ON)
            break;
          if (lpstate_call == GSTATE_CALL_IDLE &&
              lpstate_reg  == GSTATE_REG_OK) {
            if (strlen(ylc_ptr->dialnum) == 0 &&
                strlen(ylc_ptr->dialback) > 0) {
              /* dial the current number displayed */
              strcpy(ylc_ptr->dialnum, ylc_ptr->dialback);
            }
            if (strlen(ylc_ptr->dialnum) > 0) {
              strcpy(ylc_ptr->dialback, ylc_ptr->dialnum);
              lpstates_submit_command(LPCOMMAND_CALL, ylc_ptr->dialnum);
              
              /* TODO: add number to history */
              
              ylc_ptr->dialnum[0] = '\0';
            }
            else {
              /* TODO: display history */
            }
          }
          else
          if (lpstate_call == GSTATE_CALL_IN_INVITE) {
            lpstates_submit_command(LPCOMMAND_PICKUP, NULL);
          }
          break;

        case 1:          /* hang up */
          if (lpstate_power != GSTATE_POWER_ON)
            break;
          set_yldisp_ringer(YL_RINGER_OFF, 0);
          if (lpstate_call == GSTATE_CALL_OUT_INVITE ||
              lpstate_call == GSTATE_CALL_OUT_CONNECTED ||
              lpstate_call == GSTATE_CALL_IN_INVITE ||
              lpstate_call == GSTATE_CALL_IN_CONNECTED) {
            lpstates_submit_command(LPCOMMAND_HANGUP, NULL);
          }
          else
          if (lpstate_call == GSTATE_CALL_IDLE &&
              lpstate_reg  == GSTATE_REG_OK) {
            ylc_ptr->dialnum[0] = '\0';
            ylc_ptr->dialback[0] = '\0';
            ylc_ptr->prep_store = 0;
            ylc_ptr->prep_recall = 0;
            set_yldisp_store_type(YL_STORE_NONE);
            display_dialnum(ylc_ptr->default_display);
          }
          break;
        
        case 105:        /* KEY_LEFT (VOL-) */
        case 114:        /* KEY_VOLUMEDOWN */
          if (lpstate_call == GSTATE_CALL_OUT_CONNECTED ||
              lpstate_call == GSTATE_CALL_IN_CONNECTED) {
            lpstates_submit_command(LPCOMMAND_SPKR_VOLDN, NULL);
          }
          else
          if (lpstate_call == GSTATE_CALL_IN_INVITE /*||
              lpstate_call == GSTATE_CALL_IDLE*/) {
            lpstates_submit_command(LPCOMMAND_RING_VOLDN, NULL);
          }
          break;
        
        case 106:        /* KEY_RIGHT (VOL+) */
        case 115:        /* KEY_VOLUMEUP */
          if (lpstate_call == GSTATE_CALL_OUT_CONNECTED ||
              lpstate_call == GSTATE_CALL_IN_CONNECTED) {
            lpstates_submit_command(LPCOMMAND_SPKR_VOLUP, NULL);
          }
          else
          if (lpstate_call == GSTATE_CALL_IN_INVITE /*||
              lpstate_call == GSTATE_CALL_IDLE*/) {
            lpstates_submit_command(LPCOMMAND_RING_VOLUP, NULL);
          }
          break;
        
        case 108:        /* DOWN */
          break;
        
        default:
          break;
      }
    }
  }
}

/**********************************/

void handle_long_key(ylcontrol_data_t *ylc_ptr, int code) {
  gstate_t lpstate_power;
  gstate_t lpstate_call;
  
  lpstate_power = gstate_get_state(GSTATE_GROUP_POWER);
  lpstate_call = gstate_get_state(GSTATE_GROUP_CALL);
  
  switch (code) {
    case 14:         /* C */
      if (lpstate_power != GSTATE_POWER_ON)
        break;
      if (lpstate_call == GSTATE_CALL_IDLE) {
        ylcontrol_data.dialnum[0] = '\0';
        display_dialnum(ylcontrol_data.default_display);
      }
      break;
    
    case 1:          /* hang up */
      if (lpstate_power == GSTATE_POWER_OFF) {
        lpstates_submit_command(LPCOMMAND_STARTUP, NULL);
      }
      else
      if (lpstate_power != GSTATE_POWER_OFF &&
          lpstate_power != GSTATE_POWER_SHUTDOWN) {
        lpstates_submit_command(LPCOMMAND_SHUTDOWN, NULL);
      }
      break;
    
    default:
      break;
  }
}

/**********************************/

void lps_callback(struct _LinphoneCore *lc,
                  LinphoneGeneralState *gstate) {
  gstate_t lpstate_power;
  gstate_t lpstate_call;
  gstate_t lpstate_reg;
  ylsysfs_model model;
  
  /* make sure this is the same thread as our main loop! */
  assert(yp_ml_same_thread());
  
  lpstate_power = gstate_get_state(GSTATE_GROUP_POWER);
  lpstate_call = gstate_get_state(GSTATE_GROUP_CALL);
  lpstate_reg = gstate_get_state(GSTATE_GROUP_REG);
  
  model = ylsysfs_get_model();
  
  switch (gstate->new_state) {
    case GSTATE_POWER_OFF:
      yldisp_hide_all();
      if (ylcontrol_data.hard_shutdown)
        yp_ml_stop();
      else
        set_yldisp_text("   - off -  ");
      break;
      
    case GSTATE_POWER_STARTUP:
      yldisp_show_date();
      set_yldisp_text("- startup - ");
      yldisp_led_blink(150, 150);
      break;
      
    case GSTATE_POWER_ON:
      display_dialnum(ylcontrol_data.default_display);
      break;
      
    case GSTATE_REG_FAILED:
      if (lpstate_power != GSTATE_POWER_ON)
        break;
      if (ylcontrol_data.linphone_2_1_1_bug &&
          !strcmp(gstate->message, "Authentication required")) {
        /* force GSTATE_REG_OK */
        gstate_new_state(lc, GSTATE_REG_OK, NULL);
        break;
      }
      if (lpstate_call == GSTATE_CALL_IDLE) {
        if (ylcontrol_data.dialnum[0] == '\0') {
          set_yldisp_text("-reg failed-");
        }
        yldisp_led_blink(150, 150);
      }
      break;
      
    case GSTATE_POWER_SHUTDOWN:
      yldisp_hide_all();
      yldisp_led_blink(150, 150);
      set_yldisp_text("- shutdown -");
      break;
      
    case GSTATE_REG_OK:
      if (lpstate_power != GSTATE_POWER_ON)
        break;
      if (lpstate_call == GSTATE_CALL_IDLE) {
        if (ylcontrol_data.dialnum[0] == '\0') {
          display_dialnum((ylcontrol_data.dialback[0]) ?
                     ylcontrol_data.dialback : ylcontrol_data.default_display);
        }
        yldisp_led_on();
      }
      break;
      
    case GSTATE_CALL_IDLE:
      if (lpstate_power != GSTATE_POWER_ON)
        break;
      if (lpstate_reg == GSTATE_REG_FAILED) {
        set_yldisp_text("-reg failed-");
        ylcontrol_data.dialnum[0] = '\0';
        yldisp_led_blink(150, 150);
      }
      else if (lpstate_reg == GSTATE_REG_OK) {
        yldisp_led_on();
      }
      break;
      
    case GSTATE_CALL_IN_INVITE:
      /* In linphone <= 2.1.1 there is a bug which causes the message
       * field to be NULL, the actual caller ID has to be captured in
       * call_received_callback().
       */
      if (gstate->message == NULL) {
        if (!ylcontrol_data.linphone_2_1_1_bug) {
          ylcontrol_data.linphone_2_1_1_bug = 1;
          printf("Warning: A liblinphone bug was detected, please consider\n"
                 "         patching or upgrading linphone to > 2.1.1!\n");
        }
        break;
      }
      extract_callernum(&ylcontrol_data, gstate->message);
      load_custom_ringtone(ylcontrol_data.callernum);
      if (strlen(ylcontrol_data.callernum)) {
        display_dialnum(ylcontrol_data.callernum);
        strcpy(ylcontrol_data.dialback, ylcontrol_data.callernum);
      }
      else {
        display_dialnum(" - - -");
        ylcontrol_data.dialback[0] = '\0';
      }
      ylcontrol_data.dialnum[0] = '\0';
      
      set_yldisp_call_type(YL_CALL_IN);
      yldisp_led_blink(300, 300);

      if (model == YL_MODEL_P1K || model == YL_MODEL_P4K) {
        /* ringing seems to block displaying line 3,
         * so we have to wait for about 170ms.
         * This seems to be a limitation of the hardware */
        usleep(170000);
      }
      set_yldisp_ringer(YL_RINGER_ON, get_custom_minring(ylcontrol_data.callernum));
      break;
      
    case GSTATE_CALL_IN_CONNECTED:
      set_yldisp_ringer(YL_RINGER_OFF, 0);
      /* start timer */
      yldisp_start_counter();
      yldisp_led_blink(1000, 100);
      break;
      
    case GSTATE_CALL_OUT_INVITE:
      set_yldisp_call_type(YL_CALL_OUT);
      yldisp_led_blink(300, 300);
      yldisp_show_counter();
      break;
      
    case GSTATE_CALL_OUT_CONNECTED:
      /* Unfortunately this state is sent already if early media is
       * available. If the remote party picks up it is sent again, so
       * the duration of the call is reset and displayed correctly. */
      yldisp_start_counter();
      yldisp_led_blink(1000, 100);
      break;
      
    case GSTATE_CALL_END:
      set_yldisp_ringer(YL_RINGER_OFF_DELAYED, 0);
      set_yldisp_call_type(YL_CALL_NONE);
      display_dialnum((ylcontrol_data.dialback[0]) ?
                      ylcontrol_data.dialback : ylcontrol_data.default_display);
      yldisp_show_date();
      yldisp_led_on();
      break;
      
    case GSTATE_CALL_ERROR:
      set_yldisp_ringer(YL_RINGER_OFF, 0);
      ylcontrol_data.dialback[0] = '\0';
      set_yldisp_call_type(YL_CALL_NONE);
      set_yldisp_text(" - error -  ");
      yldisp_show_date();
      yldisp_led_on();
      break;
      
    default:
      break;
  }
}

/**********************************/

void call_received_callback(struct _LinphoneCore *lc, const char *from)
{
  if (ylcontrol_data.linphone_2_1_1_bug) {
    /* fake a second "general-state" callback */
    LinphoneGeneralState gstate;
    gstate.old_state = GSTATE_CALL_IDLE;
    gstate.new_state = GSTATE_CALL_IN_INVITE;
    gstate.group = GSTATE_GROUP_CALL;
    gstate.message = from;
    lps_callback(lc, &gstate);
  }
}

/**********************************/

void ylcontrol_keylong_callback(int id, int group, void *private_data) {
  ylcontrol_data_t *ylc_ptr = private_data;
  
  handle_long_key(ylc_ptr, ylcontrol_data.pressed);
  ylc_ptr->pressed = -1;
}

/**********************************/

void ylcontrol_io_callback(int id, int group, void *private_data) {
  ylcontrol_data_t *ylc_ptr = private_data;
  int bytes;
  struct input_event event;

  bytes = read(ylc_ptr->evfd, &event, sizeof(struct input_event));

  if (bytes != (int) sizeof(struct input_event)) {
    if (bytes < 0)
      perror("Error reading from event device");
    else
      fprintf(stderr, "%s: Expected %d bytes, got %d bytes\n", __FUNCTION__,
              sizeof(struct input_event), bytes);
    close(ylc_ptr->evfd);
    /* remove myself and shut down */
    yp_ml_remove_event(-1, YLCONTROL_IO_ID);
    stop_ylcontrol();
    event.type = 0;
  }

  if (event.type == 1) {        /* key */
    yp_ml_remove_event(-1, YLCONTROL_KEYLONG_ID);
    handle_key(ylc_ptr, event.code, event.value);
    
    if (ylcontrol_data.pressed >= 0) {
      /* wait for key being pressed long (1 second) */
      yp_ml_schedule_timer(YLCONTROL_KEYLONG_ID, 1000,
                           ylcontrol_keylong_callback, private_data);
    }
  }
}

/*****************************************************************/

void init_ylcontrol(char *countrycode) {
  int modified = 0;
  
  set_lpstates_callback(lps_callback);
  set_call_received_callback(call_received_callback);

  ylcontrol_data.intl_access_code = ypconfig_get_value("intl-access-code");
  if (!ylcontrol_data.intl_access_code) {
    ylcontrol_data.intl_access_code = "00";
    ypconfig_set_pair("intl-access-code", ylcontrol_data.intl_access_code);
    modified = 1;
  }
  ylcontrol_data.natl_access_code = ypconfig_get_value("natl-access-code");
  if (!ylcontrol_data.natl_access_code) {
    ylcontrol_data.natl_access_code = "0";
    ypconfig_set_pair("natl-access-code", ylcontrol_data.natl_access_code);
    modified = 1;
  }
  ylcontrol_data.country_code = ypconfig_get_value("country-code");
  if (!ylcontrol_data.country_code) {
    ylcontrol_data.country_code = "";
    ypconfig_set_pair("country-code", ylcontrol_data.country_code);
    modified = 1;
  }
  ylcontrol_data.default_display = ypconfig_get_value("display-id");

  if (modified) {
    /* write back modified configuration */
    ypconfig_write(NULL);
  }
}

/*************************************/

void start_ylcontrol() {
  const char *path_event;
  
  ylcontrol_data.hard_shutdown = 0;
  ylcontrol_data.linphone_2_1_1_bug = 0;
  
  path_event = ylsysfs_get_event_path();
  
  ylcontrol_data.evfd = open(path_event, O_RDONLY);
  if (ylcontrol_data.evfd < 0) {
    perror(path_event);
    abort();
  }
  
  /* grab the event device to prevent it from propagating
     its events to the regular keyboard driver            */
  if (ioctl(ylcontrol_data.evfd, EVIOCGRAB, (void *)1)) {
    perror("EVIOCGRAB");
    abort();
  }
  
  yp_ml_poll_io(YLCONTROL_IO_ID, ylcontrol_data.evfd,
                ylcontrol_io_callback, &ylcontrol_data);
}

/*************************************/

void stop_ylcontrol() {
  ylcontrol_data.hard_shutdown = 1;
  if (gstate_get_state(GSTATE_GROUP_POWER) == GSTATE_POWER_OFF) {
    /* already powered off */
    yldisp_hide_all();
    yp_ml_stop();
  }
  else {
    /* need to shut down first */
    lpstates_submit_command(LPCOMMAND_SHUTDOWN, NULL);
  }
}

