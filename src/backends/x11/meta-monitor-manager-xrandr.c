/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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

/**
 * SECTION:meta-monitor-manager-xrandr
 * @title: MetaMonitorManagerXrandr
 * @short_description: A subclass of #MetaMonitorManager using XRadR
 *
 * #MetaMonitorManagerXrandr is a subclass of #MetaMonitorManager which
 * implements its functionality using the RandR X protocol.
 *
 * See also #MetaMonitorManagerKms for a native implementation using Linux DRM
 * and udev.
 */

#include "config.h"

#include "backends/x11/meta-monitor-manager-xrandr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlibint.h>
#include <X11/extensions/dpms.h>
#include <xcb/randr.h>

#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-output.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-crtc-xrandr.h"
#include "backends/x11/meta-gpu-xrandr.h"
#include "backends/x11/meta-output-xrandr.h"
#include "clutter/clutter.h"
#include "meta/main.h"
#include "meta/meta-x11-errors.h"

/* Look for DPI_FALLBACK in:
 * http://git.gnome.org/browse/gnome-settings-daemon/tree/plugins/xsettings/gsd-xsettings-manager.c
 * for the reasoning */
#define DPI_FALLBACK 96.0

struct _MetaMonitorManagerXrandr
{
  MetaMonitorManager parent_instance;

  Display *xdisplay;
  int rr_event_base;
  int rr_error_base;
  gboolean has_randr15;

  xcb_timestamp_t last_xrandr_set_timestamp;

  GHashTable *tiled_monitor_atoms;

  float *supported_scales;
  int n_supported_scales;
};

struct _MetaMonitorManagerXrandrClass
{
  MetaMonitorManagerClass parent_class;
};

G_DEFINE_TYPE (MetaMonitorManagerXrandr, meta_monitor_manager_xrandr, META_TYPE_MONITOR_MANAGER);

typedef struct _MetaMonitorXrandrData
{
  Atom xrandr_name;
} MetaMonitorXrandrData;

GQuark quark_meta_monitor_xrandr_data;

Display *
meta_monitor_manager_xrandr_get_xdisplay (MetaMonitorManagerXrandr *manager_xrandr)
{
  return manager_xrandr->xdisplay;
}

gboolean
meta_monitor_manager_xrandr_has_randr15 (MetaMonitorManagerXrandr *manager_xrandr)
{
  return manager_xrandr->has_randr15;
}

static GBytes *
meta_monitor_manager_xrandr_read_edid (MetaMonitorManager *manager,
                                       MetaOutput         *output)
{
  return meta_output_xrandr_read_edid (output);
}

static MetaPowerSave
x11_dpms_state_to_power_save (CARD16 dpms_state)
{
  switch (dpms_state)
    {
    case DPMSModeOn:
      return META_POWER_SAVE_ON;
    case DPMSModeStandby:
      return META_POWER_SAVE_STANDBY;
    case DPMSModeSuspend:
      return META_POWER_SAVE_SUSPEND;
    case DPMSModeOff:
      return META_POWER_SAVE_OFF;
    default:
      return META_POWER_SAVE_UNSUPPORTED;
    }
}

static void
meta_monitor_manager_xrandr_read_current_state (MetaMonitorManager *manager)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorManagerClass *parent_class =
    META_MONITOR_MANAGER_CLASS (meta_monitor_manager_xrandr_parent_class);
  Display *xdisplay = meta_monitor_manager_xrandr_get_xdisplay (manager_xrandr);
  BOOL dpms_capable, dpms_enabled;
  CARD16 dpms_state;
  MetaPowerSave power_save_mode;

  dpms_capable = DPMSCapable (xdisplay);

  if (dpms_capable &&
      DPMSInfo (xdisplay, &dpms_state, &dpms_enabled) &&
      dpms_enabled)
    power_save_mode = x11_dpms_state_to_power_save (dpms_state);
  else
    power_save_mode = META_POWER_SAVE_UNSUPPORTED;

  meta_monitor_manager_power_save_mode_changed (manager, power_save_mode);

  parent_class->read_current_state (manager);
}

