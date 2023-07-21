/*
 * Copyright (C) 2022 Red Hat Inc.
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

#pragma once

#include <gtk/gtk.h>

#include <X11/Xlib.h>

#define META_TYPE_FRAME_CONTENT (meta_frame_content_get_type ())
G_DECLARE_FINAL_TYPE (MetaFrameContent, meta_frame_content,
                      META, FRAME_CONTENT, GtkWidget)

GtkWidget * meta_frame_content_new (Window window);

Window meta_frame_content_get_window (MetaFrameContent *content);

GtkBorder meta_frame_content_get_border (MetaFrameContent *content);
