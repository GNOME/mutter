/*
 * meta-background-content.h: ClutterContent for painting the wallpaper
 *
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2020 Endless Foundation
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

#ifndef META_BACKGROUND_CONTENT_H
#define META_BACKGROUND_CONTENT_H

#include <gdesktop-enums.h>

#include "clutter/clutter.h"
#include "meta/meta-background.h"

/**
 * MetaBackgroundContent:
 *
 * This class handles tracking and painting the root window background.
 * By integrating with #MetaWindowGroup we can avoid painting parts of
 * the background that are obscured by other windows.
 */

#define META_TYPE_BACKGROUND_CONTENT (meta_background_content_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaBackgroundContent,
                      meta_background_content,
                      META, BACKGROUND_CONTENT,
                      GObject)


META_EXPORT
ClutterContent *meta_background_content_new (MetaDisplay *display,
                                             int          monitor);

META_EXPORT
void meta_background_content_set_background (MetaBackgroundContent *self,
                                             MetaBackground        *background);

META_EXPORT
void meta_background_content_set_gradient (MetaBackgroundContent *self,
                                           gboolean               enabled,
                                           int                    height,
                                           double                 tone_start);

META_EXPORT
void meta_background_content_set_vignette (MetaBackgroundContent *self,
                                           gboolean               enabled,
                                           double                 brightness,
                                           double                 sharpness);

#endif /* META_BACKGROUND_CONTENT_H */
