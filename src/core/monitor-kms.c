/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <clutter/clutter.h>

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <meta/main.h>
#include <meta/errors.h>
#include "monitor-private.h"

#define ALL_WL_TRANSFORMS ((1 << (WL_OUTPUT_TRANSFORM_FLIPPED_270 + 1)) - 1)

typedef struct {
  drmModeConnector *connector;

  unsigned n_encoders;
  drmModeEncoderPtr *encoders;
  drmModeEncoderPtr  current_encoder;

  /* bitmasks of encoder position in the resources array */
  uint32_t encoder_mask;
  uint32_t enc_clone_mask;

  uint32_t dpms_prop_id;
} MetaOutputKms;

struct _MetaMonitorManagerKms
{
  MetaMonitorManager parent_instance;

  int fd;

  drmModeConnector **connectors;
  unsigned int       n_connectors;

  drmModeEncoder   **encoders;
  unsigned int       n_encoders;

  drmModeEncoder    *current_encoder;
};

struct _MetaMonitorManagerKmsClass
{
  MetaMonitorManagerClass parent_class;
};

G_DEFINE_TYPE (MetaMonitorManagerKms, meta_monitor_manager_kms, META_TYPE_MONITOR_MANAGER);

static int
compare_outputs (const void *one,
                 const void *two)
{
  const MetaOutput *o_one = one, *o_two = two;

  return strcmp (o_one->name, o_two->name);
}

static char *
make_output_name (drmModeConnector *connector)
{
  static const char * const connector_type_names[] = {
    "unknown", "VGA", "DVII", "DVID", "DVID", "Composite",
    "SVIDEO", "LVDS", "Component", "9PinDIN", "DisplayPort",
    "HDMIA", "HDMIB", "TV", "eDP"
  };
  const char *connector_type_name;

  if (connector->connector_type >= 0 &&
      connector->connector_type < G_N_ELEMENTS (connector_type_names))
    connector_type_name = connector_type_names[connector->connector_type];
  else
    connector_type_name = "unknown";

  return g_strdup_printf ("%s%d", connector_type_name, connector->connector_id);
}

static void
meta_output_destroy_notify (MetaOutput *output)
{
  MetaOutputKms *output_kms;
  unsigned i;

  output_kms = output->driver_private;

  for (i = 0; i < output_kms->n_encoders; i++)
    drmModeFreeEncoder (output_kms->encoders[i]);

  g_slice_free (MetaOutputKms, output_kms);
}

static void
meta_monitor_mode_destroy_notify (MetaMonitorMode *output)
{
  g_slice_free (drmModeModeInfo, output->driver_private);
}

static gboolean
drm_mode_equal (gconstpointer one,
                gconstpointer two)
{
  return memcmp (one, two, sizeof (drmModeModeInfo)) == 0;
}

static guint
drm_mode_hash (gconstpointer ptr)
{
  const drmModeModeInfo *mode = ptr;
  guint hash = 0;

  hash ^= mode->clock;
  hash ^= mode->hdisplay ^ mode->hsync_start ^ mode->hsync_end;
  hash ^= mode->vdisplay ^ mode->vsync_start ^ mode->vsync_end;
  hash ^= mode->vrefresh;
  hash ^= mode->flags ^ mode->type;

  return hash;
}

