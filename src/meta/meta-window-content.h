/*
 * Copyright (C) 2018 Endless, Inc.
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
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#ifndef META_WINDOW_CONTENT_H
#define META_WINDOW_CONTENT_H

#include "meta/meta-window-actor.h"

#define META_TYPE_WINDOW_CONTENT (meta_window_content_get_type())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaWindowContent,
                      meta_window_content,
                      META, WINDOW_CONTENT,
                      GObject)

META_EXPORT
MetaWindowActor * meta_window_content_get_window_actor (MetaWindowContent *window_content);

#endif /* META_WINDOW_CONTENT_H */
