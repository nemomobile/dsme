/**
   @file vibrafeedback.c

   Play vibra using ngfd

   <p>
   Copyright (C) 2014 Jolla Oy.

   @author Pekka Lundstrom <pekka.lundstrom@jolla.com>

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
#ifndef VIBRAFEEDBACK_H
#define VIBRAFEEDBACK_H

extern void dsme_ini_vibrafeedback(void);
extern void dsme_fini_vibrafeedback(void);
extern void dsme_play_vibra(const char *event_name);

#endif