static void
meta_monitor_manager_kms_read_current (MetaMonitorManager *manager)
{
  MetaMonitorManagerKms *manager_kms = META_MONITOR_MANAGER_KMS (manager);
  drmModeRes *resources;
  GHashTable *modes;
  GHashTableIter iter;
  drmModeModeInfo *mode;
  unsigned int i, j, k;
  unsigned int n_actual_outputs;
  int width, height;

  resources = drmModeGetResources(manager_kms->fd);
  modes = g_hash_table_new (drm_mode_hash, drm_mode_equal);

  manager->max_screen_width = resources->max_width;
  manager->max_screen_height = resources->max_height;

  manager->power_save_mode = META_POWER_SAVE_ON;

  manager_kms->n_connectors = resources->count_connectors;
  manager_kms->connectors = g_new (drmModeConnector *, manager_kms->n_connectors);
  for (i = 0; i < manager_kms->n_connectors; i++)
    {
      drmModeConnector *connector;

      connector = drmModeGetConnector (manager_kms->fd, resources->connectors[i]);
      manager_kms->connectors[i] = connector;

      if (connector->connection == DRM_MODE_CONNECTED)
        {
          /* Collect all modes for this connector */
          for (j = 0; j < (unsigned)connector->count_modes; j++)
            g_hash_table_add (modes, &connector->modes[j]);
        }
    }

  manager_kms->n_encoders = resources->count_encoders;
  manager_kms->encoders = g_new (drmModeEncoder *, manager_kms->n_encoders);
  for (i = 0; i < manager_kms->n_encoders; i++)
    {
      manager_kms->encoders[i] = drmModeGetEncoder (manager_kms->fd,
                                                    resources->encoders[i]);
    }

  manager->n_modes = g_hash_table_size (modes);
  manager->modes = g_new0 (MetaMonitorMode, manager->n_modes);
  g_hash_table_iter_init (&iter, modes);
  i = 0;
  while (g_hash_table_iter_next (&iter, NULL, (gpointer)&mode))
    {
      MetaMonitorMode *meta_mode;

      meta_mode = &manager->modes[i];

      meta_mode->mode_id = i;
      meta_mode->name = g_strndup (mode->name, DRM_DISPLAY_MODE_LEN);
      meta_mode->width = mode->hdisplay;
      meta_mode->height = mode->vdisplay;
      meta_mode->refresh_rate = (1000 * mode->clock /
                                 ((float)mode->htotal * mode->vtotal));

      meta_mode->driver_private = g_slice_dup (drmModeModeInfo, mode);
      meta_mode->driver_notify = (GDestroyNotify)meta_monitor_mode_destroy_notify;

      i++;
    }
  g_hash_table_destroy (modes);

  manager->n_crtcs = resources->count_crtcs;
  manager->crtcs = g_new0 (MetaCRTC, manager->n_crtcs);
  width = 0; height = 0;
  for (i = 0; i < (unsigned)resources->count_crtcs; i++)
    {
      drmModeCrtc *crtc;
      MetaCRTC *meta_crtc;

      crtc = drmModeGetCrtc (manager_kms->fd, resources->crtcs[i]);

      meta_crtc = &manager->crtcs[i];

      meta_crtc->crtc_id = crtc->crtc_id;
      meta_crtc->rect.x = crtc->x;
      meta_crtc->rect.y = crtc->y;
      meta_crtc->rect.width = crtc->width;
      meta_crtc->rect.height = crtc->height;
      meta_crtc->dirty = FALSE;

      /* FIXME: we can handle some transforms, with a combination of
         scaling and fitting, but it is very driver dependent */
      meta_crtc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
      meta_crtc->all_transforms = 1 << WL_OUTPUT_TRANSFORM_NORMAL;

      if (crtc->mode_valid)
        {
          for (j = 0; j < manager->n_modes; j++)
            {
              if (drm_mode_equal (&crtc->mode, manager->modes[j].driver_private))
                {
                  meta_crtc->current_mode = &manager->modes[j];
                  break;
                }
            }

          width = MAX (width, meta_crtc->rect.x + meta_crtc->rect.width);
          height = MAX (height, meta_crtc->rect.y + meta_crtc->rect.height);
        }

      drmModeFreeCrtc (crtc);
    }

  manager->screen_width = width;
  manager->screen_height = height;

  manager->outputs = g_new0 (MetaOutput, manager_kms->n_connectors);
  n_actual_outputs = 0;

  for (i = 0; i < manager_kms->n_connectors; i++)
    {
      MetaOutput *meta_output;
      MetaOutputKms *output_kms;
      drmModeConnector *connector;
      GArray *crtcs;
      unsigned int crtc_mask;

      connector = manager_kms->connectors[i];
      meta_output = &manager->outputs[n_actual_outputs];

      if (connector->connection == DRM_MODE_CONNECTED)
	{
	  meta_output->output_id = connector->connector_id;
	  meta_output->name = make_output_name (connector);
	  meta_output->vendor = g_strdup ("unknown");
	  meta_output->product = g_strdup ("unknown");
	  meta_output->serial = g_strdup ("");
	  meta_output->width_mm = connector->mmWidth;
	  meta_output->height_mm = connector->mmHeight;

          if (connector->subpixel == DRM_MODE_SUBPIXEL_UNKNOWN)
            meta_output->subpixel_order = COGL_SUBPIXEL_ORDER_UNKNOWN;
          else if (connector->subpixel == DRM_MODE_SUBPIXEL_NONE)
            meta_output->subpixel_order = COGL_SUBPIXEL_ORDER_NONE;
          else
            meta_output->subpixel_order = connector->subpixel;

	  meta_output->n_modes = connector->count_modes;
	  meta_output->modes = g_new0 (MetaMonitorMode *, meta_output->n_modes);
	  for (j = 0; j < meta_output->n_modes; j++)
	    {
	      for (k = 0; k < manager->n_modes; k++)
		{
		  if (drm_mode_equal (&connector->modes[j], manager->modes[k].driver_private))
		    {
		      meta_output->modes[j] = &manager->modes[k];
		      break;
		    }
		}
	    }
	  meta_output->preferred_mode = meta_output->modes[0];

          meta_output->driver_private = output_kms = g_slice_new0 (MetaOutputKms);
          meta_output->driver_notify = (GDestroyNotify)meta_output_destroy_notify;

          output_kms->connector = connector;
          output_kms->n_encoders = connector->count_encoders;
          output_kms->encoders = g_new0 (drmModeEncoderPtr, output_kms->n_encoders);

          crtc_mask = 0x7F;
	  for (j = 0; j < output_kms->n_encoders; j++)
	    {
              output_kms->encoders[j] = drmModeGetEncoder (manager_kms->fd, connector->encoders[j]);

              crtc_mask &= output_kms->encoders[j]->possible_crtcs;

              if (output_kms->encoders[j]->encoder_id == connector->encoder_id)
                output_kms->current_encoder = output_kms->encoders[j];
            }

          crtcs = g_array_new (FALSE, FALSE, sizeof (MetaCRTC*));

          for (j = 0; j < manager->n_crtcs; j++)
            {
              if (crtc_mask & (1 << j))
                {
                  MetaCRTC *crtc = &manager->crtcs[j];
                  g_array_append_val (crtcs, crtc);
		}
	    }
          
	  meta_output->n_possible_crtcs = crtcs->len;
	  meta_output->possible_crtcs = (void*)g_array_free (crtcs, FALSE);

          if (output_kms->current_encoder && output_kms->current_encoder->crtc_id != 0)
            {
              for (j = 0; j < manager->n_crtcs; j++)
                {
                  if (manager->crtcs[j].crtc_id == output_kms->current_encoder->crtc_id)
                    {
                      meta_output->crtc = &manager->crtcs[j];
                      break;
                    }
                }
            }
          else
            meta_output->crtc = NULL;

	  meta_output->is_primary = FALSE;
	  meta_output->is_presentation = FALSE;

          for (j = 0; j < (unsigned)connector->count_props; j++)
            {
              drmModePropertyPtr prop;

              prop = drmModeGetProperty(manager_kms->fd, connector->props[j]);

              if (prop)
                {
                  if ((prop->flags & DRM_MODE_PROP_ENUM) &&
                      strcmp(prop->name, "DPMS") == 0)
                    {
                      output_kms->dpms_prop_id = prop->prop_id;
                      drmModeFreeProperty(prop);
                      break;
                    }

                  drmModeFreeProperty(prop);
		}
            }

          /* FIXME: backlight is a very driver specific thing unfortunately,
             every DDX does its own thing, and the dumb KMS API does not include it.

             For example, xf86-video-intel has a list of paths to probe in /sys/class/backlight
             (one for each major HW maker, and then some).
             We can't do the same because we're not root.
             It might be best to leave backlight out of the story and rely on the setuid
             helper in gnome-settings-daemon.
          */
	  meta_output->backlight_min = 0;
          meta_output->backlight_max = 0;
          meta_output->backlight = -1;

	  n_actual_outputs++;
	}
    }

  manager->n_outputs = n_actual_outputs;
  manager->outputs = g_renew (MetaOutput, manager->outputs, manager->n_outputs);

  /* Sort the outputs for easier handling in MetaMonitorConfig */
  qsort (manager->outputs, manager->n_outputs, sizeof (MetaOutput), compare_outputs);

  /* Now fix the clones.
     Code mostly inspired by xf86-video-modesetting. */

  /* XXX: intel hardware doesn't usually have clones, but we only have intel
     cards, so this code was never tested! */
  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *meta_output;
      MetaOutputKms *output_kms;

      meta_output = &manager->outputs[i];
      output_kms = meta_output->driver_private;

      output_kms->enc_clone_mask = 0xff;
      output_kms->encoder_mask = 0;

      for (j = 0; j < output_kms->n_encoders; j++)
	{
	  for (k = 0; k < manager_kms->n_encoders; k++)
	    {
	      if (output_kms->encoders[j]->encoder_id == manager_kms->encoders[k]->encoder_id)
		{
                  output_kms->encoder_mask |= (1 << k);
		  break;
		}
	    }

          output_kms->enc_clone_mask &= output_kms->encoders[j]->possible_clones;
	}
    }

  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *meta_output;
      MetaOutputKms *output_kms;

      meta_output = &manager->outputs[i];
      output_kms = meta_output->driver_private;

      if (output_kms->enc_clone_mask == 0)
        continue;

      for (j = 0; j < manager->n_outputs; j++)
        {
          MetaOutput *meta_clone;
          MetaOutputKms *clone_kms;

          meta_clone = &manager->outputs[i];
          clone_kms = meta_clone->driver_private;

          if (meta_clone == meta_output)
            continue;

          if (clone_kms->encoder_mask == 0)
            continue;

          if (clone_kms->encoder_mask == output_kms->enc_clone_mask)
            {
              meta_output->n_possible_clones++;
              meta_output->possible_clones = g_renew (MetaOutput *,
                                                      meta_output->possible_clones,
                                                      meta_output->n_possible_clones);
              meta_output->possible_clones[meta_output->n_possible_clones - 1] = meta_clone;
            }
        }
    }

  drmModeFreeResources (resources);
}

