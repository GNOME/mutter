/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter Keybindings */
/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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
 */

/**
 * MetaKeybinding:
 *
 * Key bindings
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-keymap-utils.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-input-device-x11.h"
#include "compositor/compositor-private.h"
#include "core/frame.h"
#include "core/keybindings-private.h"
#include "core/meta-accel-parse.h"
#include "core/meta-workspace-manager-private.h"
#include "core/workspace-private.h"
#include "meta/compositor.h"
#include "meta/prefs.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#ifdef __linux__
#include <linux/input.h>
#elif !defined KEY_GRAVE
#define KEY_GRAVE 0x29 /* assume the use of xf86-input-keyboard */
#endif

#define SCHEMA_COMMON_KEYBINDINGS "org.gnome.desktop.wm.keybindings"
#define SCHEMA_MUTTER_KEYBINDINGS "org.gnome.mutter.keybindings"
#define SCHEMA_MUTTER_WAYLAND_KEYBINDINGS "org.gnome.mutter.wayland.keybindings"

#define META_KEY_BINDING_PRIMARY_LAYOUT 0
#define META_KEY_BINDING_SECONDARY_LAYOUT 1

/* Only for special modifier keys */
#define IGNORED_MODIFIERS (CLUTTER_LOCK_MASK |          \
                           CLUTTER_MOD2_MASK |          \
                           CLUTTER_BUTTON1_MASK |       \
                           CLUTTER_BUTTON2_MASK |       \
                           CLUTTER_BUTTON3_MASK |       \
                           CLUTTER_BUTTON4_MASK |       \
                           CLUTTER_BUTTON5_MASK)

static gboolean add_builtin_keybinding (MetaDisplay          *display,
                                        const char           *name,
                                        GSettings            *settings,
                                        MetaKeyBindingFlags   flags,
                                        MetaKeyBindingAction  action,
                                        MetaKeyHandlerFunc    handler,
                                        int                   handler_arg);

static void
resolved_key_combo_reset (MetaResolvedKeyCombo *resolved_combo)
{
  g_free (resolved_combo->keycodes);
  resolved_combo->len = 0;
  resolved_combo->keycodes = NULL;
}

static void
resolved_key_combo_copy (MetaResolvedKeyCombo *from,
                         MetaResolvedKeyCombo *to)
{
  to->len = from->len;
  to->keycodes = g_memdup2 (from->keycodes,
                            from->len * sizeof (xkb_keycode_t));
}

static gboolean
resolved_key_combo_has_keycode (MetaResolvedKeyCombo *resolved_combo,
                                int                   keycode)
{
  int i;

  for (i = 0; i < resolved_combo->len; i++)
    if ((int) resolved_combo->keycodes[i] == keycode)
      return TRUE;

  return FALSE;
}

static gboolean
resolved_key_combo_intersect (MetaResolvedKeyCombo *a,
                              MetaResolvedKeyCombo *b)
{
  int i;

  for (i = 0; i < a->len; i++)
    if (resolved_key_combo_has_keycode (b, a->keycodes[i]))
      return TRUE;

  return FALSE;
}

static void
meta_key_binding_free (MetaKeyBinding *binding)
{
  resolved_key_combo_reset (&binding->resolved_combo);
  g_free (binding);
}

static MetaKeyBinding *
meta_key_binding_copy (MetaKeyBinding *binding)
{
  MetaKeyBinding *clone = g_memdup2 (binding, sizeof (MetaKeyBinding));
  resolved_key_combo_copy (&binding->resolved_combo,
                           &clone->resolved_combo);
  return clone;
}

G_DEFINE_BOXED_TYPE(MetaKeyBinding,
                    meta_key_binding,
                    meta_key_binding_copy,
                    meta_key_binding_free)

const char *
meta_key_binding_get_name (MetaKeyBinding *binding)
{
  return binding->name;
}

ClutterModifierType
meta_key_binding_get_modifiers (MetaKeyBinding *binding)
{
  return binding->combo.modifiers;
}

gboolean
meta_key_binding_is_reversed (MetaKeyBinding *binding)
{
  return (binding->handler->flags & META_KEY_BINDING_IS_REVERSED) != 0;
}

guint
meta_key_binding_get_mask (MetaKeyBinding *binding)
{
  return binding->resolved_combo.mask;
}

gboolean
meta_key_binding_is_builtin (MetaKeyBinding *binding)
{
  return binding->handler->flags & META_KEY_BINDING_BUILTIN;
}

/* These can't be bound to anything, but they are used to handle
 * various other events.  TODO: Possibly we should include them as event
 * handler functions and have some kind of flag to say they're unbindable.
 */

static void maybe_update_locate_pointer_keygrab (MetaDisplay *display,
                                                 gboolean     grab);

static GHashTable *key_handlers;
static GHashTable *external_grabs;

#define HANDLER(name) g_hash_table_lookup (key_handlers, (name))

static void
key_handler_free (MetaKeyHandler *handler)
{
  g_free (handler->name);
  if (handler->user_data_free_func && handler->user_data)
    handler->user_data_free_func (handler->user_data);
  g_free (handler);
}

typedef struct _MetaKeyGrab MetaKeyGrab;
struct _MetaKeyGrab {
  char *name;
  guint action;
  MetaKeyCombo combo;
  gint flags;
};

static void
meta_key_grab_free (MetaKeyGrab *grab)
{
  g_free (grab->name);
  g_free (grab);
}

static guint32
key_combo_key (MetaResolvedKeyCombo *resolved_combo,
               int                   i)
{
  /* On X, keycodes are only 8 bits while libxkbcommon supports 32 bit
     keycodes, but since we're using the same XKB keymaps that X uses,
     we won't find keycodes bigger than 8 bits in practice. The bits
     that mutter cares about in the modifier mask are also all in the
     lower 8 bits both on X and clutter key events. This means that we
     can use a 32 bit integer to safely concatenate both keycode and
     mask and thus making it easy to use them as an index in a
     GHashTable. */
  guint32 key = resolved_combo->keycodes[i] & 0xffff;
  return (key << 16) | (resolved_combo->mask & 0xffff);
}

static void
reload_modmap (MetaKeyBindingManager *keys)
{
  struct xkb_keymap *keymap = meta_backend_get_keymap (keys->backend);
  struct xkb_state *scratch_state;
  xkb_mod_mask_t scroll_lock_mask;
  xkb_mod_mask_t dummy_mask;

  /* Modifiers to find. */
  struct {
    const char *name;
    xkb_mod_mask_t *mask_p;
    xkb_mod_mask_t *virtual_mask_p;
  } mods[] = {
    { "ScrollLock", &scroll_lock_mask, &dummy_mask },
    { "Meta",       &keys->meta_mask,  &keys->virtual_meta_mask },
    { "Hyper",      &keys->hyper_mask, &keys->virtual_hyper_mask },
    { "Super",      &keys->super_mask, &keys->virtual_super_mask },
  };

  scratch_state = xkb_state_new (keymap);

  gsize i;
  for (i = 0; i < G_N_ELEMENTS (mods); i++)
    {
      xkb_mod_mask_t *mask_p = mods[i].mask_p;
      xkb_mod_mask_t *virtual_mask_p = mods[i].virtual_mask_p;
      xkb_mod_index_t idx = xkb_keymap_mod_get_index (keymap, mods[i].name);

      if (idx != XKB_MOD_INVALID)
        {
          xkb_mod_mask_t vmodmask = (1 << idx);
          xkb_state_update_mask (scratch_state, vmodmask, 0, 0, 0, 0, 0);
          *mask_p = xkb_state_serialize_mods (scratch_state, XKB_STATE_MODS_DEPRESSED) & ~vmodmask;
          *virtual_mask_p = vmodmask;
        }
      else
        {
          *mask_p = 0;
          *virtual_mask_p = 0;
        }
    }

  xkb_state_unref (scratch_state);

  keys->ignored_modifier_mask = (scroll_lock_mask | Mod2Mask | LockMask);

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Ignoring modmask 0x%x scroll lock 0x%x hyper 0x%x super 0x%x meta 0x%x",
              keys->ignored_modifier_mask,
              scroll_lock_mask,
              keys->hyper_mask,
              keys->super_mask,
              keys->meta_mask);
}

static gboolean
is_keycode_for_keysym (struct xkb_keymap *keymap,
                       xkb_layout_index_t layout,
                       xkb_level_index_t  level,
                       xkb_keycode_t      keycode,
                       xkb_keysym_t       keysym)
{
  const xkb_keysym_t *syms;
  int num_syms, k;

  num_syms = xkb_keymap_key_get_syms_by_level (keymap, keycode, layout, level, &syms);
  for (k = 0; k < num_syms; k++)
    {
      if (syms[k] == keysym)
        return TRUE;
    }

  return FALSE;
}

typedef struct
{
  GArray *keycodes;
  xkb_keysym_t keysym;
  xkb_layout_index_t layout;
  xkb_level_index_t level;
} FindKeysymData;

static void
get_keycodes_for_keysym_iter (struct xkb_keymap *keymap,
                              xkb_keycode_t      keycode,
                              void              *data)
{
  FindKeysymData *search_data = data;
  GArray *keycodes = search_data->keycodes;
  xkb_keysym_t keysym = search_data->keysym;
  xkb_layout_index_t layout = search_data->layout;
  xkb_level_index_t level = search_data->level;

  if (is_keycode_for_keysym (keymap, layout, level, keycode, keysym))
    {
      guint i;
      gboolean missing = TRUE;

      /* duplicate keycode detection */
      for (i = 0; i < keycodes->len; i++)
        if (g_array_index (keycodes, xkb_keysym_t, i) == keycode)
          {
            missing = FALSE;
            break;
          }

      if (missing)
        g_array_append_val (keycodes, keycode);
    }
}

static void
add_keysym_keycodes_from_layout (int                           keysym,
                                 MetaKeyBindingKeyboardLayout *layout,
                                 GArray                       *keycodes)
{
  xkb_level_index_t layout_level;

  for (layout_level = 0;
       layout_level < layout->n_levels && keycodes->len == 0;
       layout_level++)
    {
      FindKeysymData search_data = (FindKeysymData) {
        .keycodes = keycodes,
        .keysym = keysym,
        .layout = layout->index,
        .level = layout_level
      };
      xkb_keymap_key_for_each (layout->keymap,
                               get_keycodes_for_keysym_iter,
                               &search_data);
    }
}

/* Original code from gdk_x11_keymap_get_entries_for_keyval() in
 * gdkkeys-x11.c */
static void
get_keycodes_for_keysym (MetaKeyBindingManager  *keys,
                         int                     keysym,
                         MetaResolvedKeyCombo   *resolved_combo)
{
  unsigned int i;
  GArray *keycodes;
  int keycode;

  keycodes = g_array_new (FALSE, FALSE, sizeof (xkb_keysym_t));

  /* Special-case: Fake mutter keysym */
  if (keysym == META_KEY_ABOVE_TAB)
    {
      keycode = KEY_GRAVE + 8;
      g_array_append_val (keycodes, keycode);
      goto out;
    }

  for (i = 0; i < G_N_ELEMENTS (keys->active_layouts); i++)
    {
      MetaKeyBindingKeyboardLayout *layout = &keys->active_layouts[i];

      if (!layout->keymap)
        continue;

      add_keysym_keycodes_from_layout (keysym, layout, keycodes);
    }

 out:
  resolved_combo->len = keycodes->len;
  resolved_combo->keycodes =
    (xkb_keycode_t *) g_array_free (keycodes,
                                    keycodes->len == 0 ? TRUE : FALSE);
}

typedef struct _CalculateLayoutLevelsState
{
  struct xkb_keymap *keymap;
  xkb_layout_index_t layout_index;

  xkb_level_index_t out_n_levels;
} CalculateLayoutLevelState;

static void
calculate_n_layout_levels_iter (struct xkb_keymap *keymap,
                                xkb_keycode_t      keycode,
                                void              *data)
{
  CalculateLayoutLevelState *state = data;
  xkb_level_index_t n_levels;

  n_levels = xkb_keymap_num_levels_for_key (keymap,
                                            keycode,
                                            state->layout_index);

  state->out_n_levels = MAX (n_levels, state->out_n_levels);
}

static xkb_level_index_t
calculate_n_layout_levels (struct xkb_keymap *keymap,
                           xkb_layout_index_t layout_index)

{
  CalculateLayoutLevelState state = {
    .keymap = keymap,
    .layout_index = layout_index,

    .out_n_levels = 0
  };

  xkb_keymap_key_for_each (keymap, calculate_n_layout_levels_iter, &state);

  return state.out_n_levels;
}

