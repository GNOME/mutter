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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#include "meta/meta-selection-source.h"

#define META_TYPE_SELECTION_SOURCE_MEMORY (meta_selection_source_memory_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaSelectionSourceMemory,
                      meta_selection_source_memory,
                      META, SELECTION_SOURCE_MEMORY,
                      MetaSelectionSource)

META_EXPORT
MetaSelectionSource * meta_selection_source_memory_new (const char  *mimetype,
                                                        GBytes      *content,
                                                        GError     **error);
