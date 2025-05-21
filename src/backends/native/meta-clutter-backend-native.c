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

/**
 * MetaClutterBackendNative:
 *
 * A native backend which renders using EGL.
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
#include "backends/native/meta-sprite-native.h"
#include "backends/native/meta-stage-native.h"
#include "clutter/clutter.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

struct _MetaClutterBackendNative
{
  ClutterBackend parent;

  MetaBackend *backend;

  GHashTable *touch_sprites;
  GHashTable *stylus_sprites;
  ClutterSprite *pointer_sprite;
  ClutterKeyFocus *key_focus;
};

G_DEFINE_TYPE (MetaClutterBackendNative, meta_clutter_backend_native,
               CLUTTER_TYPE_BACKEND)

static CoglRenderer *
meta_clutter_backend_native_get_renderer (ClutterBackend  *clutter_backend,
                                          GError         **error)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  MetaBackend *backend = clutter_backend_native->backend;
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_native_create_stage (ClutterBackend  *clutter_backend,
                                          ClutterStage    *wrapper,
                                          GError         **error)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);

  return g_object_new (META_TYPE_STAGE_NATIVE,
                       "backend", clutter_backend_native->backend,
                       "wrapper", wrapper,
                       NULL);
}

static ClutterSeat *
meta_clutter_backend_native_get_default_seat (ClutterBackend *clutter_backend)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  MetaBackend *backend = clutter_backend_native->backend;

  return meta_backend_get_default_seat (backend);
}

static gboolean
meta_clutter_backend_native_is_display_server (ClutterBackend *clutter_backend)
{
  return TRUE;
}

static ClutterSprite *
create_sprite (ClutterBackend     *clutter_backend,
               ClutterStage       *stage,
               const ClutterEvent *for_event)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  MetaBackend *backend = clutter_backend_native->backend;
  ClutterInputDevice *device;
  ClutterEventSequence *sequence;

  device = clutter_event_get_device (for_event);
  sequence = clutter_event_get_event_sequence (for_event);

  return g_object_new (META_TYPE_SPRITE_NATIVE,
                       "backend", backend,
                       "stage", stage,
                       "device", device,
                       "sequence", sequence,
                       NULL);
}

static ClutterSprite *
ensure_sprite (ClutterBackend     *clutter_backend,
               ClutterStage       *stage,
               const ClutterEvent *for_event,
               GHashTable         *ht,
               gpointer            key)
{
  ClutterSprite *sprite;

  sprite = g_hash_table_lookup (ht, key);
  if (!sprite)
    {
      sprite = create_sprite (clutter_backend, stage, for_event);
      g_hash_table_insert (ht, key, sprite);
    }

  return sprite;
}

static void
ensure_pointer_sprite (ClutterBackend *clutter_backend)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);

  if (!clutter_backend_native->pointer_sprite)
    {
      MetaBackend *backend = clutter_backend_native->backend;
      ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);

      clutter_backend_native->pointer_sprite =
        g_object_new (META_TYPE_SPRITE_NATIVE,
                      "backend", backend,
                      "stage", stage,
                      "device", clutter_seat_get_pointer (seat),
                      NULL);
    }
}

static ClutterSprite *
meta_clutter_backend_native_get_sprite (ClutterBackend     *clutter_backend,
                                        ClutterStage       *stage,
                                        const ClutterEvent *for_event)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  ClutterInputDevice *source_device;
  ClutterEventSequence *event_sequence;
  ClutterInputDeviceType device_type;

  event_sequence = clutter_event_get_event_sequence (for_event);
  if (event_sequence)
    {
      return ensure_sprite (clutter_backend, stage, for_event,
                            clutter_backend_native->touch_sprites,
                            event_sequence);
    }

  source_device = clutter_event_get_source_device (for_event);
  device_type = clutter_input_device_get_device_type (source_device);

  if (device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE ||
      device_type == CLUTTER_ERASER_DEVICE)
    {
      return ensure_sprite (clutter_backend, stage, for_event,
                            clutter_backend_native->stylus_sprites,
                            source_device);
    }

  if (device_type == CLUTTER_POINTER_DEVICE ||
      device_type == CLUTTER_TOUCHPAD_DEVICE)
    {
      if (!clutter_backend_native->pointer_sprite)
        {
          clutter_backend_native->pointer_sprite =
            create_sprite (clutter_backend, stage, for_event);
        }

      return clutter_backend_native->pointer_sprite;
    }

  return NULL;
}

static ClutterSprite *
meta_clutter_backend_native_lookup_sprite (ClutterBackend       *clutter_backend,
                                           ClutterStage         *stage,
                                           ClutterInputDevice   *device,
                                           ClutterEventSequence *sequence)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  ClutterInputDeviceType device_type;

  if (sequence)
    return g_hash_table_lookup (clutter_backend_native->touch_sprites, sequence);

  device_type = clutter_input_device_get_device_type (device);

  if (device_type == CLUTTER_TABLET_DEVICE)
    return g_hash_table_lookup (clutter_backend_native->stylus_sprites, device);
  else if (device_type != CLUTTER_KEYBOARD_DEVICE &&
           device_type != CLUTTER_PAD_DEVICE)
    return clutter_backend_native->pointer_sprite;

  return NULL;
}

static ClutterSprite *
meta_clutter_backend_native_get_pointer_sprite (ClutterBackend *clutter_backend,
                                                ClutterStage   *stage)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);

  ensure_pointer_sprite (clutter_backend);

  return clutter_backend_native->pointer_sprite;
}

static void
meta_clutter_backend_native_destroy_sprite (ClutterBackend *clutter_backend,
                                            ClutterSprite  *sprite)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);

  g_hash_table_remove (clutter_backend_native->touch_sprites,
                       clutter_sprite_get_sequence (sprite));
  g_hash_table_remove (clutter_backend_native->stylus_sprites,
                       clutter_sprite_get_device (sprite));

  if (clutter_backend_native->pointer_sprite == sprite)
    g_clear_object (&clutter_backend_native->pointer_sprite);
}

static gboolean
meta_clutter_backend_native_foreach_sprite (ClutterBackend               *clutter_backend,
                                            ClutterStage                 *stage,
                                            ClutterStageInputForeachFunc  func,
                                            gpointer                      user_data)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);
  GHashTableIter iter;
  ClutterSprite *sprite;

  if (clutter_backend_native->pointer_sprite)
    {
      if (!func (stage, clutter_backend_native->pointer_sprite, user_data))
        return FALSE;
    }

  g_hash_table_iter_init (&iter, clutter_backend_native->stylus_sprites);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &sprite))
    {
      if (!func (stage, sprite, user_data))
        return FALSE;
    }

  g_hash_table_iter_init (&iter, clutter_backend_native->touch_sprites);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &sprite))
    {
      if (!func (stage, sprite, user_data))
        return FALSE;
    }

  return TRUE;
}

static ClutterKeyFocus *
meta_clutter_backend_native_get_key_focus (ClutterBackend *clutter_backend,
                                           ClutterStage   *stage)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (clutter_backend);

  if (!clutter_backend_native->key_focus)
    {
      clutter_backend_native->key_focus =
        g_object_new (CLUTTER_TYPE_KEY_FOCUS,
                      "stage", stage,
                      NULL);
    }

  return clutter_backend_native->key_focus;
}

static void
meta_clutter_backend_native_constructed (GObject *object)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (object);

  G_OBJECT_CLASS (meta_clutter_backend_native_parent_class)->constructed (object);

  clutter_backend_native->touch_sprites =
    g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
  clutter_backend_native->stylus_sprites =
    g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
}

static void
meta_clutter_backend_native_finalize (GObject *object)
{
  MetaClutterBackendNative *clutter_backend_native =
    META_CLUTTER_BACKEND_NATIVE (object);

  g_clear_pointer (&clutter_backend_native->touch_sprites, g_hash_table_unref);
  g_clear_pointer (&clutter_backend_native->stylus_sprites, g_hash_table_unref);

  G_OBJECT_CLASS (meta_clutter_backend_native_parent_class)->finalize (object);
}

static void
meta_clutter_backend_native_init (MetaClutterBackendNative *clutter_backend_native)
{
}

static void
meta_clutter_backend_native_class_init (MetaClutterBackendNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  object_class->constructed = meta_clutter_backend_native_constructed;
  object_class->finalize = meta_clutter_backend_native_finalize;

  clutter_backend_class->get_renderer = meta_clutter_backend_native_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_native_create_stage;
  clutter_backend_class->get_default_seat = meta_clutter_backend_native_get_default_seat;
  clutter_backend_class->is_display_server = meta_clutter_backend_native_is_display_server;
  clutter_backend_class->get_sprite = meta_clutter_backend_native_get_sprite;
  clutter_backend_class->lookup_sprite = meta_clutter_backend_native_lookup_sprite;
  clutter_backend_class->get_pointer_sprite = meta_clutter_backend_native_get_pointer_sprite;
  clutter_backend_class->destroy_sprite = meta_clutter_backend_native_destroy_sprite;
  clutter_backend_class->foreach_sprite = meta_clutter_backend_native_foreach_sprite;
  clutter_backend_class->get_key_focus = meta_clutter_backend_native_get_key_focus;
}

MetaClutterBackendNative *
meta_clutter_backend_native_new (MetaBackend    *backend,
                                 ClutterContext *context)
{
  MetaClutterBackendNative *clutter_backend_native;

  clutter_backend_native = g_object_new (META_TYPE_CLUTTER_BACKEND_NATIVE,
                                         "context", context,
                                         NULL);
  clutter_backend_native->backend = backend;

  return clutter_backend_native;
}