static void
reload_iso_next_group_combos (MetaKeyBindingManager *keys)
{
  const char *iso_next_group_option;
  int i;

  for (i = 0; i < keys->n_iso_next_group_combos; i++)
    resolved_key_combo_reset (&keys->iso_next_group_combo[i]);

  keys->n_iso_next_group_combos = 0;

  iso_next_group_option = meta_prefs_get_iso_next_group_option ();
  if (iso_next_group_option == NULL)
    return;

  get_keycodes_for_keysym (keys, XKB_KEY_ISO_Next_Group, keys->iso_next_group_combo);

  if (keys->iso_next_group_combo[0].len == 0)
    return;

  keys->n_iso_next_group_combos = 1;

  if (g_str_equal (iso_next_group_option, "toggle") ||
      g_str_equal (iso_next_group_option, "lalt_toggle") ||
      g_str_equal (iso_next_group_option, "lwin_toggle") ||
      g_str_equal (iso_next_group_option, "rwin_toggle") ||
      g_str_equal (iso_next_group_option, "lshift_toggle") ||
      g_str_equal (iso_next_group_option, "rshift_toggle") ||
      g_str_equal (iso_next_group_option, "lctrl_toggle") ||
      g_str_equal (iso_next_group_option, "rctrl_toggle") ||
      g_str_equal (iso_next_group_option, "sclk_toggle") ||
      g_str_equal (iso_next_group_option, "menu_toggle") ||
      g_str_equal (iso_next_group_option, "caps_toggle"))
    {
      keys->iso_next_group_combo[0].mask = 0;
    }
  else if (g_str_equal (iso_next_group_option, "shift_caps_toggle") ||
           g_str_equal (iso_next_group_option, "shifts_toggle"))
    {
      keys->iso_next_group_combo[0].mask = ShiftMask;
    }
  else if (g_str_equal (iso_next_group_option, "alt_caps_toggle") ||
           g_str_equal (iso_next_group_option, "alt_space_toggle"))
    {
      keys->iso_next_group_combo[0].mask = Mod1Mask;
    }
  else if (g_str_equal (iso_next_group_option, "ctrl_shift_toggle") ||
           g_str_equal (iso_next_group_option, "lctrl_lshift_toggle") ||
           g_str_equal (iso_next_group_option, "rctrl_rshift_toggle"))
    {
      resolved_key_combo_copy (&keys->iso_next_group_combo[0],
                               &keys->iso_next_group_combo[1]);

      keys->iso_next_group_combo[0].mask = ShiftMask;
      keys->iso_next_group_combo[1].mask = ControlMask;
      keys->n_iso_next_group_combos = 2;
    }
  else if (g_str_equal (iso_next_group_option, "ctrl_alt_toggle"))
    {
      resolved_key_combo_copy (&keys->iso_next_group_combo[0],
                               &keys->iso_next_group_combo[1]);

      keys->iso_next_group_combo[0].mask = Mod1Mask;
      keys->iso_next_group_combo[1].mask = ControlMask;
      keys->n_iso_next_group_combos = 2;
    }
  else if (g_str_equal (iso_next_group_option, "alt_shift_toggle") ||
           g_str_equal (iso_next_group_option, "lalt_lshift_toggle"))
    {
      resolved_key_combo_copy (&keys->iso_next_group_combo[0],
                               &keys->iso_next_group_combo[1]);

      keys->iso_next_group_combo[0].mask = Mod1Mask;
      keys->iso_next_group_combo[1].mask = ShiftMask;
      keys->n_iso_next_group_combos = 2;
    }
  else
    {
      resolved_key_combo_reset (keys->iso_next_group_combo);
      keys->n_iso_next_group_combos = 0;
    }
}

static void
devirtualize_modifiers (MetaKeyBindingManager *keys,
                        ClutterModifierType    modifiers,
                        unsigned int          *mask)
{
  *mask = 0;

  if (modifiers & CLUTTER_SHIFT_MASK)
    *mask |= ShiftMask;
  if (modifiers & CLUTTER_CONTROL_MASK)
    *mask |= ControlMask;
  if (modifiers & CLUTTER_MOD1_MASK)
    *mask |= Mod1Mask;
  if (modifiers & CLUTTER_META_MASK)
    *mask |= keys->meta_mask;
  if (modifiers & CLUTTER_HYPER_MASK)
    *mask |= keys->hyper_mask;
  if (modifiers & CLUTTER_SUPER_MASK)
    *mask |= keys->super_mask;
  if (modifiers & CLUTTER_MOD2_MASK)
    *mask |= Mod2Mask;
  if (modifiers & CLUTTER_MOD3_MASK)
    *mask |= Mod3Mask;
  if (modifiers & CLUTTER_MOD4_MASK)
    *mask |= Mod4Mask;
  if (modifiers & CLUTTER_MOD5_MASK)
    *mask |= Mod5Mask;
}

static void
index_binding (MetaKeyBindingManager *keys,
               MetaKeyBinding         *binding)
{
  int i;

  for (i = 0; i < binding->resolved_combo.len; i++)
    {
      MetaKeyBinding *existing;
      guint32 index_key;

      index_key = key_combo_key (&binding->resolved_combo, i);

      existing = g_hash_table_lookup (keys->key_bindings_index,
                                      GINT_TO_POINTER (index_key));
      if (existing != NULL)
        {
          /* Overwrite already indexed keycodes only for the first
           * keycode, i.e. we give those primary keycodes precedence
           * over non-first ones. */
          if (i > 0)
            continue;

          meta_warning ("Overwriting existing binding of keysym %x"
                        " with keysym %x (keycode %x).",
                        binding->combo.keysym,
                        existing->combo.keysym,
                        binding->resolved_combo.keycodes[i]);
        }

      g_hash_table_replace (keys->key_bindings_index,
                            GINT_TO_POINTER (index_key), binding);
    }
}

static void
resolve_key_combo (MetaKeyBindingManager *keys,
                   MetaKeyCombo          *combo,
                   MetaResolvedKeyCombo  *resolved_combo)
{

  resolved_key_combo_reset (resolved_combo);

  if (combo->keysym != 0)
    {
      get_keycodes_for_keysym (keys, combo->keysym, resolved_combo);
    }
  else if (combo->keycode != 0)
    {
      resolved_combo->keycodes = g_new0 (xkb_keycode_t, 1);
      resolved_combo->keycodes[0] = combo->keycode;
      resolved_combo->len = 1;
    }

  devirtualize_modifiers (keys, combo->modifiers, &resolved_combo->mask);
}

static void
binding_reload_combos_foreach (gpointer key,
                               gpointer value,
                               gpointer data)
{
  MetaKeyBindingManager *keys = data;
  MetaKeyBinding *binding = value;

  resolve_key_combo (keys, &binding->combo, &binding->resolved_combo);
  index_binding (keys, binding);
}

typedef struct _FindLatinKeysymsState
{
  MetaKeyBindingKeyboardLayout *layout;
  gboolean *required_keysyms_found;
  int n_required_keysyms;
} FindLatinKeysymsState;

static void
find_latin_keysym (struct xkb_keymap *keymap,
                   xkb_keycode_t      key,
                   void              *data)
{
  FindLatinKeysymsState *state = data;
  int n_keysyms, i;
  const xkb_keysym_t *keysyms;

  n_keysyms = xkb_keymap_key_get_syms_by_level (state->layout->keymap,
                                                key,
                                                state->layout->index,
                                                0,
                                                &keysyms);
  for (i = 0; i < n_keysyms; i++)
    {
      xkb_keysym_t keysym = keysyms[i];

      if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
        {
          unsigned int keysym_index = keysym - XKB_KEY_a;

          if (!state->required_keysyms_found[keysym_index])
            {
              state->required_keysyms_found[keysym_index] = TRUE;
              state->n_required_keysyms--;
            }
        }
    }
}

static gboolean
needs_secondary_layout (MetaKeyBindingKeyboardLayout *layout)
{
  gboolean required_keysyms_found[] = {
    FALSE, /* XKB_KEY_a */
    FALSE, /* XKB_KEY_b */
    FALSE, /* XKB_KEY_c */
    FALSE, /* XKB_KEY_d */
    FALSE, /* XKB_KEY_e */
    FALSE, /* XKB_KEY_f */
    FALSE, /* XKB_KEY_g */
    FALSE, /* XKB_KEY_h */
    FALSE, /* XKB_KEY_i */
    FALSE, /* XKB_KEY_j */
    FALSE, /* XKB_KEY_k */
    FALSE, /* XKB_KEY_l */
    FALSE, /* XKB_KEY_m */
    FALSE, /* XKB_KEY_n */
    FALSE, /* XKB_KEY_o */
    FALSE, /* XKB_KEY_p */
    FALSE, /* XKB_KEY_q */
    FALSE, /* XKB_KEY_r */
    FALSE, /* XKB_KEY_s */
    FALSE, /* XKB_KEY_t */
    FALSE, /* XKB_KEY_u */
    FALSE, /* XKB_KEY_v */
    FALSE, /* XKB_KEY_w */
    FALSE, /* XKB_KEY_x */
    FALSE, /* XKB_KEY_y */
    FALSE, /* XKB_KEY_z */
  };
  FindLatinKeysymsState state = {
    .layout = layout,
    .required_keysyms_found = required_keysyms_found,
    .n_required_keysyms = G_N_ELEMENTS (required_keysyms_found),
  };

  xkb_keymap_key_for_each (layout->keymap, find_latin_keysym, &state);

  return state.n_required_keysyms != 0;
}

static void
clear_active_keyboard_layouts (MetaKeyBindingManager *keys)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (keys->active_layouts); i++)
    {
      MetaKeyBindingKeyboardLayout *layout = &keys->active_layouts[i];

      g_clear_pointer (&layout->keymap, xkb_keymap_unref);
      *layout = (MetaKeyBindingKeyboardLayout) { 0 };
    }
}

static MetaKeyBindingKeyboardLayout
create_us_layout (void)
{
  struct xkb_rule_names names;
  struct xkb_keymap *keymap;
  struct xkb_context *context;

  names.rules = DEFAULT_XKB_RULES_FILE;
  names.model = DEFAULT_XKB_MODEL;
  names.layout = "us";
  names.variant = "";
  names.options = "";

  context = meta_create_xkb_context ();
  keymap = xkb_keymap_new_from_names (context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  return (MetaKeyBindingKeyboardLayout) {
    .keymap = keymap,
    .n_levels = calculate_n_layout_levels (keymap, 0),
  };
}

static void
reload_active_keyboard_layouts (MetaKeyBindingManager *keys)
{
  struct xkb_keymap *keymap;
  xkb_layout_index_t layout_index;
  MetaKeyBindingKeyboardLayout primary_layout;

  clear_active_keyboard_layouts (keys);

  keymap = meta_backend_get_keymap (keys->backend);
  layout_index = meta_backend_get_keymap_layout_group (keys->backend);
  primary_layout = (MetaKeyBindingKeyboardLayout) {
    .keymap = xkb_keymap_ref (keymap),
    .index = layout_index,
    .n_levels = calculate_n_layout_levels (keymap, layout_index),
  };

  keys->active_layouts[META_KEY_BINDING_PRIMARY_LAYOUT] = primary_layout;

  if (needs_secondary_layout (&primary_layout))
    {
      MetaKeyBindingKeyboardLayout us_layout;

      us_layout = create_us_layout ();
      keys->active_layouts[META_KEY_BINDING_SECONDARY_LAYOUT] = us_layout;
    }
}

static void
reload_combos (MetaKeyBindingManager *keys)
{
  g_hash_table_remove_all (keys->key_bindings_index);

  reload_active_keyboard_layouts (keys);

  resolve_key_combo (keys,
                     &keys->overlay_key_combo,
                     &keys->overlay_resolved_key_combo);

  resolve_key_combo (keys,
                     &keys->locate_pointer_key_combo,
                     &keys->locate_pointer_resolved_key_combo);

  reload_iso_next_group_combos (keys);

  g_hash_table_foreach (keys->key_bindings, binding_reload_combos_foreach, keys);
}

static void
rebuild_binding_table (MetaKeyBindingManager *keys,
                       GList                  *prefs,
                       GList                  *grabs)
{
  MetaKeyBinding *b;
  GList *p, *g;

  g_hash_table_remove_all (keys->key_bindings);

  p = prefs;
  while (p)
    {
      MetaKeyPref *pref = (MetaKeyPref*)p->data;
      GSList *tmp = pref->combos;

      while (tmp)
        {
          MetaKeyCombo *combo = tmp->data;

          if (combo && (combo->keysym != None || combo->keycode != 0))
            {
              MetaKeyHandler *handler = HANDLER (pref->name);

              b = g_new0 (MetaKeyBinding, 1);
              b->name = pref->name;
              b->handler = handler;
              b->flags = handler->flags;
              b->combo = *combo;

              g_hash_table_add (keys->key_bindings, b);
            }

          tmp = tmp->next;
        }

      p = p->next;
    }

  g = grabs;
  while (g)
    {
      MetaKeyGrab *grab = (MetaKeyGrab*)g->data;
      if (grab->combo.keysym != None || grab->combo.keycode != 0)
        {
          MetaKeyHandler *handler = HANDLER ("external-grab");

          b = g_new0 (MetaKeyBinding, 1);
          b->name = grab->name;
          b->handler = handler;
          b->flags = grab->flags;
          b->combo = grab->combo;

          g_hash_table_add (keys->key_bindings, b);
        }

      g = g->next;
    }

  meta_topic (META_DEBUG_KEYBINDINGS,
              " %d bindings in table",
              g_hash_table_size (keys->key_bindings));
}

static void
rebuild_key_binding_table (MetaKeyBindingManager *keys)
{
  GList *prefs, *grabs;

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Rebuilding key binding table from preferences");

  prefs = meta_prefs_get_keybindings ();
  grabs = g_hash_table_get_values (external_grabs);
  rebuild_binding_table (keys, prefs, grabs);
  g_list_free (prefs);
  g_list_free (grabs);
}

static void
rebuild_special_bindings (MetaKeyBindingManager *keys)
{
  MetaKeyCombo combo;

  meta_prefs_get_overlay_binding (&combo);
  keys->overlay_key_combo = combo;

  meta_prefs_get_locate_pointer_binding (&combo);
  keys->locate_pointer_key_combo = combo;
}

static void
ungrab_key_bindings (MetaDisplay *display)
{
  GSList *windows, *l;

  if (display->x11_display)
    meta_x11_display_ungrab_keys (display->x11_display);

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *w = l->data;
      meta_window_ungrab_keys (w);
    }

  g_slist_free (windows);
}

