/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013-2017 Red Hat Inc.
 * Copyright (C) 2020 NVIDIA CORPORATION
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

#include "backends/x11/meta-output-xrandr.h"

#include <glib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <xcb/randr.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"
#include "meta/util.h"
#include "mtk/mtk-x11.h"

struct _MetaOutputXrandr
{
  MetaOutput parent;

  gboolean ctm_initialized;
  MetaOutputCtm ctm;
};

G_DEFINE_TYPE (MetaOutputXrandr, meta_output_xrandr, META_TYPE_OUTPUT)

static Display *
xdisplay_from_gpu (MetaGpu *gpu)
{
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);

  return meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
}

static Display *
xdisplay_from_output (MetaOutput *output)
{
  return xdisplay_from_gpu (meta_output_get_gpu (output));
}

static void
output_set_presentation_xrandr (MetaOutput *output,
                                gboolean    presentation)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom atom;
  int value = presentation;

  atom = XInternAtom (xdisplay, "_MUTTER_PRESENTATION_OUTPUT", False);

  xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                    (XID) meta_output_get_id (output),
                                    atom, XCB_ATOM_CARDINAL, 32,
                                    XCB_PROP_MODE_REPLACE,
                                    1, &value);
}

static void
output_set_underscanning_xrandr (MetaOutput *output,
                                 gboolean    underscanning)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom prop, valueatom;
  const char *value;

  prop = XInternAtom (xdisplay, "underscan", False);

  value = underscanning ? "on" : "off";
  valueatom = XInternAtom (xdisplay, value, False);

  xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                    (XID) meta_output_get_id (output),
                                    prop, XCB_ATOM_ATOM, 32,
                                    XCB_PROP_MODE_REPLACE,
                                    1, &valueatom);

  /* Configure the border at the same time. Currently, we use a
   * 5% of the width/height of the mode. In the future, we should
   * make the border configurable. */
  if (underscanning)
    {
      MetaCrtc *crtc;
      const MetaCrtcConfig *crtc_config;
      const MetaCrtcModeInfo *crtc_mode_info;
      uint32_t border_value;

      crtc = meta_output_get_assigned_crtc (output);
      crtc_config = meta_crtc_get_config (crtc);
      crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);

      prop = XInternAtom (xdisplay, "underscan hborder", False);
      border_value = (uint32_t) (crtc_mode_info->width * 0.05);

      xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                        (XID) meta_output_get_id (output),
                                        prop, XCB_ATOM_INTEGER, 32,
                                        XCB_PROP_MODE_REPLACE,
                                        1, &border_value);

      prop = XInternAtom (xdisplay, "underscan vborder", False);
      border_value = (uint32_t) (crtc_mode_info->height * 0.05);

      xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                        (XID) meta_output_get_id (output),
                                        prop, XCB_ATOM_INTEGER, 32,
                                        XCB_PROP_MODE_REPLACE,
                                        1, &border_value);
    }
}

static void
output_set_max_bpc_xrandr (MetaOutput   *output,
                           unsigned int  max_bpc)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom prop = XInternAtom (xdisplay, "max bpc", False);
  uint32_t value = max_bpc;

  xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                    (XID) meta_output_get_id (output),
                                    prop, XCB_ATOM_INTEGER, 32,
                                    XCB_PROP_MODE_REPLACE,
                                    1, &value);
}

void
meta_output_xrandr_apply_mode (MetaOutputXrandr *output_xrandr)
{
  MetaOutput *output = META_OUTPUT (output_xrandr);
  Display *xdisplay = xdisplay_from_output (output);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  unsigned int max_bpc;

  if (meta_output_is_primary (output))
    {
      XRRSetOutputPrimary (xdisplay, DefaultRootWindow (xdisplay),
                           (XID) meta_output_get_id (output));
    }

  output_set_presentation_xrandr (output, meta_output_is_presentation (output));

  if (meta_output_get_info (output)->supports_underscanning)
    {
      output_set_underscanning_xrandr (output,
                                       meta_output_is_underscanning (output));
    }

  if (meta_output_get_max_bpc (output, &max_bpc) &&
      max_bpc >= output_info->max_bpc_min &&
      max_bpc <= output_info->max_bpc_max)
    {
      output_set_max_bpc_xrandr (output, max_bpc);
    }
}

