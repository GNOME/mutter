/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corp.
 * Copyright (C) 2014  Jonas Ådahl
 * Copyright (C) 2016  Red Hat Inc.
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
 *
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include "backends/native/meta-seat-native.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-keymap-description-private.h"
#include "backends/meta-keymap-utils.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "core/bell.h"
#include "meta/meta-keymap-description.h"

#include "meta-private-enum-types.h"

enum
{
  PROP_0,
  PROP_SEAT_ID,
  PROP_FLAGS,
  PROP_BACKEND,
  N_PROPS,

  /* This property is overridden */
  PROP_TOUCH_MODE,
};

static GParamSpec *props[N_PROPS] = { NULL };

enum
{
  KEYMAP_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (MetaSeatNative, meta_seat_native, CLUTTER_TYPE_SEAT)

static gboolean meta_seat_native_set_keymap_sync (MetaSeatNative         *seat_native,
                                                  MetaKeymapDescription  *description,
                                                  xkb_layout_index_t      layout_index,
                                                  GCancellable           *cancellable,
                                                  GError                **error);

static gboolean
meta_seat_native_handle_event_post (ClutterSeat        *seat,
                                    const ClutterEvent *event)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  ClutterInputDevice *device = clutter_event_get_source_device (event);
  ClutterEventType event_type = clutter_event_type (event);

  if (event_type == CLUTTER_DEVICE_ADDED)
    {
      seat_native->devices = g_list_prepend (seat_native->devices, g_object_ref (device));
    }
  else if (event_type == CLUTTER_DEVICE_REMOVED)
    {
      GList *l = g_list_find (seat_native->devices, device);

      if (l)
        {
          seat_native->devices = g_list_delete_link (seat_native->devices, l);
          g_object_unref (device);
        }
    }
  else if (event_type == CLUTTER_PROXIMITY_OUT)
    {
      ClutterStage *stage =
        CLUTTER_STAGE (meta_backend_get_stage (seat_native->backend));
      ClutterBackend *clutter_backend =
        meta_backend_get_clutter_backend (seat_native->backend);
      ClutterSprite *sprite;

      sprite = clutter_backend_get_sprite (clutter_backend, stage, event);
      if (sprite)
        meta_seat_native_remove_cursor_renderer (seat_native, sprite);
    }

  return FALSE;
}

static void
proxy_kbd_a11y_flags_changed (MetaSeatImpl          *seat_impl,
                              MetaKeyboardA11yFlags  new_flags,
                              MetaKeyboardA11yFlags  what_changed,
                              MetaSeatNative        *seat_native)
{
  g_signal_emit_by_name (seat_native,
                         "kbd-a11y-flags-changed",
                         new_flags, what_changed);
}

static void
proxy_kbd_a11y_mods_state_changed (MetaSeatImpl   *seat_impl,
                                   xkb_mod_mask_t  new_latched_mods,
                                   xkb_mod_mask_t  new_locked_mods,
                                   MetaSeatNative *seat_native)
{
  g_signal_emit_by_name (seat_native,
                         "kbd-a11y-mods-state-changed",
                         new_latched_mods,
                         new_locked_mods);
}

static void
proxy_touch_mode_changed (MetaSeatImpl   *seat_impl,
                          gboolean        enabled,
                          MetaSeatNative *seat_native)
{
  seat_native->touch_mode = enabled;
  g_object_notify (G_OBJECT (seat_native), "touch-mode");
}

static void
proxy_bell (MetaSeatImpl   *seat_impl,
            MetaSeatNative *seat_native)
{
  clutter_seat_bell_notify (CLUTTER_SEAT (seat_native));
}

static void
keymap_state_changed_cb (MetaSeatNative *seat_native,
                         ClutterKeymap  *keymap)
{
  xkb_layout_index_t idx;

  idx = clutter_keymap_get_layout_index (keymap);

  if (idx != seat_native->xkb_layout_index)
    {
      seat_native->xkb_layout_index = idx;
      meta_backend_notify_keymap_layout_group_changed (seat_native->backend,
                                                       idx);
    }
}