static void
grab_key_bindings (MetaDisplay *display)
{
  GSList *windows, *l;

  if (display->x11_display)
    meta_x11_display_grab_keys (display->x11_display);

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *w = l->data;
      meta_window_grab_keys (w);
    }

  g_slist_free (windows);
}

static MetaKeyBinding *
get_keybinding (MetaKeyBindingManager *keys,
                MetaResolvedKeyCombo  *resolved_combo)
{
  MetaKeyBinding *binding = NULL;
  int i;

  for (i = 0; i < resolved_combo->len; i++)
    {
      guint32 key;

      key = key_combo_key (resolved_combo, i);
      binding = g_hash_table_lookup (keys->key_bindings_index,
                                     GINT_TO_POINTER (key));

      if (binding != NULL)
        break;
    }

  return binding;
}

static guint
next_dynamic_keybinding_action (void)
{
  static guint num_dynamic_bindings = 0;
  return META_KEYBINDING_ACTION_LAST + (++num_dynamic_bindings);
}

static gboolean
add_keybinding_internal (MetaDisplay          *display,
                         const char           *name,
                         GSettings            *settings,
                         MetaKeyBindingFlags   flags,
                         MetaKeyBindingAction  action,
                         MetaKeyHandlerFunc    func,
                         int                   data,
                         gpointer              user_data,
                         GDestroyNotify        free_data)
{
  MetaKeyHandler *handler;

  if (!meta_prefs_add_keybinding (name, settings, action, flags))
    return FALSE;

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup (name);
  handler->func = func;
  handler->default_func = func;
  handler->data = data;
  handler->flags = flags;
  handler->user_data = user_data;
  handler->user_data_free_func = free_data;

  g_hash_table_insert (key_handlers, g_strdup (name), handler);

  return TRUE;
}

static gboolean
add_builtin_keybinding (MetaDisplay          *display,
                        const char           *name,
                        GSettings            *settings,
                        MetaKeyBindingFlags   flags,
                        MetaKeyBindingAction  action,
                        MetaKeyHandlerFunc    handler,
                        int                   handler_arg)
{
  return add_keybinding_internal (display, name, settings,
                                  flags | META_KEY_BINDING_BUILTIN,
                                  action, handler, handler_arg, NULL, NULL);
}

/**
 * meta_display_add_keybinding:
 * @display: a #MetaDisplay
 * @name: the binding's name
 * @settings: the #GSettings object where @name is stored
 * @flags: flags to specify binding details
 * @handler: function to run when the keybinding is invoked
 * @user_data: the data to pass to @handler
 * @free_data: function to free @user_data
 *
 * Add a keybinding at runtime. The key @name in @schema needs to be of
 * type %G_VARIANT_TYPE_STRING_ARRAY, with each string describing a
 * keybinding in the form of "&lt;Control&gt;a" or "&lt;Shift&gt;&lt;Alt&gt;F1". The parser
 * is fairly liberal and allows lower or upper case, and also abbreviations
 * such as "&lt;Ctl&gt;" and "&lt;Ctrl&gt;". If the key is set to the empty list or a
 * list with a single element of either "" or "disabled", the keybinding is
 * disabled.
 *
 * Use meta_display_remove_keybinding() to remove the binding.
 *
 * Returns: the corresponding keybinding action if the keybinding was
 *          added successfully, otherwise %META_KEYBINDING_ACTION_NONE
 */
guint
meta_display_add_keybinding (MetaDisplay         *display,
                             const char          *name,
                             GSettings           *settings,
                             MetaKeyBindingFlags  flags,
                             MetaKeyHandlerFunc   handler,
                             gpointer             user_data,
                             GDestroyNotify       free_data)
{
  guint new_action = next_dynamic_keybinding_action ();

  if (!add_keybinding_internal (display, name, settings, flags, new_action,
                                handler, 0, user_data, free_data))
    return META_KEYBINDING_ACTION_NONE;

  return new_action;
}

/**
 * meta_display_remove_keybinding:
 * @display: the #MetaDisplay
 * @name: name of the keybinding to remove
 *
 * Remove keybinding @name; the function will fail if @name is not a known
 * keybinding or has not been added with meta_display_add_keybinding().
 *
 * Returns: %TRUE if the binding has been removed successfully,
 *          otherwise %FALSE
 */
gboolean
meta_display_remove_keybinding (MetaDisplay *display,
                                const char  *name)
{
  if (!meta_prefs_remove_keybinding (name))
    return FALSE;

  g_hash_table_remove (key_handlers, name);

  return TRUE;
}

static guint
get_keybinding_action (MetaKeyBindingManager *keys,
                       MetaResolvedKeyCombo  *resolved_combo)
{
  MetaKeyBinding *binding;

  /* This is much more vague than the MetaDisplay::overlay-key signal,
   * which is only emitted if the overlay-key is the only key pressed;
   * as this method is primarily intended for plugins to allow processing
   * of mutter keybindings while holding a grab, the overlay-key-only-pressed
   * tracking is left to the plugin here.
   */
  if (resolved_key_combo_intersect (resolved_combo,
                                    &keys->overlay_resolved_key_combo))
    return META_KEYBINDING_ACTION_OVERLAY_KEY;

  if (resolved_key_combo_intersect (resolved_combo,
                                    &keys->locate_pointer_resolved_key_combo))
    return META_KEYBINDING_ACTION_LOCATE_POINTER_KEY;

  binding = get_keybinding (keys, resolved_combo);
  if (binding)
    {
      MetaKeyGrab *grab = g_hash_table_lookup (external_grabs, binding->name);
      if (grab)
        return grab->action;
      else
        return (guint) meta_prefs_get_keybinding_action (binding->name);
    }
  else
    {
      return META_KEYBINDING_ACTION_NONE;
    }
}

static xkb_mod_mask_t
mask_from_event_params (MetaKeyBindingManager *keys,
                        unsigned long mask)
{
  return mask & 0xff & ~keys->ignored_modifier_mask;
}

/**
 * meta_display_get_keybinding_action:
 * @display: A #MetaDisplay
 * @keycode: Raw keycode
 * @mask: Event mask
 *
 * Get the keybinding action bound to @keycode. Builtin keybindings
 * have a fixed associated #MetaKeyBindingAction, for bindings added
 * dynamically the function will return the keybinding action
 * meta_display_add_keybinding() returns on registration.
 *
 * Returns: The action that should be taken for the given key, or
 * %META_KEYBINDING_ACTION_NONE.
 */
guint
meta_display_get_keybinding_action (MetaDisplay  *display,
                                    unsigned int  keycode,
                                    unsigned long mask)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  xkb_keycode_t code = (xkb_keycode_t) keycode;
  MetaResolvedKeyCombo resolved_combo = { &code, 1 };

  resolved_combo.mask = mask_from_event_params (keys, mask);
  return get_keybinding_action (keys, &resolved_combo);
}

static void
reload_keybindings (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  ungrab_key_bindings (display);

  /* Deciphering the modmap depends on the loaded keysyms to find out
   * what modifiers is Super and so forth, so we need to reload it
   * even when only the keymap changes */
  reload_modmap (keys);

  reload_combos (keys);

  grab_key_bindings (display);
}

static GArray *
calc_grab_modifiers (MetaKeyBindingManager *keys,
                     unsigned int modmask)
{
  unsigned int ignored_mask;
  XIGrabModifiers mods;
  GArray *mods_array = g_array_new (FALSE, TRUE, sizeof (XIGrabModifiers));

  /* The X server crashes if XIAnyModifier gets passed in with any
     other bits. It doesn't make sense to ask for a grab of
     XIAnyModifier plus other bits anyway so we avoid that. */
  if (modmask & XIAnyModifier)
    {
      mods = (XIGrabModifiers) { XIAnyModifier, 0 };
      g_array_append_val (mods_array, mods);
      return mods_array;
    }

  mods = (XIGrabModifiers) { modmask, 0 };
  g_array_append_val (mods_array, mods);

  for (ignored_mask = 1;
       ignored_mask <= keys->ignored_modifier_mask;
       ++ignored_mask)
    {
      if (ignored_mask & keys->ignored_modifier_mask)
        {
          mods = (XIGrabModifiers) { modmask | ignored_mask, 0 };
          g_array_append_val (mods_array, mods);
        }
    }

  return mods_array;
}

static void
meta_change_button_grab (MetaKeyBindingManager *keys,
                         MetaWindow            *window,
                         gboolean               grab,
                         gboolean               sync,
                         int                    button,
                         int                    modmask)
{
  MetaBackendX11 *backend;
  Display *xdisplay;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };
  Window xwindow;
  GArray *mods;

  if (meta_is_wayland_compositor ())
    return;
  if (window->client_type != META_WINDOW_CLIENT_TYPE_X11)
    return;

  backend = META_BACKEND_X11 (keys->backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend);

  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Motion);

  mods = calc_grab_modifiers (keys, modmask);

  mtk_x11_error_trap_push (xdisplay);

  if (window->frame)
    xwindow = window->frame->xwindow;
  else
    xwindow = meta_window_x11_get_xwindow (window);

  /* GrabModeSync means freeze until XAllowEvents */
  if (grab)
    XIGrabButton (xdisplay,
                  META_VIRTUAL_CORE_POINTER_ID,
                  button, xwindow, None,
                  sync ? XIGrabModeSync : XIGrabModeAsync,
                  XIGrabModeAsync, False,
                  &mask, mods->len, (XIGrabModifiers *)mods->data);
  else
    XIUngrabButton (xdisplay,
                    META_VIRTUAL_CORE_POINTER_ID,
                    button, xwindow, mods->len, (XIGrabModifiers *)mods->data);

  XSync (xdisplay, False);

  mtk_x11_error_trap_pop (xdisplay);

  g_array_free (mods, TRUE);
}

ClutterModifierType
meta_display_get_compositor_modifiers (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  return keys->window_grab_modifiers;
}

static void
meta_change_buttons_grab (MetaKeyBindingManager *keys,
                          MetaWindow            *window,
                          gboolean               grab,
                          gboolean               sync,
                          int                    modmask)
{
#define MAX_BUTTON 3

  int i;
  for (i = 1; i <= MAX_BUTTON; i++)
    meta_change_button_grab (keys, window, grab, sync, i, modmask);
}

void
meta_display_grab_window_buttons (MetaDisplay *display,
                                  MetaWindow  *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  /* Grab Alt + button1 for moving window.
   * Grab Alt + button2 for resizing window.
   * Grab Alt + button3 for popping up window menu.
   * Grab Alt + Shift + button1 for snap-moving window.
   */
  meta_verbose ("Grabbing window buttons for %s", window->desc);

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  if (keys->window_grab_modifiers != 0)
    {
      meta_change_buttons_grab (keys, window, TRUE, FALSE,
                                keys->window_grab_modifiers);

      /* In addition to grabbing Alt+Button1 for moving the window,
       * grab Alt+Shift+Button1 for snap-moving the window.  See bug
       * 112478.  Unfortunately, this doesn't work with
       * Shift+Alt+Button1 for some reason; so at least part of the
       * order still matters, which sucks (please FIXME).
       */
      meta_change_button_grab (keys, window,
                               TRUE,
                               FALSE,
                               1, keys->window_grab_modifiers | ShiftMask);
    }
}