static gboolean
ctm_is_equal (const MetaOutputCtm *ctm1,
              const MetaOutputCtm *ctm2)
{
  int i;

  for (i = 0; i < 9; i++)
    {
      if (ctm1->matrix[i] != ctm2->matrix[i])
        return FALSE;
    }

  return TRUE;
}

void
meta_output_xrandr_set_ctm (MetaOutputXrandr *output_xrandr,
                            const MetaOutputCtm *ctm)
{
  if (!output_xrandr->ctm_initialized ||
      !ctm_is_equal (ctm, &output_xrandr->ctm))
    {
      MetaOutput *output = META_OUTPUT (output_xrandr);
      Display *xdisplay = xdisplay_from_output (output);
      Atom ctm_atom = XInternAtom (xdisplay, "CTM", False);

      xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                        (XID) meta_output_get_id (output),
                                        ctm_atom, XCB_ATOM_INTEGER, 32,
                                        XCB_PROP_MODE_REPLACE,
                                        18, &ctm->matrix);

      output_xrandr->ctm = *ctm;
      output_xrandr->ctm_initialized = TRUE;
    }
}

static gboolean
output_get_integer_property (Display    *xdisplay,
                             RROutput    output_id,
                             const char *propname,
                             gint       *value)
{
  gboolean exists = FALSE;
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *buffer;

  atom = XInternAtom (xdisplay, propname, False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_INTEGER,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  exists = (actual_type == XA_INTEGER && actual_format == 32 && nitems == 1);

  if (exists && value != NULL)
    *value = ((int*)buffer)[0];

  XFree (buffer);
  return exists;
}

static gboolean
output_get_property_exists (Display    *xdisplay,
                            RROutput    output_id,
                            const char *propname)
{
  gboolean exists = FALSE;
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *buffer;

  atom = XInternAtom (xdisplay, propname, False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, AnyPropertyType,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  exists = (actual_type != None);

  XFree (buffer);
  return exists;
}

static gboolean
output_get_boolean_property (MetaOutput *output,
                             const char *propname)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (xdisplay, propname, False);
  XRRGetOutputProperty (xdisplay,
                        (XID) meta_output_get_id (output),
                        atom,
                        0, G_MAXLONG, False, False, XA_CARDINAL,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_CARDINAL || actual_format != 32 || nitems < 1)
    return FALSE;

  return ((int*)buffer)[0];
}

static gboolean
output_get_presentation_xrandr (MetaOutput *output)
{
  return output_get_boolean_property (output, "_MUTTER_PRESENTATION_OUTPUT");
}

static gboolean
output_get_underscanning_xrandr (MetaOutput *output)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;
  g_autofree char *str = NULL;

  atom = XInternAtom (xdisplay, "underscan", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) meta_output_get_id (output),
                        atom,
                        0, G_MAXLONG, False, False, XA_ATOM,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_ATOM || actual_format != 32 || nitems < 1)
    return FALSE;

  str = XGetAtomName (xdisplay, *(Atom *)buffer);
  return (strcmp (str, "on") == 0);
}

static gboolean
output_get_max_bpc_xrandr (MetaOutput   *output,
                           unsigned int *max_bpc)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (xdisplay, "max bpc", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) meta_output_get_id (output),
                        atom,
                        0, G_MAXLONG, False, False, XCB_ATOM_INTEGER,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XCB_ATOM_INTEGER || actual_format != 32 || nitems < 1)
    return FALSE;

  if (max_bpc)
    *max_bpc = *((uint32_t*) buffer);

  return TRUE;
}

static gboolean
output_get_supports_underscanning_xrandr (Display  *xdisplay,
                                          RROutput  output_id)
{
  Atom atom, actual_type;
  int actual_format, i;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;
  XRRPropertyInfo *property_info;
  Atom *values;
  gboolean supports_underscanning = FALSE;

  atom = XInternAtom (xdisplay, "underscan", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_ATOM,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_ATOM || actual_format != 32 || nitems < 1)
    return FALSE;

  property_info = XRRQueryOutputProperty (xdisplay,
                                          (XID) output_id,
                                          atom);
  values = (Atom *) property_info->values;

  for (i = 0; i < property_info->num_values; i++)
    {
      /* The output supports underscanning if "on" is a valid value
       * for the underscan property.
       */
      char *name = XGetAtomName (xdisplay, values[i]);
      if (strcmp (name, "on") == 0)
        supports_underscanning = TRUE;

      XFree (name);
    }

  XFree (property_info);

  return supports_underscanning;
}

