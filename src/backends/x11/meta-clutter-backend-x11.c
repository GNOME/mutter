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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <glib-object.h>
#include <glib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-renderer.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-seat-x11.h"
#include "backends/x11/meta-sprite-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "backends/x11/nested/meta-sprite-x11-nested.h"
#include "backends/x11/nested/meta-stage-x11-nested.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter.h"
#include "cogl/cogl-xlib-renderer.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

typedef struct _MetaClutterBackendX11Private
{
  MetaBackend *backend;
  ClutterSprite *virtual_core_pointer;
  ClutterKeyFocus *virtual_core_keyboard;
  GHashTable *touch_sprites;
} MetaClutterBackendX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaClutterBackendX11, meta_clutter_backend_x11,
                            CLUTTER_TYPE_BACKEND)

/* atoms; remember to add the code that assigns the atom value to
 * the member of the MetaClutterBackendX11 structure if you add an
 * atom name here. do not change the order!
 */
static const gchar *atom_names[] = {
  "_NET_WM_PID",
  "_NET_WM_PING",
  "_NET_WM_STATE",
  "_NET_WM_USER_TIME",
  "WM_PROTOCOLS",
  "WM_DELETE_WINDOW",
  "_XEMBED",
  "_XEMBED_INFO",
  "_NET_WM_NAME",
  "UTF8_STRING",
};

#define N_ATOM_NAMES G_N_ELEMENTS (atom_names)

static CoglRenderer *
meta_clutter_backend_x11_get_renderer (ClutterBackend  *clutter_backend,
                                       GError         **error)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  MetaRenderer *renderer = meta_backend_get_renderer (priv->backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_x11_create_stage (ClutterBackend  *clutter_backend,
                                       ClutterStage    *wrapper,
                                       GError         **error)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  ClutterStageWindow *stage;
  GType stage_type;

  if (meta_is_wayland_compositor ())
    stage_type = META_TYPE_STAGE_X11_NESTED;
  else
    stage_type = META_TYPE_STAGE_X11;

  stage = g_object_new (stage_type,
			"backend", priv->backend,
			"wrapper", wrapper,
			NULL);
  return stage;
}

static ClutterSeat *
meta_clutter_backend_x11_get_default_seat (ClutterBackend *clutter_backend)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  return meta_backend_get_default_seat (priv->backend);
}

static gboolean
meta_clutter_backend_x11_is_display_server (ClutterBackend *clutter_backend)
{
  return meta_is_wayland_compositor ();
}

static ClutterSprite *
lookup_sprite (ClutterBackend       *clutter_backend,
               ClutterStage         *stage,
               ClutterInputDevice   *device,
               ClutterEventSequence *sequence,
               gboolean              create)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  ClutterInputDeviceType device_type;
  GType sprite_type;

  if (meta_is_wayland_compositor ())
    sprite_type = META_TYPE_SPRITE_X11_NESTED;
  else
    sprite_type = META_TYPE_SPRITE_X11;

  device_type = clutter_input_device_get_device_type (device);

  if (sequence)
    {
      ClutterSprite *touch_sprite;

      touch_sprite = g_hash_table_lookup (priv->touch_sprites, sequence);

      if (!touch_sprite && create)
        {
          touch_sprite =
            g_object_new (sprite_type,
                          "backend", priv->backend,
                          "stage", stage,
                          "device", device,
                          "sequence", sequence,
                          NULL);

          g_hash_table_insert (priv->touch_sprites, sequence, touch_sprite);
        }

      return touch_sprite;
    }

  if (device_type == CLUTTER_POINTER_DEVICE ||
      device_type == CLUTTER_TOUCHPAD_DEVICE ||
      device_type == CLUTTER_TOUCHSCREEN_DEVICE ||
      device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_ERASER_DEVICE)
    {
      if (!priv->virtual_core_pointer && create)
        {
          priv->virtual_core_pointer =
            g_object_new (sprite_type,
                          "backend", priv->backend,
                          "stage", stage,
                          "device", device,
                          "sequence", sequence,
                          NULL);
        }
      return priv->virtual_core_pointer;
    }

  return NULL;
}

static ClutterSprite *
meta_clutter_backend_x11_get_sprite (ClutterBackend     *clutter_backend,
                                     ClutterStage       *stage,
                                     const ClutterEvent *for_event)
{
  ClutterInputDevice *source_device;
  ClutterEventSequence *sequence;

  sequence = clutter_event_get_event_sequence (for_event);
  if (sequence &&
      (clutter_event_get_flags (for_event) & CLUTTER_EVENT_FLAG_POINTER_EMULATED) == 0)
    return NULL;

  source_device = clutter_event_get_source_device (for_event);

  return lookup_sprite (clutter_backend, stage,
                        source_device, sequence, TRUE);
}

