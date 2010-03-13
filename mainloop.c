/**
   @file mainloop.c

   Implements DSME mainloop functionality.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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
#include "dsme/mainloop.h"

#include <glib.h>
#include <stdlib.h>

static GMainLoop* the_loop = 0;
static gboolean   running  = FALSE;

struct _GMainLoop* dsme_main_loop(void)
{
  if (!the_loop) {
    the_loop = g_main_loop_new(0, FALSE);
    if (!the_loop) {
      /* TODO: crash and burn */
      exit(EXIT_FAILURE);
    }
  }

  return the_loop;
}


void dsme_main_loop_run(void (*iteration)(void))
{
  GMainContext* ctx = g_main_loop_get_context(dsme_main_loop());

  running = TRUE;
  while (running) {
    if (iteration) {
      iteration();
    }
    if (running) {
      (void)g_main_context_iteration(ctx, TRUE);
    }
  }

  g_main_loop_unref(the_loop);
  the_loop = 0;
}

void dsme_main_loop_quit(void)
{
  g_main_loop_quit(dsme_main_loop());
  running = FALSE;
}
