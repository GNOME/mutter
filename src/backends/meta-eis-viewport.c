/*
 * Copyright (C) 2023 Red Hat Inc.
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

#include "config.h"

#include "backends/meta-eis-viewport.h"

enum
{
  VIEWPORT_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_INTERFACE (MetaEisViewport, meta_eis_viewport, G_TYPE_OBJECT)

static void
meta_eis_viewport_default_init (MetaEisViewportInterface *iface)
{
  signals[VIEWPORT_CHANGED] =
    g_signal_new ("viewport-changed",
                  G_TYPE_FROM_INTERFACE (iface),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

gboolean
meta_eis_viewport_is_standalone (MetaEisViewport *viewport)
{
  return META_EIS_VIEWPORT_GET_IFACE (viewport)->is_standalone (viewport);
}

const char *
meta_eis_viewport_get_mapping_id (MetaEisViewport *viewport)
{
  return META_EIS_VIEWPORT_GET_IFACE (viewport)->get_mapping_id (viewport);
}

gboolean
meta_eis_viewport_get_position (MetaEisViewport *viewport,
                                int             *out_x,
                                int             *out_y)
{
  return META_EIS_VIEWPORT_GET_IFACE (viewport)->get_position (viewport,
                                                               out_x,
                                                               out_y);
}

void
meta_eis_viewport_get_size (MetaEisViewport *viewport,
                            int             *out_width,
                            int             *out_height)
{
  META_EIS_VIEWPORT_GET_IFACE (viewport)->get_size (viewport,
                                                    out_width,
                                                    out_height);
}

double
meta_eis_viewport_get_physical_scale (MetaEisViewport *viewport)
{
  return META_EIS_VIEWPORT_GET_IFACE (viewport)->get_physical_scale (viewport);
}

gboolean
meta_eis_viewport_transform_coordinate (MetaEisViewport *viewport,
                                        double           x,
                                        double           y,
                                        double          *out_x,
                                        double          *out_y)
{
  return META_EIS_VIEWPORT_GET_IFACE (viewport)->transform_coordinate (viewport,
                                                                       x,
                                                                       y,
                                                                       out_x,
                                                                       out_y);
}

void
meta_eis_viewport_notify_changed (MetaEisViewport *viewport)
{
  g_signal_emit (viewport, signals[VIEWPORT_CHANGED], 0);
}
