/**
   @file dsme-rd-mode.c

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

#include "dsme-rd-mode.h"
#include <stdlib.h>

#define DSME_RD_FLAGS_ENV "DSME_RD_FLAGS"

bool dsme_rd_mode_enabled(void)
{
    return dsme_rd_mode_get_flags() != 0;
}

const char* dsme_rd_mode_get_flags(void) {
    return getenv(DSME_RD_FLAGS_ENV);
}