static void
meta_monitor_manager_xrandr_set_power_save_mode (MetaMonitorManager *manager,
						 MetaPowerSave       mode)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  CARD16 state;

  switch (mode) {
  case META_POWER_SAVE_ON:
    state = DPMSModeOn;
    break;
  case META_POWER_SAVE_STANDBY:
    state = DPMSModeStandby;
    break;
  case META_POWER_SAVE_SUSPEND:
    state = DPMSModeSuspend;
    break;
  case META_POWER_SAVE_OFF:
    state = DPMSModeOff;
    break;
  default:
    return;
  }

  DPMSForceLevel (manager_xrandr->xdisplay, state);
  DPMSSetTimeouts (manager_xrandr->xdisplay, 0, 0, 0);
}

static xcb_randr_rotation_t
meta_monitor_transform_to_xrandr (MetaMonitorTransform transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      return XCB_RANDR_ROTATION_ROTATE_0;
    case META_MONITOR_TRANSFORM_90:
      return XCB_RANDR_ROTATION_ROTATE_90;
    case META_MONITOR_TRANSFORM_180:
      return XCB_RANDR_ROTATION_ROTATE_180;
    case META_MONITOR_TRANSFORM_270:
      return XCB_RANDR_ROTATION_ROTATE_270;
    case META_MONITOR_TRANSFORM_FLIPPED:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_0;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_90;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_180;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return XCB_RANDR_ROTATION_REFLECT_X | XCB_RANDR_ROTATION_ROTATE_270;
    }

  g_assert_not_reached ();
  return 0;
}

static gboolean
xrandr_set_crtc_config (MetaMonitorManagerXrandr *manager_xrandr,
                        MetaCrtc                 *crtc,
                        gboolean                  save_timestamp,
                        xcb_randr_crtc_t          xrandr_crtc,
                        xcb_timestamp_t           timestamp,
                        int                       x,
                        int                       y,
                        xcb_randr_mode_t          mode,
                        xcb_randr_rotation_t      rotation,
                        xcb_randr_output_t       *outputs,
                        int                       n_outputs)
{
  xcb_timestamp_t new_timestamp;

  if (!meta_crtc_xrandr_set_config (crtc, xrandr_crtc, timestamp,
                                    x, y, mode, rotation,
                                    outputs, n_outputs,
                                    &new_timestamp))
    return FALSE;

  if (save_timestamp)
    manager_xrandr->last_xrandr_set_timestamp = new_timestamp;

  return TRUE;
}

static gboolean
is_crtc_assignment_changed (MetaCrtc      *crtc,
                            MetaCrtcInfo **crtc_infos,
                            unsigned int   n_crtc_infos)
{
  unsigned int i;

  for (i = 0; i < n_crtc_infos; i++)
    {
      MetaCrtcInfo *crtc_info = crtc_infos[i];

      if (crtc_info->crtc != crtc)
        continue;

      return meta_crtc_xrandr_is_assignment_changed (crtc, crtc_info);
    }

  return !!meta_crtc_xrandr_get_current_mode (crtc);
}

static gboolean
is_output_assignment_changed (MetaOutput      *output,
                              MetaCrtcInfo   **crtc_infos,
                              unsigned int     n_crtc_infos,
                              MetaOutputInfo **output_infos,
                              unsigned int     n_output_infos)
{
  MetaCrtc *assigned_crtc;
  gboolean output_is_found = FALSE;
  unsigned int i;

  for (i = 0; i < n_output_infos; i++)
    {
      MetaOutputInfo *output_info = output_infos[i];

      if (output_info->output != output)
        continue;

      if (output->is_primary != output_info->is_primary)
        return TRUE;

      if (output->is_presentation != output_info->is_presentation)
        return TRUE;

      if (output->is_underscanning != output_info->is_underscanning)
        return TRUE;

      output_is_found = TRUE;
    }

  assigned_crtc = meta_output_get_assigned_crtc (output);

  if (!output_is_found)
    return assigned_crtc != NULL;

  for (i = 0; i < n_crtc_infos; i++)
    {
      MetaCrtcInfo *crtc_info = crtc_infos[i];
      unsigned int j;

      for (j = 0; j < crtc_info->outputs->len; j++)
        {
          MetaOutput *crtc_info_output =
            ((MetaOutput**) crtc_info->outputs->pdata)[j];

          if (crtc_info_output == output &&
              crtc_info->crtc == assigned_crtc)
            return FALSE;
        }
    }

  return TRUE;
}

