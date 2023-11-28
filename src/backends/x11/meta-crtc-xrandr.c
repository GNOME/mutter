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

#include "backends/x11/meta-crtc-xrandr.h"

#include <X11/Xlib-xcb.h>
#include <stdlib.h>
#include <xcb/randr.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-output.h"
#include "backends/x11/meta-crtc-xrandr.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"

struct _MetaCrtcXrandr
{
  MetaCrtc parent;

  MtkRectangle rect;
  MetaMonitorTransform transform;
  MetaCrtcMode *current_mode;
};

G_DEFINE_TYPE (MetaCrtcXrandr, meta_crtc_xrandr, META_TYPE_CRTC)

gboolean
meta_crtc_xrandr_set_config (MetaCrtcXrandr      *crtc_xrandr,
                             xcb_randr_crtc_t     xrandr_crtc,
                             xcb_timestamp_t      timestamp,
                             int                  x,
                             int                  y,
                             xcb_randr_mode_t     mode,
                             xcb_randr_rotation_t rotation,
                             xcb_randr_output_t  *outputs,
                             int                  n_outputs,
                             xcb_timestamp_t     *out_timestamp)
{
  MetaGpu *gpu = meta_crtc_get_gpu (META_CRTC (crtc_xrandr));
  MetaGpuXrandr *gpu_xrandr = META_GPU_XRANDR (gpu);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay;
  XRRScreenResources *resources;
  xcb_connection_t *xcb_conn;
  xcb_timestamp_t config_timestamp;
  xcb_randr_set_crtc_config_cookie_t cookie;
  xcb_randr_set_crtc_config_reply_t *reply;
  xcb_generic_error_t *xcb_error = NULL;

  xdisplay = meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  xcb_conn = XGetXCBConnection (xdisplay);
  resources = meta_gpu_xrandr_get_resources (gpu_xrandr);
  config_timestamp = resources->configTimestamp;
  cookie = xcb_randr_set_crtc_config (xcb_conn,
                                      xrandr_crtc,
                                      timestamp,
                                      config_timestamp,
                                      x, y,
                                      mode,
                                      rotation,
                                      n_outputs,
                                      outputs);
  reply = xcb_randr_set_crtc_config_reply (xcb_conn,
                                           cookie,
                                           &xcb_error);
  if (xcb_error || !reply)
    {
      free (xcb_error);
      free (reply);
      return FALSE;
    }

  *out_timestamp = reply->timestamp;
  free (reply);

  return TRUE;
}

static MetaMonitorTransform
meta_monitor_transform_from_xrandr (Rotation rotation)
{
  static const MetaMonitorTransform y_reflected_map[4] = {
    META_MONITOR_TRANSFORM_FLIPPED_180,
    META_MONITOR_TRANSFORM_FLIPPED_90,
    META_MONITOR_TRANSFORM_FLIPPED,
    META_MONITOR_TRANSFORM_FLIPPED_270
  };
  MetaMonitorTransform ret;

  switch (rotation & 0x7F)
    {
    default:
    case RR_Rotate_0:
      ret = META_MONITOR_TRANSFORM_NORMAL;
      break;
    case RR_Rotate_90:
      ret = META_MONITOR_TRANSFORM_90;
      break;
    case RR_Rotate_180:
      ret = META_MONITOR_TRANSFORM_180;
      break;
    case RR_Rotate_270:
      ret = META_MONITOR_TRANSFORM_270;
      break;
    }

  if (rotation & RR_Reflect_X)
    return ret + 4;
  else if (rotation & RR_Reflect_Y)
    return y_reflected_map[ret];
  else
    return ret;
}

#define ALL_ROTATIONS (RR_Rotate_0 | RR_Rotate_90 | RR_Rotate_180 | RR_Rotate_270)

