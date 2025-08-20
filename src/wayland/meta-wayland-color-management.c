/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 * Copyright (C) 2024 Red Hat
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
 * Written by:
 *     Joan Torres <joan.torres@suse.com>
 *     Sebastian Wick <sebastian.wick@redhat.com>
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>

#include "meta-wayland-color-management.h"

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-outputs.h"

#include "color-management-v1-server-protocol.h"

struct _MetaWaylandColorManager
{
  GObject parent;

  MetaWaylandCompositor *compositor;

  gulong color_state_changed_handler_id;

  /* struct wl_resource */
  GList *resources;

  /* Key:   MetaWaylandOutput
   * Value: MetaWaylandColorManagementOutput
   */
  GHashTable *outputs;

  /* Key:   MetaWaylandSurface
   * Value: MetaWaylandColorManagementSurface
   */
  GHashTable *surfaces;
};

#define META_TYPE_WAYLAND_COLOR_MANAGER (meta_wayland_color_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandColorManager,
                      meta_wayland_color_manager,
                      META, WAYLAND_COLOR_MANAGER,
                      GObject)

G_DEFINE_TYPE (MetaWaylandColorManager,
               meta_wayland_color_manager,
               G_TYPE_OBJECT)

typedef struct _MetaWaylandColorManagementOutput
{
  MetaWaylandColorManager *color_manager;

  /* struct wl_resource */
  GList *resources;
  MetaWaylandOutput *output;
  gulong output_destroyed_handler_id;
} MetaWaylandColorManagementOutput;

typedef struct _MetaWaylandColorManagementSurface
{
  MetaWaylandColorManager *color_manager;

  struct wl_resource *resource;
  /* struct wl_resource */
  GList *feedback_resources;

  MetaWaylandSurface *surface;
  gulong surface_destroyed_handler_id;
  gulong surface_main_monitor_handler_id;
  ClutterColorState *preferred_color_state;
} MetaWaylandColorManagementSurface;

typedef enum _MetaWaylandImageDescriptionState
{
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_PENDING,
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY,
  META_WAYLAND_IMAGE_DESCRIPTION_STATE_FAILED,
} MetaWaylandImageDescriptionState;

typedef enum _MetaWaylandImageDescriptionFlags
{
  META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_DEFAULT = 0,
  META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_ALLOW_INFO = (1 << 0),
} MetaWaylandImageDescriptionFlags;

typedef struct _MetaWaylandImageDescription
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;
  MetaWaylandImageDescriptionState state;
  gboolean has_info;
  ClutterColorState *color_state;
} MetaWaylandImageDescription;

typedef struct _MetaWaylandCreatorParams
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;

  ClutterColorimetry colorimetry;
  ClutterEOTF eotf;
  ClutterLuminance lum;

  gboolean is_colorimetry_set;
  gboolean is_eotf_set;
  gboolean is_luminance_set;
} MetaWaylandCreatorParams;

typedef struct _MetaWaylandCreatorIcc
{
  MetaWaylandColorManager *color_manager;
  struct wl_resource *resource;
  struct wl_resource *image_desc_resource;

  int fd;
  uint32_t offset;
  uint32_t length;

  grefcount ref_count;
} MetaWaylandCreatorIcc;

static void meta_wayland_color_management_surface_free (MetaWaylandColorManagementSurface *cm_surface);

static void meta_wayland_color_management_output_free (MetaWaylandColorManagementOutput *cm_output);

static MetaMonitorManager *
get_monitor_manager (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_monitor_manager (backend);
}

static ClutterContext *
get_clutter_context (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_clutter_context (backend);
}

static MetaColorManager *
get_meta_color_manager (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaBackend *backend = meta_context_get_backend (compositor->context);

  return meta_backend_get_color_manager (backend);
}

static ClutterColorManager *
get_clutter_color_manager (MetaWaylandColorManager *color_manager)
{
  ClutterContext *clutter_context = get_clutter_context (color_manager);

  return clutter_context_get_color_manager (clutter_context);
}

static float
scaled_uint32_to_float_chromaticity (uint32_t value)
{
  return value * 0.000001f;
}

static uint32_t
float_to_scaled_uint32_chromaticity (float value)
{
  return (uint32_t) (value * 1000000);
}

static float
scaled_uint32_to_float (uint32_t value)
{
  return value * 0.0001f;
}

static uint32_t
float_to_scaled_uint32 (float value)
{
  return (uint32_t) (value * 10000);
}

