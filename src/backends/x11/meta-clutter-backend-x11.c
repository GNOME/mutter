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

#include "config.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-renderer.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-device-manager-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "backends/x11/nested/meta-stage-x11-nested.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

struct _MetaClutterBackendX11
{
  ClutterBackendX11 parent;
  MetaKeymapX11 *keymap;
  MetaDeviceManagerX11 *device_manager;
};

G_DEFINE_TYPE (MetaClutterBackendX11, meta_clutter_backend_x11,
               CLUTTER_TYPE_BACKEND_X11)

static CoglRenderer *
meta_clutter_backend_x11_get_renderer (ClutterBackend  *clutter_backend,
                                       GError         **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_x11_create_stage (ClutterBackend  *backend,
                                       ClutterStage    *wrapper,
                                       GError         **error)
{
  ClutterStageWindow *stage;
  GType stage_type;

  if (meta_is_wayland_compositor ())
    stage_type = META_TYPE_STAGE_X11_NESTED;
  else
    stage_type  = META_TYPE_STAGE_X11;

  stage = g_object_new (stage_type,
			"backend", backend,
			"wrapper", wrapper,
			NULL);
  return stage;
}

static void
meta_clutter_backend_x11_bell_notify (ClutterBackend  *backend)
{
  MetaDisplay *display = meta_get_display ();

  meta_bell_notify (display, NULL);
}

static ClutterDeviceManager *
meta_clutter_backend_x11_get_device_manager (ClutterBackend *backend)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (backend);

  return CLUTTER_DEVICE_MANAGER (backend_x11->device_manager);
}

static PangoDirection
meta_clutter_backend_x11_get_keymap_direction (ClutterBackend *backend)
{
  ClutterKeymap *keymap = clutter_backend_get_keymap (backend);

  if (G_UNLIKELY (keymap == NULL))
    return PANGO_DIRECTION_NEUTRAL;

  return meta_keymap_x11_get_direction (META_KEYMAP_X11 (keymap));
}

static ClutterKeymap *
meta_clutter_backend_x11_get_keymap (ClutterBackend *backend)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (backend);

  return CLUTTER_KEYMAP (backend_x11->keymap);
}

static gboolean
meta_clutter_backend_x11_translate_event (ClutterBackend *backend,
                                          gpointer        native,
                                          ClutterEvent   *event)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (backend);
  MetaDeviceManagerX11 *device_manager_x11;
  MetaStageX11 *stage_x11;
  ClutterBackendClass *clutter_backend_class;

  clutter_backend_class =
    CLUTTER_BACKEND_CLASS (meta_clutter_backend_x11_parent_class);
  if (clutter_backend_class->translate_event (backend, native, event))
    return TRUE;

  if (meta_keymap_x11_handle_event (backend_x11->keymap, native))
    return TRUE;

  stage_x11 = META_STAGE_X11 (clutter_backend_get_stage_window (backend));
  if (meta_stage_x11_translate_event (stage_x11, native, event))
    return TRUE;

  device_manager_x11 = META_DEVICE_MANAGER_X11 (backend_x11->device_manager);
  if (meta_device_manager_x11_translate_event (device_manager_x11,
                                               native, event))
    return TRUE;

  return FALSE;
}

static void
on_keymap_state_change (MetaKeymapX11 *keymap_x11,
                        gpointer       data)
{
  ClutterDeviceManager *device_manager = CLUTTER_DEVICE_MANAGER (data);
  ClutterKbdA11ySettings kbd_a11y_settings;

  /* On keymaps state change, just reapply the current settings, it'll
   * take care of enabling/disabling mousekeys based on NumLock state.
   */
  clutter_device_manager_get_kbd_a11y_settings (device_manager, &kbd_a11y_settings);
  meta_device_manager_x11_apply_kbd_a11y_settings (device_manager, &kbd_a11y_settings);
}

static void
meta_clutter_backend_x11_init_events (ClutterBackend *backend)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (backend);
  int event_base, first_event, first_error;

  if (XQueryExtension (clutter_x11_get_default_display (),
                       "XInputExtension",
                       &event_base,
                       &first_event,
                       &first_error))
    {
      int major = 2;
      int minor = 3;

      if (XIQueryVersion (clutter_x11_get_default_display (),
                          &major, &minor) != BadRequest)
        {
          g_debug ("Creating XI2 device manager");
          backend_x11->device_manager =
            g_object_new (META_TYPE_DEVICE_MANAGER_X11,
                          "backend", backend_x11,
                          "opcode", event_base,
                          NULL);
        }
    }

  if (!backend_x11->device_manager)
    g_error ("No XInput 2.3 support");

  backend_x11->keymap = g_object_new (META_TYPE_KEYMAP_X11,
                                      "backend", backend_x11,
                                      NULL);
  g_signal_connect (backend_x11->keymap,
                    "state-changed",
                    G_CALLBACK (on_keymap_state_change),
                    backend_x11->device_manager);
}

static void
meta_clutter_backend_x11_init (MetaClutterBackendX11 *clutter_backend_x11)
{
}

static void
meta_clutter_backend_x11_class_init (MetaClutterBackendX11Class *klass)
{
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  clutter_backend_class->get_renderer = meta_clutter_backend_x11_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_x11_create_stage;
  clutter_backend_class->bell_notify = meta_clutter_backend_x11_bell_notify;
  clutter_backend_class->get_device_manager = meta_clutter_backend_x11_get_device_manager;
  clutter_backend_class->get_keymap_direction = meta_clutter_backend_x11_get_keymap_direction;
  clutter_backend_class->get_keymap = meta_clutter_backend_x11_get_keymap;
  clutter_backend_class->translate_event = meta_clutter_backend_x11_translate_event;
  clutter_backend_class->init_events = meta_clutter_backend_x11_init_events;
}
