/*
 * Copyright (C) 2020 Red Hat
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
 * Written by:
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_PAD_ACTION_MAPPER_H
#define META_PAD_ACTION_MAPPER_H

#include "clutter/clutter.h"
#include "meta/display.h"
#include "meta/meta-monitor-manager.h"

#define META_TYPE_PAD_ACTION_MAPPER (meta_pad_action_mapper_get_type ())
G_DECLARE_FINAL_TYPE (MetaPadActionMapper, meta_pad_action_mapper,
                      META, PAD_ACTION_MAPPER, GObject)

MetaPadActionMapper * meta_pad_action_mapper_new (MetaMonitorManager *monitor_manager);

gboolean meta_pad_action_mapper_is_button_grabbed (MetaPadActionMapper *mapper,
                                                   ClutterInputDevice  *pad,
                                                   guint                button);
gboolean meta_pad_action_mapper_handle_event      (MetaPadActionMapper *mapper,
                                                   const ClutterEvent  *event);
gchar *  meta_pad_action_mapper_get_action_label  (MetaPadActionMapper *mapper,
                                                   ClutterInputDevice  *pad,
                                                   MetaPadActionType    action,
                                                   guint                number);

#endif /* META_PAD_ACTION_MAPPER_H */