static gboolean
wayland_tf_to_clutter (enum wp_color_manager_v1_transfer_function  tf,
                       ClutterEOTF                                *eotf)
{
  switch (tf)
    {
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.2f;
      return TRUE;
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.8f;
      return TRUE;
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
      return TRUE;
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_PQ;
      return TRUE;
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_BT709;
      return TRUE;
    case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum wp_color_manager_v1_transfer_function
clutter_tf_to_wayland (ClutterTransferFunction tf)
{
  switch (tf)
    {
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
    case CLUTTER_TRANSFER_FUNCTION_BT709:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR;
    }
  g_assert_not_reached ();
}

static gboolean
wayland_primaries_to_clutter (enum wp_color_manager_v1_primaries  primaries,
                              ClutterColorimetry                 *colorimetry)
{
  switch (primaries)
    {
    case WP_COLOR_MANAGER_V1_PRIMARIES_SRGB:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_SRGB;
      return TRUE;
    case WP_COLOR_MANAGER_V1_PRIMARIES_BT2020:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_BT2020;
      return TRUE;
    case WP_COLOR_MANAGER_V1_PRIMARIES_NTSC:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_NTSC;
      return TRUE;
    case WP_COLOR_MANAGER_V1_PRIMARIES_PAL:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_PAL;
      return TRUE;
    case WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_P3;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum wp_color_manager_v1_primaries
clutter_colorspace_to_wayland (ClutterColorspace colorspace)
{
  switch (colorspace)
    {
    case CLUTTER_COLORSPACE_SRGB:
      return WP_COLOR_MANAGER_V1_PRIMARIES_SRGB;
    case CLUTTER_COLORSPACE_BT2020:
      return WP_COLOR_MANAGER_V1_PRIMARIES_BT2020;
    case CLUTTER_COLORSPACE_NTSC:
      return WP_COLOR_MANAGER_V1_PRIMARIES_NTSC;
    case CLUTTER_COLORSPACE_PAL:
      return WP_COLOR_MANAGER_V1_PRIMARIES_PAL;
    case CLUTTER_COLORSPACE_P3:
      return WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3;
    }
  g_assert_not_reached ();
}

static ClutterColorState *
get_default_color_state (MetaWaylandColorManager *color_manager)
{
  ClutterColorManager *clutter_color_manager =
    get_clutter_color_manager (color_manager);

  return clutter_color_manager_get_default_color_state (clutter_color_manager);
}

static ClutterColorState *
get_output_color_state (MetaWaylandColorManager *color_manager,
                        MetaMonitor             *monitor)
{
  MetaColorManager *meta_color_manager = get_meta_color_manager (color_manager);
  MetaColorDevice *color_device =
    meta_color_manager_get_color_device (meta_color_manager, monitor);
  ClutterColorState *color_state = NULL;

  if (color_device)
    color_state = meta_color_device_get_color_state (color_device);

  if (!color_state)
    color_state = get_default_color_state (color_manager);

  return color_state;
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new (MetaWaylandColorManager *color_manager,
                                    struct wl_resource      *resource)
{
  MetaWaylandImageDescription *image_desc;

  image_desc = g_new0 (MetaWaylandImageDescription, 1);
  image_desc->color_manager = color_manager;
  image_desc->resource = resource;

  return image_desc;
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new_failed (MetaWaylandColorManager            *color_manager,
                                           struct wl_resource                 *resource,
                                           enum wp_image_description_v1_cause  cause,
                                           const char                         *message)
{
  MetaWaylandImageDescription *image_desc;

  image_desc = meta_wayland_image_description_new (color_manager, resource);
  image_desc->state = META_WAYLAND_IMAGE_DESCRIPTION_STATE_FAILED;
  image_desc->has_info = FALSE;
  wp_image_description_v1_send_failed (resource, cause, message);

  return image_desc;
}

static MetaWaylandImageDescription *
meta_wayland_image_description_new_color_state (MetaWaylandColorManager          *color_manager,
                                                struct wl_resource               *resource,
                                                ClutterColorState                *color_state,
                                                MetaWaylandImageDescriptionFlags  flags)
{
  MetaWaylandImageDescription *image_desc;

  image_desc = meta_wayland_image_description_new (color_manager, resource);
  image_desc->state = META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY;
  image_desc->has_info = !!(flags & META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_ALLOW_INFO);
  image_desc->color_state = g_object_ref (color_state);
  wp_image_description_v1_send_ready (resource,
                                      clutter_color_state_get_id (color_state));

  return image_desc;
}

static void
meta_wayland_image_description_free (MetaWaylandImageDescription *image_desc)
{
  g_clear_object (&image_desc->color_state);

  free (image_desc);
}

static void
image_description_destructor (struct wl_resource *resource)
{
  MetaWaylandImageDescription *image_desc = wl_resource_get_user_data (resource);

  meta_wayland_image_description_free (image_desc);
}

static void
image_description_destroy (struct wl_client   *client,
                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
send_information_from_icc_profile (struct wl_resource *info_resource,
                                   ClutterColorState  *color_state)
{
  ClutterColorStateIcc *color_state_icc;
  g_autoptr (GError) error = NULL;
  const MtkAnonymousFile *file;
  uint32_t icc_length;
  int icc_fd;

  color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);

  file = clutter_color_state_icc_get_file (color_state_icc);

  icc_fd = mtk_anonymous_file_open_fd (file, MTK_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  if (icc_fd == -1)
    {
      g_warning ("Failed sending ICC profile, couldn't open fd: %s",
                 g_strerror (errno));
      return;
    }

  icc_length = mtk_anonymous_file_size (file);

  wp_image_description_info_v1_send_icc_file (info_resource,
                                              icc_fd,
                                              icc_length);

  mtk_anonymous_file_close_fd (icc_fd);
}

static void
send_information_from_params (struct wl_resource *info_resource,
                              ClutterColorState  *color_state)
{
  enum wp_color_manager_v1_primaries primaries_named;
  enum wp_color_manager_v1_transfer_function tf;
  ClutterColorStateParams *color_state_params;
  const ClutterColorimetry *colorimetry;
  const ClutterPrimaries *primaries;
  const ClutterEOTF *eotf;
  const ClutterLuminance *lum;

  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);

  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  switch (colorimetry->type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      primaries_named = clutter_colorspace_to_wayland (colorimetry->colorspace);
      wp_image_description_info_v1_send_primaries_named (info_resource,
                                                         primaries_named);

      primaries = clutter_colorspace_to_primaries (colorimetry->colorspace);
      wp_image_description_info_v1_send_primaries (
        info_resource,
        float_to_scaled_uint32_chromaticity (primaries->r_x),
        float_to_scaled_uint32_chromaticity (primaries->r_y),
        float_to_scaled_uint32_chromaticity (primaries->g_x),
        float_to_scaled_uint32_chromaticity (primaries->g_y),
        float_to_scaled_uint32_chromaticity (primaries->b_x),
        float_to_scaled_uint32_chromaticity (primaries->b_y),
        float_to_scaled_uint32_chromaticity (primaries->w_x),
        float_to_scaled_uint32_chromaticity (primaries->w_y));
      break;
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      wp_image_description_info_v1_send_primaries (
        info_resource,
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->r_x),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->r_y),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->g_x),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->g_y),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->b_x),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->b_y),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->w_x),
        float_to_scaled_uint32_chromaticity (colorimetry->primaries->w_y));
      break;
    }

  eotf = clutter_color_state_params_get_eotf (color_state_params);
  switch (eotf->type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      tf = clutter_tf_to_wayland (eotf->tf_name);
      wp_image_description_info_v1_send_tf_named (info_resource, tf);
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      if (G_APPROX_VALUE (eotf->gamma_exp, 2.2f, 0.0001f))
        wp_image_description_info_v1_send_tf_named (info_resource,
                                                    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
      else if (G_APPROX_VALUE (eotf->gamma_exp, 2.8f, 0.0001f))
        wp_image_description_info_v1_send_tf_named (info_resource,
                                                    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28);
      else
        wp_image_description_info_v1_send_tf_power (info_resource,
                                                    float_to_scaled_uint32 (eotf->gamma_exp));
      break;
    }

  lum = clutter_color_state_params_get_luminance (color_state_params);
  wp_image_description_info_v1_send_luminances (info_resource,
                                                float_to_scaled_uint32 (lum->min),
                                                (uint32_t) lum->max,
                                                (uint32_t) lum->ref);
}

