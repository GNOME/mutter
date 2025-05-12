/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2015-2025 Red Hat
 *
 * The implementation is heavily inspired by cairo_region_t.
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

#pragma once

#include <stdint.h>

#include "mtk/mtk-macros.h"

MTK_EXPORT
int64_t mtk_extrapolate_next_interval_boundary (int64_t base_us,
                                                int64_t interval_us);

static inline uint64_t
ns (uint64_t ns)
{
  return ns;
}

static inline int64_t
us (int64_t us)
{
  return us;
}

static inline int64_t
ms (int64_t ms)
{
  return ms;
}

static inline int64_t
ms2us (int64_t ms)
{
  return us (ms * 1000);
}

static inline int64_t
us2ns (int64_t us)
{
  return ns (us * 1000);
}

static inline int64_t
us2ms (int64_t us)
{
  return (int64_t) (us / 1000);
}

static inline int64_t
ns2us (int64_t ns)
{
  return us (ns / 1000);
}

static inline int64_t
s2us (int64_t s)
{
  return s * G_USEC_PER_SEC;
}

static inline int64_t
us2s (int64_t us)
{
  return us / G_USEC_PER_SEC;
}

static inline int64_t
s2ns (int64_t s)
{
  return us2ns (s2us (s));
}

static inline int64_t
s2ms (int64_t s)
{
  return (int64_t) ms (s * 1000);
}
