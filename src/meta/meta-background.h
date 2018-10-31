/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background-actor.h:  for painting the root window background
 *
 * Copyright 2010 Red Hat, Inc.
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

#ifndef META_BACKGROUND_H
#define META_BACKGROUND_H

#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include "clutter/clutter.h"
#include "meta/display.h"

/**
 * MetaBackground:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #MetaWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define META_TYPE_BACKGROUND (meta_background_get_type ())
G_DECLARE_FINAL_TYPE (MetaBackground,
                      meta_background,
                      META, BACKGROUND,
                      GObject)


void meta_background_refresh_all (void);

MetaBackground *meta_background_new (MetaDisplay *display);

void meta_background_set_color    (MetaBackground            *self,
                                   ClutterColor              *color);
void meta_background_set_gradient (MetaBackground            *self,
                                   GDesktopBackgroundShading  shading_direction,
                                   ClutterColor              *color,
                                   ClutterColor              *second_color);
void meta_background_set_file     (MetaBackground            *self,
                                   GFile                     *file,
                                   GDesktopBackgroundStyle    style);
void meta_background_set_blend    (MetaBackground            *self,
                                   GFile                     *file1,
                                   GFile                     *file2,
                                   double                     blend_factor,
                                   GDesktopBackgroundStyle    style);

#endif /* META_BACKGROUND_H */
