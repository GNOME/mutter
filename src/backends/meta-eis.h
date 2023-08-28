/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-viewport-info.h"

#define META_TYPE_EIS (meta_eis_get_type ())
G_DECLARE_FINAL_TYPE (MetaEis, meta_eis,
                      META, EIS, GObject)

MetaEis * meta_eis_new (MetaBackend *backend);
MetaBackend * meta_eis_get_backend (MetaEis *eis);
int meta_eis_add_client_get_fd (MetaEis *eis);