void
meta_display_ungrab_window_buttons (MetaDisplay *display,
                                    MetaWindow  *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (keys->window_grab_modifiers == 0)
    return;

  meta_change_buttons_grab (keys, window, FALSE, FALSE,
                            keys->window_grab_modifiers);
}

static void
update_window_grab_modifiers (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  ClutterModifierType virtual_mods;
  unsigned int mods;

  virtual_mods = meta_prefs_get_mouse_button_mods ();
  devirtualize_modifiers (keys, virtual_mods, &mods);

  if (keys->window_grab_modifiers != mods)
    {
      keys->window_grab_modifiers = mods;
      g_object_notify (G_OBJECT (display), "compositor-modifiers");
    }
}

void
meta_display_grab_focus_window_button (MetaDisplay *display,
                                       MetaWindow  *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  /* Grab button 1 for activating unfocused windows */
  meta_verbose ("Grabbing unfocused window buttons for %s", window->desc);

  if (window->have_focus_click_grab)
    {
      meta_verbose (" (well, not grabbing since we already have the grab)");
      return;
    }

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  meta_change_buttons_grab (keys, window, TRUE, TRUE, XIAnyModifier);
  window->have_focus_click_grab = TRUE;
}

void
meta_display_ungrab_focus_window_button (MetaDisplay *display,
                                         MetaWindow  *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  meta_verbose ("Ungrabbing unfocused window buttons for %s", window->desc);

  if (!window->have_focus_click_grab)
    return;

  meta_change_buttons_grab (keys, window, FALSE, FALSE, XIAnyModifier);
  window->have_focus_click_grab = FALSE;
}

static void
prefs_changed_callback (MetaPreference pref,
                        void          *data)
{
  MetaDisplay *display = data;
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  switch (pref)
    {
    case META_PREF_LOCATE_POINTER:
      maybe_update_locate_pointer_keygrab (display,
                                           meta_prefs_is_locate_pointer_enabled());
      break;
    case META_PREF_KEYBINDINGS:
      ungrab_key_bindings (display);
      rebuild_key_binding_table (keys);
      rebuild_special_bindings (keys);
      reload_combos (keys);
      grab_key_bindings (display);
      break;
    case META_PREF_MOUSE_BUTTON_MODS:
      {
        GSList *windows, *l;
        windows = meta_display_list_windows (display, META_LIST_DEFAULT);

        for (l = windows; l; l = l->next)
          {
            MetaWindow *w = l->data;
            meta_display_ungrab_window_buttons (display, w);
          }

        update_window_grab_modifiers (display);

        for (l = windows; l; l = l->next)
          {
            MetaWindow *w = l->data;
            if (w->type != META_WINDOW_DOCK)
              meta_display_grab_window_buttons (display, w);
          }

        g_slist_free (windows);
      }
    default:
      break;
    }
}


void
meta_display_shutdown_keys (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  meta_prefs_remove_listener (prefs_changed_callback, display);

  g_hash_table_destroy (keys->key_bindings_index);
  g_hash_table_destroy (keys->key_bindings);

  clear_active_keyboard_layouts (keys);
}

/* Grab/ungrab, ignoring all annoying modifiers like NumLock etc. */
static void
meta_change_keygrab (MetaKeyBindingManager *keys,
                     Window                 xwindow,
                     gboolean               grab,
                     MetaResolvedKeyCombo  *resolved_combo)
{
  MetaBackendX11 *backend_x11;
  Display *xdisplay;
  GArray *mods;
  int i;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  if (meta_is_wayland_compositor ())
    return;

  backend_x11 = META_BACKEND_X11 (keys->backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  /* Grab keycode/modmask, together with
   * all combinations of ignored modifiers.
   * X provides no better way to do this.
   */

  mods = calc_grab_modifiers (keys, resolved_combo->mask);

  mtk_x11_error_trap_push (xdisplay);

  for (i = 0; i < resolved_combo->len; i++)
    {
      xkb_keycode_t keycode = resolved_combo->keycodes[i];

      meta_topic (META_DEBUG_KEYBINDINGS,
                  "%s keybinding keycode %d mask 0x%x on 0x%lx",
                  grab ? "Grabbing" : "Ungrabbing",
                  keycode, resolved_combo->mask, xwindow);

      if (grab)
        XIGrabKeycode (xdisplay,
                       META_VIRTUAL_CORE_KEYBOARD_ID,
                       keycode, xwindow,
                       XIGrabModeSync, XIGrabModeAsync,
                       False, &mask, mods->len, (XIGrabModifiers *)mods->data);
      else
        XIUngrabKeycode (xdisplay,
                         META_VIRTUAL_CORE_KEYBOARD_ID,
                         keycode, xwindow,
                         mods->len, (XIGrabModifiers *)mods->data);
    }

  XSync (xdisplay, False);

  mtk_x11_error_trap_pop (xdisplay);

  g_array_free (mods, TRUE);
}

typedef struct
{
  MetaKeyBindingManager *keys;
  Window xwindow;
  gboolean only_per_window;
  gboolean grab;
} ChangeKeygrabData;

static void
change_keygrab_foreach (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  ChangeKeygrabData *data = user_data;
  MetaKeyBinding *binding = value;
  gboolean binding_is_per_window = (binding->flags & META_KEY_BINDING_PER_WINDOW) != 0;

  if (data->only_per_window != binding_is_per_window)
    return;

  /* Ignore the key bindings marked as META_KEY_BINDING_NO_AUTO_GRAB,
   * those are handled separately
   */
  if (binding->flags & META_KEY_BINDING_NO_AUTO_GRAB)
    return;

  if (binding->resolved_combo.len == 0)
    return;

  meta_change_keygrab (data->keys, data->xwindow, data->grab, &binding->resolved_combo);
}

static void
change_binding_keygrabs (MetaKeyBindingManager *keys,
                         Window                 xwindow,
                         gboolean               only_per_window,
                         gboolean               grab)
{
  ChangeKeygrabData data;

  data.keys = keys;
  data.xwindow = xwindow;
  data.only_per_window = only_per_window;
  data.grab = grab;

  g_hash_table_foreach (keys->key_bindings, change_keygrab_foreach, &data);
}

static void
maybe_update_locate_pointer_keygrab (MetaDisplay *display,
                                     gboolean     grab)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (!display->x11_display)
    return;

  if (keys->locate_pointer_resolved_key_combo.len != 0)
    meta_change_keygrab (keys, display->x11_display->xroot,
                         (!!grab & !!meta_prefs_is_locate_pointer_enabled()),
                         &keys->locate_pointer_resolved_key_combo);
}

static void
meta_x11_display_change_keygrabs (MetaX11Display *x11_display,
                                  gboolean        grab)
{
  MetaKeyBindingManager *keys = &x11_display->display->key_binding_manager;
  int i;

  if (keys->overlay_resolved_key_combo.len != 0)
    meta_change_keygrab (keys, x11_display->xroot,
                         grab, &keys->overlay_resolved_key_combo);

  maybe_update_locate_pointer_keygrab (x11_display->display, grab);

  for (i = 0; i < keys->n_iso_next_group_combos; i++)
    meta_change_keygrab (keys, x11_display->xroot,
                         grab, &keys->iso_next_group_combo[i]);

  change_binding_keygrabs (keys, x11_display->xroot,
                           FALSE, grab);
}

void
meta_x11_display_grab_keys (MetaX11Display *x11_display)
{
  if (x11_display->keys_grabbed)
    return;

  meta_x11_display_change_keygrabs (x11_display, TRUE);

  x11_display->keys_grabbed = TRUE;
}

void
meta_x11_display_ungrab_keys (MetaX11Display *x11_display)
{
  if (!x11_display->keys_grabbed)
    return;

  meta_x11_display_change_keygrabs (x11_display, FALSE);

  x11_display->keys_grabbed = FALSE;
}

static void
change_window_keygrabs (MetaKeyBindingManager *keys,
                        Window                 xwindow,
                        gboolean               grab)
{
  change_binding_keygrabs (keys, xwindow, TRUE, grab);
}

void
meta_window_grab_keys (MetaWindow  *window)
{
  MetaDisplay *display = window->display;
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (meta_is_wayland_compositor ())
    return;

  if (window->type == META_WINDOW_DOCK
      || window->override_redirect)
    {
      if (window->keys_grabbed)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);
      window->keys_grabbed = FALSE;
      return;
    }

  if (window->keys_grabbed)
    {
      if (window->frame && !window->grab_on_frame)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);
      else if (window->frame == NULL &&
               window->grab_on_frame)
        ; /* continue to regrab on client window */
      else
        return; /* already all good */
    }

  change_window_keygrabs (keys,
                          meta_window_x11_get_toplevel_xwindow (window),
                          TRUE);

  window->keys_grabbed = TRUE;
  window->grab_on_frame = window->frame != NULL;
}

void
meta_window_ungrab_keys (MetaWindow  *window)
{
  if (!meta_is_wayland_compositor () && window->keys_grabbed)
    {
      MetaDisplay *display = window->display;
      MetaKeyBindingManager *keys = &display->key_binding_manager;

      if (window->grab_on_frame &&
          window->frame != NULL)
        change_window_keygrabs (keys, window->frame->xwindow, FALSE);
      else if (!window->grab_on_frame)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);

      window->keys_grabbed = FALSE;
    }
}

static void
handle_external_grab (MetaDisplay           *display,
                      MetaWindow            *window,
                      const ClutterKeyEvent *event,
                      MetaKeyBinding        *binding,
                      gpointer               user_data)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  guint action = get_keybinding_action (keys, &binding->resolved_combo);
  meta_display_accelerator_activate (display, action, event);
}


guint
meta_display_grab_accelerator (MetaDisplay         *display,
                               const char          *accelerator,
                               MetaKeyBindingFlags  flags)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  MetaKeyBinding *binding;
  MetaKeyGrab *grab;
  MetaKeyCombo combo = { 0 };
  MetaResolvedKeyCombo resolved_combo = { NULL, 0 };

  if (!meta_parse_accelerator (accelerator, &combo))
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Failed to parse accelerator");
      meta_warning ("\"%s\" is not a valid accelerator", accelerator);

      return META_KEYBINDING_ACTION_NONE;
    }

  resolve_key_combo (keys, &combo, &resolved_combo);

  if (resolved_combo.len == 0)
    return META_KEYBINDING_ACTION_NONE;

  if (get_keybinding (keys, &resolved_combo))
    {
      resolved_key_combo_reset (&resolved_combo);
      return META_KEYBINDING_ACTION_NONE;
    }

  if (!meta_is_wayland_compositor ())
    {
      meta_change_keygrab (keys, display->x11_display->xroot,
                           TRUE, &resolved_combo);
    }

  grab = g_new0 (MetaKeyGrab, 1);
  grab->action = next_dynamic_keybinding_action ();
  grab->name = meta_external_binding_name_for_action (grab->action);
  grab->combo = combo;
  grab->flags = flags;

  g_hash_table_insert (external_grabs, grab->name, grab);

  binding = g_new0 (MetaKeyBinding, 1);
  binding->name = grab->name;
  binding->handler = HANDLER ("external-grab");
  binding->combo = combo;
  binding->resolved_combo = resolved_combo;
  binding->flags = flags;

  g_hash_table_add (keys->key_bindings, binding);
  index_binding (keys, binding);

  return grab->action;
}

gboolean
meta_display_ungrab_accelerator (MetaDisplay *display,
                                 guint        action)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  MetaKeyBinding *binding;
  MetaKeyGrab *grab;
  g_autofree char *key = NULL;
  MetaResolvedKeyCombo resolved_combo = { NULL, 0 };

  g_return_val_if_fail (action != META_KEYBINDING_ACTION_NONE, FALSE);

  key = meta_external_binding_name_for_action (action);
  grab = g_hash_table_lookup (external_grabs, key);
  if (!grab)
    return FALSE;

  resolve_key_combo (keys, &grab->combo, &resolved_combo);
  binding = get_keybinding (keys, &resolved_combo);
  if (binding)
    {
      int i;

      if (!meta_is_wayland_compositor ())
        {
          meta_change_keygrab (keys, display->x11_display->xroot,
                               FALSE, &binding->resolved_combo);
        }

      for (i = 0; i < binding->resolved_combo.len; i++)
        {
          guint32 index_key = key_combo_key (&binding->resolved_combo, i);
          g_hash_table_remove (keys->key_bindings_index, GINT_TO_POINTER (index_key));
        }

      g_hash_table_remove (keys->key_bindings, binding);
    }

  g_hash_table_remove (external_grabs, key);
  resolved_key_combo_reset (&resolved_combo);

  return TRUE;
}

