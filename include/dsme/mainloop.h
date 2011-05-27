/**
   @file mainloop.h

   DSME mainloop abstraction
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

#ifndef DSME_MAINLOOP_H
#define DSME_MAINLOOP_H

void dsme_main_loop_run(void (*iteration)(void));

void dsme_main_loop_quit(int exit_code);

int dsme_main_loop_exit_code(void);

#endif