static MetaMonitorTransform
meta_monitor_transform_from_xrandr_all (Rotation rotation)
{
  unsigned ret;

  /* Handle the common cases first (none or all) */
  if (rotation == 0 || rotation == RR_Rotate_0)
    return (1 << META_MONITOR_TRANSFORM_NORMAL);

  /* All rotations and one reflection -> all of them by composition */
  if ((rotation & ALL_ROTATIONS) &&
      ((rotation & RR_Reflect_X) || (rotation & RR_Reflect_Y)))
    return META_MONITOR_ALL_TRANSFORMS;

  ret = 1 << META_MONITOR_TRANSFORM_NORMAL;
  if (rotation & RR_Rotate_90)
    ret |= 1 << META_MONITOR_TRANSFORM_90;
  if (rotation & RR_Rotate_180)
    ret |= 1 << META_MONITOR_TRANSFORM_180;
  if (rotation & RR_Rotate_270)
    ret |= 1 << META_MONITOR_TRANSFORM_270;
  if (rotation & (RR_Rotate_0 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED;
  if (rotation & (RR_Rotate_90 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_90;
  if (rotation & (RR_Rotate_180 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_180;
  if (rotation & (RR_Rotate_270 | RR_Reflect_X))
    ret |= 1 << META_MONITOR_TRANSFORM_FLIPPED_270;

  return ret;
}

gboolean
meta_crtc_xrandr_is_assignment_changed (MetaCrtcXrandr     *crtc_xrandr,
                                        MetaCrtcAssignment *crtc_assignment)
{
  unsigned int i;

  if (crtc_xrandr->current_mode != crtc_assignment->mode)
    return TRUE;

  if (crtc_xrandr->rect.x != (int) roundf (crtc_assignment->layout.origin.x))
    return TRUE;

  if (crtc_xrandr->rect.y != (int) roundf (crtc_assignment->layout.origin.y))
    return TRUE;

  if (crtc_xrandr->transform != crtc_assignment->transform)
    return TRUE;

  for (i = 0; i < crtc_assignment->outputs->len; i++)
    {
      MetaOutput *output = ((MetaOutput**) crtc_assignment->outputs->pdata)[i];
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc != META_CRTC (crtc_xrandr))
        return TRUE;
    }

  return FALSE;
}

MetaCrtcMode *
meta_crtc_xrandr_get_current_mode (MetaCrtcXrandr *crtc_xrandr)
{
  return crtc_xrandr->current_mode;
}

MetaCrtcXrandr *
meta_crtc_xrandr_new (MetaGpuXrandr      *gpu_xrandr,
                      XRRCrtcInfo        *xrandr_crtc,
                      RRCrtc              crtc_id,
                      XRRScreenResources *resources)
{
  MetaGpu *gpu = META_GPU (gpu_xrandr);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerXrandr *monitor_manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (monitor_manager);
  Display *xdisplay =
    meta_monitor_manager_xrandr_get_xdisplay (monitor_manager_xrandr);
  MetaMonitorTransform all_transforms;
  MetaCrtcXrandr *crtc_xrandr;
  XRRPanning *panning;
  unsigned int i;
  GList *modes;

  all_transforms =
    meta_monitor_transform_from_xrandr_all (xrandr_crtc->rotations);
  crtc_xrandr = g_object_new (META_TYPE_CRTC_XRANDR,
                              "id", (uint64_t) crtc_id,
                              "backend", backend,
                              "gpu", gpu,
                              "all-transforms", all_transforms,
                              NULL);

  crtc_xrandr->transform =
    meta_monitor_transform_from_xrandr (xrandr_crtc->rotation);

  panning = XRRGetPanning (xdisplay, resources, crtc_id);
  if (panning && panning->width > 0 && panning->height > 0)
    {
      crtc_xrandr->rect = (MtkRectangle) {
        .x = panning->left,
        .y = panning->top,
        .width = panning->width,
        .height = panning->height,
      };
    }
  else
    {
      crtc_xrandr->rect = (MtkRectangle) {
        .x = xrandr_crtc->x,
        .y = xrandr_crtc->y,
        .width = xrandr_crtc->width,
        .height = xrandr_crtc->height,
      };
    }
  XRRFreePanning (panning);

  modes = meta_gpu_get_modes (gpu);
  for (i = 0; i < (unsigned int) resources->nmode; i++)
    {
      if (resources->modes[i].id == xrandr_crtc->mode)
        {
          crtc_xrandr->current_mode = g_list_nth_data (modes, i);
          break;
        }
    }

  if (crtc_xrandr->current_mode)
    {
      MetaCrtcConfig *crtc_config;

      crtc_config =
        meta_crtc_config_new (&GRAPHENE_RECT_INIT (crtc_xrandr->rect.x,
                                                   crtc_xrandr->rect.y,
                                                   crtc_xrandr->rect.width,
                                                   crtc_xrandr->rect.height),
                              crtc_xrandr->current_mode,
                              crtc_xrandr->transform);
      meta_crtc_set_config (META_CRTC (crtc_xrandr), crtc_config, NULL);
    }

  return crtc_xrandr;
}

static MetaGammaLut *
meta_crtc_xrandr_get_gamma_lut (MetaCrtc *crtc)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  Display *xdisplay =
    meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XRRCrtcGamma *gamma;
  MetaGammaLut *lut;

  gamma = XRRGetCrtcGamma (xdisplay, (XID) meta_crtc_get_id (crtc));

  lut = g_new0 (MetaGammaLut, 1);
  lut->size = gamma->size;
  lut->red = g_memdup2 (gamma->red, sizeof (unsigned short) * gamma->size);
  lut->green = g_memdup2 (gamma->green, sizeof (unsigned short) * gamma->size);
  lut->blue = g_memdup2 (gamma->blue, sizeof (unsigned short) * gamma->size);

  XRRFreeGamma (gamma);

  return lut;
}

static size_t
meta_crtc_xrandr_get_gamma_lut_size (MetaCrtc *crtc)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  Display *xdisplay =
    meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XRRCrtcGamma *gamma;
  size_t size;

  gamma = XRRGetCrtcGamma (xdisplay, (XID) meta_crtc_get_id (crtc));

  size = gamma->size;

  XRRFreeGamma (gamma);

  return size;
}

static void
meta_crtc_xrandr_set_gamma_lut (MetaCrtc           *crtc,
                                const MetaGammaLut *lut)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  Display *xdisplay =
    meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
  XRRCrtcGamma *gamma;

  gamma = XRRAllocGamma (lut->size);
  memcpy (gamma->red, lut->red, sizeof (uint16_t) * lut->size);
  memcpy (gamma->green, lut->green, sizeof (uint16_t) * lut->size);
  memcpy (gamma->blue, lut->blue, sizeof (uint16_t) * lut->size);

  XRRSetCrtcGamma (xdisplay, (XID) meta_crtc_get_id (crtc), gamma);

  XRRFreeGamma (gamma);
}

static void
meta_crtc_xrandr_init (MetaCrtcXrandr *crtc_xrandr)
{
}

static void
meta_crtc_xrandr_class_init (MetaCrtcXrandrClass *klass)
{
  MetaCrtcClass *crtc_class = META_CRTC_CLASS (klass);

  crtc_class->get_gamma_lut_size = meta_crtc_xrandr_get_gamma_lut_size;
  crtc_class->get_gamma_lut = meta_crtc_xrandr_get_gamma_lut;
  crtc_class->set_gamma_lut = meta_crtc_xrandr_set_gamma_lut;
}
