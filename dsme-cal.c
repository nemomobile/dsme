/**
   @file dsme-cal.c

   DSME internal interface for using CAL.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
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

#include "dsme/dsme-cal.h"
#include "dsme/logging.h"

#include <cal.h>


static void dsme_cal_debug(int level, const char *str);
static void dsme_cal_error(const char *str);


int dsme_cal_init(void) {

	cal_debug_log = dsme_cal_debug;
	cal_error_log = dsme_cal_error;

	return 0;
}

static void dsme_cal_debug(int level, const char *str) {

	if (level >= 1)
		return;

	dsme_log(LOG_DEBUG, "CAL: %s", str);
}

static void dsme_cal_error(const char *str) {

	dsme_log(LOG_ERR, "CAL ERROR: %s", str);
}
