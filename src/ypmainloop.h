/****************************************************************************
 *
 *  File: ypmainloop.h
 *
 *  Copyright (C) 2008  Thomas Reitmayr <treitmayr@devbase.at>
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

#ifndef YPMAINLOOP_H
#define YPMAINLOOP_H

typedef void (*yp_ml_callback)(int id, int group, void *private_data);

int yp_ml_init();
int yp_ml_run();
int yp_ml_shutdown();

int yp_ml_schedule_timer(int group_id, int delay,
                         yp_ml_callback cb, void *private_data);

int yp_ml_schedule_periodic_timer(int group_id, int interval,
                                  yp_ml_callback cb, void *private_data);

int yp_ml_poll_io(int group_id, int fd,
                  yp_ml_callback cb, void *private_data);

int yp_ml_remove_event(int event_id, int group_id);

int yp_ml_count_events(int event_id, int group_id);

int yp_ml_same_thread(void);

#endif