static gboolean
grab_keyboard (MetaBackend *backend,
               Window       xwindow,
               uint32_t     timestamp,
               int          grab_mode)
{
  MetaBackendX11 *backend_x11;
  Display *xdisplay;
  int grab_status;

  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  if (meta_is_wayland_compositor ())
    return TRUE;

  /* Grab the keyboard, so we get key releases and all key
   * presses
   */

  backend_x11 = META_BACKEND_X11 (backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  /* Strictly, we only need to set grab_mode on the keyboard device
   * while the pointer should always be XIGrabModeAsync. Unfortunately
   * there is a bug in the X server, only fixed (link below) in 1.15,
   * which swaps these arguments for keyboard devices. As such, we set
   * both the device and the paired device mode which works around
   * that bug and also works on fixed X servers.
   *
   * http://cgit.freedesktop.org/xorg/xserver/commit/?id=9003399708936481083424b4ff8f18a16b88b7b3
   */
  grab_status = XIGrabDevice (xdisplay,
                              META_VIRTUAL_CORE_KEYBOARD_ID,
                              xwindow,
                              timestamp,
                              None,
                              grab_mode, grab_mode,
                              False, /* owner_events */
                              &mask);

  return (grab_status == Success);
}

static void
ungrab_keyboard (MetaBackend *backend,
                 uint32_t     timestamp)
{
  MetaBackendX11 *backend_x11;
  Display *xdisplay;

  if (meta_is_wayland_compositor ())
    return;

  backend_x11 = META_BACKEND_X11 (backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  XIUngrabDevice (xdisplay, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);
}

void
meta_display_freeze_keyboard (MetaDisplay *display, guint32 timestamp)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  if (!META_IS_BACKEND_X11 (backend))
    return;

  Window window = meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend));
  grab_keyboard (backend, window, timestamp, XIGrabModeSync);
}

void
meta_display_ungrab_keyboard (MetaDisplay *display, guint32 timestamp)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  ungrab_keyboard (backend, timestamp);
}

void
meta_display_unfreeze_keyboard (MetaDisplay *display, guint32 timestamp)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  if (!META_IS_BACKEND_X11 (backend))
    return;

  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));

  XIAllowEvents (xdisplay, META_VIRTUAL_CORE_KEYBOARD_ID,
                 XIAsyncDevice, timestamp);
  /* We shouldn't need to unfreeze the pointer device here, however we
   * have to, due to the workaround we do in grab_keyboard().
   */
  XIAllowEvents (xdisplay, META_VIRTUAL_CORE_POINTER_ID,
                 XIAsyncDevice, timestamp);
}

static void
invoke_handler (MetaDisplay           *display,
                MetaKeyHandler        *handler,
                MetaWindow            *window,
                const ClutterKeyEvent *event,
                MetaKeyBinding        *binding)
{
  if (handler->func)
    (* handler->func) (display,
                       handler->flags & META_KEY_BINDING_PER_WINDOW ?
                       window : NULL,
                       event,
                       binding,
                       handler->user_data);
  else
    (* handler->default_func) (display,
                               handler->flags & META_KEY_BINDING_PER_WINDOW ?
                               window: NULL,
                               event,
                               binding,
                               NULL);
}

static gboolean
meta_key_binding_has_handler_func (MetaKeyBinding *binding)
{
  return (!!binding->handler->func || !!binding->handler->default_func);
}

static ClutterModifierType
get_modifiers (ClutterEvent *event)
{
  ClutterModifierType pressed, latched;

  clutter_event_get_key_state (event, &pressed, &latched, NULL);

  return pressed | latched;
}

static gboolean
process_event (MetaDisplay          *display,
               MetaWindow           *window,
               ClutterKeyEvent      *event)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  xkb_keycode_t keycode =
    (xkb_keycode_t) clutter_event_get_key_code ((ClutterEvent *) event);
  MetaResolvedKeyCombo resolved_combo = { &keycode, 1 };
  MetaKeyBinding *binding;
  ClutterModifierType modifiers;

  /* we used to have release-based bindings but no longer. */
  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return FALSE;

  modifiers = get_modifiers ((ClutterEvent *) event);
  resolved_combo.mask = mask_from_event_params (keys, modifiers);

  binding = get_keybinding (keys, &resolved_combo);

  if (!binding)
    goto not_found;

  if (!window && binding->flags & META_KEY_BINDING_PER_WINDOW)
    goto not_found;

  if (binding->flags & META_KEY_BINDING_CUSTOM_TRIGGER)
    goto not_found;

  if (binding->handler == NULL)
    meta_bug ("Binding %s has no handler", binding->name);

  if (!meta_key_binding_has_handler_func (binding))
    goto not_found;

  if (display->focus_window &&
      !(binding->handler->flags & META_KEY_BINDING_NON_MASKABLE))
    {
      ClutterInputDevice *source;

      source = clutter_event_get_source_device ((ClutterEvent *) event);
      if (meta_window_shortcuts_inhibited (display->focus_window, source))
        goto not_found;
    }

  /* If the compositor filtered out the keybindings, that
   * means they don't want the binding to trigger, so we do
   * the same thing as if the binding didn't exist. */
  if (meta_compositor_filter_keybinding (display->compositor, binding))
    goto not_found;

  if (clutter_event_get_flags ((ClutterEvent *) event) & CLUTTER_EVENT_FLAG_REPEATED &&
      binding->flags & META_KEY_BINDING_IGNORE_AUTOREPEAT)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Ignore autorepeat for handler %s",
                  binding->name);
      return TRUE;
    }

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Running handler for %s",
              binding->name);

  invoke_handler (display, binding->handler, window, event, binding);

  return TRUE;

 not_found:
  meta_topic (META_DEBUG_KEYBINDINGS,
              "No handler found for this event in this binding table");
  return FALSE;
}

static gboolean
process_special_modifier_key (MetaDisplay          *display,
                              ClutterKeyEvent      *event,
                              MetaWindow           *window,
                              gboolean             *modifier_press_only,
                              MetaResolvedKeyCombo *resolved_key_combo,
                              GFunc                 trigger_callback)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  MetaBackend *backend = keys->backend;
  ClutterInputDevice *device;
  ClutterModifierType modifiers;
  uint32_t time_ms;
  uint32_t hardware_keycode;
  Display *xdisplay;

  if (META_IS_BACKEND_X11 (backend))
    xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  else
    xdisplay = NULL;

  hardware_keycode = clutter_event_get_key_code ((ClutterEvent *) event);
  time_ms = clutter_event_get_time ((ClutterEvent *) event);
  device = clutter_event_get_device ((ClutterEvent *) event);
  modifiers = get_modifiers ((ClutterEvent *) event);

  if (*modifier_press_only)
    {
      if (! resolved_key_combo_has_keycode (resolved_key_combo, hardware_keycode))
        {
          *modifier_press_only = FALSE;

          /* If this is a wayland session, we can avoid the shenanigans
           * about passive grabs below, and let the event continue to
           * be processed through the regular paths.
           */
          if (!xdisplay)
            return FALSE;

          /* OK, the user hit modifier+key rather than pressing and
           * releasing the modifier key alone. We want to handle the key
           * sequence "normally". Unfortunately, using
           * XAllowEvents(..., ReplayKeyboard, ...) doesn't quite
           * work, since global keybindings won't be activated ("this
           * time, however, the function ignores any passive grabs at
           * above (toward the root of) the grab_window of the grab
           * just released.") So, we first explicitly check for one of
           * our global keybindings, and if not found, we then replay
           * the event. Other clients with global grabs will be out of
           * luck.
           */
          if (process_event (display, window, event))
            {
              /* As normally, after we've handled a global key
               * binding, we unfreeze the keyboard but keep the grab
               * (this is important for something like cycling
               * windows */

              if (xdisplay)
                {
                  XIAllowEvents (xdisplay,
                                 meta_input_device_x11_get_device_id (device),
                                 XIAsyncDevice, time_ms);
                }
            }
          else
            {
              /* Replay the event so it gets delivered to our
               * per-window key bindings or to the application */
              if (xdisplay)
                {
                  XIAllowEvents (xdisplay,
                                 meta_input_device_x11_get_device_id (device),
                                 XIReplayDevice, time_ms);
                }
            }
        }
      else if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
        {
          MetaKeyBinding *binding;

          *modifier_press_only = FALSE;

          /* We want to unfreeze events, but keep the grab so that if the user
           * starts typing into the overlay we get all the keys */
          if (xdisplay)
            {
              XIAllowEvents (xdisplay,
                             meta_input_device_x11_get_device_id (device),
                             XIAsyncDevice, time_ms);
            }

          binding = get_keybinding (keys, resolved_key_combo);
          if (binding &&
              meta_compositor_filter_keybinding (display->compositor, binding))
            return TRUE;
          trigger_callback (display, NULL);
        }
      else
        {
          /* In some rare race condition, mutter might not receive the Super_L
           * KeyRelease event because:
           * - the compositor might end the modal mode and call XIUngrabDevice
           *   while the key is still down
           * - passive grabs are only activated on KeyPress and not KeyRelease.
           *
           * In this case, modifier_press_only might be wrong.
           * Mutter still ought to acknowledge events, otherwise the X server
           * will not send the next events.
           *
           * https://bugzilla.gnome.org/show_bug.cgi?id=666101
           */
          if (xdisplay)
            {
              XIAllowEvents (xdisplay,
                             meta_input_device_x11_get_device_id (device),
                             XIAsyncDevice, time_ms);
            }
        }

      return TRUE;
    }
  else if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_PRESS &&
           ((modifiers & ~(IGNORED_MODIFIERS)) & CLUTTER_MODIFIER_MASK) == 0 &&
           resolved_key_combo_has_keycode (resolved_key_combo, hardware_keycode))
    {
      *modifier_press_only = TRUE;
      /* We keep the keyboard frozen - this allows us to use ReplayKeyboard
       * on the next event if it's not the release of the modifier key */
      if (xdisplay)
        {
          XIAllowEvents (xdisplay,
                         meta_input_device_x11_get_device_id (device),
                         XISyncDevice, time_ms);
        }

      return TRUE;
    }
  else
    return FALSE;
}


static gboolean
process_overlay_key (MetaDisplay     *display,
                     ClutterKeyEvent *event,
                     MetaWindow      *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (display->focus_window && !keys->overlay_key_only_pressed)
    {
      ClutterInputDevice *source;

      source = clutter_event_get_source_device ((ClutterEvent *) event);
      if (meta_window_shortcuts_inhibited (display->focus_window, source))
        return FALSE;
    }

  return process_special_modifier_key (display,
                                       event,
                                       window,
                                       &keys->overlay_key_only_pressed,
                                       &keys->overlay_resolved_key_combo,
                                       (GFunc) meta_display_overlay_key_activate);
}

static void
handle_locate_pointer (MetaDisplay *display)
{
  meta_compositor_locate_pointer (display->compositor);
}

static gboolean
process_locate_pointer_key (MetaDisplay     *display,
                            ClutterKeyEvent *event,
                            MetaWindow      *window)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  return process_special_modifier_key (display,
                                       event,
                                       window,
                                       &keys->locate_pointer_key_only_pressed,
                                       &keys->locate_pointer_resolved_key_combo,
                                       (GFunc) handle_locate_pointer);
}

static gboolean
process_iso_next_group (MetaDisplay *display,
                        ClutterKeyEvent *event)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  gboolean activate;
  xkb_keycode_t keycode =
    (xkb_keycode_t) clutter_event_get_key_code ((ClutterEvent *) event);
  ClutterModifierType modifiers;
  xkb_mod_mask_t mask;
  int i, j;

  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return FALSE;

  activate = FALSE;
  modifiers = get_modifiers ((ClutterEvent *) event);
  mask = mask_from_event_params (keys, modifiers);

  for (i = 0; i < keys->n_iso_next_group_combos; ++i)
    {
      for (j = 0; j <  keys->iso_next_group_combo[i].len; ++j)
        {
          if (keycode == keys->iso_next_group_combo[i].keycodes[j] &&
              mask == keys->iso_next_group_combo[i].mask)
            {
              /* If the signal handler returns TRUE the keyboard will
                 remain frozen. It's the signal handler's responsibility
                 to unfreeze it. */
              if (!meta_display_modifiers_accelerator_activate (display))
                meta_display_unfreeze_keyboard (display,
                                                clutter_event_get_time ((ClutterEvent *) event));
              activate = TRUE;
              break;
            }
        }
    }

  return activate;
}

static gboolean
process_key_event (MetaDisplay     *display,
                   MetaWindow      *window,
                   ClutterKeyEvent *event)
{
  if (process_overlay_key (display, event, window))
    return TRUE;

  if (process_locate_pointer_key (display, event, window))
    return FALSE;  /* Continue with the event even if handled */

  if (process_iso_next_group (display, event))
    return TRUE;

  {
    MetaContext *context = meta_display_get_context (display);
    MetaBackend *backend = meta_context_get_backend (context);
    ClutterInputDevice *device;

    if (META_IS_BACKEND_X11 (backend))
      {
        Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
        device = clutter_event_get_device ((ClutterEvent *) event);
        XIAllowEvents (xdisplay,
                       meta_input_device_x11_get_device_id (device),
                       XIAsyncDevice,
                       clutter_event_get_time ((ClutterEvent *) event));
      }
  }

  /* Do the normal keybindings */
  return process_event (display, window, event);
}

