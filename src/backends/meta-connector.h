/*
 * Copyright (C) 2017-2025 Red Hat
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

#include "core/util-private.h"

/* The first 17 matches the values in drm_mode.h, the ones starting with
 * 1000 do not. */
typedef enum
{
  META_CONNECTOR_TYPE_Unknown = 0,
  META_CONNECTOR_TYPE_VGA = 1,
  META_CONNECTOR_TYPE_DVII = 2,
  META_CONNECTOR_TYPE_DVID = 3,
  META_CONNECTOR_TYPE_DVIA = 4,
  META_CONNECTOR_TYPE_Composite = 5,
  META_CONNECTOR_TYPE_SVIDEO = 6,
  META_CONNECTOR_TYPE_LVDS = 7,
  META_CONNECTOR_TYPE_Component = 8,
  META_CONNECTOR_TYPE_9PinDIN = 9,
  META_CONNECTOR_TYPE_DisplayPort = 10,
  META_CONNECTOR_TYPE_HDMIA = 11,
  META_CONNECTOR_TYPE_HDMIB = 12,
  META_CONNECTOR_TYPE_TV = 13,
  META_CONNECTOR_TYPE_eDP = 14,
  META_CONNECTOR_TYPE_VIRTUAL = 15,
  META_CONNECTOR_TYPE_DSI = 16,
  META_CONNECTOR_TYPE_DPI = 17,
  META_CONNECTOR_TYPE_WRITEBACK = 18,
  META_CONNECTOR_TYPE_SPI = 19,
  META_CONNECTOR_TYPE_USB = 20,

  META_CONNECTOR_TYPE_META = 1000,
} MetaConnectorType;

META_EXPORT_TEST
const char * meta_connector_type_get_name (MetaConnectorType connector_type);
