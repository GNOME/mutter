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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-build-config.h"

#include "clutter-device-manager-evdev.h"
#include "clutter-keymap-evdev.h"

static const char *option_xkb_layout = "us";
static const char *option_xkb_variant = "";
static const char *option_xkb_options = "";

typedef struct _ClutterKeymapEvdev ClutterKeymapEvdev;

struct _ClutterKeymapEvdev
{
  ClutterKeymap parent_instance;

  struct xkb_keymap *keymap;
};

G_DEFINE_TYPE (ClutterKeymapEvdev, clutter_keymap_evdev,
               CLUTTER_TYPE_KEYMAP)

static void
clutter_keymap_evdev_finalize (GObject *object)
{
  ClutterKeymapEvdev *keymap = CLUTTER_KEYMAP_EVDEV (object);

  xkb_keymap_unref (keymap->keymap);

  G_OBJECT_CLASS (clutter_keymap_evdev_parent_class)->finalize (object);
}

static gboolean
clutter_keymap_evdev_get_num_lock_state (ClutterKeymap *keymap)
{
  ClutterDeviceManagerEvdev *device_manager;
  struct xkb_state *xkb_state;

  device_manager =
    CLUTTER_DEVICE_MANAGER_EVDEV (clutter_device_manager_get_default ());
  xkb_state = _clutter_device_manager_evdev_get_xkb_state (device_manager);

  return xkb_state_mod_name_is_active (xkb_state,
                                       XKB_MOD_NAME_NUM,
                                       XKB_STATE_MODS_LATCHED ||
                                       XKB_STATE_MODS_LOCKED);
}

static gboolean
clutter_keymap_evdev_get_caps_lock_state (ClutterKeymap *keymap)
{
  ClutterDeviceManagerEvdev *device_manager;
  struct xkb_state *xkb_state;

  device_manager =
    CLUTTER_DEVICE_MANAGER_EVDEV (clutter_device_manager_get_default ());
  xkb_state = _clutter_device_manager_evdev_get_xkb_state (device_manager);

  return xkb_state_mod_name_is_active (xkb_state,
                                       XKB_MOD_NAME_CAPS,
                                       XKB_STATE_MODS_LATCHED ||
                                       XKB_STATE_MODS_LOCKED);
}

static void
clutter_keymap_evdev_class_init (ClutterKeymapEvdevClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterKeymapClass *keymap_class = CLUTTER_KEYMAP_CLASS (klass);

  object_class->finalize = clutter_keymap_evdev_finalize;

  keymap_class->get_num_lock_state = clutter_keymap_evdev_get_num_lock_state;
  keymap_class->get_caps_lock_state = clutter_keymap_evdev_get_caps_lock_state;
}

static void
clutter_keymap_evdev_init (ClutterKeymapEvdev *keymap)
{
  struct xkb_context *ctx;
  struct xkb_rule_names names;

  names.rules = "evdev";
  names.model = "pc105";
  names.layout = option_xkb_layout;
  names.variant = option_xkb_variant;
  names.options = option_xkb_options;

  ctx = xkb_context_new (0);
  g_assert (ctx);
  keymap->keymap = xkb_keymap_new_from_names (ctx, &names, 0);
  xkb_context_unref (ctx);
}

void
clutter_keymap_evdev_set_keyboard_map (ClutterKeymapEvdev *keymap,
                                       struct xkb_keymap  *xkb_keymap)
{
  if (keymap->keymap)
    xkb_keymap_unref (keymap->keymap);
  keymap->keymap = xkb_keymap_ref (xkb_keymap);
}

struct xkb_keymap *
clutter_keymap_evdev_get_keyboard_map (ClutterKeymapEvdev *keymap)
{
  return keymap->keymap;
}
