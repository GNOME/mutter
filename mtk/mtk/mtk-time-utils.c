/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2025 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>

#include "mtk-time-utils.h"

int64_t
mtk_extrapolate_next_interval_boundary (int64_t base_us,
                                        int64_t interval_us)
{
  int64_t now_us;
  int64_t num_intervals;

  now_us = g_get_monotonic_time ();
  num_intervals = MAX ((now_us - base_us + interval_us - 1) / interval_us, 0);
  return base_us + num_intervals * interval_us;
}
