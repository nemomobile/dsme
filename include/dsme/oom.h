/**
   @file oom.h

   Prototypes for public interfaces in oom.c
   <p>
   Copyright (C) 2006-2010 Nokia Corporation.

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

#ifndef DSME_OOM_H
#define DSME_OOM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool protect_from_oom(void);

bool unprotect_from_oom(void);

bool adjust_oom(int oom_adj);

#ifdef __cplusplus
}
#endif

#endif