static gboolean
output_get_max_bpc_range_xrandr (Display      *xdisplay,
                                 RROutput      output_id,
                                 unsigned int *min,
                                 unsigned int *max)
{
  Atom atom;
  XRRPropertyInfo *property_info;
  long *values;

  atom = XInternAtom (xdisplay, "max bpc", False);

  mtk_x11_error_trap_push (xdisplay);
  property_info = XRRQueryOutputProperty (xdisplay,
                                          (XID) output_id,
                                          atom);
  mtk_x11_error_trap_pop (xdisplay);

  if (!property_info)
    return FALSE;

  if (property_info->num_values != 2)
    {
      XFree (property_info);
      return FALSE;
    }

  values = (long *) property_info->values;
  if (min)
    *min = values[0];
  if (max)
    *max = values[1];

  XFree (property_info);

  return TRUE;
}

static gboolean
output_get_supports_color_transform_xrandr (Display  *xdisplay,
                                            RROutput  output_id)
{
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (xdisplay, "CTM", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_INTEGER,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  /*
   * X's CTM property is 9 64-bit integers represented as an array of 18 32-bit
   * integers.
   */
  return (actual_type == XA_INTEGER &&
          actual_format == 32 &&
          nitems == 18);
}

static int
output_get_backlight_xrandr (MetaOutput *output)
{
  Display *xdisplay = xdisplay_from_output (output);
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (xdisplay, "Backlight", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) meta_output_get_id (output),
                        atom,
                        0, G_MAXLONG, False, False, XA_INTEGER,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_INTEGER || actual_format != 32 || nitems < 1)
    return -1;

  return ((int *) buffer)[0];
}

static void
output_info_init_backlight_limits_xrandr (MetaOutputInfo     *output_info,
                                          Display            *xdisplay,
                                          xcb_randr_output_t  output_id)
{
  Atom atom;
  xcb_connection_t *xcb_conn;
  xcb_randr_query_output_property_cookie_t cookie;
  g_autofree xcb_randr_query_output_property_reply_t *reply = NULL;

  atom = XInternAtom (xdisplay, "Backlight", False);

  xcb_conn = XGetXCBConnection (xdisplay);
  cookie = xcb_randr_query_output_property (xcb_conn,
                                            output_id,
                                            (xcb_atom_t) atom);
  reply = xcb_randr_query_output_property_reply (xcb_conn,
                                                 cookie,
                                                 NULL);

  /* This can happen on systems without backlights. */
  if (reply == NULL)
    return;

  if (!reply->range || reply->length != 2)
    {
      meta_verbose ("backlight %s was not range", output_info->name);
      return;
    }

  int32_t *values = xcb_randr_query_output_property_valid_values (reply);
  output_info->backlight_min = values[0];
  output_info->backlight_max = values[1];
}

static guint8 *
get_edid_property (Display  *xdisplay,
                   RROutput  output,
                   Atom      atom,
                   gsize    *len)
{
  unsigned char *prop;
  int actual_format;
  unsigned long nitems, bytes_after;
  Atom actual_type;
  guint8 *result;

  XRRGetOutputProperty (xdisplay, output, atom,
                        0, 100, False, False,
                        AnyPropertyType,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &prop);

  if (actual_type == XA_INTEGER && actual_format == 8)
    {
      result = g_memdup2 (prop, nitems);
      if (len)
        *len = nitems;
    }
  else
    {
      result = NULL;
    }

  XFree (prop);

  return result;
}

static GBytes *
read_xrandr_edid (Display  *xdisplay,
                  RROutput  output_id)
{
  Atom edid_atom;
  guint8 *result;
  gsize len;

  edid_atom = XInternAtom (xdisplay, "EDID", FALSE);
  result = get_edid_property (xdisplay, output_id, edid_atom, &len);

  if (!result)
    {
      edid_atom = XInternAtom (xdisplay, "EDID_DATA", FALSE);
      result = get_edid_property (xdisplay, output_id, edid_atom, &len);
    }

  if (result)
    {
      if (len > 0 && len % 128 == 0)
        return g_bytes_new_take (result, len);
      else
        g_free (result);
    }

  return NULL;
}

