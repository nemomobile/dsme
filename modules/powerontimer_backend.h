/**
   @file powerontimer_backend.h

   DSME poweron timer backend abstraction
   <p>
   Copyright (C) 2010 Nokia Corporation.

   @author Simo Piiroinen <simo.piiroinen@nokia.com>

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

#ifndef POWERONTIMER_BACKEND_H_
#define POWERONTIMER_BACKEND_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    pot_update_cal(bool user_mode, bool force_save);
int32_t pot_get_poweron_secs(void);

#ifdef __cplusplus
};
#endif

#endif /* POWERONTIMER_BACKEND_H_ */