static void
send_information (struct wl_resource *info_resource,
                  ClutterColorState  *color_state)
{
  if (CLUTTER_IS_COLOR_STATE_ICC (color_state))
    send_information_from_icc_profile (info_resource, color_state);
  else if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state))
    send_information_from_params (info_resource, color_state);
  else
    g_assert_not_reached ();
}

static void
image_description_get_information (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id)
{
  MetaWaylandImageDescription *image_desc = wl_resource_get_user_data (resource);
  struct wl_resource *info_resource;

  if (image_desc->state != META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY,
                              "The image description is not ready");
      return;
    }

  if (!image_desc->has_info)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION,
                              "The image description has no information");
      return;
    }

  g_return_if_fail (image_desc->color_state);

  info_resource =
    wl_resource_create (client,
                        &wp_image_description_info_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  send_information (info_resource, image_desc->color_state);

  wp_image_description_info_v1_send_done (info_resource);
  wl_resource_destroy (info_resource);
}

static const struct wp_image_description_v1_interface
  meta_wayland_image_description_interface =
{
  image_description_destroy,
  image_description_get_information,
};

static void
update_preferred_color_state (MetaWaylandColorManagementSurface *cm_surface)
{
  MetaWaylandColorManager *color_manager = cm_surface->color_manager;
  MetaMonitorManager *monitor_manager = get_monitor_manager (color_manager);
  MetaWaylandSurface *surface = cm_surface->surface;
  MetaLogicalMonitor *logical_monitor;
  ClutterColorState *color_state = NULL;
  GList *l;
  gboolean initial = !cm_surface->preferred_color_state;

  g_return_if_fail (surface != NULL);

  logical_monitor = meta_wayland_surface_get_main_monitor (surface);
  if (!logical_monitor)
    {
      logical_monitor =
        meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
    }

  if (logical_monitor)
    {
      GList *monitors = meta_logical_monitor_get_monitors (logical_monitor);

      g_return_if_fail (monitors != NULL);

      color_state = get_output_color_state (color_manager, monitors->data);
    }

  if (!color_state)
    color_state = get_default_color_state (color_manager);

  if (cm_surface->preferred_color_state &&
      clutter_color_state_equals (color_state,
                                  cm_surface->preferred_color_state))
    return;

  g_set_object (&cm_surface->preferred_color_state, color_state);

  if (initial)
    return;

  for (l = cm_surface->feedback_resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      wp_color_management_surface_feedback_v1_send_preferred_changed (resource,
                                                                      clutter_color_state_get_id (color_state));
    }
}

static void
on_surface_destroy (MetaWaylandSurface *surface,
                    gpointer            user_data)
{
  MetaWaylandColorManagementSurface *cm_surface = user_data;

  meta_wayland_color_management_surface_free (cm_surface);
}

static void
on_main_monitor_changed (MetaWaylandSurface *surface,
                         GParamSpec         *pspec,
                         gpointer            user_data)
{
  MetaWaylandColorManagementSurface *cm_surface = user_data;

  update_preferred_color_state (cm_surface);
}

static MetaWaylandColorManagementSurface *
meta_wayland_color_management_surface_new (MetaWaylandColorManager *color_manager,
                                           MetaWaylandSurface      *surface)
{
  MetaWaylandColorManagementSurface *cm_surface;

  cm_surface = g_new0 (MetaWaylandColorManagementSurface, 1);
  cm_surface->color_manager = color_manager;
  cm_surface->surface = surface;

  cm_surface->surface_destroyed_handler_id =
    g_signal_connect (surface, "destroy",
                      G_CALLBACK (on_surface_destroy),
                      cm_surface);
  cm_surface->surface_main_monitor_handler_id =
    g_signal_connect (surface, "notify::main-monitor",
                      G_CALLBACK (on_main_monitor_changed),
                      cm_surface);

  g_hash_table_insert (color_manager->surfaces, surface, cm_surface);

  return cm_surface;
}

static void
meta_wayland_color_management_surface_free (MetaWaylandColorManagementSurface *cm_surface)
{
  GList *l;

  for (l = cm_surface->feedback_resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      wl_resource_set_user_data (resource, NULL);
    }

  g_clear_list (&cm_surface->feedback_resources, NULL);

  if (cm_surface->resource)
    wl_resource_set_user_data (cm_surface->resource, NULL);

  g_clear_signal_handler (&cm_surface->surface_destroyed_handler_id,
                          cm_surface->surface);
  g_clear_signal_handler (&cm_surface->surface_main_monitor_handler_id,
                          cm_surface->surface);

  g_hash_table_remove (cm_surface->color_manager->surfaces,
                       cm_surface->surface);

  free (cm_surface);
}

