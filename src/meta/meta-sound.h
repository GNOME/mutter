/*
 * Copyright (C) 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef META_SOUND_H
#define META_SOUND_H

#include <gio/gio.h>

G_DECLARE_FINAL_TYPE (MetaSound, meta_sound, META, SOUND, GObject)

#define META_TYPE_SOUND (meta_sound_get_type ())

void meta_sound_play_from_theme (MetaSound    *sound,
                                 const char   *name,
                                 const char   *description,
                                 GCancellable *cancellable);
void meta_sound_play_from_file  (MetaSound    *sound,
                                 GFile        *file,
                                 const char   *description,
                                 GCancellable *cancellable);

#endif /* META_SOUND_H */
