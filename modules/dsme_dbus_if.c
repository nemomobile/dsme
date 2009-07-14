/**
   @file dsme_dbus_if.c

   D-Bus names for DSME
   <p>
   Copyright (C) 2008-2009 Nokia Corporation.

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

#include "dsme_dbus_if.h"

const char dsme_service[]               = "com.nokia.dsme";
const char dsme_req_interface[]         = "com.nokia.dsme.request";
const char dsme_sig_interface[]         = "com.nokia.dsme.signal";
const char dsme_req_path[]              = "/com/nokia/dsme/request";
const char dsme_sig_path[]              = "/com/nokia/dsme/signal";

const char dsme_get_version[]           = "get_version";
const char dsme_req_powerup[]           = "req_powerup";
const char dsme_req_reboot[]            = "req_reboot";
const char dsme_req_shutdown[]          = "req_shutdown";

const char dsme_state_req_denied_ind[]  = "denied_req_ind";
const char dsme_shutdown_ind[]          = "shutdown_ind";
const char dsme_thermal_shutdown_ind[]  = "thermal_shutdown_ind";
const char dsme_save_unsaved_data_ind[] = "save_unsaved_data_ind";
