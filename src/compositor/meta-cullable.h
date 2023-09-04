/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Owen Taylor <otaylor@redhat.com>
 *     Ray Strode <rstrode@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define META_TYPE_CULLABLE (meta_cullable_get_type ())
G_DECLARE_INTERFACE (MetaCullable, meta_cullable, META, CULLABLE, ClutterActor)

struct _MetaCullableInterface
{
  GTypeInterface g_iface;

  void (* cull_unobscured) (MetaCullable *cullable,
                            MtkRegion    *unobscured_region);
  void (* cull_redraw_clip) (MetaCullable *cullable,
                             MtkRegion    *clip_region);
};

void meta_cullable_cull_unobscured (MetaCullable *cullable,
                                    MtkRegion    *unobscured_region);
void meta_cullable_cull_redraw_clip (MetaCullable *cullable,
                                     MtkRegion    *clip_region);

/* Utility methods for implementations */
void meta_cullable_cull_unobscured_children (MetaCullable *cullable,
                                             MtkRegion    *unobscured_region);
void meta_cullable_cull_redraw_clip_children (MetaCullable *cullable,
                                              MtkRegion    *clip_region);

G_END_DECLS
