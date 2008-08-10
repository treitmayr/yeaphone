/****************************************************************************
 *
 *  File: yldisp.h
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

#ifndef YLDISP_H
#define YLDISP_H

typedef enum { YL_CALL_NONE, YL_CALL_IN, YL_CALL_OUT } yl_call_type_t;
typedef enum { YL_STORE_NONE, YL_STORE_ON } yl_store_type_t;

typedef enum { YL_RINGER_OFF,
               YL_RINGER_OFF_DELAYED,
               YL_RINGER_ON } yl_ringer_state_t;


void yldisp_clear();

void yldisp_led_blink(unsigned int on_time, unsigned int off_time);
void yldisp_led_off();
void yldisp_led_on();

void yldisp_show_date();
void yldisp_show_counter();
void yldisp_start_counter();
void yldisp_stop_counter();

void set_yldisp_call_type(yl_call_type_t ct);
yl_call_type_t get_yldisp_call_type();

void set_yldisp_store_type(yl_store_type_t st);
yl_store_type_t get_yldisp_store_type();

void set_yldisp_ringtone(char *ringname, unsigned char volume);

void set_yldisp_ringer(yl_ringer_state_t rs, int minring);
yl_ringer_state_t get_yldisp_ringer();

void yldisp_ringer_vol_up();
void yldisp_ringer_vol_down();

void set_yldisp_text(char *text);
char *get_yldisp_text();

void set_yldisp_pstn_mode(int pstn);
void set_yldisp_dial_tone(int pstn);

void yldisp_hide_all();

#endif
