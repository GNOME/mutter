/*
 * Copyright (C) 2019 Red Hat Inc.
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
 *
 */

#pragma once

#include "core/meta-debug-control.h"
#include "core/meta-private-enums.h"
#include "core/meta-service-channel.h"
#include "core/util-private.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"
#include "wayland/meta-wayland-types.h"

#ifdef HAVE_PROFILER
#include "core/meta-profiler.h"
#endif

struct _MetaContextClass
{
  GObjectClass parent_class;

  gboolean (* configure) (MetaContext   *context,
                          int           *argc,
                          char        ***argv,
                          GError       **error);

  MetaCompositorType (* get_compositor_type) (MetaContext *context);

  MetaX11DisplayPolicy (* get_x11_display_policy) (MetaContext *context);

  gboolean (* is_replacing) (MetaContext *context);

  gboolean (* setup) (MetaContext  *context,
                      GError      **error);

  MetaBackend * (* create_backend) (MetaContext  *context,
                                    GError      **error);

  void (* notify_ready) (MetaContext *context);

#ifdef HAVE_X11
  gboolean (* is_x11_sync) (MetaContext *context);
#endif
};

const char * meta_context_get_name (MetaContext *context);

const char * meta_context_get_gnome_wm_keybindings (MetaContext *context);

void meta_context_set_unsafe_mode (MetaContext *context,
                                   gboolean     enable);

#ifdef HAVE_WAYLAND
META_EXPORT_TEST
MetaServiceChannel * meta_context_get_service_channel (MetaContext *context);
#endif

MetaX11DisplayPolicy meta_context_get_x11_display_policy (MetaContext *context);

#ifdef HAVE_X11
META_EXPORT_TEST
gboolean meta_context_is_x11_sync (MetaContext *context);
#endif

#ifdef HAVE_PROFILER
MetaProfiler *
meta_context_get_profiler (MetaContext *context);

void meta_context_set_trace_file (MetaContext *context,
                                  const char  *trace_file);
#endif

MetaDebugControl * meta_context_get_debug_control (MetaContext *context);