static void
meta_monitor_manager_kms_set_power_save_mode (MetaMonitorManager *manager,
                                              MetaPowerSave       mode)
{
  MetaMonitorManagerKms *manager_kms = META_MONITOR_MANAGER_KMS (manager);
  uint64_t state;
  unsigned i;

  switch (mode) {
  case META_POWER_SAVE_ON:
    state = DRM_MODE_DPMS_ON;
    break;
  case META_POWER_SAVE_STANDBY:
    state = DRM_MODE_DPMS_STANDBY;
    break;
  case META_POWER_SAVE_SUSPEND:
    state = DRM_MODE_DPMS_SUSPEND;
    break;
  case META_POWER_SAVE_OFF:
    state = DRM_MODE_DPMS_SUSPEND;
    break;
  default:
    return;
  }

  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *meta_output;
      MetaOutputKms *output_kms;

      meta_output = &manager->outputs[i];
      output_kms = meta_output->driver_private;

      if (output_kms->dpms_prop_id)
        {
          int ok = drmModeConnectorSetProperty(manager_kms->fd, meta_output->output_id,
                                               output_kms->dpms_prop_id, state);

          if (ok < 0)
            meta_warning ("Failed to set power save mode for output %s: %s\n",
                          meta_output->name, strerror (errno));
        }
    }
}