GBytes *
meta_output_xrandr_read_edid (MetaOutput *output)
{
  Display *xdisplay = xdisplay_from_output (output);
  RROutput output_id = (RROutput) meta_output_get_id (output);

  return read_xrandr_edid (xdisplay, output_id);
}

static gboolean
output_get_hotplug_mode_update (Display  *xdisplay,
                                RROutput  output_id)
{
  return output_get_property_exists (xdisplay, output_id, "hotplug_mode_update");
}

static gint
output_get_suggested_x (Display  *xdisplay,
                        RROutput  output_id)
{
  gint val;
  if (output_get_integer_property (xdisplay, output_id, "suggested X", &val))
    return val;

  return -1;
}

static gint
output_get_suggested_y (Display  *xdisplay,
                        RROutput  output_id)
{
  gint val;
  if (output_get_integer_property (xdisplay, output_id, "suggested Y", &val))
    return val;

  return -1;
}

static MetaConnectorType
connector_type_from_atom (Display *xdisplay,
                          Atom     atom)
{
  if (atom == XInternAtom (xdisplay, "HDMI", True))
    return META_CONNECTOR_TYPE_HDMIA;
  if (atom == XInternAtom (xdisplay, "VGA", True))
    return META_CONNECTOR_TYPE_VGA;
  /* Doesn't have a DRM equivalent, but means an internal panel.
   * We could pick either LVDS or eDP here. */
  if (atom == XInternAtom (xdisplay, "Panel", True))
    return META_CONNECTOR_TYPE_LVDS;
  if (atom == XInternAtom (xdisplay, "DVI", True) ||
      atom == XInternAtom (xdisplay, "DVI-I", True))
    return META_CONNECTOR_TYPE_DVII;
  if (atom == XInternAtom (xdisplay, "DVI-A", True))
    return META_CONNECTOR_TYPE_DVIA;
  if (atom == XInternAtom (xdisplay, "DVI-D", True))
    return META_CONNECTOR_TYPE_DVID;
  if (atom == XInternAtom (xdisplay, "DisplayPort", True))
    return META_CONNECTOR_TYPE_DisplayPort;

  if (atom == XInternAtom (xdisplay, "TV", True))
    return META_CONNECTOR_TYPE_TV;
  if (atom == XInternAtom (xdisplay, "TV-Composite", True))
    return META_CONNECTOR_TYPE_Composite;
  if (atom == XInternAtom (xdisplay, "TV-SVideo", True))
    return META_CONNECTOR_TYPE_SVIDEO;
  /* Another set of mismatches. */
  if (atom == XInternAtom (xdisplay, "TV-SCART", True))
    return META_CONNECTOR_TYPE_TV;
  if (atom == XInternAtom (xdisplay, "TV-C4", True))
    return META_CONNECTOR_TYPE_TV;

  return META_CONNECTOR_TYPE_Unknown;
}

static MetaConnectorType
output_get_connector_type_from_prop (Display  *xdisplay,
                                     RROutput  output_id)
{
  Atom atom, actual_type, connector_type_atom;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (xdisplay, "ConnectorType", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_ATOM,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_ATOM || actual_format != 32 || nitems < 1)
    return META_CONNECTOR_TYPE_Unknown;

  connector_type_atom = ((Atom *) buffer)[0];
  return connector_type_from_atom (xdisplay, connector_type_atom);
}

