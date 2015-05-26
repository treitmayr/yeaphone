/****************************************************************************
 *
 *  File: lpcontrol.h
 *
 *  Copyright (C) 2006  Thomas Reitmayr <treitmayr@devbase.at>
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

#ifndef LPCONTROL_H
#define LPCONTROL_H


#include <linphone/linphonecore.h>

#define VERSIONCONV(a,b,c,...) (((a) << 16) + ((b) << 8) + (c))

#define LPCONTROL_TIMER_ID  1

typedef enum lpstates_command_e {
  LPCOMMAND_STARTUP,
  LPCOMMAND_SHUTDOWN,
  LPCOMMAND_CALL,
  LPCOMMAND_DTMF,
  LPCOMMAND_PICKUP,
  LPCOMMAND_HANGUP,
  LPCOMMAND_RING_VOLUP,
  LPCOMMAND_RING_VOLDN,
  LPCOMMAND_SPKR_VOLUP,
  LPCOMMAND_SPKR_VOLDN,
} lpstates_command_t;


void set_lpstates_callback(GeneralStateChange callback);
void set_call_received_callback(InviteReceivedCb callback);

void start_lpcontrol(int autoregister, void *userdata);

void lpstates_submit_command(lpstates_command_t command, char *arg);

#endif