static MetaWaylandColorManagementSurface *
ensure_color_management_surface (MetaWaylandColorManager *color_manager,
                                 MetaWaylandSurface      *surface)
{
  MetaWaylandColorManagementSurface *cm_surface;

  cm_surface = g_hash_table_lookup (color_manager->surfaces, surface);
  if (cm_surface)
    return cm_surface;

  return meta_wayland_color_management_surface_new (color_manager, surface);
}

static void
set_image_description (MetaWaylandColorManagementSurface *cm_surface,
                       ClutterColorState                 *color_state)
{
  MetaWaylandColorManager *color_manager = cm_surface->color_manager;
  MetaWaylandSurface *surface = cm_surface->surface;
  MetaWaylandSurfaceState *pending =
    meta_wayland_surface_get_pending_state (surface);
  ClutterColorState *new_color_state;

  if (color_state)
    new_color_state = color_state;
  else
    new_color_state = get_default_color_state (color_manager);

  g_assert (new_color_state);

  pending->has_new_color_state = TRUE;
  g_set_object (&pending->color_state, new_color_state);
}

static void
color_management_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);

  if (!cm_surface)
    return;

  set_image_description (cm_surface, NULL);

  cm_surface->resource = NULL;
}

static void
color_management_surface_destroy (struct wl_client   *client,
                                  struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_surface_set_image_description (struct wl_client   *client,
                                                struct wl_resource *resource,
                                                struct wl_resource *image_desc_resource,
                                                uint32_t            render_intent)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);
  MetaWaylandImageDescription *image_desc =
    wl_resource_get_user_data (image_desc_resource);

  if (!cm_surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  if (!image_desc->color_state ||
      image_desc->state != META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION,
                              "Trying to set an image description which is not ready");
      return;
    }

  switch (render_intent)
    {
    case WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL:
      break;
    default:
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_RENDER_INTENT,
                              "Trying to use an unsupported rendering intent");
      return;
    }

  set_image_description (cm_surface, image_desc->color_state);
}

static void
color_management_surface_unset_image_description (struct wl_client   *client,
                                                  struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);

  if (!cm_surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  set_image_description (cm_surface, NULL);
}

static const struct wp_color_management_surface_v1_interface
  meta_wayland_color_management_surface_interface =
{
  color_management_surface_destroy,
  color_management_surface_set_image_description,
  color_management_surface_unset_image_description,
};

static void
on_output_destroyed (MetaWaylandOutput *wayland_output,
                     gpointer           user_data)
{
  MetaWaylandColorManagementOutput *cm_output = user_data;

  meta_wayland_color_management_output_free (cm_output);
}

static MetaWaylandColorManagementOutput *
meta_wayland_color_management_output_new (MetaWaylandColorManager *color_manager,
                                          MetaWaylandOutput       *output)
{
  MetaWaylandColorManagementOutput *cm_output;

  cm_output = g_new0 (MetaWaylandColorManagementOutput, 1);
  cm_output->color_manager = color_manager;
  cm_output->output = output;

  cm_output->output_destroyed_handler_id =
    g_signal_connect (output, "output-destroyed",
                      G_CALLBACK (on_output_destroyed),
                      cm_output);

  g_hash_table_insert (color_manager->outputs, output, cm_output);

  return cm_output;
}

static void
meta_wayland_color_management_output_free (MetaWaylandColorManagementOutput *cm_output)
{
  GList *l;

  for (l = cm_output->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      wl_resource_set_user_data (resource, NULL);
    }

  g_clear_list (&cm_output->resources, NULL);

  g_clear_signal_handler (&cm_output->output_destroyed_handler_id,
                          cm_output->output);

  g_hash_table_remove (cm_output->color_manager->outputs, cm_output->output);

  free (cm_output);
}

static MetaWaylandColorManagementOutput *
ensure_color_management_output (MetaWaylandColorManager *color_manager,
                                MetaWaylandOutput       *output)
{
  MetaWaylandColorManagementOutput *cm_output;

  cm_output = g_hash_table_lookup (color_manager->outputs, output);
  if (cm_output)
    return cm_output;

  return meta_wayland_color_management_output_new (color_manager, output);
}

static void
color_management_output_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementOutput *cm_output =
    wl_resource_get_user_data (resource);

  if (!cm_output)
    return;

  cm_output->resources = g_list_remove (cm_output->resources, resource);
}

