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

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-keymap-utils.h"
#include "backends/native/meta-barrier-native.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-virtual-input-device-native.h"
#include "clutter/clutter-mutter.h"
#include "core/bell.h"

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

G_DEFINE_TYPE (MetaSeatNative, meta_seat_native, CLUTTER_TYPE_SEAT)

static gboolean
meta_seat_native_handle_event_post (ClutterSeat        *seat,
                                    const ClutterEvent *event)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  ClutterInputDevice *device = clutter_event_get_source_device (event);
  ClutterEventType event_type = clutter_event_type (event);

  if (event_type == CLUTTER_PROXIMITY_OUT)
    {
      if (seat_native->tablet_cursors)
        g_hash_table_remove (seat_native->tablet_cursors, device);
      return TRUE;
    }
  else if (event_type == CLUTTER_DEVICE_ADDED)
    {
      if (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL)
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
proxy_mods_state_changed (MetaSeatImpl   *seat_impl,
                          ClutterSeat    *seat)
{
  ClutterKeymap *keymap;

  keymap = clutter_seat_get_keymap (seat);
  g_signal_emit_by_name (keymap, "state-changed");
}

static void
meta_seat_native_constructed (GObject *object)
{
  MetaSeatNative *seat = META_SEAT_NATIVE (object);

  seat->impl = meta_seat_impl_new (seat, seat->seat_id, seat->flags);
  g_signal_connect (seat->impl, "kbd-a11y-flags-changed",
                    G_CALLBACK (proxy_kbd_a11y_flags_changed), seat);
  g_signal_connect (seat->impl, "kbd-a11y-mods-state-changed",
                    G_CALLBACK (proxy_kbd_a11y_mods_state_changed), seat);
  g_signal_connect (seat->impl, "touch-mode",
                    G_CALLBACK (proxy_touch_mode_changed), seat);
  g_signal_connect (seat->impl, "bell",
                    G_CALLBACK (proxy_bell), seat);
  g_signal_connect (seat->impl, "mods-state-changed",
                    G_CALLBACK (proxy_mods_state_changed), seat);

  seat->core_pointer = meta_seat_impl_get_pointer (seat->impl);
  seat->core_keyboard = meta_seat_impl_get_keyboard (seat->impl);

  meta_seat_native_set_keyboard_map (seat, "us", "", "", DEFAULT_XKB_MODEL);

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

  g_clear_pointer (&seat->xkb_keymap, xkb_keymap_unref);
  g_clear_object (&seat->core_pointer);
  g_clear_object (&seat->core_keyboard);
  g_clear_pointer (&seat->impl, meta_seat_impl_destroy);
  g_list_free_full (g_steal_pointer (&seat->devices), g_object_unref);
  g_clear_pointer (&seat->reserved_virtual_slots, g_hash_table_destroy);
  g_clear_pointer (&seat->tablet_cursors, g_hash_table_unref);
  g_clear_object (&seat->cursor_renderer);

  g_clear_pointer (&seat->seat_id, g_free);

  G_OBJECT_CLASS (meta_seat_native_parent_class)->dispose (object);
}

static ClutterInputDevice *
meta_seat_native_get_pointer (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return seat_native->core_pointer;
}

static ClutterInputDevice *
meta_seat_native_get_keyboard (ClutterSeat *seat)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return seat_native->core_keyboard;
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
    seat_native->keymap = meta_seat_impl_get_keymap (seat_native->impl);

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
meta_seat_native_query_state (ClutterSeat          *seat,
                              ClutterInputDevice   *device,
                              ClutterEventSequence *sequence,
                              graphene_point_t     *coords,
                              ClutterModifierType  *modifiers)
{
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);

  return meta_seat_impl_query_state (seat_native->impl, device, sequence,
                                     coords, modifiers);
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

  seat_class->get_pointer = meta_seat_native_get_pointer;
  seat_class->get_keyboard = meta_seat_native_get_keyboard;
  seat_class->peek_devices = meta_seat_native_peek_devices;
  seat_class->bell_notify = meta_seat_native_bell_notify;
  seat_class->get_keymap = meta_seat_native_get_keymap;
  seat_class->create_virtual_device = meta_seat_native_create_virtual_device;
  seat_class->get_supported_virtual_device_types = meta_seat_native_get_supported_virtual_device_types;
  seat_class->warp_pointer = meta_seat_native_warp_pointer;
  seat_class->init_pointer_position = meta_seat_native_init_pointer_position;
  seat_class->handle_event_post = meta_seat_native_handle_event_post;
  seat_class->query_state = meta_seat_native_query_state;

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

static struct xkb_keymap *
create_keymap (const char *layouts,
               const char *variants,
               const char *options,
               const char *model)
{
  struct xkb_rule_names names;
  struct xkb_keymap *keymap;
  struct xkb_context *context;

  names.rules = DEFAULT_XKB_RULES_FILE;
  names.model = model;
  names.layout = layouts;
  names.variant = variants;
  names.options = options;

  context = meta_create_xkb_context ();
  keymap = xkb_keymap_new_from_names (context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  return keymap;
}

/**
 * meta_seat_native_set_keyboard_map: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @keymap: the new keymap
 *
 * Instructs @evdev to use the specified keyboard map. This will cause
 * the backend to drop the state and create a new one with the new
 * map. To avoid state being lost, callers should ensure that no key
 * is pressed when calling this function.
 */
void
meta_seat_native_set_keyboard_map (MetaSeatNative *seat,
                                   const char     *layouts,
                                   const char     *variants,
                                   const char     *options,
                                   const char     *model)
{
  struct xkb_keymap *keymap, *impl_keymap;

  keymap = create_keymap (layouts, variants, options, model);
  impl_keymap = create_keymap (layouts, variants, options, model);

  if (keymap == NULL)
    {
      g_warning ("Unable to load configured keymap: rules=%s, model=%s, layout=%s, variant=%s, options=%s",
                 DEFAULT_XKB_RULES_FILE, model, layouts,
                 variants, options);
      return;
    }

  if (seat->xkb_keymap)
    xkb_keymap_unref (seat->xkb_keymap);
  seat->xkb_keymap = keymap;

  meta_seat_impl_set_keyboard_map (seat->impl, impl_keymap);
  xkb_keymap_unref (impl_keymap);
}

/**
 * meta_seat_native_get_keyboard_map: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 *
 * Retrieves the #xkb_keymap in use by the evdev backend.
 *
 * Return value: the #xkb_keymap.
 */
struct xkb_keymap *
meta_seat_native_get_keyboard_map (MetaSeatNative *seat)
{
  g_return_val_if_fail (META_IS_SEAT_NATIVE (seat), NULL);

  return seat->xkb_keymap;
}

/**
 * meta_seat_native_set_keyboard_layout_index: (skip)
 * @seat: the #ClutterSeat created by the evdev backend
 * @idx: the xkb layout index to set
 *
 * Sets the xkb layout index on the backend's #xkb_state .
 */
void
meta_seat_native_set_keyboard_layout_index (MetaSeatNative     *seat,
                                            xkb_layout_index_t  idx)
{
  g_return_if_fail (META_IS_SEAT_NATIVE (seat));

  seat->xkb_layout_index = idx;
  meta_seat_impl_set_keyboard_layout_index (seat->impl, idx);
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
meta_seat_native_maybe_ensure_cursor_renderer (MetaSeatNative     *seat_native,
                                               ClutterInputDevice *device)
{
  if (device == seat_native->core_pointer)
    {
      if (!seat_native->cursor_renderer)
        {
          MetaCursorRendererNative *cursor_renderer_native;

          cursor_renderer_native =
            meta_cursor_renderer_native_new (seat_native->backend,
                                             seat_native->core_pointer);
          seat_native->cursor_renderer =
            META_CURSOR_RENDERER (cursor_renderer_native);
        }

      return seat_native->cursor_renderer;
    }

  if (clutter_input_device_get_device_type (device) == CLUTTER_TABLET_DEVICE)
    {
      MetaCursorRenderer *cursor_renderer = NULL;

      if (!seat_native->tablet_cursors)
        {
          seat_native->tablet_cursors =
            g_hash_table_new_full (NULL, NULL, NULL,
                                   g_object_unref);
        }
      else
        {
          cursor_renderer = g_hash_table_lookup (seat_native->tablet_cursors,
                                                 device);
        }

      if (!cursor_renderer)
        {
          cursor_renderer = meta_cursor_renderer_new (seat_native->backend,
                                                      device);
          g_hash_table_insert (seat_native->tablet_cursors,
                               device, cursor_renderer);
        }

      return cursor_renderer;
    }

  return NULL;
}

void
meta_seat_native_set_viewports (MetaSeatNative   *seat,
                                MetaViewportInfo *viewports)
{
  meta_seat_impl_set_viewports (seat->impl, viewports);
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