static MetaGpu *
meta_monitor_manager_xrandr_get_gpu (MetaMonitorManagerXrandr *manager_xrandr)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);

  return META_GPU (meta_backend_get_gpus (backend)->data);
}

static gboolean
is_assignments_changed (MetaMonitorManager *manager,
                        MetaCrtcInfo      **crtc_infos,
                        unsigned int        n_crtc_infos,
                        MetaOutputInfo    **output_infos,
                        unsigned int        n_output_infos)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if (is_crtc_assignment_changed (crtc, crtc_infos, n_crtc_infos))
        return TRUE;
    }

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (is_output_assignment_changed (output,
                                        crtc_infos,
                                        n_crtc_infos,
                                        output_infos,
                                        n_output_infos))
        return TRUE;
    }

  return FALSE;
}

static void
apply_crtc_assignments (MetaMonitorManager *manager,
                        gboolean            save_timestamp,
                        MetaCrtcInfo      **crtcs,
                        unsigned int        n_crtcs,
                        MetaOutputInfo    **outputs,
                        unsigned int        n_outputs)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  unsigned i;
  GList *l;
  int width, height, width_mm, height_mm;

  XGrabServer (manager_xrandr->xdisplay);

  /* First compute the new size of the screen (framebuffer) */
  width = 0; height = 0;
  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;
      crtc->is_dirty = TRUE;

      if (crtc_info->mode == NULL)
        continue;

      width = MAX (width, (int) roundf (crtc_info->layout.origin.x +
                                        crtc_info->layout.size.width));
      height = MAX (height, (int) roundf (crtc_info->layout.origin.y +
                                          crtc_info->layout.size.height));
    }

  /* Second disable all newly disabled CRTCs, or CRTCs that in the previous
     configuration would be outside the new framebuffer (otherwise X complains
     loudly when resizing)
     CRTC will be enabled again after resizing the FB
  */
  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;
      MetaCrtcConfig *crtc_config;
      int x2, y2;

      crtc_config = crtc->config;
      if (!crtc_config)
        continue;

      x2 = (int) roundf (crtc_config->layout.origin.x +
                         crtc_config->layout.size.width);
      y2 = (int) roundf (crtc_config->layout.origin.y +
                         crtc_config->layout.size.height);

      if (!crtc_info->mode || x2 > width || y2 > height)
        {
          xrandr_set_crtc_config (manager_xrandr,
                                  crtc,
                                  save_timestamp,
                                  (xcb_randr_crtc_t) crtc->crtc_id,
                                  XCB_CURRENT_TIME,
                                  0, 0, XCB_NONE,
                                  XCB_RANDR_ROTATION_ROTATE_0,
                                  NULL, 0);

          meta_crtc_unset_config (crtc);
        }
    }

  /* Disable CRTCs not mentioned in the list */
  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;

      if (crtc->is_dirty)
        {
          crtc->is_dirty = FALSE;
          continue;
        }

      if (!crtc->config)
        continue;

      xrandr_set_crtc_config (manager_xrandr,
                              crtc,
                              save_timestamp,
                              (xcb_randr_crtc_t) crtc->crtc_id,
                              XCB_CURRENT_TIME,
                              0, 0, XCB_NONE,
                              XCB_RANDR_ROTATION_ROTATE_0,
                              NULL, 0);

      meta_crtc_unset_config (crtc);
    }

  if (!n_crtcs)
    goto out;

  g_assert (width > 0 && height > 0);
  /* The 'physical size' of an X screen is meaningless if that screen
   * can consist of many monitors. So just pick a size that make the
   * dpi 96.
   *
   * Firefox and Evince apparently believe what X tells them.
   */
  width_mm = (width / DPI_FALLBACK) * 25.4 + 0.5;
  height_mm = (height / DPI_FALLBACK) * 25.4 + 0.5;
  XRRSetScreenSize (manager_xrandr->xdisplay, DefaultRootWindow (manager_xrandr->xdisplay),
                    width, height, width_mm, height_mm);

  for (i = 0; i < n_crtcs; i++)
    {
      MetaCrtcInfo *crtc_info = crtcs[i];
      MetaCrtc *crtc = crtc_info->crtc;

      if (crtc_info->mode != NULL)
        {
          MetaCrtcMode *mode;
          g_autofree xcb_randr_output_t *output_ids = NULL;
          unsigned int j, n_output_ids;
          xcb_randr_rotation_t rotation;

          mode = crtc_info->mode;

          n_output_ids = crtc_info->outputs->len;
          output_ids = g_new (xcb_randr_output_t, n_output_ids);

          for (j = 0; j < n_output_ids; j++)
            {
              MetaOutput *output;

              output = ((MetaOutput**)crtc_info->outputs->pdata)[j];

              output->is_dirty = TRUE;
              meta_output_assign_crtc (output, crtc);

              output_ids[j] = output->winsys_id;
            }

          rotation = meta_monitor_transform_to_xrandr (crtc_info->transform);
          if (!xrandr_set_crtc_config (manager_xrandr,
                                       crtc,
                                       save_timestamp,
                                       (xcb_randr_crtc_t) crtc->crtc_id,
                                       XCB_CURRENT_TIME,
                                       (int) roundf (crtc_info->layout.origin.x),
                                       (int) roundf (crtc_info->layout.origin.y),
                                       (xcb_randr_mode_t) mode->mode_id,
                                       rotation,
                                       output_ids, n_output_ids))
            {
              meta_warning ("Configuring CRTC %d with mode %d (%d x %d @ %f) at position %d, %d and transform %u failed\n",
                            (unsigned)(crtc->crtc_id), (unsigned)(mode->mode_id),
                            mode->width, mode->height, (float)mode->refresh_rate,
                            (int) roundf (crtc_info->layout.origin.x),
                            (int) roundf (crtc_info->layout.origin.y),
                            crtc_info->transform);
              continue;
            }

          meta_crtc_set_config (crtc,
                                &crtc_info->layout,
                                mode,
                                crtc_info->transform);
        }
    }

  for (i = 0; i < n_outputs; i++)
    {
      MetaOutputInfo *output_info = outputs[i];
      MetaOutput *output = output_info->output;

      output->is_primary = output_info->is_primary;
      output->is_presentation = output_info->is_presentation;
      output->is_underscanning = output_info->is_underscanning;

      meta_output_xrandr_apply_mode (output);
    }

  /* Disable outputs not mentioned in the list */
  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (output->is_dirty)
        {
          output->is_dirty = FALSE;
          continue;
        }

      meta_output_unassign_crtc (output);
      output->is_primary = FALSE;
    }