static ClutterSprite *
meta_clutter_backend_x11_lookup_sprite (ClutterBackend       *clutter_backend,
                                        ClutterStage         *stage,
                                        ClutterInputDevice   *device,
                                        ClutterEventSequence *sequence)
{
  return lookup_sprite (clutter_backend, stage, device, sequence, FALSE);
}

static ClutterSprite *
meta_clutter_backend_x11_get_pointer_sprite (ClutterBackend *clutter_backend,
                                             ClutterStage   *stage)
{
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);

  return lookup_sprite (clutter_backend, stage,
                        clutter_seat_get_pointer (seat),
                        NULL, TRUE);
}

static void
meta_clutter_backend_x11_destroy_sprite (ClutterBackend *clutter_backend,
                                         ClutterSprite  *sprite)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  if (sprite == priv->virtual_core_pointer)
    g_clear_object (&priv->virtual_core_pointer);

  g_hash_table_remove (priv->touch_sprites,
                       clutter_sprite_get_sequence (sprite));
}

static gboolean
meta_clutter_backend_x11_foreach_sprite (ClutterBackend               *clutter_backend,
                                         ClutterStage                 *stage,
                                         ClutterStageInputForeachFunc  func,
                                         gpointer                      user_data)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  ClutterSprite *touch_sprite;
  GHashTableIter iter;

  if (priv->virtual_core_pointer &&
      !func (stage, priv->virtual_core_pointer, user_data))
    return FALSE;

  g_hash_table_iter_init (&iter, priv->touch_sprites);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &touch_sprite))
    {
      if (!func (stage, touch_sprite, user_data))
          return FALSE;
    }

  return TRUE;
}

static ClutterKeyFocus *
meta_clutter_backend_x11_get_key_focus (ClutterBackend *clutter_backend,
                                        ClutterStage   *stage)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  if (!priv->virtual_core_keyboard)
    {
      priv->virtual_core_keyboard =
        g_object_new (CLUTTER_TYPE_KEY_FOCUS,
                      "stage", stage,
                      NULL);
    }

  return priv->virtual_core_keyboard;
}

static void
meta_clutter_backend_x11_finalize (GObject *object)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (object);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  g_clear_pointer (&priv->touch_sprites, g_hash_table_unref);

  G_OBJECT_CLASS (meta_clutter_backend_x11_parent_class)->finalize (object);
}

static void
meta_clutter_backend_x11_init (MetaClutterBackendX11 *clutter_backend_x11)
{
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  priv->touch_sprites =
    g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) g_object_unref);
}

static void
meta_clutter_backend_x11_class_init (MetaClutterBackendX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  object_class->finalize = meta_clutter_backend_x11_finalize;

  clutter_backend_class->get_renderer = meta_clutter_backend_x11_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_x11_create_stage;
  clutter_backend_class->get_default_seat = meta_clutter_backend_x11_get_default_seat;
  clutter_backend_class->is_display_server = meta_clutter_backend_x11_is_display_server;
  clutter_backend_class->get_sprite = meta_clutter_backend_x11_get_sprite;
  clutter_backend_class->lookup_sprite = meta_clutter_backend_x11_lookup_sprite;
  clutter_backend_class->get_pointer_sprite = meta_clutter_backend_x11_get_pointer_sprite;
  clutter_backend_class->destroy_sprite = meta_clutter_backend_x11_destroy_sprite;
  clutter_backend_class->foreach_sprite = meta_clutter_backend_x11_foreach_sprite;
  clutter_backend_class->get_key_focus = meta_clutter_backend_x11_get_key_focus;
}

MetaClutterBackendX11 *
meta_clutter_backend_x11_new (MetaBackend    *backend,
                              ClutterContext *context)
{
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Atom atoms[N_ATOM_NAMES];
  MetaClutterBackendX11 *clutter_backend_x11;
  MetaClutterBackendX11Private *priv;

  clutter_backend_x11 = g_object_new (META_TYPE_CLUTTER_BACKEND_X11,
                                      "context", context,
                                      NULL);
  priv = meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  priv->backend = backend;

  clutter_backend_x11->xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  XInternAtoms (clutter_backend_x11->xdisplay,
                (char **) atom_names, N_ATOM_NAMES,
                False, atoms);

  clutter_backend_x11->atom_NET_WM_PID = atoms[0];
  clutter_backend_x11->atom_NET_WM_PING = atoms[1];
  clutter_backend_x11->atom_NET_WM_STATE = atoms[2];
  clutter_backend_x11->atom_NET_WM_USER_TIME = atoms[3];
  clutter_backend_x11->atom_WM_PROTOCOLS = atoms[4];
  clutter_backend_x11->atom_WM_DELETE_WINDOW = atoms[5];
  clutter_backend_x11->atom_XEMBED = atoms[6];
  clutter_backend_x11->atom_XEMBED_INFO = atoms[7];
  clutter_backend_x11->atom_NET_WM_NAME = atoms[8];
  clutter_backend_x11->atom_UTF8_STRING = atoms[9];

  return clutter_backend_x11;
}