static void
color_management_output_destroy (struct wl_client   *client,
                                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_output_get_image_description (struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t            id)
{
  MetaWaylandColorManagementOutput *cm_output =
    wl_resource_get_user_data (resource);
  MetaWaylandClient *wayland_client = meta_get_wayland_client (client);
  MetaContext *context = meta_wayland_client_get_context (wayland_client);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
  MetaWaylandColorManager *color_manager =
    g_object_get_data (G_OBJECT (compositor), "-meta-wayland-color-manager");
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  image_desc_resource =
    wl_resource_create (client,
                        &wp_image_description_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  if (cm_output)
    {
      MetaMonitor *monitor = meta_wayland_output_get_monitor (cm_output->output);
      ClutterColorState *color_state =
        get_output_color_state (color_manager, monitor);

      image_desc =
        meta_wayland_image_description_new_color_state (color_manager,
                                                        image_desc_resource,
                                                        color_state,
                                                        META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_DEFAULT |
                                                        META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_ALLOW_INFO);
    }
  else
    {
      image_desc =
        meta_wayland_image_description_new_failed (color_manager,
                                                   image_desc_resource,
                                                   WP_IMAGE_DESCRIPTION_V1_CAUSE_NO_OUTPUT,
                                                   "Underlying output object has been destroyed");
    }

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);
}

static const struct wp_color_management_output_v1_interface
  meta_wayland_color_management_output_interface =
{
  color_management_output_destroy,
  color_management_output_get_image_description,
};

static MetaWaylandCreatorIcc *
meta_wayland_creator_icc_new (MetaWaylandColorManager *color_manager,
                              struct wl_resource      *resource)
{
  MetaWaylandCreatorIcc *creator_icc;

  creator_icc = g_new0 (MetaWaylandCreatorIcc, 1);
  creator_icc->color_manager = color_manager;
  creator_icc->resource = resource;
  creator_icc->fd = -1;

  g_ref_count_init (&creator_icc->ref_count);

  return creator_icc;
}

static MetaWaylandCreatorIcc *
meta_wayland_creator_icc_ref (MetaWaylandCreatorIcc *creator_icc)
{
  g_ref_count_inc (&creator_icc->ref_count);
  return creator_icc;
}

static void
meta_wayland_creator_icc_unref (MetaWaylandCreatorIcc *creator_icc)
{
  if (g_ref_count_dec (&creator_icc->ref_count))
    {
      g_clear_fd (&creator_icc->fd, NULL);
      g_free (creator_icc);
    }
}

static void
on_icc_create_bytes_read (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  MetaWaylandCreatorIcc *creator_icc = user_data;
  MetaWaylandColorManager *color_manager = creator_icc->color_manager;
  ClutterContext *clutter_context = get_clutter_context (color_manager);
  struct wl_resource *image_desc_resource = creator_icc->image_desc_resource;
  g_autoptr (ClutterColorState) color_state = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree uint8_t *icc_bytes = NULL;
  MetaWaylandImageDescription *old_image_desc;
  MetaWaylandImageDescription *image_desc;
  uint32_t icc_length;

  if (meta_read_bytes_finish (result, &icc_bytes, &icc_length, &error))
    {
      color_state = clutter_color_state_icc_new (clutter_context,
                                                 icc_bytes,
                                                 icc_length,
                                                 &error);
    }

  if (color_state)
    {
      image_desc =
        meta_wayland_image_description_new_color_state (color_manager,
                                                        image_desc_resource,
                                                        color_state,
                                                        META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_DEFAULT);
    }
  else
    {
      image_desc =
        meta_wayland_image_description_new_failed (color_manager,
                                                   image_desc_resource,
                                                   WP_IMAGE_DESCRIPTION_V1_CAUSE_OPERATING_SYSTEM,
                                                   error->message);
    }

  old_image_desc = wl_resource_get_user_data (image_desc_resource);
  wl_resource_set_user_data (image_desc_resource, image_desc);
  meta_wayland_image_description_free (old_image_desc);

  meta_wayland_creator_icc_unref (creator_icc);
}

static void
creator_icc_create (struct wl_client   *client,
                    struct wl_resource *resource,
                    uint32_t            id)
{
  MetaWaylandCreatorIcc *creator_icc = wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = creator_icc->color_manager;
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  if (creator_icc->fd == -1)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_INCOMPLETE_SET,
                              "The ICC file has not been set");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &wp_image_description_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  image_desc =
    meta_wayland_image_description_new (color_manager,
                                        image_desc_resource);

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);

  creator_icc->image_desc_resource = image_desc_resource;

  meta_read_bytes (creator_icc->fd,
                   creator_icc->offset,
                   creator_icc->length,
                   on_icc_create_bytes_read,
                   meta_wayland_creator_icc_ref (creator_icc));

  wl_resource_destroy (resource);
}

static void
creator_icc_set_icc_file (struct wl_client   *client,
                          struct wl_resource *resource,
                          int32_t             fd,
                          uint32_t            offset,
                          uint32_t            length)
{
  MetaWaylandCreatorIcc *creator_icc = wl_resource_get_user_data (resource);
  g_autofd int icc_profile_fd = fd;
  struct stat stat;
  int flags;

  if (creator_icc->fd > 0)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_ALREADY_SET,
                              "The ICC file was already set");
      return;
    }

  flags = fcntl (icc_profile_fd, F_GETFL);
  if ((flags & O_ACCMODE) == O_WRONLY ||
      lseek (icc_profile_fd, 0, SEEK_CUR) < 0)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD,
                              "The ICC file is not readable and seekable");
      return;
    }

  if (length == 0 || length > (32 * 1024 * 1024))
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE,
                              "The size is 0 or bigger than 32 MB");
      return;
    }

  if (fstat (icc_profile_fd, &stat) == -1)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD,
                              "Couldn't fstat the ICC profile fd");
      return;
    }

  if (stat.st_size < offset + length)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_OUT_OF_FILE,
                              "ICC file shorter than expected");
      return;
    }

  creator_icc->fd = g_steal_fd (&icc_profile_fd);
  creator_icc->offset = offset;
  creator_icc->length = length;
}

static const struct wp_image_description_creator_icc_v1_interface
  meta_wayland_image_description_creator_icc_interface =
{
  creator_icc_create,
  creator_icc_set_icc_file,
};

static MetaWaylandCreatorParams *
meta_wayland_creator_params_new (MetaWaylandColorManager *color_manager,
                                 struct wl_resource      *resource)
{
  MetaWaylandCreatorParams *creator_params;

  creator_params = g_new0 (MetaWaylandCreatorParams, 1);
  creator_params->color_manager = color_manager;
  creator_params->resource = resource;

  return creator_params;
}

static void
meta_wayland_creator_params_free (MetaWaylandCreatorParams *creator_params)
{
  if (creator_params->is_colorimetry_set &&
      creator_params->colorimetry.type == CLUTTER_COLORIMETRY_TYPE_PRIMARIES)
    g_clear_pointer (&creator_params->colorimetry.primaries, g_free);

  g_free (creator_params);
}

static void
creator_params_destructor (struct wl_resource *resource)
{
  MetaWaylandCreatorParams *creator_params = wl_resource_get_user_data (resource);

  meta_wayland_creator_params_free (creator_params);
}

