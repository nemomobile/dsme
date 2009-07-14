/**
   @file dsme_dbus_if.h

   D-Bus names for DSME
   <p>
   Copyright (C) 2009 Nokia Corporation.

   @author Semi Malinen <semi.malinen@nokia.com>

   This file is part of Dsme.

   Dsme is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Dsme is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Dsme.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DSME_DBUS_IF_H
#define DSME_DBUS_IF_H

extern const char dsme_service[];
extern const char dsme_req_interface[];
extern const char dsme_sig_interface[];
extern const char dsme_req_path[];
extern const char dsme_sig_path[];

extern const char dsme_get_version[];
extern const char dsme_req_powerup[];
extern const char dsme_req_reboot[];
extern const char dsme_req_shutdown[];

extern const char dsme_state_req_denied_ind[];
extern const char dsme_shutdown_ind[];
extern const char dsme_thermal_shutdown_ind[];
extern const char dsme_save_unsaved_data_ind[];

#endif
