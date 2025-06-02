/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/meta-keymap-utils.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-seat-impl.h"
#include "backends/native/meta-seat-native.h"
#include "clutter/clutter-keymap-private.h"

static const char *option_xkb_layout = "us";
static const char *option_xkb_variant = "";
static const char *option_xkb_options = "";

typedef struct _MetaKeymapNative MetaKeymapNative;

struct _MetaKeymapNative
{
  ClutterKeymap parent_instance;

  struct {
    struct xkb_keymap *keymap;
  } impl;
};

G_DEFINE_TYPE (MetaKeymapNative, meta_keymap_native,
               CLUTTER_TYPE_KEYMAP)

static void
meta_keymap_native_finalize (GObject *object)
{
  MetaKeymapNative *keymap = META_KEYMAP_NATIVE (object);

  xkb_keymap_unref (keymap->impl.keymap);

  G_OBJECT_CLASS (meta_keymap_native_parent_class)->finalize (object);
}

static ClutterTextDirection
meta_keymap_native_get_direction (ClutterKeymap *keymap)
{
  return CLUTTER_TEXT_DIRECTION_DEFAULT;
}

static void
meta_keymap_native_class_init (MetaKeymapNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterKeymapClass *keymap_class = CLUTTER_KEYMAP_CLASS (klass);

  object_class->finalize = meta_keymap_native_finalize;

  keymap_class->get_direction = meta_keymap_native_get_direction;
}

static void
meta_keymap_native_init (MetaKeymapNative *keymap)
{
  struct xkb_context *ctx;
  struct xkb_rule_names names;

  names.rules = "evdev";
  names.model = "pc105";
  names.layout = option_xkb_layout;
  names.variant = option_xkb_variant;
  names.options = option_xkb_options;

  ctx = meta_create_xkb_context ();
  g_assert (ctx);
  keymap->impl.keymap = xkb_keymap_new_from_names (ctx, &names, 0);
  xkb_context_unref (ctx);
}

void
meta_keymap_native_set_keyboard_map_in_impl (MetaKeymapNative  *keymap,
                                             struct xkb_keymap *xkb_keymap)
{
  g_return_if_fail (xkb_keymap != NULL);

  g_clear_pointer (&keymap->impl.keymap, xkb_keymap_unref);
  keymap->impl.keymap = xkb_keymap_ref (xkb_keymap);
}

struct xkb_keymap *
meta_keymap_native_get_keyboard_map_in_impl (MetaKeymapNative *keymap)
{
  return keymap->impl.keymap;
}

typedef struct
{
  MetaKeymapNative *keymap_native;

  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  xkb_layout_index_t locked_layout_group;
} UpdateLockedModifierStateData;

static gboolean
update_state_in_main (gpointer user_data)
{
  UpdateLockedModifierStateData *data = user_data;
  MetaKeymapNative *keymap_native = data->keymap_native;
  gboolean caps_lock_state;
  gboolean num_lock_state;

  num_lock_state =
    !!((data->latched_mods | data->locked_mods) &
       (1 << xkb_keymap_mod_get_index (keymap_native->impl.keymap,
                                       XKB_MOD_NAME_NUM)));
  caps_lock_state =
    !!((data->latched_mods | data->locked_mods) &
       (1 << xkb_keymap_mod_get_index (keymap_native->impl.keymap,
                                       XKB_MOD_NAME_CAPS)));

  clutter_keymap_update_state (CLUTTER_KEYMAP (keymap_native),
                               caps_lock_state,
                               num_lock_state,
                               data->locked_layout_group,
                               data->depressed_mods,
                               data->latched_mods,
                               data->locked_mods);

  return G_SOURCE_REMOVE;
}

void
meta_keymap_native_update_in_impl (MetaKeymapNative *keymap_native,
                                   MetaSeatImpl     *seat_impl,
                                   struct xkb_state *xkb_state)
{
  UpdateLockedModifierStateData *data;

  data = g_new0 (UpdateLockedModifierStateData, 1);
  data->keymap_native = keymap_native;

  data->depressed_mods =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED);
  data->latched_mods =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED);
  data->locked_mods =
    xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED);

  data->locked_layout_group =
    xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_LOCKED);

  meta_seat_impl_queue_main_thread_idle (seat_impl,
                                         update_state_in_main,
                                         data, g_free);
}