/* Handle a key event. May be called recursively: some key events cause
 * grabs to be ended and then need to be processed again in their own
 * right. This cannot cause infinite recursion because we never call
 * ourselves when there wasn't a grab, and we always clear the grab
 * first; the invariant is enforced using an assertion. See #112560.
 *
 * The return value is whether we handled the key event.
 *
 * FIXME: We need to prove there are no race conditions here.
 * FIXME: Does it correctly handle alt-Tab being followed by another
 * grabbing keypress without letting go of alt?
 * FIXME: An iterative solution would probably be simpler to understand
 * (and help us solve the other fixmes).
 */
gboolean
meta_keybindings_process_event (MetaDisplay        *display,
                                MetaWindow         *window,
                                const ClutterEvent *event)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_END:
    case CLUTTER_SCROLL:
      keys->overlay_key_only_pressed = FALSE;
      keys->locate_pointer_key_only_pressed = FALSE;
      return FALSE;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      return process_key_event (display, window, (ClutterKeyEvent *) event);

    default:
      return FALSE;
    }
}

static void
handle_switch_to_last_workspace (MetaDisplay           *display,
                                 MetaWindow            *event_window,
                                 const ClutterKeyEvent *event,
                                 MetaKeyBinding        *binding,
                                 gpointer               user_data)
{
    MetaWorkspaceManager *workspace_manager = display->workspace_manager;
    gint target = meta_workspace_manager_get_n_workspaces (workspace_manager) - 1;
    MetaWorkspace *workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager, target);

    meta_workspace_activate (workspace,
                             clutter_event_get_time ((ClutterEvent *) event));
}

static void
handle_switch_to_workspace (MetaDisplay           *display,
                            MetaWindow            *event_window,
                            const ClutterKeyEvent *event,
                            MetaKeyBinding        *binding,
                            gpointer               user_data)
{
  gint which = binding->handler->data;
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  MetaWorkspace *workspace;

  if (which < 0)
    {
      /* Negative workspace numbers are directions with respect to the
       * current workspace.
       */

      workspace = meta_workspace_get_neighbor (workspace_manager->active_workspace,
                                               which);
    }
  else
    {
      workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager, which);
    }

  if (workspace)
    {
      meta_workspace_activate (workspace,
                               clutter_event_get_time ((ClutterEvent *) event));
    }
  else
    {
      /* We could offer to create it I suppose */
    }
}


static void
handle_maximize_vertically (MetaDisplay           *display,
                            MetaWindow            *window,
                            const ClutterKeyEvent *event,
                            MetaKeyBinding        *binding,
                            gpointer               user_data)
{
  if (window->has_resize_func)
    {
      if (window->maximized_vertically)
        meta_window_unmaximize (window, META_MAXIMIZE_VERTICAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_VERTICAL);
    }
}

static void
handle_maximize_horizontally (MetaDisplay           *display,
                              MetaWindow            *window,
                              const ClutterKeyEvent *event,
                              MetaKeyBinding        *binding,
                              gpointer               user_data)
{
  if (window->has_resize_func)
    {
      if (window->maximized_horizontally)
        meta_window_unmaximize (window, META_MAXIMIZE_HORIZONTAL);
      else
        meta_window_maximize (window, META_MAXIMIZE_HORIZONTAL);
    }
}

static void
handle_always_on_top (MetaDisplay           *display,
                      MetaWindow            *window,
                      const ClutterKeyEvent *event,
                      MetaKeyBinding        *binding,
                      gpointer               user_data)
{
  if (window->wm_state_above == FALSE)
    meta_window_make_above (window);
  else
    meta_window_unmake_above (window);
}

static void
handle_move_to_corner_backend (MetaDisplay           *display,
                               MetaWindow            *window,
                               MetaGravity            gravity)
{
  MtkRectangle work_area;
  MtkRectangle frame_rect;
  int new_x, new_y;

  if (!window->monitor)
    return;

  meta_window_get_work_area_current_monitor (window, &work_area);
  meta_window_get_frame_rect (window, &frame_rect);

  switch (gravity)
    {
    case META_GRAVITY_NORTH_WEST:
    case META_GRAVITY_WEST:
    case META_GRAVITY_SOUTH_WEST:
      new_x = work_area.x;
      break;
    case META_GRAVITY_NORTH:
    case META_GRAVITY_SOUTH:
      new_x = frame_rect.x;
      break;
    case META_GRAVITY_NORTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_SOUTH_EAST:
      new_x = work_area.x + work_area.width - frame_rect.width;
      break;
    default:
      g_assert_not_reached ();
    }

  switch (gravity)
    {
    case META_GRAVITY_NORTH_WEST:
    case META_GRAVITY_NORTH:
    case META_GRAVITY_NORTH_EAST:
      new_y = work_area.y;
      break;
    case META_GRAVITY_WEST:
    case META_GRAVITY_EAST:
      new_y = frame_rect.y;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      new_y = work_area.y + work_area.height - frame_rect.height;
      break;
    default:
      g_assert_not_reached ();
    }

  meta_window_move_frame (window,
                          TRUE,
                          new_x,
                          new_y);
}

static void
handle_move_to_corner_nw (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_NORTH_WEST);
}

static void
handle_move_to_corner_ne (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_NORTH_EAST);
}

static void
handle_move_to_corner_sw (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_SOUTH_WEST);
}

static void
handle_move_to_corner_se (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_SOUTH_EAST);
}

static void
handle_move_to_side_n (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_NORTH);
}

static void
handle_move_to_side_s (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_SOUTH);
}

static void
handle_move_to_side_e (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_EAST);
}

static void
handle_move_to_side_w (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  handle_move_to_corner_backend (display, window, META_GRAVITY_WEST);
}

static void
handle_move_to_center (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  MtkRectangle work_area;
  MtkRectangle frame_rect;

  meta_window_get_work_area_current_monitor (window, &work_area);
  meta_window_get_frame_rect (window, &frame_rect);

  meta_window_move_frame (window,
                          TRUE,
                          work_area.x + (work_area.width  - frame_rect.width ) / 2,
                          work_area.y + (work_area.height - frame_rect.height) / 2);
}

static void
handle_show_desktop (MetaDisplay           *display,
                     MetaWindow            *window,
                     const ClutterKeyEvent *event,
                     MetaKeyBinding        *binding,
                     gpointer               user_data)
{
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;

  if (workspace_manager->active_workspace->showing_desktop)
    {
      meta_workspace_manager_unshow_desktop (workspace_manager);
      meta_workspace_focus_default_window (workspace_manager->active_workspace,
                                           NULL,
                                           clutter_event_get_time ((ClutterEvent *) event));
    }
  else
    {
      meta_workspace_manager_show_desktop (workspace_manager,
                                           clutter_event_get_time ((ClutterEvent *) event));
    }
}

static void
handle_activate_window_menu (MetaDisplay           *display,
                             MetaWindow            *event_window,
                             const ClutterKeyEvent *event,
                             MetaKeyBinding        *binding,
                             gpointer               user_data)
{
  if (display->focus_window)
    {
      int x, y;
      MtkRectangle frame_rect;
      MtkRectangle child_rect;

      meta_window_get_frame_rect (display->focus_window, &frame_rect);
      meta_window_get_client_area_rect (display->focus_window, &child_rect);

      x = frame_rect.x + child_rect.x;
      if (meta_get_locale_direction () == META_LOCALE_DIRECTION_RTL)
        x += child_rect.width;

      y = frame_rect.y + child_rect.y;
      meta_window_show_menu (display->focus_window, META_WINDOW_MENU_WM, x, y);
    }
}

static void
do_choose_window (MetaDisplay           *display,
                  MetaWindow            *event_window,
                  const ClutterKeyEvent *event,
                  MetaKeyBinding        *binding,
                  gboolean               backward)
{
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  MetaTabList type = binding->handler->data;
  MetaWindow *window;

  meta_topic (META_DEBUG_KEYBINDINGS,
              "Tab list = %u", type);

  window = meta_display_get_tab_next (display,
                                      type,
                                      workspace_manager->active_workspace,
                                      NULL,
                                      backward);

  if (window)
    {
      meta_window_activate (window,
                            clutter_event_get_time ((ClutterEvent *) event));
    }
}

static void
handle_switch (MetaDisplay           *display,
               MetaWindow            *event_window,
               const ClutterKeyEvent *event,
               MetaKeyBinding        *binding,
               gpointer               user_data)
{
  gboolean backwards = meta_key_binding_is_reversed (binding);
  do_choose_window (display, event_window, event, binding, backwards);
}

static void
handle_cycle (MetaDisplay           *display,
              MetaWindow            *event_window,
              const ClutterKeyEvent *event,
              MetaKeyBinding        *binding,
              gpointer               user_data)
{
  gboolean backwards = meta_key_binding_is_reversed (binding);
  do_choose_window (display, event_window, event, binding, backwards);
}

static void
handle_toggle_fullscreen (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  if (window->fullscreen)
    meta_window_unmake_fullscreen (window);
  else if (window->has_fullscreen_func)
    meta_window_make_fullscreen (window);
}

static void
handle_toggle_above (MetaDisplay           *display,
                     MetaWindow            *window,
                     const ClutterKeyEvent *event,
                     MetaKeyBinding        *binding,
                     gpointer               user_data)
{
  if (window->wm_state_above)
    meta_window_unmake_above (window);
  else
    meta_window_make_above (window);
}

static void
handle_toggle_tiled (MetaDisplay           *display,
                     MetaWindow            *window,
                     const ClutterKeyEvent *event,
                     MetaKeyBinding        *binding,
                     gpointer               user_data)
{
  MetaTileMode mode = binding->handler->data;

  if ((META_WINDOW_TILED_LEFT (window) && mode == META_TILE_LEFT) ||
      (META_WINDOW_TILED_RIGHT (window) && mode == META_TILE_RIGHT))
    {
      meta_window_untile (window);
    }
  else if (meta_window_can_tile_side_by_side (window, window->monitor->number))
    {
      window->tile_monitor_number = window->monitor->number;
      /* Maximization constraints beat tiling constraints, so if the window
       * is maximized, tiling won't have any effect unless we unmaximize it
       * horizontally first; rather than calling meta_window_unmaximize(),
       * we just set the flag and rely on meta_window_tile() syncing it to
       * save an additional roundtrip.
       */
      window->maximized_horizontally = FALSE;
      meta_window_tile (window, mode);
    }
}

static void
handle_toggle_maximized (MetaDisplay           *display,
                         MetaWindow            *window,
                         const ClutterKeyEvent *event,
                         MetaKeyBinding        *binding,
                         gpointer               user_data)
{
  if (META_WINDOW_MAXIMIZED (window))
    meta_window_unmaximize (window, META_MAXIMIZE_BOTH);
  else if (window->has_maximize_func)
    meta_window_maximize (window, META_MAXIMIZE_BOTH);
}

static void
handle_maximize (MetaDisplay           *display,
                 MetaWindow            *window,
                 const ClutterKeyEvent *event,
                 MetaKeyBinding        *binding,
                 gpointer               user_data)
{
  if (window->has_maximize_func)
    meta_window_maximize (window, META_MAXIMIZE_BOTH);
}

static void
handle_unmaximize (MetaDisplay           *display,
                   MetaWindow            *window,
                   const ClutterKeyEvent *event,
                   MetaKeyBinding        *binding,
                   gpointer               user_data)
{
  if (window->maximized_vertically || window->maximized_horizontally)
    meta_window_unmaximize (window, META_MAXIMIZE_BOTH);
}

static void
handle_close (MetaDisplay           *display,
              MetaWindow            *window,
              const ClutterKeyEvent *event,
              MetaKeyBinding        *binding,
              gpointer               user_data)
{
  if (window->has_close_func)
    {
      meta_window_delete (window,
                          clutter_event_get_time ((ClutterEvent *) event));
    }
}

static void
handle_minimize (MetaDisplay           *display,
                 MetaWindow            *window,
                 const ClutterKeyEvent *event,
                 MetaKeyBinding        *binding,
                 gpointer               user_data)
{
  if (window->has_minimize_func)
    meta_window_minimize (window);
}

static void
handle_begin_move (MetaDisplay           *display,
                   MetaWindow            *window,
                   const ClutterKeyEvent *event,
                   MetaKeyBinding        *binding,
                   gpointer               user_data)
{
  if (window->has_move_func)
    {
      MetaContext *context = meta_display_get_context (display);
      MetaBackend *backend = meta_context_get_backend (context);
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
      ClutterInputDevice *device;

      device = clutter_seat_get_pointer (seat);
      meta_window_begin_grab_op (window,
                                 META_GRAB_OP_KEYBOARD_MOVING |
                                 META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED,
                                 device, NULL,
                                 clutter_event_get_time ((ClutterEvent *) event));
    }
}