out:
  XUngrabServer (manager_xrandr->xdisplay);
  XFlush (manager_xrandr->xdisplay);
}

static void
meta_monitor_manager_xrandr_ensure_initial_config (MetaMonitorManager *manager)
{
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (manager);
  MetaMonitorsConfig *config;

  meta_monitor_manager_ensure_configured (manager);

  /*
   * Normally we don't rebuild our data structures until we see the
   * RRScreenNotify event, but at least at startup we want to have the right
   * configuration immediately.
   */
  meta_monitor_manager_read_current_state (manager);

  config = meta_monitor_config_manager_get_current (config_manager);
  meta_monitor_manager_update_logical_state_derived (manager, config);
}

static void
meta_monitor_manager_xrandr_rebuild_derived (MetaMonitorManager *manager,
                                             MetaMonitorsConfig *config)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);

  g_clear_pointer (&manager_xrandr->supported_scales, g_free);
  meta_monitor_manager_rebuild_derived (manager, config);
}

static gboolean
meta_monitor_manager_xrandr_apply_monitors_config (MetaMonitorManager      *manager,
                                                   MetaMonitorsConfig      *config,
                                                   MetaMonitorsConfigMethod method,
                                                   GError                 **error)
{
  GPtrArray *crtc_infos;
  GPtrArray *output_infos;

  if (!config)
    {
      if (!manager->in_init)
        apply_crtc_assignments (manager, TRUE, NULL, 0, NULL, 0);

      meta_monitor_manager_xrandr_rebuild_derived (manager, NULL);
      return TRUE;
    }

  if (!meta_monitor_config_manager_assign (manager, config,
                                           &crtc_infos, &output_infos,
                                           error))
    return FALSE;

  if (method != META_MONITORS_CONFIG_METHOD_VERIFY)
    {
      /*
       * If the assignment has not changed, we won't get any notification about
       * any new configuration from the X server; but we still need to update
       * our own configuration, as something not applicable in Xrandr might
       * have changed locally, such as the logical monitors scale. This means we
       * must check that our new assignment actually changes anything, otherwise
       * just update the logical state.
       */
      if (is_assignments_changed (manager,
                                  (MetaCrtcInfo **) crtc_infos->pdata,
                                  crtc_infos->len,
                                  (MetaOutputInfo **) output_infos->pdata,
                                  output_infos->len))
        {
          apply_crtc_assignments (manager,
                                  TRUE,
                                  (MetaCrtcInfo **) crtc_infos->pdata,
                                  crtc_infos->len,
                                  (MetaOutputInfo **) output_infos->pdata,
                                  output_infos->len);
        }
      else
        {
          meta_monitor_manager_xrandr_rebuild_derived (manager, config);
        }
    }

  g_ptr_array_free (crtc_infos, TRUE);
  g_ptr_array_free (output_infos, TRUE);

  return TRUE;
}