static void
creator_params_create (struct wl_client   *client,
                       struct wl_resource *resource,
                       uint32_t            id)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = creator_params->color_manager;
  ClutterContext *clutter_context = get_clutter_context (color_manager);
  struct wl_resource *image_desc_resource;
  g_autoptr (ClutterColorState) color_state = NULL;
  MetaWaylandImageDescription *image_desc;

  if (!creator_params->is_colorimetry_set || !creator_params->is_eotf_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INCOMPLETE_SET,
                              "Not all required parameters were set");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &wp_image_description_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  color_state =
    clutter_color_state_params_new_from_primitives (clutter_context,
                                                    creator_params->colorimetry,
                                                    creator_params->eotf,
                                                    creator_params->lum);

  image_desc =
    meta_wayland_image_description_new_color_state (color_manager,
                                                    image_desc_resource,
                                                    color_state,
                                                    META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_DEFAULT);

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);

  wl_resource_destroy (resource);
}

static void
creator_params_set_tf_named (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            tf)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterEOTF eotf;

  if (creator_params->is_eotf_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                              "The transfer characteristics were already set");
      return;
    }

  if (!wayland_tf_to_clutter (tf, &eotf))
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
                              "The named transfer function is not supported");
      return;
    }

  creator_params->eotf = eotf;
  creator_params->is_eotf_set = TRUE;
}

static void
creator_params_set_tf_power (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            eexp)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);

  if (creator_params->is_eotf_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                              "The transfer characteristics were already set");
      return;
    }

  if (eexp < 10000 || eexp > 100000)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
                              "The exponent must be between 1.0 and 10.0");
      return;
    }

  creator_params->eotf.type = CLUTTER_EOTF_TYPE_GAMMA;
  creator_params->eotf.gamma_exp = scaled_uint32_to_float (eexp);
  creator_params->is_eotf_set = TRUE;
}

static void
creator_params_set_primaries_named (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            primaries)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterColorimetry colorimetry;

  if (creator_params->is_colorimetry_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                              "The primaries were already set");
      return;
    }

  if (!wayland_primaries_to_clutter (primaries, &colorimetry))
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED,
                              "The named primaries are not supported");
      return;
    }

  creator_params->colorimetry = colorimetry;
  creator_params->is_colorimetry_set = TRUE;
}

static void
creator_params_set_primaries (struct wl_client   *client,
                              struct wl_resource *resource,
                              int32_t             r_x,
                              int32_t             r_y,
                              int32_t             g_x,
                              int32_t             g_y,
                              int32_t             b_x,
                              int32_t             b_y,
                              int32_t             w_x,
                              int32_t             w_y)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterPrimaries *primaries;

  if (creator_params->is_colorimetry_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                              "The primaries were already set");
      return;
    }

  primaries = g_new0 (ClutterPrimaries, 1);
  primaries->r_x = scaled_uint32_to_float_chromaticity (r_x);
  primaries->r_y = scaled_uint32_to_float_chromaticity (r_y);
  primaries->g_x = scaled_uint32_to_float_chromaticity (g_x);
  primaries->g_y = scaled_uint32_to_float_chromaticity (g_y);
  primaries->b_x = scaled_uint32_to_float_chromaticity (b_x);
  primaries->b_y = scaled_uint32_to_float_chromaticity (b_y);
  primaries->w_x = scaled_uint32_to_float_chromaticity (w_x);
  primaries->w_y = scaled_uint32_to_float_chromaticity (w_y);

  if (primaries->r_x < 0.0f || primaries->r_x > 1.0f ||
      primaries->r_y < 0.0f || primaries->r_y > 1.0f ||
      primaries->g_x < 0.0f || primaries->g_x > 1.0f ||
      primaries->g_y < 0.0f || primaries->g_y > 1.0f ||
      primaries->b_x < 0.0f || primaries->b_x > 1.0f ||
      primaries->b_y < 0.0f || primaries->b_y > 1.0f ||
      primaries->w_x < 0.0f || primaries->w_x > 1.0f ||
      primaries->w_y < 0.0f || primaries->w_y > 1.0f)
    {
      g_warning ("Primaries out of expected normalized range");
      clutter_primaries_ensure_normalized_range (primaries);
    }

  creator_params->colorimetry.type = CLUTTER_COLORIMETRY_TYPE_PRIMARIES;
  creator_params->colorimetry.primaries = primaries;
  creator_params->is_colorimetry_set = TRUE;
}

static void
creator_params_set_luminance (struct wl_client   *client,
                              struct wl_resource *resource,
                              uint32_t            min_lum,
                              uint32_t            max_lum,
                              uint32_t            reference_lum)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  float min, max, ref;

  if (creator_params->is_luminance_set)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                              "The luminance was already set");
      return;
    }

  min = scaled_uint32_to_float (min_lum);
  max = (float) max_lum;
  ref = (float) reference_lum;

  if (max <= min)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                              "The maximum luminance is smaller than the minimum luminance");
      return;
    }

  if (ref <= min)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                              "The reference luminance is less or equal to the minimum luminance");
      return;
    }

  if (ref > max)
    {
      wl_resource_post_error (resource,
                              WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                              "The reference luminance is bigger than the maximum luminance, "
                              "extended target volume unsupported");
      return;
    }

  creator_params->lum.type = CLUTTER_LUMINANCE_TYPE_EXPLICIT;
  creator_params->lum.min = min;
  creator_params->lum.max = max;
  creator_params->lum.ref = ref;
  creator_params->is_luminance_set = TRUE;
}

static void
creator_params_set_mastering_display_primaries (struct wl_client   *client,
                                                struct wl_resource *resource,
                                                int32_t             r_x,
                                                int32_t             r_y,
                                                int32_t             g_x,
                                                int32_t             g_y,
                                                int32_t             b_x,
                                                int32_t             b_y,
                                                int32_t             w_x,
                                                int32_t             w_y)
{
  wl_resource_post_error (resource,
                          WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
                          "Setting mastering display primaries is not supported");
}

