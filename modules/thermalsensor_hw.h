/**
   @file thermalsensor_hw.h

   DSME internal HW temperature i/f
   <p>
   Copyright (C) 2013 Jolla ltd

   @author  Pekka Lundstrom <pekka.lundstrom@jolla.com>

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
#ifndef THERMALSENSOR_HW_H
#define THERMALSENSOR_HW_H
#include <stdbool.h>

extern bool dsme_hw_get_core_temperature(thermal_object_t*         thermal_object,
                                         temperature_handler_fn_t* callback);
extern bool dsme_hw_supports_core_temp(void);
extern bool dsme_hw_get_battery_temperature(thermal_object_t*         thermal_object,
                                            temperature_handler_fn_t* callback);
extern bool dsme_hw_supports_battery_temp(void);

#endif