static void
handle_begin_resize (MetaDisplay           *display,
                     MetaWindow            *window,
                     const ClutterKeyEvent *event,
                     MetaKeyBinding        *binding,
                     gpointer               user_data)
{
  if (window->has_resize_func)
    {
      MetaContext *context = meta_display_get_context (display);
      MetaBackend *backend = meta_context_get_backend (context);
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
      ClutterInputDevice *device;

      device = clutter_seat_get_pointer (seat);
      meta_window_begin_grab_op (window,
                                 META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN |
                                 META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED,
                                 device, NULL,
                                 clutter_event_get_time ((ClutterEvent *) event));
    }
}

static void
handle_toggle_on_all_workspaces (MetaDisplay           *display,
                                 MetaWindow            *window,
                                 const ClutterKeyEvent *event,
                                 MetaKeyBinding        *binding,
                                 gpointer               user_data)
{
  if (window->on_all_workspaces_requested)
    meta_window_unstick (window);
  else
    meta_window_stick (window);
}

static void
handle_move_to_workspace_last (MetaDisplay           *display,
                               MetaWindow            *window,
                               const ClutterKeyEvent *event,
                               MetaKeyBinding        *binding,
                               gpointer               user_data)
{
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  gint which;
  MetaWorkspace *workspace;

  if (window->always_sticky)
    return;

  which = meta_workspace_manager_get_n_workspaces (workspace_manager) - 1;
  workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager, which);
  meta_window_change_workspace (window, workspace);
}


static void
handle_move_to_workspace (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  gint which = binding->handler->data;
  gboolean flip = (which < 0);
  MetaWorkspace *workspace;

  /* If which is zero or positive, it's a workspace number, and the window
   * should move to the workspace with that number.
   *
   * However, if it's negative, it's a direction with respect to the current
   * position; it's expressed as a member of the MetaMotionDirection enum,
   * all of whose members are negative.  Such a change is called a flip.
   */

  if (window->always_sticky)
    return;

  workspace = NULL;
  if (flip)
    {
      workspace = meta_workspace_get_neighbor (workspace_manager->active_workspace,
                                               which);
    }
  else
    {
      workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager, which);
    }

  if (workspace)
    {
      /* Activate second, so the window is never unmapped */
      meta_window_change_workspace (window, workspace);
      if (flip)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Resetting mouse_mode to FALSE due to "
                      "handle_move_to_workspace() call with flip set.");
          meta_display_clear_mouse_mode (workspace->display);
          meta_workspace_activate_with_focus (workspace,
                                              window,
                                              clutter_event_get_time ((ClutterEvent *) event));
        }
    }
  else
    {
      /* We could offer to create it I suppose */
    }
}

static void
handle_move_to_monitor (MetaDisplay           *display,
                        MetaWindow            *window,
                        const ClutterKeyEvent *event,
                        MetaKeyBinding        *binding,
                        gpointer               user_data)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  gint which = binding->handler->data;
  MetaLogicalMonitor *current, *new;

  current = window->monitor;
  new = meta_monitor_manager_get_logical_monitor_neighbor (monitor_manager,
                                                           current, which);

  if (new == NULL)
    return;

  meta_window_move_to_monitor (window, new->number);
}

static void
handle_raise_or_lower (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  /* Get window at pointer */

  MetaWindow *above = NULL;

  /* Check if top */
  if (meta_stack_get_top (window->display->stack) == window)
    {
      meta_window_lower (window);
      return;
    }

  /* else check if windows in same layer are intersecting it */

  above = meta_stack_get_above (window->display->stack, window, TRUE);

  while (above)
    {
      MtkRectangle tmp, win_rect, above_rect;

      if (above->mapped && meta_window_should_be_showing (above))
        {
          meta_window_get_frame_rect (window, &win_rect);
          meta_window_get_frame_rect (above, &above_rect);

          /* Check if obscured */
          if (mtk_rectangle_intersect (&win_rect, &above_rect, &tmp))
            {
              meta_window_raise (window);
              return;
            }
        }

      above = meta_stack_get_above (window->display->stack, above, TRUE);
    }

  /* window is not obscured */
  meta_window_lower (window);
}

static void
handle_raise (MetaDisplay           *display,
              MetaWindow            *window,
              const ClutterKeyEvent *event,
              MetaKeyBinding        *binding,
              gpointer               user_data)
{
  meta_window_raise (window);
}

static void
handle_lower (MetaDisplay           *display,
              MetaWindow            *window,
              const ClutterKeyEvent *event,
              MetaKeyBinding        *binding,
              gpointer               user_data)
{
  meta_window_lower (window);
}

static void
handle_set_spew_mark (MetaDisplay           *display,
                      MetaWindow            *window,
                      const ClutterKeyEvent *event,
                      MetaKeyBinding        *binding,
                      gpointer               user_data)
{
  meta_verbose ("-- MARK MARK MARK MARK --");
}

#ifdef HAVE_NATIVE_BACKEND
static void
handle_switch_vt (MetaDisplay           *display,
                  MetaWindow            *window,
                  const ClutterKeyEvent *event,
                  MetaKeyBinding        *binding,
                  gpointer               user_data)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  gint vt = binding->handler->data;
  GError *error = NULL;

  if (!meta_backend_native_activate_vt (META_BACKEND_NATIVE (backend),
                                        vt, &error))
    {
      g_warning ("Failed to switch VT: %s", error->message);
      g_error_free (error);
    }
}
#endif /* HAVE_NATIVE_BACKEND */

static void
handle_switch_monitor (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorSwitchConfigType config_type =
    meta_monitor_manager_get_switch_config (monitor_manager);

  if (!meta_monitor_manager_can_switch_config (monitor_manager))
    return;

  config_type = (config_type + 1) % (META_MONITOR_SWITCH_CONFIG_UNKNOWN);
  meta_monitor_manager_switch_config (monitor_manager, config_type);
}

static void
handle_rotate_monitor (MetaDisplay           *display,
                       MetaWindow            *window,
                       const ClutterKeyEvent *event,
                       MetaKeyBinding        *binding,
                       gpointer               user_data)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  meta_monitor_manager_rotate_monitor (monitor_manager);
}

static void
handle_cancel_input_capture (MetaDisplay           *display,
                             MetaWindow            *window,
                             const ClutterKeyEvent *event,
                             MetaKeyBinding        *binding,
                             gpointer               user_data)
{
  meta_display_cancel_input_capture (display);
}

static void
handle_restore_shortcuts (MetaDisplay           *display,
                          MetaWindow            *window,
                          const ClutterKeyEvent *event,
                          MetaKeyBinding        *binding,
                          gpointer               user_data)
{
  ClutterInputDevice *source;

  if (!display->focus_window)
    return;

  source = clutter_event_get_source_device ((ClutterEvent *) event);

  meta_topic (META_DEBUG_KEYBINDINGS, "Restoring normal keyboard shortcuts");

  meta_window_force_restore_shortcuts (display->focus_window, source);
}

/**
 * meta_keybindings_set_custom_handler:
 * @name: The name of the keybinding to set
 * @handler: (nullable): The new handler function
 * @user_data: User data to pass to the callback
 * @free_data: Will be called when this handler is overridden.
 *
 * Allows users to register a custom handler for a
 * builtin key binding.
 *
 * Returns: %TRUE if the binding known as @name was found,
 * %FALSE otherwise.
 */
gboolean
meta_keybindings_set_custom_handler (const gchar        *name,
                                     MetaKeyHandlerFunc  handler,
                                     gpointer            user_data,
                                     GDestroyNotify      free_data)
{
  MetaKeyHandler *key_handler = HANDLER (name);

  if (!key_handler)
    return FALSE;

  if (key_handler->user_data_free_func && key_handler->user_data)
    key_handler->user_data_free_func (key_handler->user_data);

  key_handler->func = handler;
  key_handler->user_data = user_data;
  key_handler->user_data_free_func = free_data;

  return TRUE;
}