static void
creator_params_set_mastering_luminance (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            min_lum,
                                        uint32_t            max_lum)
{
  wl_resource_post_error (resource,
                          WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
                          "Setting mastering display luminances is not supported");
}

static void
creator_params_set_max_cll (struct wl_client   *client,
                            struct wl_resource *resource,
                            uint32_t            max_cll)
{
  /* ignoring for now */
  /* FIXME: technically we must send errors in some cases */
}

static void
creator_params_set_max_fall (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            max_fall)
{
  /* ignoring for now */
  /* FIXME: technically we must send errors in some cases */
}

static const struct wp_image_description_creator_params_v1_interface
  meta_wayland_image_description_creator_params_interface =
{
  creator_params_create,
  creator_params_set_tf_named,
  creator_params_set_tf_power,
  creator_params_set_primaries_named,
  creator_params_set_primaries,
  creator_params_set_luminance,
  creator_params_set_mastering_display_primaries,
  creator_params_set_mastering_luminance,
  creator_params_set_max_cll,
  creator_params_set_max_fall,
};

static void
color_manager_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);

  color_manager->resources = g_list_remove (color_manager->resources, resource);
}

static void
color_manager_destroy (struct wl_client   *client,
                       struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_manager_get_output (struct wl_client   *client,
                          struct wl_resource *resource,
                          uint32_t            id,
                          struct wl_resource *output_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandOutput *output = wl_resource_get_user_data (output_resource);
  MetaWaylandColorManagementOutput *cm_output = NULL;
  struct wl_resource *cm_output_resource;

  cm_output_resource =
    wl_resource_create (client,
                        &wp_color_management_output_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  if (output)
    {
      cm_output = ensure_color_management_output (color_manager, output);

      cm_output->resources = g_list_prepend (cm_output->resources,
                                             cm_output_resource);
    }

  wl_resource_set_implementation (cm_output_resource,
                                  &meta_wayland_color_management_output_interface,
                                  cm_output,
                                  color_management_output_destructor);
}

static void
color_manager_get_surface (struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t            id,
                           struct wl_resource *surface_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorManagementSurface *cm_surface;

  cm_surface = ensure_color_management_surface (color_manager, surface);

  if (cm_surface->resource)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGER_V1_ERROR_SURFACE_EXISTS,
                              "surface already requested");
      return;
    }

  cm_surface->resource =
    wl_resource_create (client,
                        &wp_color_management_surface_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  wl_resource_set_implementation (cm_surface->resource,
                                  &meta_wayland_color_management_surface_interface,
                                  cm_surface,
                                  color_management_surface_destructor);
}

static void
color_management_surface_feedback_destroy (struct wl_client   *client,
                                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_surface_feedback_get_preferred (struct wl_client   *client,
                                                 struct wl_resource *resource,
                                                 uint32_t            id)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);
  MetaWaylandColorManager *color_manager = cm_surface->color_manager;
  MetaWaylandSurface *surface = cm_surface->surface;
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &wp_image_description_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  image_desc =
    meta_wayland_image_description_new_color_state (color_manager,
                                                    image_desc_resource,
                                                    cm_surface->preferred_color_state,
                                                    META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_DEFAULT |
                                                    META_WAYLAND_IMAGE_DESCRIPTION_FLAGS_ALLOW_INFO);

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);
}

static void
color_management_surface_feedback_get_preferred_parametric (struct wl_client   *client,
                                                            struct wl_resource *resource,
                                                            uint32_t            id)
{
  /* we currently only support parametric ones, so this is the same as get_preferred */
  color_management_surface_feedback_get_preferred (client, resource, id);
}

static const struct wp_color_management_surface_feedback_v1_interface
  meta_wayland_color_management_surface_feedback_interface =
{
  color_management_surface_feedback_destroy,
  color_management_surface_feedback_get_preferred,
  color_management_surface_feedback_get_preferred_parametric,
};

static void
color_management_surface_feedback_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);

  if (!cm_surface)
    return;

  cm_surface->feedback_resources =
    g_list_remove (cm_surface->feedback_resources, resource);
}

