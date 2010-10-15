/**
   @file dsme-rd-mode.h

   DSME internal interface for reading R&D mode flags.
   <p>
   Copyright (C) 2010 Nokia Corporation.

   @author Markus Lehtonen <markus.lehtonen@nokia.com>
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

#ifndef DSME_RD_MODE_H
#define DSME_RD_MODE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Function for querying if R&D mode is enabled
 */
bool dsme_rd_mode_enabled(void);

/**
 * Function for querying if R&D mode flags
 */
const char* dsme_rd_mode_get_flags(void);

#ifdef __cplusplus
}
#endif

#endif
