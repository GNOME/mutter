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

#include <libeis.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-eis.h"

#define META_TYPE_EIS_CLIENT (meta_eis_client_get_type ())
G_DECLARE_FINAL_TYPE (MetaEisClient, meta_eis_client,
                      META, EIS_CLIENT, GObject)

MetaEisClient *meta_eis_client_new (MetaEis           *eis,
                                    struct eis_client *eis_client);

gboolean meta_eis_client_process_event (MetaEisClient    *client,
                                        struct eis_event *eis_event);