static MetaConnectorType
output_info_get_connector_type_from_name (const MetaOutputInfo *output_info)
{
  const char *name = output_info->name;

  /* drmmode_display.c, which was copy/pasted across all the FOSS
   * xf86-video-* drivers, seems to name its outputs based on the
   * connector type, so look for that....
   *
   * SNA has its own naming scheme, because what else did you expect
   * from SNA, but it's not too different, so we can thankfully use
   * that with minor changes.
   *
   * http://cgit.freedesktop.org/xorg/xserver/tree/hw/xfree86/drivers/modesetting/drmmode_display.c#n953
   * http://cgit.freedesktop.org/xorg/driver/xf86-video-intel/tree/src/sna/sna_display.c#n3486
   */

  if (g_str_has_prefix (name, "DVI"))
    return META_CONNECTOR_TYPE_DVII;
  if (g_str_has_prefix (name, "LVDS"))
    return META_CONNECTOR_TYPE_LVDS;
  if (g_str_has_prefix (name, "HDMI"))
    return META_CONNECTOR_TYPE_HDMIA;
  if (g_str_has_prefix (name, "VGA"))
    return META_CONNECTOR_TYPE_VGA;
  if (g_str_has_prefix (name, "DPI"))
    return META_CONNECTOR_TYPE_DPI;
  /* SNA uses DP, not DisplayPort. Test for both. */
  if (g_str_has_prefix (name, "DP") || g_str_has_prefix (name, "DisplayPort"))
    return META_CONNECTOR_TYPE_DisplayPort;
  if (g_str_has_prefix (name, "eDP"))
    return META_CONNECTOR_TYPE_eDP;
  if (g_str_has_prefix (name, "Virtual"))
    return META_CONNECTOR_TYPE_VIRTUAL;
  if (g_str_has_prefix (name, "Composite"))
    return META_CONNECTOR_TYPE_Composite;
  if (g_str_has_prefix (name, "S-video"))
    return META_CONNECTOR_TYPE_SVIDEO;
  if (g_str_has_prefix (name, "TV"))
    return META_CONNECTOR_TYPE_TV;
  if (g_str_has_prefix (name, "CTV"))
    return META_CONNECTOR_TYPE_Composite;
  if (g_str_has_prefix (name, "DSI"))
    return META_CONNECTOR_TYPE_DSI;
  if (g_str_has_prefix (name, "DIN"))
    return META_CONNECTOR_TYPE_9PinDIN;

  return META_CONNECTOR_TYPE_Unknown;
}

static MetaConnectorType
output_info_get_connector_type (MetaOutputInfo *output_info,
                                Display        *xdisplay,
                                RROutput        output_id)
{
  MetaConnectorType ret;

  /* The "ConnectorType" property is considered mandatory since RandR 1.3,
   * but none of the FOSS drivers support it, because we're a bunch of
   * professional software developers.
   *
   * Try poking it first, without any expectations that it will work.
   * If it's not there, we thankfully have other bonghits to try next.
   */
  ret = output_get_connector_type_from_prop (xdisplay, output_id);
  if (ret != META_CONNECTOR_TYPE_Unknown)
    return ret;

  /* Fall back to heuristics based on the output name. */
  ret = output_info_get_connector_type_from_name (output_info);
  if (ret != META_CONNECTOR_TYPE_Unknown)
    return ret;

  return META_CONNECTOR_TYPE_Unknown;
}

static gint
output_get_panel_orientation_transform (Display  *xdisplay,
                                        RROutput  output_id)
{
  unsigned long nitems, bytes_after;
  Atom atom, actual_type;
  int actual_format;
  g_autofree unsigned char *buffer = NULL;
  g_autofree char *str = NULL;

  atom = XInternAtom (xdisplay, "panel orientation", False);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_ATOM,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_ATOM || actual_format != 32 || nitems < 1)
    return MTK_MONITOR_TRANSFORM_NORMAL;

  str = XGetAtomName (xdisplay, *(Atom *)buffer);
  if (strcmp (str, "Upside Down") == 0)
    return MTK_MONITOR_TRANSFORM_180;

  if (strcmp (str, "Left Side Up") == 0)
    return MTK_MONITOR_TRANSFORM_90;

  if (strcmp (str, "Right Side Up") == 0)
    return MTK_MONITOR_TRANSFORM_270;

  return MTK_MONITOR_TRANSFORM_NORMAL;
}