static void
drop_cursor_renderer (gpointer user_data)
{
  MetaCursorRenderer *cursor_renderer = user_data;

  meta_cursor_renderer_set_sprite (cursor_renderer, NULL);
  g_object_unref (cursor_renderer);
}

static void
on_prepare_shutdown (MetaBackend    *backend,
                     MetaSeatNative *seat_native)
{
  meta_seat_impl_prepare_shutdown (seat_native->impl);
}

static void
on_keymap_changed (MetaKeymapNative      *keymap_native,
                   MetaKeymapDescription *keymap_description,
                   MetaSeatNative        *seat_native)
{
  g_autoptr (GError) error = NULL;
  struct xkb_keymap *xkb_keymap;

  if (seat_native->keymap_description == keymap_description)
    return;

  xkb_keymap =
    meta_keymap_description_create_xkb_keymap (keymap_description,
                                               NULL, NULL,
                                               &error);
  if (!xkb_keymap)
    {
      g_warning ("Failed to create xkb_keymap for seat: %s", error->message);
      return;
    }

  g_clear_pointer (&seat_native->keymap_description,
                   meta_keymap_description_unref);
  seat_native->keymap_description =
    meta_keymap_description_ref (keymap_description);

  g_clear_pointer (&seat_native->xkb_keymap, xkb_keymap_unref);
  seat_native->xkb_keymap = g_steal_pointer (&xkb_keymap);
  seat_native->xkb_layout_index =
    clutter_keymap_get_layout_index (CLUTTER_KEYMAP (keymap_native));

  g_signal_emit (seat_native, signals[KEYMAP_CHANGED], 0);
}

static void
meta_seat_native_constructed (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);
  g_autoptr (MetaKeymapDescription) keymap_description = NULL;
  g_autoptr (GError) error = NULL;
  ClutterKeymap *keymap;

  seat->impl = meta_seat_impl_new (seat, seat->seat_id, seat->flags);
  meta_seat_impl_setup (seat->impl);
  g_signal_connect (seat->impl, "kbd-a11y-flags-changed",
                    G_CALLBACK (proxy_kbd_a11y_flags_changed), seat);
  g_signal_connect (seat->impl, "kbd-a11y-mods-state-changed",
                    G_CALLBACK (proxy_kbd_a11y_mods_state_changed), seat);
  g_signal_connect (seat->impl, "touch-mode",
                    G_CALLBACK (proxy_touch_mode_changed), seat);
  g_signal_connect (seat->impl, "bell",
                    G_CALLBACK (proxy_bell), seat);

  keymap = clutter_seat_get_keymap (CLUTTER_SEAT (seat));
  g_signal_connect (keymap, "keymap-changed",
                    G_CALLBACK (on_keymap_changed), seat);

  keymap_description = meta_keymap_description_new_from_rules (NULL,
                                                               "us",
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
  if (!meta_seat_native_set_keymap_sync (seat,
                                         keymap_description, 0,
                                         NULL, &error))
    g_warning ("Failed to set keyboard map: %s", error->message);

  seat->secondary_cursor_renderers = g_hash_table_new_full (NULL, NULL, NULL,
                                                            drop_cursor_renderer);

  g_signal_connect_after (seat->backend, "prepare-shutdown",
                          G_CALLBACK (on_prepare_shutdown), seat);

  if (G_OBJECT_CLASS (meta_seat_native_parent_class)->constructed)
    G_OBJECT_CLASS (meta_seat_native_parent_class)->constructed (object);
}

