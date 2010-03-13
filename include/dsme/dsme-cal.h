/**
   @file dsme-cal.h

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

#ifndef DSMECAL_H
#define DSMECAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function initializes the dsme-cal.
 * Currently this means setting the debug finctions.
 */
int dsme_cal_init(void);

#ifdef __cplusplus
}
#endif

#endif
