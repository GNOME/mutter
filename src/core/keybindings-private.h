/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file keybindings.h  Grab and ungrab keys, and process the key events
 *
 * Performs global X grabs on the keys we need to be told about, like
 * the one to close a window.  It also deals with incoming key events.
 */

/*
 * Copyright (C) 2001 Havoc Pennington
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

#pragma once

#include <gio/gio.h>
#include <xkbcommon/xkbcommon.h>

#include "core/meta-accel-parse.h"
#include "meta/keybindings.h"

typedef struct _MetaKeyHandler MetaKeyHandler;
struct _MetaKeyHandler
{
  grefcount ref_count;
  char *name;
  MetaKeyHandlerFunc func;
  MetaKeyHandlerFunc default_func;
  int data;
  MetaKeyBindingFlags flags;
  gpointer user_data;
  GDestroyNotify user_data_free_func;
  gboolean removed;
};

typedef struct _MetaResolvedKeyCombo {
  xkb_keycode_t *keycodes;
  int            len;
  xkb_mod_mask_t mask;
} MetaResolvedKeyCombo;

/**
 * MetaKeyCombo:
 * @keysym: keysym
 * @keycode: keycode
 * @modifiers: modifiers
 */
struct _MetaKeyCombo
{
  unsigned int keysym;
  unsigned int keycode;
  ClutterModifierType modifiers;
};

struct _MetaKeyBinding
{
  char *name;
  MetaKeyCombo combo;
  MetaResolvedKeyCombo resolved_combo;
  gint flags;
  /* The binding should respond to release, and was just pressed */
  gboolean release_pending;
  MetaKeyHandler *handler;
};

typedef struct
{
  char *name;
  GSettings *settings;

  MetaKeyBindingAction action;

  /*
   * A list of MetaKeyCombos. Each of them is bound to
   * this keypref. If one has keysym==modifiers==0, it is
   * ignored.
   */
  GSList *combos;

  /* for keybindings not added with meta_display_add_keybinding() */
  gboolean      builtin:1;
} MetaKeyPref;

typedef struct _MetaKeyBindingKeyboardLayout
{
  struct xkb_keymap *keymap;
  xkb_layout_index_t index;
  xkb_level_index_t n_levels;
} MetaKeyBindingKeyboardLayout;

typedef struct
{
  MetaBackend *backend;

  GHashTable *key_bindings;
  GHashTable *key_bindings_index;
  xkb_mod_mask_t ignored_modifier_mask;
  xkb_mod_mask_t hyper_mask;
  xkb_mod_mask_t virtual_hyper_mask;
  xkb_mod_mask_t super_mask;
  xkb_mod_mask_t virtual_super_mask;
  xkb_mod_mask_t meta_mask;
  xkb_mod_mask_t virtual_meta_mask;
  MetaResolvedKeyCombo overlay_resolved_key_combo;
  gboolean overlay_key_only_pressed;
  MetaResolvedKeyCombo locate_pointer_resolved_key_combo;
  gboolean locate_pointer_key_only_pressed;
  MetaResolvedKeyCombo iso_next_group_combos[2];
  int n_iso_next_group_combos;

  /*
   * A primary layout, and an optional secondary layout for when the
   * primary layout does not use the latin alphabet.
   */
  MetaKeyBindingKeyboardLayout active_layouts[2];

  /* Alt+click button grabs */
  ClutterModifierType window_grab_modifiers;
} MetaKeyBindingManager;

typedef void (* MetaKeyBindingForeach) (MetaDisplay          *display,
                                        MetaKeyBindingFlags   flags,
                                        MetaResolvedKeyCombo *resolved_key_binding,
                                        gpointer              user_data);

void     meta_display_init_keys             (MetaDisplay *display);
void     meta_display_shutdown_keys         (MetaDisplay *display);
gboolean meta_keybindings_process_event     (MetaDisplay        *display,
                                             MetaWindow         *window,
                                             const ClutterEvent *event);

gboolean meta_prefs_add_keybinding          (const char           *name,
                                             GSettings            *settings,
                                             MetaKeyBindingAction  action,
                                             MetaKeyBindingFlags   flags);

gboolean meta_prefs_remove_keybinding       (const char    *name);

GList * meta_prefs_get_keybindings (void);
void meta_prefs_get_overlay_bindings (MetaKeyCombo combos[2]);
void meta_prefs_get_locate_pointer_bindings (MetaKeyCombo combos[2]);
const char * meta_prefs_get_iso_next_group_option (void);
gboolean meta_prefs_is_locate_pointer_enabled (void);

gboolean meta_display_process_keybinding_event (MetaDisplay        *display,
                                                const char         *name,
                                                const ClutterEvent *event);

void meta_display_keybinding_foreach (MetaDisplay           *display,
                                      MetaKeyBindingForeach  func,
                                      gpointer               user_data);
