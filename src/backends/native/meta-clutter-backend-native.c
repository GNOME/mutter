/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:meta-clutter-backend-native
 * @title: MetaClutterBackendNatve
 * @short_description: A native backend which renders using EGL.
 *
 * MetaClutterBackendNative is the #ClutterBackend which is used by the native
 * (as opposed to the X) backend. It creates a stage with #MetaStageNative and
 * renders using the #CoglRenderer.
 *
 * Note that MetaClutterBackendNative is something different than a
 * #MetaBackendNative. The former is a #ClutterBackend implementation, while
 * the latter is a #MetaBackend implementation.
 */

#include "config.h"

#include "backends/native/meta-clutter-backend-native.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-renderer.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-seat-native.h"
#include "backends/native/meta-stage-native.h"
#include "clutter/clutter.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

struct _MetaClutterBackendNative
{
  ClutterBackend parent;
};

G_DEFINE_TYPE (MetaClutterBackendNative, meta_clutter_backend_native,
               CLUTTER_TYPE_BACKEND)

static CoglRenderer *
meta_clutter_backend_native_get_renderer (ClutterBackend  *clutter_backend,
                                          GError         **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_native_create_stage (ClutterBackend  *clutter_backend,
                                          ClutterStage    *wrapper,
                                          GError         **error)
{
  return g_object_new (META_TYPE_STAGE_NATIVE,
                       "backend", clutter_backend,
                       "wrapper", wrapper,
                       NULL);
}

static ClutterSeat *
meta_clutter_backend_native_get_default_seat (ClutterBackend *clutter_backend)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_default_seat (backend);
}

static gboolean
meta_clutter_backend_native_is_display_server (ClutterBackend *clutter_backend)
{
  return TRUE;
}

static void
meta_clutter_backend_native_init (MetaClutterBackendNative *clutter_backend_nativen)
{
}

static void
meta_clutter_backend_native_class_init (MetaClutterBackendNativeClass *klass)
{
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  clutter_backend_class->get_renderer = meta_clutter_backend_native_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_native_create_stage;
  clutter_backend_class->get_default_seat = meta_clutter_backend_native_get_default_seat;
  clutter_backend_class->is_display_server = meta_clutter_backend_native_is_display_server;
}