static void
output_info_init_tile_info (MetaOutputInfo *output_info,
                            Display        *xdisplay,
                            RROutput        output_id)
{
  Atom tile_atom;
  unsigned char *prop;
  unsigned long nitems, bytes_after;
  int actual_format;
  Atom actual_type;

  tile_atom = XInternAtom (xdisplay, "TILE", FALSE);
  XRRGetOutputProperty (xdisplay,
                        (XID) output_id,
                        tile_atom, 0, 100, False,
                        False, AnyPropertyType,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &prop);

  if (actual_type == XA_INTEGER && actual_format == 32 && nitems == 8)
    {
      long *values = (long *)prop;

      output_info->tile_info.group_id = values[0];
      output_info->tile_info.flags = values[1];
      output_info->tile_info.max_h_tiles = values[2];
      output_info->tile_info.max_v_tiles = values[3];
      output_info->tile_info.loc_h_tile = values[4];
      output_info->tile_info.loc_v_tile = values[5];
      output_info->tile_info.tile_w = values[6];
      output_info->tile_info.tile_h = values[7];
    }
  XFree (prop);
}

static gboolean
sanity_check_duplicate (MetaCrtcMode **modes,
                        size_t         n_modes,
                        MetaCrtcMode  *mode)
{
  size_t i;

  for (i = 0; i < n_modes; i++)
    {
      if (meta_crtc_mode_get_id (modes[i]) == meta_crtc_mode_get_id (mode))
        return FALSE;
    }

  return TRUE;
}

static void
output_info_init_modes (MetaOutputInfo *output_info,
                        MetaGpu        *gpu,
                        XRROutputInfo  *xrandr_output)
{
  unsigned int i;
  unsigned int n_actual_modes;

  output_info->modes = g_new0 (MetaCrtcMode *, xrandr_output->nmode);

  n_actual_modes = 0;
  for (i = 0; i < (unsigned int) xrandr_output->nmode; i++)
    {
      GList *l;

      for (l = meta_gpu_get_modes (gpu); l; l = l->next)
        {
          MetaCrtcMode *mode = l->data;

          if (xrandr_output->modes[i] == (XID) meta_crtc_mode_get_id (mode))
            {
              if (sanity_check_duplicate (output_info->modes, n_actual_modes, mode))
                {
                  output_info->modes[n_actual_modes] = g_object_ref (mode);
                  n_actual_modes += 1;
                }
              else
                {
                  g_warning ("X11 server advertized duplicate identical modes "
                             "(0x%" G_GINT64_MODIFIER "x)",
                             meta_crtc_mode_get_id (mode));
                }
              break;
            }
        }
    }
  output_info->n_modes = n_actual_modes;
  if (n_actual_modes > 0)
    output_info->preferred_mode = output_info->modes[0];
}

static void
output_info_init_crtcs (MetaOutputInfo *output_info,
                        MetaGpu        *gpu,
                        XRROutputInfo  *xrandr_output)
{
  unsigned int i;
  unsigned int n_actual_crtcs;
  GList *l;

  output_info->possible_crtcs = g_new0 (MetaCrtc *, xrandr_output->ncrtc);

  n_actual_crtcs = 0;
  for (i = 0; i < (unsigned int) xrandr_output->ncrtc; i++)
    {
      for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;

          if ((XID) meta_crtc_get_id (crtc) == xrandr_output->crtcs[i])
            {
              output_info->possible_crtcs[n_actual_crtcs] = crtc;
              n_actual_crtcs += 1;
              break;
            }
        }
    }
  output_info->n_possible_crtcs = n_actual_crtcs;
}

static MetaCrtc *
find_assigned_crtc (MetaGpu       *gpu,
                    XRROutputInfo *xrandr_output)
{
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if ((XID) meta_crtc_get_id (crtc) == xrandr_output->crtc)
        return crtc;
    }

  return NULL;
}

static void
on_backlight_changed (MetaOutput *output)
{
  Display *xdisplay = xdisplay_from_output (output);
  int value = meta_output_get_backlight (output);
  Atom atom;

  atom = XInternAtom (xdisplay, "Backlight", False);

  xcb_randr_change_output_property (XGetXCBConnection (xdisplay),
                                    (XID) meta_output_get_id (output),
                                    atom, XCB_ATOM_INTEGER, 32,
                                    XCB_PROP_MODE_REPLACE,
                                    1, &value);
}

