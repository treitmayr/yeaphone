/****************************************************************************
 *
 *  File: ylsysfs.h
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

#ifndef YLSYSFS_H
#define YLSYSFS_H

typedef enum ylsysfs_model ylsysfs_model;
enum ylsysfs_model {
        YL_MODEL_UNKNOWN,
        YL_MODEL_P1K,
        YL_MODEL_P4K,
        YL_MODEL_B2K,
        YL_MODEL_B3G,
        YL_MODEL_P1KH };


int ylsysfs_find_device(const char *uniq);

const char *ylsysfs_get_sysfs_path();
const char *ylsysfs_get_event_path();

ylsysfs_model ylsysfs_get_model();
int ylsysfs_get_led_inverted();
int ylsysfs_get_alsa_card();

int ylsysfs_write_control_file_buf(const char *control,
                                   const char *buf,
                                   int size);

int ylsysfs_write_control_file(const char *control,
                               const char *line);

int ylsysfs_read_control_file_buf(const char *control,
                                  char *buf,
                                  int size);

int ylsysfs_read_control_file(const char *control,
                              char *line,
                              int size);

#endif