static void
meta_monitor_manager_xrandr_change_backlight (MetaMonitorManager *manager,
					      MetaOutput         *output,
					      gint                value)
{
  meta_output_xrandr_change_backlight (output, value);
}

static void
meta_monitor_manager_xrandr_get_crtc_gamma (MetaMonitorManager  *manager,
					    MetaCrtc            *crtc,
					    gsize               *size,
					    unsigned short     **red,
					    unsigned short     **green,
					    unsigned short     **blue)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  XRRCrtcGamma *gamma;

  gamma = XRRGetCrtcGamma (manager_xrandr->xdisplay, (XID)crtc->crtc_id);

  *size = gamma->size;
  *red = g_memdup (gamma->red, sizeof (unsigned short) * gamma->size);
  *green = g_memdup (gamma->green, sizeof (unsigned short) * gamma->size);
  *blue = g_memdup (gamma->blue, sizeof (unsigned short) * gamma->size);

  XRRFreeGamma (gamma);
}

static void
meta_monitor_manager_xrandr_set_crtc_gamma (MetaMonitorManager *manager,
					    MetaCrtc           *crtc,
					    gsize               size,
					    unsigned short     *red,
					    unsigned short     *green,
					    unsigned short     *blue)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  XRRCrtcGamma *gamma;

  gamma = XRRAllocGamma (size);
  memcpy (gamma->red, red, sizeof (unsigned short) * size);
  memcpy (gamma->green, green, sizeof (unsigned short) * size);
  memcpy (gamma->blue, blue, sizeof (unsigned short) * size);

  XRRSetCrtcGamma (manager_xrandr->xdisplay, (XID)crtc->crtc_id, gamma);

  XRRFreeGamma (gamma);
}

static MetaMonitorXrandrData *
meta_monitor_xrandr_data_from_monitor (MetaMonitor *monitor)
{
  MetaMonitorXrandrData *monitor_xrandr_data;

  monitor_xrandr_data = g_object_get_qdata (G_OBJECT (monitor),
                                            quark_meta_monitor_xrandr_data);
  if (monitor_xrandr_data)
    return monitor_xrandr_data;

  monitor_xrandr_data = g_new0 (MetaMonitorXrandrData, 1);
  g_object_set_qdata_full (G_OBJECT (monitor),
                           quark_meta_monitor_xrandr_data,
                           monitor_xrandr_data,
                           g_free);

  return monitor_xrandr_data;
}

static void
meta_monitor_manager_xrandr_increase_monitor_count (MetaMonitorManagerXrandr *manager_xrandr,
                                                    Atom                      name_atom)
{
  int count;

  count =
    GPOINTER_TO_INT (g_hash_table_lookup (manager_xrandr->tiled_monitor_atoms,
                                          GSIZE_TO_POINTER (name_atom)));

  count++;
  g_hash_table_insert (manager_xrandr->tiled_monitor_atoms,
                       GSIZE_TO_POINTER (name_atom),
                       GINT_TO_POINTER (count));
}

static int
meta_monitor_manager_xrandr_decrease_monitor_count (MetaMonitorManagerXrandr *manager_xrandr,
                                                    Atom                      name_atom)
{
  int count;

  count =
    GPOINTER_TO_SIZE (g_hash_table_lookup (manager_xrandr->tiled_monitor_atoms,
                                           GSIZE_TO_POINTER (name_atom)));
  g_assert (count > 0);

  count--;
  g_hash_table_insert (manager_xrandr->tiled_monitor_atoms,
                       GSIZE_TO_POINTER (name_atom),
                       GINT_TO_POINTER (count));

  return count;
}