static void
crtc_free (CoglKmsCrtc *crtc)
{
  g_free (crtc->connectors);
  g_slice_free (CoglKmsCrtc, crtc);
}

static void
meta_monitor_manager_kms_apply_configuration (MetaMonitorManager *manager,
                                              MetaCRTCInfo       **crtcs,
                                              unsigned int         n_crtcs,
                                              MetaOutputInfo     **outputs,
                                              unsigned int         n_outputs)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  unsigned i;
  GList *cogl_crtcs;
  int width, height;
  gboolean ok;
  GError *error;

  cogl_crtcs = NULL;
  width = 0; height = 0;
  for (i = 0; i < n_crtcs; i++)
    {
      MetaCRTCInfo *crtc_info = crtcs[i];
      MetaCRTC *crtc = crtc_info->crtc;
      CoglKmsCrtc *cogl_crtc;

      crtc->dirty = TRUE;

      cogl_crtc = g_slice_new0 (CoglKmsCrtc);
      cogl_crtcs = g_list_prepend (cogl_crtcs, cogl_crtc);

      if (crtc_info->mode == NULL)
        {
          cogl_crtc->id = crtc->crtc_id;
          cogl_crtc->x = 0;
          cogl_crtc->y = 0;
          cogl_crtc->count = 0;
          memset (&cogl_crtc->mode, 0, sizeof (drmModeModeInfo));
          cogl_crtc->connectors = NULL;
          cogl_crtc->count = 0;

          crtc->rect.x = 0;
          crtc->rect.y = 0;
          crtc->rect.width = 0;
          crtc->rect.height = 0;
          crtc->current_mode = NULL;
        }
      else
        {
          MetaMonitorMode *mode;
          uint32_t *outputs;
          unsigned int j, n_outputs;

          mode = crtc_info->mode;

          cogl_crtc->id = crtc->crtc_id;
          cogl_crtc->x = crtc_info->x;
          cogl_crtc->y = crtc_info->y;
          cogl_crtc->count = n_outputs = crtc_info->outputs->len;
          cogl_crtc->connectors = outputs = g_new (uint32_t, n_outputs);

          for (j = 0; j < n_outputs; j++)
            {
              MetaOutput *output = ((MetaOutput**)crtc_info->outputs->pdata)[j];

              outputs[j] = output->output_id;

              output->dirty = TRUE;
              output->crtc = crtc;
            }

          memcpy (&cogl_crtc->mode, crtc_info->mode->driver_private,
                  sizeof (drmModeModeInfo));

          width = MAX (width, crtc_info->x + crtc_info->mode->width);
          height = MAX (height, crtc_info->y + crtc_info->mode->height);

          crtc->rect.x = crtc_info->x;
          crtc->rect.y = crtc_info->y;
          crtc->rect.width = mode->width;
          crtc->rect.height = mode->height;
          crtc->current_mode = mode;
          crtc->transform = crtc_info->transform;
        }
    }

  /* Disable CRTCs not mentioned in the list */
  for (i = 0; i < manager->n_crtcs; i++)
    {
      MetaCRTC *crtc = &manager->crtcs[i];
      CoglKmsCrtc *cogl_crtc;

      crtc->logical_monitor = NULL;

      if (crtc->dirty)
        {
          crtc->dirty = FALSE;
          continue;
        }

      cogl_crtc = g_slice_new0 (CoglKmsCrtc);
      cogl_crtcs = g_list_prepend (cogl_crtcs, cogl_crtc);

      cogl_crtc->id = crtc->crtc_id;
      cogl_crtc->x = 0;
      cogl_crtc->y = 0;
      cogl_crtc->count = 0;
      memset (&cogl_crtc->mode, 0, sizeof (drmModeModeInfo));
      cogl_crtc->connectors = NULL;
      cogl_crtc->count = 0;

      crtc->rect.x = 0;
      crtc->rect.y = 0;
      crtc->rect.width = 0;
      crtc->rect.height = 0;
      crtc->current_mode = NULL;
    }

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cogl_display = cogl_context_get_display (cogl_context);

  error = NULL;
  ok = cogl_kms_display_set_layout (cogl_display, width, height, cogl_crtcs, &error);
  g_list_free_full (cogl_crtcs, (GDestroyNotify) crtc_free);

  if (!ok)
    {
      meta_warning ("Applying display configuration failed: %s\n", error->message);
      g_error_free (error);
      return;
    }

  for (i = 0; i < n_outputs; i++)
    {
      MetaOutputInfo *output_info = outputs[i];
      MetaOutput *output = output_info->output;

      output->is_primary = output_info->is_primary;
      output->is_presentation = output_info->is_presentation;
    }

  /* Disable outputs not mentioned in the list */
  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *output = &manager->outputs[i];

      if (output->dirty)
        {
          output->dirty = FALSE;
          continue;
        }

      output->crtc = NULL;
      output->is_primary = FALSE;
    }

  manager->screen_width = width;
  manager->screen_height = height;

  meta_monitor_manager_rebuild_derived (manager);
}