static void
color_manager_get_surface_feedback (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            id,
                                    struct wl_resource *surface_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorManagementSurface *cm_surface;
  struct wl_resource *cm_surface_feedback_resource;

  cm_surface = ensure_color_management_surface (color_manager, surface);

  cm_surface_feedback_resource =
    wl_resource_create (client,
                        &wp_color_management_surface_feedback_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  wl_resource_set_implementation (cm_surface_feedback_resource,
                                  &meta_wayland_color_management_surface_feedback_interface,
                                  cm_surface,
                                  color_management_surface_feedback_destructor);

  cm_surface->feedback_resources =
    g_list_prepend (cm_surface->feedback_resources,
                    cm_surface_feedback_resource);

  update_preferred_color_state (cm_surface);
}

static void
creator_icc_destructor (struct wl_resource *resource)
{
  MetaWaylandCreatorIcc *creator_icc = wl_resource_get_user_data (resource);

  meta_wayland_creator_icc_unref (creator_icc);
}

static void
color_manager_create_icc_creator (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  uint32_t            id)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandCreatorIcc *creator_icc;
  struct wl_resource *creator_resource;

  creator_resource =
    wl_resource_create (client,
                        &wp_image_description_creator_icc_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  creator_icc = meta_wayland_creator_icc_new (color_manager, creator_resource);

  wl_resource_set_implementation (creator_resource,
                                  &meta_wayland_image_description_creator_icc_interface,
                                  creator_icc,
                                  creator_icc_destructor);
}

static void
color_manager_create_parametric_creator (struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            id)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandCreatorParams *creator_params;
  struct wl_resource *creator_resource;

  creator_resource =
    wl_resource_create (client,
                        &wp_image_description_creator_params_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  creator_params = meta_wayland_creator_params_new (color_manager,
                                                    creator_resource);

  wl_resource_set_implementation (creator_resource,
                                  &meta_wayland_image_description_creator_params_interface,
                                  creator_params,
                                  creator_params_destructor);
}

static void
color_manager_create_windows_scrgb (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            id)
{
  wl_resource_post_error (resource,
                          WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE,
                          "Windows scRGB is not supported");
}

static void
color_manager_send_supported_events (struct wl_resource *resource)
{
  wp_color_manager_v1_send_supported_intent (resource,
                                             WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
  wp_color_manager_v1_send_supported_feature (resource,
                                              WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4);
  wp_color_manager_v1_send_supported_feature (resource,
                                              WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC);
  wp_color_manager_v1_send_supported_feature (resource,
                                              WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES);
  wp_color_manager_v1_send_supported_feature (resource,
                                              WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER);
  wp_color_manager_v1_send_supported_feature (resource,
                                              WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA28);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886);
  wp_color_manager_v1_send_supported_tf_named (resource,
                                               WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR);
  wp_color_manager_v1_send_supported_primaries_named (resource,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
  wp_color_manager_v1_send_supported_primaries_named (resource,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
  wp_color_manager_v1_send_supported_primaries_named (resource,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_NTSC);
  wp_color_manager_v1_send_supported_primaries_named (resource,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_PAL);
  wp_color_manager_v1_send_supported_primaries_named (resource,
                                                      WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3);
  wp_color_manager_v1_send_done (resource);
}

static const struct wp_color_manager_v1_interface
  meta_wayland_color_manager_interface =
{
  color_manager_destroy,
  color_manager_get_output,
  color_manager_get_surface,
  color_manager_get_surface_feedback,
  color_manager_create_icc_creator,
  color_manager_create_parametric_creator,
  color_manager_create_windows_scrgb,
};

static void
color_management_bind (struct wl_client *client,
                       void             *data,
                       uint32_t          version,
                       uint32_t          id)
{
  MetaWaylandColorManager *color_manager = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_color_manager_v1_interface,
                                 version,
                                 id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_color_manager_interface,
                                  color_manager,
                                  color_manager_destructor);

  color_manager->resources = g_list_prepend (color_manager->resources,
                                             resource);

  color_manager_send_supported_events (resource);
}

static void
update_output_color_state (MetaWaylandColorManager *color_manager,
                           MetaMonitor             *monitor)
{
  MetaWaylandColorManagementOutput *cm_output;
  GHashTableIter iter_surfaces;
  MetaWaylandColorManagementSurface *cm_surface;
  MetaWaylandOutput *wayland_output;

  wayland_output = g_hash_table_lookup (color_manager->compositor->outputs,
                                        meta_monitor_get_spec (monitor));

  cm_output = g_hash_table_lookup (color_manager->outputs, wayland_output);

  if (cm_output)
    {
      GList *l;

      for (l = cm_output->resources; l; l = l->next)
        {
          struct wl_resource *resource = l->data;

          wp_color_management_output_v1_send_image_description_changed (resource);
        }
    }

  g_hash_table_iter_init (&iter_surfaces, color_manager->surfaces);
  while (g_hash_table_iter_next (&iter_surfaces, NULL, (gpointer *)&cm_surface))
    {
      MetaWaylandSurface *surface = cm_surface->surface;

      if (g_hash_table_contains (surface->outputs, wayland_output))
        update_preferred_color_state (cm_surface);
    }
}

static void
on_device_color_state_changed (MetaColorManager        *meta_color_manager,
                               MetaColorDevice         *color_device,
                               MetaWaylandColorManager *color_manager)
{
  MetaMonitor *monitor = meta_color_device_get_monitor (color_device);

  update_output_color_state (color_manager, monitor);
}

static void
meta_wayland_color_manager_dispose (GObject *object)
{
  MetaWaylandColorManager *color_manager = META_WAYLAND_COLOR_MANAGER (object);
  MetaColorManager *meta_color_manager = get_meta_color_manager (color_manager);

  g_clear_signal_handler (&color_manager->color_state_changed_handler_id,
                          meta_color_manager);

  g_clear_pointer (&color_manager->outputs, g_hash_table_destroy);
  g_clear_pointer (&color_manager->surfaces, g_hash_table_destroy);
}

static void
meta_wayland_color_manager_init (MetaWaylandColorManager *color_manager)
{
  color_manager->outputs = g_hash_table_new (NULL, NULL);
  color_manager->surfaces = g_hash_table_new (NULL, NULL);
}

static void
meta_wayland_color_manager_class_init (MetaWaylandColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_color_manager_dispose;
}

static MetaWaylandColorManager *
meta_wayland_color_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandColorManager *color_manager;
  MetaColorManager *meta_color_manager;

  color_manager = g_object_new (META_TYPE_WAYLAND_COLOR_MANAGER, NULL);
  color_manager->compositor = compositor;

  meta_color_manager = get_meta_color_manager (color_manager);
  color_manager->color_state_changed_handler_id =
    g_signal_connect_object (meta_color_manager, "device-color-state-changed",
                             G_CALLBACK (on_device_color_state_changed),
                             color_manager, 0);

  return color_manager;
}

void
meta_wayland_init_color_management (MetaWaylandCompositor *compositor)
{
  g_autoptr (MetaWaylandColorManager) color_manager = NULL;

  color_manager = meta_wayland_color_manager_new (compositor);

  if (wl_global_create (compositor->wayland_display,
                        &wp_color_manager_v1_interface,
                        META_WP_COLOR_MANAGEMENT_VERSION,
                        color_manager,
                        color_management_bind) == NULL)
    g_error ("Failed to register a global wp_color_management object");

  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-color-manager",
                          g_steal_pointer (&color_manager),
                          g_object_unref);
}
