/*
 * Copyright (C) 2019 Endless, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "meta-dbus-sysprof3-profiler.h"

G_BEGIN_DECLS

#define META_TYPE_PROFILER (meta_profiler_get_type())

G_DECLARE_FINAL_TYPE (MetaProfiler,
                      meta_profiler,
                      META,
                      PROFILER,
                      MetaDBusSysprof3ProfilerSkeleton)

MetaProfiler * meta_profiler_new (const char *trace_file);

void meta_profiler_register_thread (MetaProfiler *profiler,
                                    GMainContext *main_context,
                                    const char   *name);

void meta_profiler_unregister_thread (MetaProfiler *profiler,
                                      GMainContext *main_context);

G_END_DECLS
