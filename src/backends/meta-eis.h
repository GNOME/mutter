/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"

typedef enum _MetaEisDeviceTypes
{
  META_EIS_DEVICE_TYPE_NONE = 0,
  META_EIS_DEVICE_TYPE_KEYBOARD = 1 << 0,
  META_EIS_DEVICE_TYPE_POINTER = 1 << 1,
  META_EIS_DEVICE_TYPE_TOUCHSCREEN = 1 << 2,
} MetaEisDeviceTypes;

#define META_TYPE_EIS_VIEWPORT (meta_eis_viewport_get_type ())
G_DECLARE_INTERFACE (MetaEisViewport, meta_eis_viewport,
                     META, EIS_VIEWPORT, GObject)

#define META_TYPE_EIS (meta_eis_get_type ())
G_DECLARE_FINAL_TYPE (MetaEis, meta_eis,
                      META, EIS, GObject)

struct _MetaEisViewportInterface
{
  GTypeInterface parent_iface;

  gboolean (* is_standalone) (MetaEisViewport *viewport);

  const char * (* get_mapping_id) (MetaEisViewport *viewport);

  gboolean (* get_position) (MetaEisViewport *viewport,
                             int             *out_x,
                             int             *out_y);

  void (* get_size) (MetaEisViewport *viewport,
                     int             *out_width,
                     int             *out_height);

  double (* get_physical_scale) (MetaEisViewport *viewport);

  gboolean (* transform_coordinate) (MetaEisViewport *viewport,
                                     double           x,
                                     double           y,
                                     double          *out_x,
                                     double          *out_y);
};

MetaEis * meta_eis_new (MetaBackend        *backend,
                        MetaEisDeviceTypes  device_types);

MetaBackend * meta_eis_get_backend (MetaEis *eis);

int meta_eis_add_client_get_fd (MetaEis *eis);

void meta_eis_add_viewport (MetaEis         *eis,
                            MetaEisViewport *viewport);

void meta_eis_remove_viewport (MetaEis         *eis,
                               MetaEisViewport *viewport);

GList * meta_eis_peek_viewports (MetaEis *eis);

MetaEisDeviceTypes meta_eis_get_device_types (MetaEis *eis);

gboolean meta_eis_viewport_is_standalone (MetaEisViewport *viewport);

const char * meta_eis_viewport_get_mapping_id (MetaEisViewport *viewport);

gboolean meta_eis_viewport_get_position (MetaEisViewport *viewport,
                                         int             *out_x,
                                         int             *out_y);

void meta_eis_viewport_get_size (MetaEisViewport *viewport,
                                 int             *out_width,
                                 int             *out_height);

double meta_eis_viewport_get_physical_scale (MetaEisViewport *viewport);

gboolean meta_eis_viewport_transform_coordinate (MetaEisViewport *viewport,
                                                 double           x,
                                                 double           y,
                                                 double          *out_x,
                                                 double          *out_y);
