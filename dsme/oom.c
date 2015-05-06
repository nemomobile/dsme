/**
   @file oom.c

   This file implements functions to protect/unprotect from the OOM killer
   by setting the appropriate value to oom_adj.
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

#include "../include/dsme/oom.h"

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "../include/dsme/logging.h"

/* Kernel 2.6.36 and newer are using /proc/<pid>/oom_score_adj
 * and older kernels /proc/<pid>/oom_adj
 * We support both and detect it on the fly
 */

#define OOM_ADJ_PATH_OLD        "/proc/self/oom_adj"
#define OOM_ADJ_PATH_NEW        "/proc/self/oom_score_adj"
#define OOM_PROTECT_VALUE_OLD   (-17)
#define OOM_PROTECT_VALUE_NEW   (-1000)
#define OOM_UNPROTECT_VALUE     0
typedef enum {oom_protected, oom_unprotected} oom_mode;
  
static bool set_oom_protection_mode(oom_mode mode)
{
  FILE* file = 0;
  const char* new_path = OOM_ADJ_PATH_NEW;
  const char* old_path = OOM_ADJ_PATH_OLD;
  char* oom_path;
  int  oom_value;
  struct stat st;

  if (stat(OOM_ADJ_PATH_NEW, &st) == 0) {
    oom_path = (char*)new_path;
  } else {
    oom_path = (char*)old_path; 
  }

  if (mode == oom_protected) {
      if (oom_path == new_path) {
          oom_value = OOM_PROTECT_VALUE_NEW;
      } else {
          oom_value = OOM_PROTECT_VALUE_OLD;
      }
  } else {
      oom_value = OOM_UNPROTECT_VALUE;
  }

  file = fopen(oom_path, "w");
  if (!file) {
      dsme_log(LOG_ERR, "set_oom_protection_mode() can't open %s", oom_path);
      return false;
  }

  if (fprintf(file, "%i", oom_value) < 0) {
      (void)fclose(file);
      dsme_log(LOG_CRIT, "set_oom_protection_mode(%s,%i) failed", oom_path, oom_value);
      return false;
  }

  if (fclose(file) < 0) {
      return false;
  }
  return true;
}

bool protect_from_oom(void)
{
  return set_oom_protection_mode(oom_protected);
}

bool unprotect_from_oom(void)
{
  return set_oom_protection_mode(oom_unprotected);
}

bool adjust_oom(int oom_adj)
{
  /* This function is not used anywhere, but we keep it anyhow 
   * Actual value can not be set anymore but only protected/unprotected
   */
  if (oom_adj < 0) {
      return set_oom_protection_mode(oom_protected);
  } else {
      return set_oom_protection_mode(oom_unprotected);
  }
}
