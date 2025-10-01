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

enum
{
  KEYMAP_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

enum
{
  PROP_0,
  PROP_SEAT_IMPL,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _MetaKeymapNative MetaKeymapNative;

struct _MetaKeymapNative
{
  ClutterKeymap parent_instance;

  struct {
    MetaSeatImpl *seat_impl;
    struct xkb_keymap *keymap;
  } impl;
};

typedef struct
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  xkb_layout_index_t effective_layout_group;
} ModifierState;

G_DEFINE_TYPE (MetaKeymapNative, meta_keymap_native,
               CLUTTER_TYPE_KEYMAP)

static void
meta_keymap_native_finalize (GObject *object)
{
  MetaKeymapNative *keymap = META_KEYMAP_NATIVE (object);

  xkb_keymap_unref (keymap->impl.keymap);

  G_OBJECT_CLASS (meta_keymap_native_parent_class)->finalize (object);
}

static void
meta_keymap_native_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaKeymapNative *keymap_native = META_KEYMAP_NATIVE (object);

  switch (prop_id)
    {
    case PROP_SEAT_IMPL:
      keymap_native->impl.seat_impl = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
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
  object_class->set_property = meta_keymap_native_set_property;

  keymap_class->get_direction = meta_keymap_native_get_direction;

  props[PROP_SEAT_IMPL] =
    g_param_spec_object ("seat-impl", NULL, NULL,
                         META_TYPE_SEAT_IMPL,
                         G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  signals[KEYMAP_CHANGED] =
    g_signal_new ("keymap-changed",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_FIRST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1,
                 META_TYPE_KEYMAP_DESCRIPTION);
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

static ModifierState
calculate_modifier_state (struct xkb_state *xkb_state)
{
  return (ModifierState) {
    .depressed_mods =
      xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_DEPRESSED),
    .latched_mods =
      xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LATCHED),
    .locked_mods =
      xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED),
    .effective_layout_group =
      xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_EFFECTIVE),
  };
}

static gboolean
update_state_from_modifier_state (MetaKeymapNative *keymap_native,
                                  ModifierState    *modifier_state,
                                  gboolean          emit_signal)
{
  gboolean caps_lock_state;
  gboolean num_lock_state;

  num_lock_state =
    !!((modifier_state->latched_mods | modifier_state->locked_mods) &
       (1 << xkb_keymap_mod_get_index (keymap_native->impl.keymap,
                                       XKB_MOD_NAME_NUM)));
  caps_lock_state =
    !!((modifier_state->latched_mods | modifier_state->locked_mods) &
       (1 << xkb_keymap_mod_get_index (keymap_native->impl.keymap,
                                       XKB_MOD_NAME_CAPS)));

  return clutter_keymap_update_state (CLUTTER_KEYMAP (keymap_native),
                                      caps_lock_state,
                                      num_lock_state,
                                      modifier_state->effective_layout_group,
                                      modifier_state->depressed_mods,
                                      modifier_state->latched_mods,
                                      modifier_state->locked_mods,
                                      emit_signal);
}

typedef struct
{
  MetaKeymapNative *keymap_native;

  MetaKeymapDescription *keymap_description;

  ModifierState modifier_state;

  GStrv display_names;
  GStrv short_names;
} UpdateKeymapData;

static void
update_keymap_data_free (gpointer user_data)
{
  UpdateKeymapData *data = user_data;

  meta_keymap_description_unref (data->keymap_description);
  g_strfreev (data->display_names);
  g_strfreev (data->short_names);
  g_free (data);
}

static gboolean
update_keymap_in_main (gpointer user_data)
{
  UpdateKeymapData *data = user_data;
  MetaKeymapNative *keymap_native = data->keymap_native;
  gboolean state_changed;

  clutter_keymap_update_keymap_names (CLUTTER_KEYMAP (keymap_native),
                                      g_steal_pointer (&data->display_names),
                                      g_steal_pointer (&data->short_names));

  state_changed = update_state_from_modifier_state (keymap_native,
                                                    &data->modifier_state,
                                                    FALSE);

  g_signal_emit (keymap_native, signals[KEYMAP_CHANGED], 0,
                 data->keymap_description);
  if (state_changed)
    g_signal_emit_by_name (keymap_native, "state-changed");

  return G_SOURCE_REMOVE;
}

void
meta_keymap_native_set_keyboard_map_in_impl (MetaKeymapNative      *keymap,
                                             MetaSeatImpl          *seat_impl,
                                             MetaKeymapDescription *keymap_description,
                                             struct xkb_keymap     *xkb_keymap,
                                             struct xkb_state      *xkb_state,
                                             GStrv                  display_names,
                                             GStrv                  short_names)
{
  UpdateKeymapData *data;

  g_return_if_fail (xkb_keymap != NULL);

  g_clear_pointer (&keymap->impl.keymap, xkb_keymap_unref);
  keymap->impl.keymap = xkb_keymap_ref (xkb_keymap);

  data = g_new0 (UpdateKeymapData, 1);
  data->keymap_native = keymap;
  data->modifier_state = calculate_modifier_state (xkb_state);
  data->keymap_description = meta_keymap_description_ref (keymap_description);
  data->display_names = g_steal_pointer (&display_names);
  data->short_names = g_steal_pointer (&short_names);

  meta_seat_impl_queue_main_thread_idle (seat_impl,
                                         update_keymap_in_main,
                                         data, update_keymap_data_free);
}

struct xkb_keymap *
meta_keymap_native_get_keyboard_map_in_impl (MetaKeymapNative *keymap)
{
  return keymap->impl.keymap;
}

typedef struct
{
  MetaKeymapNative *keymap_native;

  ModifierState modifier_state;
} UpdateLockedModifierStateData;

static gboolean
update_state_in_main (gpointer user_data)
{
  UpdateLockedModifierStateData *data = user_data;
  MetaKeymapNative *keymap_native = data->keymap_native;

  update_state_from_modifier_state (keymap_native, &data->modifier_state, TRUE);

  return G_SOURCE_REMOVE;
}

void
meta_keymap_native_update_in_impl (MetaKeymapNative *keymap_native,
                                   struct xkb_state *xkb_state)
{
  UpdateLockedModifierStateData *data;

  data = g_new0 (UpdateLockedModifierStateData, 1);
  data->keymap_native = keymap_native;
  data->modifier_state = calculate_modifier_state (xkb_state);

  meta_seat_impl_queue_main_thread_idle (keymap_native->impl.seat_impl,
                                         update_state_in_main,
                                         data, g_free);
}

MetaKeymapNative *
meta_keymap_native_new (MetaSeatImpl *seat_impl)
{
  return g_object_new (META_TYPE_KEYMAP_NATIVE,
                       "seat-impl", seat_impl,
                       NULL);
}