static void
meta_monitor_manager_xrandr_tiled_monitor_added (MetaMonitorManager *manager,
                                                 MetaMonitor        *monitor)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorTiled *monitor_tiled = META_MONITOR_TILED (monitor);
  const char *product;
  char *name;
  uint32_t tile_group_id;
  MetaMonitorXrandrData *monitor_xrandr_data;
  Atom name_atom;
  XRRMonitorInfo *xrandr_monitor_info;
  GList *outputs;
  GList *l;
  int i;

  if (manager_xrandr->has_randr15 == FALSE)
    return;

  product = meta_monitor_get_product (monitor);
  tile_group_id = meta_monitor_tiled_get_tile_group_id (monitor_tiled);

  if (product)
    name = g_strdup_printf ("%s-%d", product, tile_group_id);
  else
    name = g_strdup_printf ("Tiled-%d", tile_group_id);

  name_atom = XInternAtom (manager_xrandr->xdisplay, name, False);
  g_free (name);

  monitor_xrandr_data = meta_monitor_xrandr_data_from_monitor (monitor);
  monitor_xrandr_data->xrandr_name = name_atom;

  meta_monitor_manager_xrandr_increase_monitor_count (manager_xrandr,
                                                      name_atom);

  outputs = meta_monitor_get_outputs (monitor);
  xrandr_monitor_info = XRRAllocateMonitor (manager_xrandr->xdisplay,
                                            g_list_length (outputs));
  xrandr_monitor_info->name = name_atom;
  xrandr_monitor_info->primary = meta_monitor_is_primary (monitor);
  xrandr_monitor_info->automatic = True;
  for (l = outputs, i = 0; l; l = l->next, i++)
    {
      MetaOutput *output = l->data;

      xrandr_monitor_info->outputs[i] = output->winsys_id;
    }

  XRRSetMonitor (manager_xrandr->xdisplay,
                 DefaultRootWindow (manager_xrandr->xdisplay),
                 xrandr_monitor_info);
  XRRFreeMonitors (xrandr_monitor_info);
}

static void
meta_monitor_manager_xrandr_tiled_monitor_removed (MetaMonitorManager *manager,
                                                   MetaMonitor        *monitor)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorXrandrData *monitor_xrandr_data;
  Atom monitor_name;

  int monitor_count;

  if (manager_xrandr->has_randr15 == FALSE)
    return;

  monitor_xrandr_data = meta_monitor_xrandr_data_from_monitor (monitor);
  monitor_name = monitor_xrandr_data->xrandr_name;
  monitor_count =
    meta_monitor_manager_xrandr_decrease_monitor_count (manager_xrandr,
                                                        monitor_name);

  if (monitor_count == 0)
    XRRDeleteMonitor (manager_xrandr->xdisplay,
                      DefaultRootWindow (manager_xrandr->xdisplay),
                      monitor_name);
}

static void
meta_monitor_manager_xrandr_init_monitors (MetaMonitorManagerXrandr *manager_xrandr)
{
  XRRMonitorInfo *m;
  int n, i;

  if (manager_xrandr->has_randr15 == FALSE)
    return;

  /* delete any tiled monitors setup, as mutter will want to recreate
     things in its image */
  m = XRRGetMonitors (manager_xrandr->xdisplay,
                      DefaultRootWindow (manager_xrandr->xdisplay),
                      FALSE, &n);
  if (n == -1)
    return;

  for (i = 0; i < n; i++)
    {
      if (m[i].noutput > 1)
        XRRDeleteMonitor (manager_xrandr->xdisplay,
                          DefaultRootWindow (manager_xrandr->xdisplay),
                          m[i].name);
    }
  XRRFreeMonitors (m);
}

static gboolean
meta_monitor_manager_xrandr_is_transform_handled (MetaMonitorManager  *manager,
                                                  MetaCrtc            *crtc,
                                                  MetaMonitorTransform transform)
{
  g_warn_if_fail ((crtc->all_transforms & transform) == transform);

  return TRUE;
}

static float
meta_monitor_manager_xrandr_calculate_monitor_mode_scale (MetaMonitorManager *manager,
                                                          MetaMonitor        *monitor,
                                                          MetaMonitorMode    *monitor_mode)
{
  return meta_monitor_calculate_mode_scale (monitor, monitor_mode);
}