static void
meta_monitor_manager_kms_get_crtc_gamma (MetaMonitorManager  *manager,
                                         MetaCRTC            *crtc,
                                         gsize               *size,
                                         unsigned short     **red,
                                         unsigned short     **green,
                                         unsigned short     **blue)
{
  MetaMonitorManagerKms *manager_kms = META_MONITOR_MANAGER_KMS (manager);
  drmModeCrtc *kms_crtc;

  kms_crtc = drmModeGetCrtc (manager_kms->fd, crtc->crtc_id);

  *size = kms_crtc->gamma_size;
  *red = g_new (unsigned short, *size);
  *green = g_new (unsigned short, *size);
  *blue = g_new (unsigned short, *size);

  drmModeCrtcGetGamma (manager_kms->fd, crtc->crtc_id, *size, *red, *green, *blue);

  drmModeFreeCrtc (kms_crtc);
}

static void
meta_monitor_manager_kms_set_crtc_gamma (MetaMonitorManager *manager,
                                         MetaCRTC           *crtc,
                                         gsize               size,
                                         unsigned short     *red,
                                         unsigned short     *green,
                                         unsigned short     *blue)
{
  MetaMonitorManagerKms *manager_kms = META_MONITOR_MANAGER_KMS (manager);

  drmModeCrtcSetGamma (manager_kms->fd, crtc->crtc_id, size, red, green, blue);
}

