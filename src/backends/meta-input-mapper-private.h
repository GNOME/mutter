/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2018 Red Hat, Inc.
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

#ifndef __META_INPUT_MAPPER_H__
#define __META_INPUT_MAPPER_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define META_TYPE_INPUT_MAPPER (meta_input_mapper_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputMapper, meta_input_mapper,
		      META, INPUT_MAPPER, GObject)

GType             meta_input_mapper_get_type (void) G_GNUC_CONST;
MetaInputMapper * meta_input_mapper_new      (void);

void meta_input_mapper_add_device    (MetaInputMapper    *mapper,
				      ClutterInputDevice *device,
                                      gboolean            builtin);
void meta_input_mapper_remove_device (MetaInputMapper    *mapper,
				      ClutterInputDevice *device);

G_END_DECLS

#endif /* __META_INPUT_MAPPER_H__ */