static void
add_supported_scale (GArray *supported_scales,
                     float   scale)
{
  unsigned int i;

  for (i = 0; i < supported_scales->len; i++)
    {
      float supported_scale = g_array_index (supported_scales, float, i);

      if (scale == supported_scale)
        return;
    }

  g_array_append_val (supported_scales, scale);
}

static int
compare_scales (gconstpointer a,
                gconstpointer b)
{
  float f = *(float *) a - *(float *) b;

  if (f < 0)
    return -1;
  if (f > 0)
    return 1;
  return 0;
}

static void
ensure_supported_monitor_scales (MetaMonitorManager *manager)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaMonitorScalesConstraint constraints;
  GList *l;
  GArray *supported_scales;

  if (manager_xrandr->supported_scales)
    return;

  constraints = META_MONITOR_SCALES_CONSTRAINT_NO_FRAC;
  supported_scales = g_array_new (FALSE, FALSE, sizeof (float));

  for (l = manager->monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;
      MetaMonitorMode *monitor_mode;
      float *monitor_scales;
      int n_monitor_scales;
      int i;

      monitor_mode = meta_monitor_get_preferred_mode (monitor);
      monitor_scales =
        meta_monitor_calculate_supported_scales (monitor,
                                                 monitor_mode,
                                                 constraints,
                                                 &n_monitor_scales);

      for (i = 0; i < n_monitor_scales; i++)
        add_supported_scale (supported_scales, monitor_scales[i]);
      g_array_sort (supported_scales, compare_scales);
      g_free (monitor_scales);
    }

  manager_xrandr->supported_scales = (float *) supported_scales->data;
  manager_xrandr->n_supported_scales = supported_scales->len;
  g_array_free (supported_scales, FALSE);
}

static float *
meta_monitor_manager_xrandr_calculate_supported_scales (MetaMonitorManager           *manager,
                                                        MetaLogicalMonitorLayoutMode  layout_mode,
                                                        MetaMonitor                  *monitor,
                                                        MetaMonitorMode              *monitor_mode,
                                                        int                          *n_supported_scales)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);

  ensure_supported_monitor_scales (manager);

  *n_supported_scales = manager_xrandr->n_supported_scales;
  return g_memdup (manager_xrandr->supported_scales,
                   manager_xrandr->n_supported_scales * sizeof (float));
}

static MetaMonitorManagerCapability
meta_monitor_manager_xrandr_get_capabilities (MetaMonitorManager *manager)
{
  return META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED;
}

static gboolean
meta_monitor_manager_xrandr_get_max_screen_size (MetaMonitorManager *manager,
                                                 int                *max_width,
                                                 int                *max_height)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (manager);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);

  meta_gpu_xrandr_get_max_screen_size (META_GPU_XRANDR (gpu),
                                       max_width, max_height);

  return TRUE;
}

static MetaLogicalMonitorLayoutMode
meta_monitor_manager_xrandr_get_default_layout_mode (MetaMonitorManager *manager)
{
  return META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL;
}

static void
meta_monitor_manager_xrandr_constructed (GObject *object)
{
  MetaMonitorManagerXrandr *manager_xrandr =
    META_MONITOR_MANAGER_XRANDR (object);
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaBackend *backend = meta_monitor_manager_get_backend (manager);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);

  manager_xrandr->xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  if (!XRRQueryExtension (manager_xrandr->xdisplay,
			  &manager_xrandr->rr_event_base,
			  &manager_xrandr->rr_error_base))
    {
      return;
    }
  else
    {
      int major_version, minor_version;
      /* We only use ScreenChangeNotify, but GDK uses the others,
	 and we don't want to step on its toes */
      XRRSelectInput (manager_xrandr->xdisplay,
		      DefaultRootWindow (manager_xrandr->xdisplay),
		      RRScreenChangeNotifyMask
		      | RRCrtcChangeNotifyMask
		      | RROutputPropertyNotifyMask);

      manager_xrandr->has_randr15 = FALSE;
      XRRQueryVersion (manager_xrandr->xdisplay, &major_version,
                       &minor_version);
      if (major_version > 1 ||
          (major_version == 1 &&
           minor_version >= 5))
        {
          manager_xrandr->has_randr15 = TRUE;
          manager_xrandr->tiled_monitor_atoms = g_hash_table_new (NULL, NULL);
        }
      meta_monitor_manager_xrandr_init_monitors (manager_xrandr);
    }

  G_OBJECT_CLASS (meta_monitor_manager_xrandr_parent_class)->constructed (object);
}

