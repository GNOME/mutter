/*
 * Copyright (C) 2019 Red Hat Inc.
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
 */

#pragma once

#include "compositor/compositor-private.h"

#define META_TYPE_COMPOSITOR_SERVER (meta_compositor_server_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCompositorServer, meta_compositor_server,
                          META, COMPOSITOR_SERVER, MetaCompositor)

struct _MetaCompositorServerClass
{
  MetaCompositorClass parent_class;
};

MetaCompositorServer * meta_compositor_server_new (MetaDisplay *display,
                                                   MetaBackend *backend);
