/**
   @file diskmonitor_backend.h

   <p>
   Copyright (C) 2011 Nokia Corporation.

   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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

#ifndef DSME_DISKMONITOR_BACKEND_H
#define DSME_DISKMONITOR_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

void check_disk_space_usage(void);

#ifdef __cplusplus
};
#endif

#endif /* DSME_DISKMONITOR_BACKEND_H */
