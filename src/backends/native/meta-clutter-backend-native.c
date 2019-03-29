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
#include "backends/native/meta-device-manager-native.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-stage-native.h"
#include "clutter/clutter.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

struct _MetaClutterBackendNative
{
  ClutterBackendEglNative parent;

  MetaStageNative *stage_native;
  MetaKeymapNative *keymap;
  MetaDeviceManagerNative *device_manager;
};

G_DEFINE_TYPE (MetaClutterBackendNative, meta_clutter_backend_native,
               CLUTTER_TYPE_BACKEND_EGL_NATIVE)

MetaStageNative *
meta_clutter_backend_native_get_stage_native (ClutterBackend *backend)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (backend);

  return clutter_backend_native->stage_native;
}

static CoglRenderer *
meta_clutter_backend_native_get_renderer (ClutterBackend  *clutter_backend,
                                          GError         **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_native_create_stage (ClutterBackend  *backend,
                                          ClutterStage    *wrapper,
                                          GError         **error)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (backend);

  g_assert (!clutter_backend_native->stage_native);

  clutter_backend_native->stage_native = g_object_new (META_TYPE_STAGE_NATIVE,
                                                       "backend", backend,
                                                       "wrapper", wrapper,
                                                       NULL);
  return CLUTTER_STAGE_WINDOW (clutter_backend_native->stage_native);
}

static void
meta_clutter_backend_native_bell_notify (ClutterBackend  *backend)
{
  MetaDisplay *display = meta_get_display ();

  meta_bell_notify (display, NULL);
}

static ClutterDeviceManager *
meta_clutter_backend_native_get_device_manager (ClutterBackend *backend)
{
  MetaClutterBackendNative *backend_native = META_CLUTTER_BACKEND_NATIVE (backend);

  return CLUTTER_DEVICE_MANAGER (backend_native->device_manager);
}

static ClutterKeymap *
meta_clutter_backend_native_get_keymap (ClutterBackend *backend)
{
  MetaClutterBackendNative *backend_native = META_CLUTTER_BACKEND_NATIVE (backend);

  return CLUTTER_KEYMAP (backend_native->keymap);
}

static void
meta_clutter_backend_native_init_events (ClutterBackend *backend)
{
  MetaClutterBackendNative *backend_native = META_CLUTTER_BACKEND_NATIVE (backend);

  backend_native->keymap = g_object_new (META_TYPE_KEYMAP_NATIVE, NULL);
  backend_native->device_manager = g_object_new (META_TYPE_DEVICE_MANAGER_NATIVE,
                                                 "backend", backend,
                                                 NULL);
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
  clutter_backend_class->bell_notify = meta_clutter_backend_native_bell_notify;
  clutter_backend_class->get_device_manager = meta_clutter_backend_native_get_device_manager;
  clutter_backend_class->get_keymap = meta_clutter_backend_native_get_keymap;
  clutter_backend_class->init_events = meta_clutter_backend_native_init_events;
}
