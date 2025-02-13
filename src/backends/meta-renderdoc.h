/*
 * Copyright (C) 2024 Red Hat, Inc.
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
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-private.h"

#define META_TYPE_RENDERDOC (meta_renderdoc_get_type ())
G_DECLARE_FINAL_TYPE (MetaRenderdoc,
                      meta_renderdoc,
                      META, RENDERDOC,
                      GObject)

MetaRenderdoc *meta_renderdoc_new (MetaBackend *backend);

void meta_renderdoc_queue_capture_all (MetaRenderdoc *renderdoc);