static void
init_builtin_key_bindings (MetaDisplay *display)
{
  GSettings *common_keybindings = g_settings_new (SCHEMA_COMMON_KEYBINDINGS);
  GSettings *mutter_keybindings = g_settings_new (SCHEMA_MUTTER_KEYBINDINGS);
  GSettings *mutter_wayland_keybindings = g_settings_new (SCHEMA_MUTTER_WAYLAND_KEYBINDINGS);

  add_builtin_keybinding (display,
                          "switch-to-workspace-1",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_1,
                          handle_switch_to_workspace, 0);
  add_builtin_keybinding (display,
                          "switch-to-workspace-2",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_2,
                          handle_switch_to_workspace, 1);
  add_builtin_keybinding (display,
                          "switch-to-workspace-3",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_3,
                          handle_switch_to_workspace, 2);
  add_builtin_keybinding (display,
                          "switch-to-workspace-4",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_4,
                          handle_switch_to_workspace, 3);
  add_builtin_keybinding (display,
                          "switch-to-workspace-5",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_5,
                          handle_switch_to_workspace, 4);
  add_builtin_keybinding (display,
                          "switch-to-workspace-6",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_6,
                          handle_switch_to_workspace, 5);
  add_builtin_keybinding (display,
                          "switch-to-workspace-7",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_7,
                          handle_switch_to_workspace, 6);
  add_builtin_keybinding (display,
                          "switch-to-workspace-8",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_8,
                          handle_switch_to_workspace, 7);
  add_builtin_keybinding (display,
                          "switch-to-workspace-9",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_9,
                          handle_switch_to_workspace, 8);
  add_builtin_keybinding (display,
                          "switch-to-workspace-10",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_10,
                          handle_switch_to_workspace, 9);
  add_builtin_keybinding (display,
                          "switch-to-workspace-11",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_11,
                          handle_switch_to_workspace, 10);
  add_builtin_keybinding (display,
                          "switch-to-workspace-12",
                          common_keybindings,
                          META_KEY_BINDING_NONE |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_WORKSPACE_12,
                          handle_switch_to_workspace, 11);

  add_builtin_keybinding (display,
                          "switch-to-workspace-left",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_LEFT,
                          handle_switch_to_workspace, META_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-right",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_RIGHT,
                          handle_switch_to_workspace, META_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "switch-to-workspace-up",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_UP,
                          handle_switch_to_workspace, META_MOTION_UP);

  add_builtin_keybinding (display,
                          "switch-to-workspace-down",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_DOWN,
                          handle_switch_to_workspace, META_MOTION_DOWN);

  add_builtin_keybinding (display,
                          "switch-to-workspace-last",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_WORKSPACE_LAST,
                          handle_switch_to_last_workspace, 0);



  /* The ones which have inverses.  These can't be bound to any keystroke
   * containing Shift because Shift will invert their "backward" state.
   *
   * TODO: "NORMAL" and "DOCKS" should be renamed to the same name as their
   * action, for obviousness.
   *
   * TODO: handle_switch and handle_cycle should probably really be the
   * same function checking a bit in the parameter for difference.
   */

  add_builtin_keybinding (display,
                          "switch-group",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SWITCH_GROUP,
                          handle_switch, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-group-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_GROUP_BACKWARD,
                          handle_switch, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "switch-applications",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SWITCH_APPLICATIONS,
                          handle_switch, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-applications-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_APPLICATIONS_BACKWARD,
                          handle_switch, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-windows",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SWITCH_WINDOWS,
                          handle_switch, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-windows-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_WINDOWS_BACKWARD,
                          handle_switch, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "switch-panels",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SWITCH_PANELS,
                          handle_switch, META_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "switch-panels-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_SWITCH_PANELS_BACKWARD,
                          handle_switch, META_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "cycle-group",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_CYCLE_GROUP,
                          handle_cycle, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "cycle-group-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_CYCLE_GROUP_BACKWARD,
                          handle_cycle, META_TAB_LIST_GROUP);

  add_builtin_keybinding (display,
                          "cycle-windows",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_CYCLE_WINDOWS,
                          handle_cycle, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "cycle-windows-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_CYCLE_WINDOWS_BACKWARD,
                          handle_cycle, META_TAB_LIST_NORMAL);

  add_builtin_keybinding (display,
                          "cycle-panels",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_CYCLE_PANELS,
                          handle_cycle, META_TAB_LIST_DOCKS);

  add_builtin_keybinding (display,
                          "cycle-panels-backward",
                          common_keybindings,
                          META_KEY_BINDING_IS_REVERSED,
                          META_KEYBINDING_ACTION_CYCLE_PANELS_BACKWARD,
                          handle_cycle, META_TAB_LIST_DOCKS);

  /***********************************/

  add_builtin_keybinding (display,
                          "show-desktop",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SHOW_DESKTOP,
                          handle_show_desktop, 0);

  add_builtin_keybinding (display,
                          "panel-run-dialog",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_PANEL_RUN_DIALOG,
                          NULL, META_KEYBINDING_ACTION_PANEL_RUN_DIALOG);

  add_builtin_keybinding (display,
                          "set-spew-mark",
                          common_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SET_SPEW_MARK,
                          handle_set_spew_mark, 0);

  add_builtin_keybinding (display,
                          "switch-monitor",
                          mutter_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_SWITCH_MONITOR,
                          handle_switch_monitor, 0);

  add_builtin_keybinding (display,
                          "rotate-monitor",
                          mutter_keybindings,
                          META_KEY_BINDING_NONE,
                          META_KEYBINDING_ACTION_ROTATE_MONITOR,
                          handle_rotate_monitor, 0);

  add_builtin_keybinding (display,
                          "cancel-input-capture",
                          mutter_keybindings,
                          META_KEY_BINDING_IGNORE_AUTOREPEAT |
                          META_KEY_BINDING_CUSTOM_TRIGGER,
                          META_KEYBINDING_ACTION_NONE,
                          handle_cancel_input_capture, 0);

#ifdef HAVE_NATIVE_BACKEND
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  if (META_IS_BACKEND_NATIVE (backend))
    {
      add_builtin_keybinding (display,
                              "switch-to-session-1",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 1);

      add_builtin_keybinding (display,
                              "switch-to-session-2",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 2);

      add_builtin_keybinding (display,
                              "switch-to-session-3",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 3);

      add_builtin_keybinding (display,
                              "switch-to-session-4",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 4);

      add_builtin_keybinding (display,
                              "switch-to-session-5",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 5);

      add_builtin_keybinding (display,
                              "switch-to-session-6",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 6);

      add_builtin_keybinding (display,
                              "switch-to-session-7",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 7);

      add_builtin_keybinding (display,
                              "switch-to-session-8",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 8);

      add_builtin_keybinding (display,
                              "switch-to-session-9",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 9);

      add_builtin_keybinding (display,
                              "switch-to-session-10",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 10);

      add_builtin_keybinding (display,
                              "switch-to-session-11",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 11);

      add_builtin_keybinding (display,
                              "switch-to-session-12",
                              mutter_wayland_keybindings,
                              META_KEY_BINDING_NON_MASKABLE,
                              META_KEYBINDING_ACTION_NONE,
                              handle_switch_vt, 12);
    }
#endif /* HAVE_NATIVE_BACKEND */

  add_builtin_keybinding (display,
                          "restore-shortcuts",
                          mutter_wayland_keybindings,
                          META_KEY_BINDING_NON_MASKABLE,
                          META_KEYBINDING_ACTION_NONE,
                          handle_restore_shortcuts, 0);

  /************************ PER WINDOW BINDINGS ************************/

  /* These take a window as an extra parameter; they have no effect
   * if no window is active.
   */

  add_builtin_keybinding (display,
                          "activate-window-menu",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_ACTIVATE_WINDOW_MENU,
                          handle_activate_window_menu, 0);

  add_builtin_keybinding (display,
                          "toggle-fullscreen",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_FULLSCREEN,
                          handle_toggle_fullscreen, 0);

  add_builtin_keybinding (display,
                          "toggle-maximized",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_MAXIMIZED,
                          handle_toggle_maximized, 0);

  add_builtin_keybinding (display,
                          "toggle-tiled-left",
                          mutter_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_TILED_LEFT,
                          handle_toggle_tiled, META_TILE_LEFT);

  add_builtin_keybinding (display,
                          "toggle-tiled-right",
                          mutter_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_TILED_RIGHT,
                          handle_toggle_tiled, META_TILE_RIGHT);

  add_builtin_keybinding (display,
                          "toggle-above",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_ABOVE,
                          handle_toggle_above, 0);

  add_builtin_keybinding (display,
                          "maximize",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MAXIMIZE,
                          handle_maximize, 0);

  add_builtin_keybinding (display,
                          "unmaximize",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_UNMAXIMIZE,
                          handle_unmaximize, 0);

  add_builtin_keybinding (display,
                          "minimize",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MINIMIZE,
                          handle_minimize, 0);

  add_builtin_keybinding (display,
                          "close",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_CLOSE,
                          handle_close, 0);

  add_builtin_keybinding (display,
                          "begin-move",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_BEGIN_MOVE,
                          handle_begin_move, 0);

  add_builtin_keybinding (display,
                          "begin-resize",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_BEGIN_RESIZE,
                          handle_begin_resize, 0);

  add_builtin_keybinding (display,
                          "toggle-on-all-workspaces",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_TOGGLE_ON_ALL_WORKSPACES,
                          handle_toggle_on_all_workspaces, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-1",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_1,
                          handle_move_to_workspace, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-2",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_2,
                          handle_move_to_workspace, 1);

  add_builtin_keybinding (display,
                          "move-to-workspace-3",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_3,
                          handle_move_to_workspace, 2);

  add_builtin_keybinding (display,
                          "move-to-workspace-4",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_4,
                          handle_move_to_workspace, 3);

  add_builtin_keybinding (display,
                          "move-to-workspace-5",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_5,
                          handle_move_to_workspace, 4);

  add_builtin_keybinding (display,
                          "move-to-workspace-6",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_6,
                          handle_move_to_workspace, 5);

  add_builtin_keybinding (display,
                          "move-to-workspace-7",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_7,
                          handle_move_to_workspace, 6);

  add_builtin_keybinding (display,
                          "move-to-workspace-8",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_8,
                          handle_move_to_workspace, 7);

  add_builtin_keybinding (display,
                          "move-to-workspace-9",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_9,
                          handle_move_to_workspace, 8);

  add_builtin_keybinding (display,
                          "move-to-workspace-10",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_10,
                          handle_move_to_workspace, 9);

  add_builtin_keybinding (display,
                          "move-to-workspace-11",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_11,
                          handle_move_to_workspace, 10);

  add_builtin_keybinding (display,
                          "move-to-workspace-12",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_12,
                          handle_move_to_workspace, 11);

  add_builtin_keybinding (display,
                          "move-to-workspace-last",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LAST,
                          handle_move_to_workspace_last, 0);

  add_builtin_keybinding (display,
                          "move-to-workspace-left",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_LEFT,
                          handle_move_to_workspace, META_MOTION_LEFT);

  add_builtin_keybinding (display,
                          "move-to-workspace-right",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_RIGHT,
                          handle_move_to_workspace, META_MOTION_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-workspace-up",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_UP,
                          handle_move_to_workspace, META_MOTION_UP);

  add_builtin_keybinding (display,
                          "move-to-workspace-down",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_WORKSPACE_DOWN,
                          handle_move_to_workspace, META_MOTION_DOWN);

  add_builtin_keybinding (display,
                          "move-to-monitor-left",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_LEFT,
                          handle_move_to_monitor, META_DISPLAY_LEFT);

  add_builtin_keybinding (display,
                          "move-to-monitor-right",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_RIGHT,
                          handle_move_to_monitor, META_DISPLAY_RIGHT);

  add_builtin_keybinding (display,
                          "move-to-monitor-down",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_DOWN,
                          handle_move_to_monitor, META_DISPLAY_DOWN);

  add_builtin_keybinding (display,
                          "move-to-monitor-up",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW,
                          META_KEYBINDING_ACTION_MOVE_TO_MONITOR_UP,
                          handle_move_to_monitor, META_DISPLAY_UP);

  add_builtin_keybinding (display,
                          "raise-or-lower",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_RAISE_OR_LOWER,
                          handle_raise_or_lower, 0);

  add_builtin_keybinding (display,
                          "raise",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_RAISE,
                          handle_raise, 0);

  add_builtin_keybinding (display,
                          "lower",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_LOWER,
                          handle_lower, 0);

  add_builtin_keybinding (display,
                          "maximize-vertically",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MAXIMIZE_VERTICALLY,
                          handle_maximize_vertically, 0);

  add_builtin_keybinding (display,
                          "maximize-horizontally",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MAXIMIZE_HORIZONTALLY,
                          handle_maximize_horizontally, 0);

  add_builtin_keybinding (display,
                          "always-on-top",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_ALWAYS_ON_TOP,
                          handle_always_on_top, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-nw",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_NW,
                          handle_move_to_corner_nw, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-ne",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_NE,
                          handle_move_to_corner_ne, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-sw",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_SW,
                          handle_move_to_corner_sw, 0);

  add_builtin_keybinding (display,
                          "move-to-corner-se",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_CORNER_SE,
                          handle_move_to_corner_se, 0);

  add_builtin_keybinding (display,
                          "move-to-side-n",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_N,
                          handle_move_to_side_n, 0);

  add_builtin_keybinding (display,
                          "move-to-side-s",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_S,
                          handle_move_to_side_s, 0);

  add_builtin_keybinding (display,
                          "move-to-side-e",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_E,
                          handle_move_to_side_e, 0);

  add_builtin_keybinding (display,
                          "move-to-side-w",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_SIDE_W,
                          handle_move_to_side_w, 0);

  add_builtin_keybinding (display,
                          "move-to-center",
                          common_keybindings,
                          META_KEY_BINDING_PER_WINDOW |
                          META_KEY_BINDING_IGNORE_AUTOREPEAT,
                          META_KEYBINDING_ACTION_MOVE_TO_CENTER,
                          handle_move_to_center, 0);

  g_object_unref (common_keybindings);
  g_object_unref (mutter_keybindings);
  g_object_unref (mutter_wayland_keybindings);
}

void
meta_display_init_keys (MetaDisplay *display)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaKeyHandler *handler;

  keys->backend = backend;

  /* Keybindings */
  keys->ignored_modifier_mask = 0;
  keys->hyper_mask = 0;
  keys->super_mask = 0;
  keys->meta_mask = 0;

  keys->key_bindings = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) meta_key_binding_free);
  keys->key_bindings_index = g_hash_table_new (NULL, NULL);

  reload_modmap (keys);

  key_handlers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) key_handler_free);

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup ("overlay-key");
  handler->flags = META_KEY_BINDING_BUILTIN | META_KEY_BINDING_NO_AUTO_GRAB;

  g_hash_table_insert (key_handlers, g_strdup (handler->name), handler);

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup ("locate-pointer-key");
  handler->flags = META_KEY_BINDING_BUILTIN | META_KEY_BINDING_NO_AUTO_GRAB;

  g_hash_table_insert (key_handlers, g_strdup (handler->name), handler);

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup ("iso-next-group");
  handler->flags = META_KEY_BINDING_BUILTIN;

  g_hash_table_insert (key_handlers, g_strdup (handler->name), handler);

  handler = g_new0 (MetaKeyHandler, 1);
  handler->name = g_strdup ("external-grab");
  handler->func = handle_external_grab;
  handler->default_func = handle_external_grab;

  g_hash_table_insert (key_handlers, g_strdup (handler->name), handler);

  external_grabs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          NULL,
                                          (GDestroyNotify)meta_key_grab_free);

  init_builtin_key_bindings (display);

  rebuild_key_binding_table (keys);
  rebuild_special_bindings (keys);

  reload_combos (keys);

  update_window_grab_modifiers (display);

  /* Keys are actually grabbed in meta_screen_grab_keys() */

  meta_prefs_add_listener (prefs_changed_callback, display);

  g_signal_connect_swapped (backend, "keymap-changed",
                            G_CALLBACK (reload_keybindings), display);
  g_signal_connect_swapped (backend, "keymap-layout-group-changed",
                            G_CALLBACK (reload_keybindings), display);
}

static gboolean
process_keybinding_key_event (MetaDisplay           *display,
                              MetaKeyHandler        *handler,
                              const ClutterKeyEvent *event)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  xkb_keycode_t keycode =
    (xkb_keycode_t) clutter_event_get_key_code ((ClutterEvent *) event);
  ClutterModifierType modifiers;
  MetaResolvedKeyCombo resolved_combo = { &keycode, 1 };
  MetaKeyBinding *binding;

  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return FALSE;

  modifiers = get_modifiers ((ClutterEvent *) event);
  resolved_combo.mask = mask_from_event_params (keys, modifiers);

  binding = get_keybinding (keys, &resolved_combo);
  if (!binding)
    return FALSE;

  if (handler != binding->handler)
    return FALSE;

  g_return_val_if_fail (binding->flags & META_KEY_BINDING_CUSTOM_TRIGGER,
                        FALSE);

  invoke_handler (display, binding->handler, NULL, event, binding);
  return TRUE;
}

gboolean
meta_display_process_keybinding_event (MetaDisplay        *display,
                                       const char         *name,
                                       const ClutterEvent *event)
{
  MetaKeyHandler *handler;

  handler = g_hash_table_lookup (key_handlers, name);
  if (!handler)
    return FALSE;

  switch (clutter_event_type (event))
    {
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      return process_keybinding_key_event (display, handler,
                                           (ClutterKeyEvent *) event);

    default:
      return FALSE;
    }
}
