/**
   @file oom.c

   This file implements functions to protect/unprotect from the OOM killer
   by setting the appropriate value to oom_adj.
   <p>
   Copyright (C) 2006-2009 Nokia Corporation.

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

#include "dsme/oom.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#define OOM_ADJ_PATH            "/proc/self/oom_adj"
#define OOM_ADJ_PROTECT_VALUE   (-17)
#define OOM_ADJ_UNPROTECT_VALUE 0

static int set_oom_adj_value(int i)
{
  FILE* file = 0;

  file = fopen(OOM_ADJ_PATH, "w");
  if (!file) {
      perror(OOM_ADJ_PATH);
      return -1;
  }

  if (fprintf(file, "%i", i) < 0) {
      fprintf(stderr, "%s: Write failed\n", OOM_ADJ_PATH);
      fclose(file);
      return -1;
  }

  if (fclose(file) < 0) {
      perror(OOM_ADJ_PATH);
      return -1;
  }

  return 0;
}

int protect_from_oom(void)
{
  return set_oom_adj_value(OOM_ADJ_PROTECT_VALUE);
}

int unprotect_from_oom(void)
{
  return set_oom_adj_value(OOM_ADJ_UNPROTECT_VALUE);
}