static void
meta_seat_native_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT_ID:
      seat_native->seat_id = g_value_dup_string (value);
      break;
    case PROP_FLAGS:
      seat_native->flags = g_value_get_flags (value);
      break;
    case PROP_BACKEND:
      seat_native->backend = g_value_get_object (value);
      break;
    case PROP_TOUCH_MODE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_native_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT_ID:
      g_value_set_string (value, seat_native->seat_id);
      break;
    case PROP_TOUCH_MODE:
      g_value_set_boolean (value, seat_native->touch_mode);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, seat_native->flags);
      break;
    case PROP_BACKEND:
      g_value_set_object (value, seat_native->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_native_dispose (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);

  g_clear_pointer (&seat->keymap_description,
                   meta_keymap_description_unref);
  g_clear_pointer (&seat->xkb_keymap, xkb_keymap_unref);
  g_clear_pointer (&seat->keymap_description, meta_keymap_description_unref);
  g_list_free_full (g_steal_pointer (&seat->devices), g_object_unref);
  g_clear_object (&seat->impl);
  g_clear_pointer (&seat->reserved_virtual_slots, g_hash_table_destroy);
  g_clear_pointer (&seat->secondary_cursor_renderers, g_hash_table_unref);
  g_clear_object (&seat->cursor_renderer);

  g_clear_pointer (&seat->seat_id, g_free);

  G_OBJECT_CLASS (meta_seat_native_parent_class)->dispose (object);
}

static const GList *
meta_seat_native_peek_devices (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return (const GList *) seat_native->devices;
}

static void
meta_seat_native_bell_notify (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  MetaContext *context = meta_backend_get_context (seat_native->backend);
  MetaDisplay *display = meta_context_get_display (context);

  meta_bell_notify (display, NULL);
}

static ClutterKeymap *
meta_seat_native_get_keymap (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  if (!seat_native->keymap)
    {
      seat_native->keymap = meta_seat_impl_get_keymap (seat_native->impl);
      g_signal_connect_object (seat_native->keymap,
                               "state-changed",
                               G_CALLBACK (keymap_state_changed_cb),
                               seat,
                               G_CONNECT_SWAPPED);
    }

  return CLUTTER_KEYMAP (seat_native->keymap);
}

static guint
bump_virtual_touch_slot_base (MetaSeatNative *seat_native)
{
  while (TRUE)
    {
      if (seat_native->virtual_touch_slot_base < 0x100)
        seat_native->virtual_touch_slot_base = 0x100;

      seat_native->virtual_touch_slot_base +=
        CLUTTER_VIRTUAL_INPUT_DEVICE_MAX_TOUCH_SLOTS;

      if (!g_hash_table_lookup (seat_native->reserved_virtual_slots,
                                GUINT_TO_POINTER (seat_native->virtual_touch_slot_base)))
        break;
    }

  return seat_native->virtual_touch_slot_base;
}

static ClutterVirtualInputDevice *
meta_seat_native_create_virtual_device (ClutterSeat            *seat,
                                        ClutterInputDeviceType  device_type)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  guint slot_base;

  slot_base = bump_virtual_touch_slot_base (seat_native);
  g_hash_table_add (seat_native->reserved_virtual_slots,
                    GUINT_TO_POINTER (slot_base));

  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_NATIVE,
                       "seat", seat,
                       "slot-base", slot_base,
                       "device-type", device_type,
                       NULL);
}

void
meta_seat_native_release_touch_slots (MetaSeatNative *seat,
                                      guint           base_slot)
{
  g_hash_table_remove (seat->reserved_virtual_slots,
                       GUINT_TO_POINTER (base_slot));
}

static ClutterVirtualDeviceType
meta_seat_native_get_supported_virtual_device_types (ClutterSeat *seat)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER |
          CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN);
}

static void
meta_seat_native_warp_pointer (ClutterSeat *seat,
                               int          x,
                               int          y)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  meta_seat_impl_warp_pointer (seat_native->impl, x, y);
}

static void
meta_seat_native_init_pointer_position (ClutterSeat *seat,
                                        float        x,
                                        float        y)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  meta_seat_impl_init_pointer_position (seat_native->impl, x, y);
}

static gboolean
meta_seat_native_query_state (ClutterSeat         *seat,
                              ClutterSprite       *sprite,
                              graphene_point_t    *coords,
                              ClutterModifierType *modifiers)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  ClutterStage *stage =
    CLUTTER_STAGE (meta_backend_get_stage (seat_native->backend));
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (seat_native->backend);
  ClutterInputDevice *sprite_device = NULL;
  ClutterEventSequence *event_sequence = NULL;

  if (sprite == clutter_backend_get_pointer_sprite (clutter_backend, stage))
    sprite = NULL;

  if (sprite)
    {
      sprite_device = clutter_sprite_get_sprite_device (sprite);
      event_sequence = clutter_sprite_get_sequence (sprite);
    }

  return meta_seat_impl_query_state (seat_native->impl,
                                     sprite_device,
                                     event_sequence,
                                     coords, modifiers);
}

