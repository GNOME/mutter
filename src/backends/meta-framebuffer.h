/*
 * Copyright © 2018 Canonical Ltd.
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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#ifndef META_FRAMEBUFFER_H
#define META_FRAMEBUFFER_H

#include <glib-object.h>

#define META_TYPE_FRAMEBUFFER (meta_framebuffer_get_type ())
G_DECLARE_FINAL_TYPE (MetaFramebuffer, meta_framebuffer, META, FRAMEBUFFER, GObject)

/* This might look pointless right now, but is used in a future branch */
struct _MetaFramebuffer
{
  GObject parent;
};

#endif /* META_FRAMEBUFFER_H */
