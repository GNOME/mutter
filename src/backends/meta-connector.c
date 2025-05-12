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

#include "config.h"

#include "backends/meta-connector.h"

const char *
meta_connector_type_get_name (MetaConnectorType connector_type)
{
  switch (connector_type)
    {
    case META_CONNECTOR_TYPE_Unknown:
      return "None";
    case META_CONNECTOR_TYPE_VGA:
      return "VGA";
    case META_CONNECTOR_TYPE_DVII:
      return "DVI-I";
    case META_CONNECTOR_TYPE_DVID:
      return "DVI-D";
    case META_CONNECTOR_TYPE_DVIA:
      return "DVI-A";
    case META_CONNECTOR_TYPE_Composite:
      return "Composite";
    case META_CONNECTOR_TYPE_SVIDEO:
      return "SVIDEO";
    case META_CONNECTOR_TYPE_LVDS:
      return "LVDS";
    case META_CONNECTOR_TYPE_Component:
      return "Component";
    case META_CONNECTOR_TYPE_9PinDIN:
      return "DIN";
    case META_CONNECTOR_TYPE_DisplayPort:
      return "DP";
    case META_CONNECTOR_TYPE_HDMIA:
      return "HDMI";
    case META_CONNECTOR_TYPE_HDMIB:
      return "HDMI-B";
    case META_CONNECTOR_TYPE_TV:
      return "TV";
    case META_CONNECTOR_TYPE_eDP:
      return "eDP";
    case META_CONNECTOR_TYPE_VIRTUAL:
      return "Virtual";
    case META_CONNECTOR_TYPE_DSI:
      return "DSI";
    case META_CONNECTOR_TYPE_DPI:
      return "DPI";
    case META_CONNECTOR_TYPE_WRITEBACK:
      return "WRITEBACK";
    case META_CONNECTOR_TYPE_SPI:
      return "SPI";
    case META_CONNECTOR_TYPE_USB:
      return "USB";
    case META_CONNECTOR_TYPE_META:
      return "Meta";
    default:
      g_warn_if_reached ();
      return NULL;
    }
  return NULL;
}
