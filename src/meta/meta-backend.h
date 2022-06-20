/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include <glib-object.h>

#include "clutter/clutter.h"
#include "meta/meta-dnd.h"
#include "meta/meta-idle-monitor.h"
#include "meta/meta-monitor-manager.h"
#include "meta/meta-remote-access-controller.h"

typedef enum _MetaBackendCapabilities
{
  META_BACKEND_CAPABILITY_NONE = 0,
  META_BACKEND_CAPABILITY_BARRIERS = 1 << 0,
} MetaBackendCapabilities;

#define META_TYPE_BACKEND (meta_backend_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaBackend, meta_backend, META, BACKEND, GObject)

META_EXPORT
void meta_backend_set_keymap (MetaBackend *backend,
                              const char  *layouts,
                              const char  *variants,
                              const char  *options,
                              const char  *model);

META_EXPORT
void meta_backend_lock_layout_group (MetaBackend *backend,
                                     guint        idx);

META_EXPORT
MetaContext * meta_backend_get_context (MetaBackend *backend);

META_EXPORT
ClutterActor *meta_backend_get_stage (MetaBackend *backend);

META_EXPORT
MetaDnd      *meta_backend_get_dnd   (MetaBackend *backend);

META_EXPORT
MetaSettings *meta_backend_get_settings (MetaBackend *backend);

META_EXPORT
MetaIdleMonitor * meta_backend_get_core_idle_monitor (MetaBackend *backend);

META_EXPORT
MetaMonitorManager * meta_backend_get_monitor_manager (MetaBackend *backend);

META_EXPORT
MetaRemoteAccessController * meta_backend_get_remote_access_controller (MetaBackend *backend);

META_EXPORT
gboolean meta_backend_is_rendering_hardware_accelerated (MetaBackend *backend);

META_EXPORT
gboolean meta_backend_is_headless (MetaBackend *backend);

META_EXPORT
void meta_backend_freeze_keyboard (MetaBackend *backend,
                                   uint32_t     timestamp);

META_EXPORT
void meta_backend_ungrab_keyboard (MetaBackend *backend,
                                   uint32_t     timestamp);

META_EXPORT
void meta_backend_unfreeze_keyboard (MetaBackend *backend,
                                     uint32_t     timestamp);

META_EXPORT
MetaBackendCapabilities meta_backend_get_capabilities (MetaBackend *backend);