static void
meta_monitor_manager_xrandr_finalize (GObject *object)
{
  MetaMonitorManagerXrandr *manager_xrandr = META_MONITOR_MANAGER_XRANDR (object);

  g_hash_table_destroy (manager_xrandr->tiled_monitor_atoms);
  g_free (manager_xrandr->supported_scales);

  G_OBJECT_CLASS (meta_monitor_manager_xrandr_parent_class)->finalize (object);
}

static void
meta_monitor_manager_xrandr_init (MetaMonitorManagerXrandr *manager_xrandr)
{
}

static void
meta_monitor_manager_xrandr_class_init (MetaMonitorManagerXrandrClass *klass)
{
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_monitor_manager_xrandr_finalize;
  object_class->constructed = meta_monitor_manager_xrandr_constructed;

  manager_class->read_edid = meta_monitor_manager_xrandr_read_edid;
  manager_class->read_current_state = meta_monitor_manager_xrandr_read_current_state;
  manager_class->ensure_initial_config = meta_monitor_manager_xrandr_ensure_initial_config;
  manager_class->apply_monitors_config = meta_monitor_manager_xrandr_apply_monitors_config;
  manager_class->set_power_save_mode = meta_monitor_manager_xrandr_set_power_save_mode;
  manager_class->change_backlight = meta_monitor_manager_xrandr_change_backlight;
  manager_class->get_crtc_gamma = meta_monitor_manager_xrandr_get_crtc_gamma;
  manager_class->set_crtc_gamma = meta_monitor_manager_xrandr_set_crtc_gamma;
  manager_class->tiled_monitor_added = meta_monitor_manager_xrandr_tiled_monitor_added;
  manager_class->tiled_monitor_removed = meta_monitor_manager_xrandr_tiled_monitor_removed;
  manager_class->is_transform_handled = meta_monitor_manager_xrandr_is_transform_handled;
  manager_class->calculate_monitor_mode_scale = meta_monitor_manager_xrandr_calculate_monitor_mode_scale;
  manager_class->calculate_supported_scales = meta_monitor_manager_xrandr_calculate_supported_scales;
  manager_class->get_capabilities = meta_monitor_manager_xrandr_get_capabilities;
  manager_class->get_max_screen_size = meta_monitor_manager_xrandr_get_max_screen_size;
  manager_class->get_default_layout_mode = meta_monitor_manager_xrandr_get_default_layout_mode;

  quark_meta_monitor_xrandr_data =
    g_quark_from_static_string ("-meta-monitor-xrandr-data");
}

gboolean
meta_monitor_manager_xrandr_handle_xevent (MetaMonitorManagerXrandr *manager_xrandr,
					   XEvent                   *event)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (manager_xrandr);
  MetaGpu *gpu = meta_monitor_manager_xrandr_get_gpu (manager_xrandr);
  MetaGpuXrandr *gpu_xrandr;
  XRRScreenResources *resources;
  gboolean is_hotplug;
  gboolean is_our_configuration;

  if ((event->type - manager_xrandr->rr_event_base) != RRScreenChangeNotify)
    return FALSE;

  XRRUpdateConfiguration (event);

  meta_monitor_manager_read_current_state (manager);

  gpu_xrandr = META_GPU_XRANDR (gpu);
  resources = meta_gpu_xrandr_get_resources (gpu_xrandr);

  is_hotplug = resources->timestamp < resources->configTimestamp;
  is_our_configuration = (resources->timestamp ==
                          manager_xrandr->last_xrandr_set_timestamp);
  if (is_hotplug)
    {
      meta_monitor_manager_on_hotplug (manager);
    }
  else
    {
      MetaMonitorsConfig *config;

      if (is_our_configuration)
        {
          MetaMonitorConfigManager *config_manager =
            meta_monitor_manager_get_config_manager (manager);

          config = meta_monitor_config_manager_get_current (config_manager);
        }
      else
        {
          config = NULL;
        }

      meta_monitor_manager_xrandr_rebuild_derived (manager, config);
    }

  return TRUE;
}
