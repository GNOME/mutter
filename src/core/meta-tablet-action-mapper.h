/*
 * Copyright (C) 2024 Red Hat
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
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "clutter/clutter.h"
#include "meta/display.h"
#include "meta/meta-monitor-manager.h"

#define META_TYPE_TABLET_ACTION_MAPPER (meta_tablet_action_mapper_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaTabletActionMapper, meta_tablet_action_mapper,
                          META, TABLET_ACTION_MAPPER, GObject)

typedef struct _MetaTabletActionMapperPrivate MetaTabletActionMapperPrivate;

/**
 * MetaTabletActionMapperClass:
 */
struct _MetaTabletActionMapperClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< private >*/
  MetaDisplay * (* get_display) (MetaTabletActionMapper  *mapper);
  void (* emulate_keybinding) (MetaTabletActionMapper  *mapper,
                               const char              *accel,
                               gboolean                 is_press);
  void (*cycle_tablet_output) (MetaTabletActionMapper *mapper,
                               ClutterInputDevice     *device);
};

gboolean meta_tablet_action_mapper_handle_event (MetaTabletActionMapper *mapper,
                                                 const ClutterEvent     *event);