MetaOutputXrandr *
meta_output_xrandr_new (MetaGpuXrandr *gpu_xrandr,
                        XRROutputInfo *xrandr_output,
                        RROutput       output_id,
                        RROutput       primary_output)
{
  MetaGpu *gpu = META_GPU (gpu_xrandr);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay =
    meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  g_autoptr (MetaOutputInfo) output_info = NULL;
  MetaOutput *output;
  GBytes *edid;
  MetaCrtc *assigned_crtc;
  unsigned int i;

  output_info = meta_output_info_new ();

  output_info->name = g_strdup (xrandr_output->name);

  edid = read_xrandr_edid (xdisplay, output_id);
  if (edid)
    {
      meta_output_info_parse_edid (output_info, edid);
      g_bytes_unref (edid);
    }

  output_info->subpixel_order = META_SUBPIXEL_ORDER_UNKNOWN;
  output_info->hotplug_mode_update = output_get_hotplug_mode_update (xdisplay,
                                                                     output_id);
  output_info->suggested_x = output_get_suggested_x (xdisplay, output_id);
  output_info->suggested_y = output_get_suggested_y (xdisplay, output_id);
  output_info->connector_type = output_info_get_connector_type (output_info,
                                                                xdisplay,
                                                                output_id);
  output_info->panel_orientation_transform =
    output_get_panel_orientation_transform (xdisplay, output_id);

  if (mtk_monitor_transform_is_rotated (output_info->panel_orientation_transform))
    {
      output_info->width_mm = xrandr_output->mm_height;
      output_info->height_mm = xrandr_output->mm_width;
    }
  else
    {
      output_info->width_mm = xrandr_output->mm_width;
      output_info->height_mm = xrandr_output->mm_height;
    }

  if (meta_monitor_manager_xrandr_has_randr15 (monitor_manager_xrandr))
    output_info_init_tile_info (output_info, xdisplay, output_id);
  output_info_init_modes (output_info, gpu, xrandr_output);
  output_info_init_crtcs (output_info, gpu, xrandr_output);

  output_info->n_possible_clones = xrandr_output->nclone;
  output_info->possible_clones = g_new0 (MetaOutput *,
                                         output_info->n_possible_clones);
  /*
   * We can build the list of clones now, because we don't have the list of
   * outputs yet, so temporarily set the pointers to the bare XIDs, and then
   * we'll fix them in a second pass.
   */
  for (i = 0; i < (unsigned int) xrandr_output->nclone; i++)
    {
      output_info->possible_clones[i] = GINT_TO_POINTER (xrandr_output->clones[i]);
    }

  output_info->supports_underscanning =
    output_get_supports_underscanning_xrandr (xdisplay, output_id);
  output_get_max_bpc_range_xrandr (xdisplay,
                                   output_id,
                                   &output_info->max_bpc_min,
                                   &output_info->max_bpc_max);
  output_info->supports_color_transform =
    output_get_supports_color_transform_xrandr (xdisplay, output_id);
  output_info_init_backlight_limits_xrandr (output_info, xdisplay, output_id);

  output = g_object_new (META_TYPE_OUTPUT_XRANDR,
                         "id", (uint64_t) output_id,
                         "gpu", gpu_xrandr,
                         "info", output_info,
                         NULL);

  assigned_crtc = find_assigned_crtc (gpu, xrandr_output);
  if (assigned_crtc)
    {
      MetaOutputAssignment output_assignment;

      output_assignment = (MetaOutputAssignment) {
        .is_primary = (XID) meta_output_get_id (output) == primary_output,
        .is_presentation = output_get_presentation_xrandr (output),
        .is_underscanning = output_get_underscanning_xrandr (output),
      };
      output_assignment.has_max_bpc =
        output_get_max_bpc_xrandr (output, &output_assignment.max_bpc);

      meta_output_assign_crtc (output, assigned_crtc, &output_assignment);
    }
  else
    {
      meta_output_unassign_crtc (output);
    }

  if (!(output_info->backlight_min == 0 && output_info->backlight_max == 0))
    {
      meta_output_set_backlight (output, output_get_backlight_xrandr (output));
      g_signal_connect (output, "backlight-changed",
                        G_CALLBACK (on_backlight_changed), NULL);
    }

  if (output_info->n_modes == 0 || output_info->n_possible_crtcs == 0)
    {
      g_object_unref (output);
      return NULL;
    }
  else
    {
      return META_OUTPUT_XRANDR (output);
    }
}

static void
meta_output_xrandr_init (MetaOutputXrandr *output_xrandr)
{
}

static void
meta_output_xrandr_class_init (MetaOutputXrandrClass *klass)
{
}
