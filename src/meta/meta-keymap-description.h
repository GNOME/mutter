/*
 * Copyright 2025 Red Hat
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

#pragma once

#include <glib-object.h>

#include "meta/util.h"

typedef struct _MetaKeymapDescription MetaKeymapDescription;

#define META_TYPE_KEYMAP_DESCRIPTION (meta_keymap_description_get_type ())

META_EXPORT
GType meta_keymap_description_get_type (void) G_GNUC_CONST;

META_EXPORT
MetaKeymapDescription * meta_keymap_description_new_from_rules (const char *model,
                                                                const char *layout,
                                                                const char *variant,
                                                                const char *options);

META_EXPORT
MetaKeymapDescription * meta_keymap_description_ref (MetaKeymapDescription *keymap_description);

META_EXPORT
void meta_keymap_description_unref (MetaKeymapDescription *keymap_description);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKeymapDescription,
                               meta_keymap_description_unref);