static void
meta_monitor_manager_kms_init (MetaMonitorManagerKms *manager_kms)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  CoglRenderer *cogl_renderer;

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cogl_display = cogl_context_get_display (cogl_context);
  cogl_renderer = cogl_display_get_renderer (cogl_display);

  manager_kms->fd = cogl_kms_renderer_get_kms_fd (cogl_renderer);
}

static void
meta_monitor_manager_kms_finalize (GObject *object)
{
  MetaMonitorManagerKms *manager_kms = META_MONITOR_MANAGER_KMS (object);
  unsigned i;

  for (i = 0; i < manager_kms->n_encoders; i++)
    drmModeFreeEncoder (manager_kms->encoders[i]);
  for (i = 0; i < manager_kms->n_connectors; i++)
    drmModeFreeConnector (manager_kms->connectors[i]);

  g_free (manager_kms->encoders);
  g_free (manager_kms->connectors);

  G_OBJECT_CLASS (meta_monitor_manager_kms_parent_class)->finalize (object);
}

static void
meta_monitor_manager_kms_class_init (MetaMonitorManagerKmsClass *klass)
{
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_monitor_manager_kms_finalize;

  manager_class->read_current = meta_monitor_manager_kms_read_current;
  manager_class->apply_configuration = meta_monitor_manager_kms_apply_configuration;
  manager_class->set_power_save_mode = meta_monitor_manager_kms_set_power_save_mode;
  manager_class->get_crtc_gamma = meta_monitor_manager_kms_get_crtc_gamma;
  manager_class->set_crtc_gamma = meta_monitor_manager_kms_set_crtc_gamma;
}