static void
meta_seat_native_is_unfocus_inhibited_changed (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  gboolean may_focus;

  may_focus = clutter_seat_is_unfocus_inhibited (seat);

  if (!may_focus)
    {
      ClutterBackend *clutter_backend =
        meta_backend_get_clutter_backend (seat_native->backend);
      ClutterStage *stage =
        CLUTTER_STAGE (meta_backend_get_stage (seat_native->backend));
      ClutterSprite *pointer_sprite;

      pointer_sprite = clutter_backend_get_pointer_sprite (clutter_backend,
                                                           stage);
      meta_seat_native_remove_cursor_renderer (seat_native,
                                               pointer_sprite);
    }
}

static ClutterInputDevice *
meta_seat_native_get_virtual_source_pointer (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return meta_seat_impl_get_virtual_source_pointer (seat_native->impl);
}

static void
meta_seat_native_class_init (MetaSeatNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterSeatClass *seat_class = CLUTTER_SEAT_CLASS (klass);

  object_class->constructed = meta_seat_native_constructed;
  object_class->set_property = meta_seat_native_set_property;
  object_class->get_property = meta_seat_native_get_property;
  object_class->dispose = meta_seat_native_dispose;

  seat_class->peek_devices = meta_seat_native_peek_devices;
  seat_class->bell_notify = meta_seat_native_bell_notify;
  seat_class->get_keymap = meta_seat_native_get_keymap;
  seat_class->create_virtual_device = meta_seat_native_create_virtual_device;
  seat_class->get_supported_virtual_device_types = meta_seat_native_get_supported_virtual_device_types;
  seat_class->warp_pointer = meta_seat_native_warp_pointer;
  seat_class->init_pointer_position = meta_seat_native_init_pointer_position;
  seat_class->handle_event_post = meta_seat_native_handle_event_post;
  seat_class->query_state = meta_seat_native_query_state;
  seat_class->is_unfocus_inhibited_changed =
    meta_seat_native_is_unfocus_inhibited_changed;
  seat_class->get_virtual_source_pointer = meta_seat_native_get_virtual_source_pointer;

  props[PROP_SEAT_ID] =
    g_param_spec_string ("seat-id", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        META_TYPE_SEAT_NATIVE_FLAG,
                        META_SEAT_NATIVE_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  g_object_class_override_property (object_class, PROP_TOUCH_MODE,
                                    "touch-mode");

  signals[KEYMAP_CHANGED] =
    g_signal_new ("keymap-changed",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
}

static void
meta_seat_native_init (MetaSeatNative *seat)
{
  seat->reserved_virtual_slots = g_hash_table_new (NULL, NULL);
}

void
meta_seat_native_start (MetaSeatNative *seat_native)
{
  meta_seat_impl_start (seat_native->impl);
}

/**
 * meta_seat_native_release_devices:
 *
 * Releases all the evdev devices that Clutter is currently managing. This api
 * is typically used when switching away from the Clutter application when
 * switching tty. The devices can be reclaimed later with a call to
 * meta_seat_native_reclaim_devices().
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_native_release_devices (MetaSeatNative *seat)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  if (seat->released)
    {
      g_warning ("meta_seat_native_release_devices() shouldn't be called "
                 "multiple times without a corresponding call to "
                 "meta_seat_native_reclaim_devices() first");
      return;
    }

  meta_seat_impl_release_devices (seat->impl);
  seat->released = TRUE;
}

/**
 * meta_seat_native_reclaim_devices:
 *
 * This causes Clutter to re-probe for evdev devices. This is must only be
 * called after a corresponding call to meta_seat_native_release_devices()
 * was previously used to release all evdev devices. This API is typically
 * used when a clutter application using evdev has regained focus due to
 * switching ttys.
 *
 * This function should only be called after clutter has been initialized.
 */
void
meta_seat_native_reclaim_devices (MetaSeatNative *seat)
{
  if (!seat->released)
    {
      g_warning ("Spurious call to meta_seat_native_reclaim_devices() without "
                 "previous call to meta_seat_native_release_devices");
      return;
    }

  meta_seat_impl_reclaim_devices (seat->impl);
  seat->released = FALSE;
}

gboolean
meta_seat_native_set_keymap_finish (MetaSeatNative  *seat_native,
                                    GAsyncResult    *result,
                                    GError         **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (g_task_is_valid (result, seat_native), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_seat_native_set_keymap_async, FALSE);

  return g_task_propagate_boolean (task, error);
}

static void
set_impl_keyboard_map_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GError) error = NULL;

  if (!meta_seat_impl_set_keymap_finish (seat_impl, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

/**
 * meta_seat_native_set_keymap_async: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the specified keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 */
void
meta_seat_native_set_keymap_async (MetaSeatNative        *seat,
                                   MetaKeymapDescription *description,
                                   xkb_layout_index_t     layout_index,
                                   GCancellable          *cancellable,
                                   GAsyncReadyCallback    callback,
                                   gpointer               user_data)
{
  g_autoptr (GTask) task = NULL;

  task = g_task_new (G_OBJECT (seat), cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_seat_native_set_keymap_async);

  meta_seat_impl_set_keymap_async (seat->impl,
                                   description,
                                   layout_index,
                                   cancellable,
                                   set_impl_keyboard_map_cb,
                                   g_object_ref (task));
}

static void
sync_set_impl_keyboard_map_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  MetaSeatImpl *seat_impl = META_SEAT_IMPL (source_object);
  g_autoptr (GError) error = NULL;
  GTask *task = G_TASK (user_data);
  GMainLoop *main_loop = g_task_get_task_data (task);

  if (!meta_seat_impl_set_keymap_finish (seat_impl, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      g_main_loop_quit (main_loop);
      return;
    }

  g_task_return_boolean (task, TRUE);
  g_main_loop_quit (main_loop);
}

static gboolean
meta_seat_native_set_keymap_sync (MetaSeatNative         *seat_native,
                                  MetaKeymapDescription  *description,
                                  xkb_layout_index_t      layout_index,
                                  GCancellable           *cancellable,
                                  GError                **error)
{
  g_autoptr (GMainContext) main_context = NULL;
  g_autoptr (GMainLoop) main_loop = NULL;
  g_autoptr (GTask) task = NULL;
  struct xkb_keymap *xkb_keymap;

  main_context = g_main_context_new ();
  main_loop = g_main_loop_new (main_context, FALSE);
  g_main_context_push_thread_default (main_context);

  task = g_task_new (NULL, NULL, NULL, NULL);
  g_task_set_task_data (task, main_loop, NULL);

  meta_seat_impl_set_keymap_async (seat_native->impl,
                                   description,
                                   layout_index,
                                   cancellable,
                                   sync_set_impl_keyboard_map_cb,
                                   task);
  g_main_loop_run (main_loop);
  g_main_context_pop_thread_default (main_context);

  if (!g_task_propagate_boolean (task, error))
    return FALSE;

  xkb_keymap =
    meta_keymap_description_create_xkb_keymap (description,
                                               NULL, NULL,
                                               error);
  if (!xkb_keymap)
    return FALSE;

  g_clear_pointer (&seat_native->xkb_keymap, xkb_keymap_unref);
  seat_native->xkb_keymap = g_steal_pointer (&xkb_keymap);
  seat_native->xkb_layout_index = layout_index;

  g_clear_pointer (&seat_native->keymap_description,
                   meta_keymap_description_unref);
  seat_native->keymap_description = meta_keymap_description_ref (description);

  return TRUE;
}

/**
 * meta_seat_native_get_keymap: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 *
 * Retrieves the #xkb_keymap in use by the evdev backend.
 *
 * Return value: the #xkb_keymap.
 */
struct xkb_keymap *
meta_seat_native_get_xkb_keymap (MetaSeatNative *seat)
{
  g_return_val_if_fail (META_IS_SEAT_NATIVE (seat), NULL);

  return seat->xkb_keymap;
}

MetaKeymapDescription *
meta_seat_native_get_keymap_description (MetaSeatNative *seat_native)
{
  g_return_val_if_fail (seat_native->keymap_description, NULL);

  return seat_native->keymap_description;
}

/**
 * meta_seat_native_get_keyboard_layout_index: (skip)
 */
xkb_layout_index_t
meta_seat_native_get_keyboard_layout_index (MetaSeatNative *seat)
{
  return seat->xkb_layout_index;
}

MetaBarrierManagerNative *
meta_seat_native_get_barrier_manager (MetaSeatNative *seat)
{
  return meta_seat_impl_get_barrier_manager (seat->impl);
}

MetaBackend *
meta_seat_native_get_backend (MetaSeatNative *seat_native)
{
  return seat_native->backend;
}

void
meta_seat_native_set_pointer_constraint (MetaSeatNative            *seat,
                                         MetaPointerConstraintImpl *constraint_impl)
{
  meta_seat_impl_set_pointer_constraint (seat->impl, constraint_impl);
}

MetaCursorRenderer *
meta_seat_native_maybe_ensure_cursor_renderer (MetaSeatNative *seat_native,
                                               ClutterSprite  *sprite)
{
  ClutterSprite *native_renderer_owner;

  if (clutter_sprite_get_role (sprite) == CLUTTER_SPRITE_ROLE_TOUCHPOINT)
    return NULL;

  if (!seat_native->cursor_renderer)
    {
      MetaCursorRendererNative *cursor_renderer_native;

      cursor_renderer_native =
        meta_cursor_renderer_native_new (seat_native->backend);
      seat_native->cursor_renderer =
        META_CURSOR_RENDERER (cursor_renderer_native);
    }

  native_renderer_owner =
    meta_cursor_renderer_get_sprite (seat_native->cursor_renderer);

  if (!native_renderer_owner)
    {
      /* Hand over the native renderer to this sprite */
      g_hash_table_remove (seat_native->secondary_cursor_renderers, sprite);
      meta_cursor_renderer_set_sprite (seat_native->cursor_renderer, sprite);

      return seat_native->cursor_renderer;
    }
  else if (native_renderer_owner == sprite)
    {
      return seat_native->cursor_renderer;
    }
  else
    {
      MetaCursorRenderer *secondary_renderer;

      secondary_renderer = g_hash_table_lookup (seat_native->secondary_cursor_renderers,
                                                sprite);

      if (!secondary_renderer)
        {
          secondary_renderer = meta_cursor_renderer_new (seat_native->backend);
          g_hash_table_insert (seat_native->secondary_cursor_renderers,
                               sprite, secondary_renderer);

          meta_cursor_renderer_set_sprite (secondary_renderer, sprite);
        }

      return secondary_renderer;
    }
}

void
meta_seat_native_set_viewports (MetaSeatNative   *seat,
                                MetaViewportInfo *viewports)
{
  meta_seat_impl_set_viewports (seat->impl, viewports);
}

void
meta_seat_native_set_a11y_modifiers (MetaSeatNative *seat,
                                     const uint32_t *modifiers,
                                     int             n_modifiers)
{
  meta_seat_impl_set_a11y_modifiers (seat->impl, modifiers, n_modifiers);
}

void
meta_seat_native_run_impl_task (MetaSeatNative *seat,
                                GSourceFunc     dispatch_func,
                                gpointer        user_data,
                                GDestroyNotify  destroy_notify)
{
  g_autoptr (GTask) task = NULL;

  task = g_task_new (seat->impl, NULL, NULL, NULL);
  g_task_set_task_data (task, user_data, destroy_notify);
  meta_seat_impl_run_input_task (seat->impl, task,
                                 (GSourceFunc) dispatch_func);
}

void
meta_seat_native_remove_cursor_renderer (MetaSeatNative *seat_native,
                                         ClutterSprite  *sprite)
{
  if (seat_native->cursor_renderer &&
      sprite == meta_cursor_renderer_get_sprite (seat_native->cursor_renderer))
    meta_cursor_renderer_set_sprite (seat_native->cursor_renderer, NULL);

  g_hash_table_remove (seat_native->secondary_cursor_renderers, sprite);
}
