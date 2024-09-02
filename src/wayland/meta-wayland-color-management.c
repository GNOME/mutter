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

#include "meta-wayland-color-management.h"

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "core/meta-debug-control-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland-outputs.h"

#include "color-management-v1-server-protocol.h"

struct _MetaWaylandColorManager
{
  GObject parent;

  MetaWaylandCompositor *compositor;
  struct wl_global *global;

  gulong color_state_changed_handler_id;

  /* struct wl_resource */
  GList *resources;

  /* Key:   MetaMonitor
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

  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;
  float min_lum, max_lum, ref_lum;
} MetaWaylandCreatorParams;

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
wayland_tf_to_clutter (enum xx_color_manager_v4_transfer_function  tf,
                       ClutterTransferFunction                    *tf_out)
{
  switch (tf)
    {
    case XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB:
      *tf_out = CLUTTER_TRANSFER_FUNCTION_SRGB;
      return TRUE;
    case XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ:
      *tf_out = CLUTTER_TRANSFER_FUNCTION_PQ;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum xx_color_manager_v4_transfer_function
clutter_tf_to_wayland (ClutterTransferFunction tf)
{
  switch (tf)
    {
    case CLUTTER_TRANSFER_FUNCTION_DEFAULT:
    case CLUTTER_TRANSFER_FUNCTION_SRGB:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB;
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ;
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LINEAR;
    }
  g_assert_not_reached ();
}

static gboolean
wayland_primaries_to_clutter (enum xx_color_manager_v4_primaries  primaries,
                              ClutterColorspace                  *primaries_out)
{
  switch (primaries)
    {
    case XX_COLOR_MANAGER_V4_PRIMARIES_SRGB:
      *primaries_out = CLUTTER_COLORSPACE_SRGB;
      return TRUE;
    case XX_COLOR_MANAGER_V4_PRIMARIES_BT2020:
      *primaries_out = CLUTTER_COLORSPACE_BT2020;
      return TRUE;
    default:
      return FALSE;
    }
}

static enum xx_color_manager_v4_primaries
clutter_primaries_to_wayland (ClutterColorspace primaries)
{
  switch (primaries)
    {
    case CLUTTER_COLORSPACE_DEFAULT:
    case CLUTTER_COLORSPACE_SRGB:
      return XX_COLOR_MANAGER_V4_PRIMARIES_SRGB;
    case CLUTTER_COLORSPACE_BT2020:
      return XX_COLOR_MANAGER_V4_PRIMARIES_BT2020;
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
                                           enum xx_image_description_v4_cause  cause,
                                           const char                         *message)
{
  MetaWaylandImageDescription *image_desc;

  image_desc = meta_wayland_image_description_new (color_manager, resource);
  image_desc->state = META_WAYLAND_IMAGE_DESCRIPTION_STATE_FAILED;
  image_desc->has_info = FALSE;
  xx_image_description_v4_send_failed (resource, cause, message);

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
  xx_image_description_v4_send_ready (resource,
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
send_information (struct wl_resource *info_resource,
                  ClutterColorState  *color_state)
{
  ClutterColorspace clutter_colorspace;
  enum xx_color_manager_v4_primaries primaries;
  ClutterTransferFunction clutter_tf;
  enum xx_color_manager_v4_transfer_function tf;
  float min_lum, max_lum, ref_lum;

  clutter_colorspace = clutter_color_state_get_colorspace (color_state);
  primaries = clutter_primaries_to_wayland (clutter_colorspace);
  xx_image_description_info_v4_send_primaries_named (info_resource, primaries);

  clutter_tf = clutter_color_state_get_transfer_function (color_state);
  tf = clutter_tf_to_wayland (clutter_tf);
  xx_image_description_info_v4_send_tf_named (info_resource, tf);

  clutter_color_state_get_luminances (color_state,
                                      &min_lum, &max_lum, &ref_lum);
  xx_image_description_info_v4_send_luminances (info_resource,
                                                float_to_scaled_uint32 (min_lum),
                                                (uint32_t) max_lum,
                                                (uint32_t) ref_lum);
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
                              XX_IMAGE_DESCRIPTION_V4_ERROR_NOT_READY,
                              "The image description is not ready");
      return;
    }

  if (!image_desc->has_info)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_V4_ERROR_NO_INFORMATION,
                              "The image description has no information");
      return;
    }

  g_return_if_fail (image_desc->color_state);

  info_resource =
    wl_resource_create (client,
                        &xx_image_description_info_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  send_information (info_resource, image_desc->color_state);

  xx_image_description_info_v4_send_done (info_resource);
  wl_resource_destroy (info_resource);
}

static const struct xx_image_description_v4_interface
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

      xx_color_management_feedback_surface_v4_send_preferred_changed (resource);
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
      /* FIXME: the next version will have an ERROR_INERT */
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Underlying surface object has been destroyed");
      return;
    }

  if (!image_desc->color_state ||
      image_desc->state != META_WAYLAND_IMAGE_DESCRIPTION_STATE_READY)
    {
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Trying to set an image description which is not ready");
      return;
    }

  switch (render_intent)
    {
    case XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL:
      break;
    default:
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_RENDER_INTENT,
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
      /* FIXME: the next version will have an ERROR_INERT */
      wl_resource_post_error (resource,
                              XX_COLOR_MANAGEMENT_SURFACE_V4_ERROR_IMAGE_DESCRIPTION,
                              "Underlying surface object has been destroyed");
      return;
    }

  set_image_description (cm_surface, NULL);
}

static const struct xx_color_management_surface_v4_interface
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

  g_hash_table_insert (color_manager->outputs,
                       meta_wayland_output_get_monitor (output),
                       cm_output);

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

  g_hash_table_remove (cm_output->color_manager->outputs,
                       meta_wayland_output_get_monitor (cm_output->output));

  free (cm_output);
}

static MetaWaylandColorManagementOutput *
ensure_color_management_output (MetaWaylandColorManager *color_manager,
                                MetaWaylandOutput       *output)
{
  MetaWaylandColorManagementOutput *cm_output;

  cm_output = g_hash_table_lookup (color_manager->outputs,
                                   meta_wayland_output_get_monitor (output));
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
  MetaWaylandCompositor *compositor = wl_client_get_user_data (client);
  MetaWaylandColorManager *color_manager =
    g_object_get_data (G_OBJECT (compositor), "-meta-wayland-color-manager");
  struct wl_resource *image_desc_resource;
  MetaWaylandImageDescription *image_desc;

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
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
                                                   XX_IMAGE_DESCRIPTION_V4_CAUSE_NO_OUTPUT,
                                                   "Underlying output object has been destroyed");
    }

  wl_resource_set_implementation (image_desc_resource,
                                  &meta_wayland_image_description_interface,
                                  image_desc,
                                  image_description_destructor);
}

static const struct xx_color_management_output_v4_interface
  meta_wayland_color_management_output_interface =
{
  color_management_output_destroy,
  color_management_output_get_image_description,
};

static MetaWaylandCreatorParams *
meta_wayland_creator_params_new (MetaWaylandColorManager *color_manager,
                                 struct wl_resource      *resource)
{
  MetaWaylandCreatorParams *creator_params;

  creator_params = g_new0 (MetaWaylandCreatorParams, 1);
  creator_params->color_manager = color_manager;
  creator_params->resource = resource;

  creator_params->colorspace = CLUTTER_COLORSPACE_DEFAULT;
  creator_params->transfer_function = CLUTTER_TRANSFER_FUNCTION_DEFAULT;

  creator_params->min_lum = -1.0f;
  creator_params->max_lum = -1.0f;
  creator_params->ref_lum = -1.0f;

  return creator_params;
}

static void
meta_wayland_creator_params_free (MetaWaylandCreatorParams *creator_params)
{
  free (creator_params);
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

  if (creator_params->colorspace == CLUTTER_COLORSPACE_DEFAULT ||
      creator_params->transfer_function == CLUTTER_TRANSFER_FUNCTION_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INCOMPLETE_SET,
                              "Not all required parameters were set");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  color_state =
    clutter_color_state_new_full (clutter_context,
                                  creator_params->colorspace,
                                  creator_params->transfer_function,
                                  creator_params->min_lum,
                                  creator_params->max_lum,
                                  creator_params->ref_lum);

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
  ClutterTransferFunction clutter_tf;

  if (creator_params->transfer_function != CLUTTER_TRANSFER_FUNCTION_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_ALREADY_SET,
                              "The transfer characteristics were already set");
      return;
    }

  if (!wayland_tf_to_clutter (tf, &clutter_tf))
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_TF,
                              "The named transfer function is not supported");
      return;
    }

  creator_params->transfer_function = clutter_tf;
}

static void
creator_params_set_tf_power (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            eexp)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_TF,
                          "Setting power based transfer characteristics is not supported");
}

static void
creator_params_set_primaries_named (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            primaries)
{
  MetaWaylandCreatorParams *creator_params =
    wl_resource_get_user_data (resource);
  ClutterColorspace colorspace;

  if (creator_params->colorspace != CLUTTER_COLORSPACE_DEFAULT)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_ALREADY_SET,
                              "The primaries were already set");
      return;
    }

  if (!wayland_primaries_to_clutter (primaries, &colorspace))
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_PRIMARIES,
                              "The named primaries are not supported");
      return;
    }

  creator_params->colorspace = colorspace;
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
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_PRIMARIES,
                          "Setting arbitrary primaries is not supported");
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

  if (creator_params->min_lum >= 0.0f ||
      creator_params->max_lum >= 0.0f ||
      creator_params->ref_lum >= 0.0f)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_ALREADY_SET,
                              "The luminance was already set");
      return;
    }

  min = scaled_uint32_to_float (min_lum);
  max = (float) max_lum;
  ref = (float) reference_lum;

  if (max < ref)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_LUMINANCE,
                              "The maximum luminance is smaller than the reference luminance");
      return;
    }

  if (ref <= min)
    {
      wl_resource_post_error (resource,
                              XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_LUMINANCE,
                              "The reference luminance is less or equal to the minimum luminance");
      return;
    }

  creator_params->min_lum = min;
  creator_params->max_lum = max;
  creator_params->ref_lum = ref;
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
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_MASTERING,
                          "Setting mastering display primaries is not supported");
}

static void
creator_params_set_mastering_luminance (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            min_lum,
                                        uint32_t            max_lum)
{
  wl_resource_post_error (resource,
                          XX_IMAGE_DESCRIPTION_CREATOR_PARAMS_V4_ERROR_INVALID_MASTERING,
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

static const struct xx_image_description_creator_params_v4_interface
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
                        &xx_color_management_output_v4_interface,
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
                              XX_COLOR_MANAGER_V4_ERROR_SURFACE_EXISTS,
                              "surface already requested");
      return;
    }

  cm_surface->resource =
    wl_resource_create (client,
                        &xx_color_management_surface_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  wl_resource_set_implementation (cm_surface->resource,
                                  &meta_wayland_color_management_surface_interface,
                                  cm_surface,
                                  color_management_surface_destructor);
}

static void
color_management_feedback_surface_destroy (struct wl_client   *client,
                                           struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_management_feedback_surface_get_preferred (struct wl_client   *client,
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
                              XX_COLOR_MANAGEMENT_FEEDBACK_SURFACE_V4_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  image_desc_resource =
    wl_resource_create (client,
                        &xx_image_description_v4_interface,
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

static const struct xx_color_management_feedback_surface_v4_interface
  meta_wayland_color_management_feedback_surface_interface =
{
  color_management_feedback_surface_destroy,
  color_management_feedback_surface_get_preferred,
};

static void
color_management_feedback_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandColorManagementSurface *cm_surface =
    wl_resource_get_user_data (resource);

  if (!cm_surface)
    return;

  cm_surface->feedback_resources =
    g_list_remove (cm_surface->feedback_resources, resource);
}

static void
color_manager_get_feedback_surface (struct wl_client   *client,
                                    struct wl_resource *resource,
                                    uint32_t            id,
                                    struct wl_resource *surface_resource)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorManagementSurface *cm_surface;
  struct wl_resource *cm_feedback_surface_resource;

  cm_surface = ensure_color_management_surface (color_manager, surface);

  cm_feedback_surface_resource =
    wl_resource_create (client,
                        &xx_color_management_feedback_surface_v4_interface,
                        wl_resource_get_version (resource),
                        id);

  wl_resource_set_implementation (cm_feedback_surface_resource,
                                  &meta_wayland_color_management_feedback_surface_interface,
                                  cm_surface,
                                  color_management_feedback_surface_destructor);

  cm_surface->feedback_resources =
    g_list_prepend (cm_surface->feedback_resources,
                    cm_feedback_surface_resource);

  update_preferred_color_state (cm_surface);
}

static void
color_manager_new_icc_creator (struct wl_client   *client,
                               struct wl_resource *resource,
                               uint32_t            id)
{
  wl_resource_post_error (resource,
                          XX_COLOR_MANAGER_V4_ERROR_UNSUPPORTED_FEATURE,
                          "ICC-based image description creator is unsupported");
}

static void
color_manager_new_parametric_creator (struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id)
{
  MetaWaylandColorManager *color_manager = wl_resource_get_user_data (resource);
  MetaWaylandCreatorParams *creator_params;
  struct wl_resource *creator_resource;

  creator_resource =
    wl_resource_create (client,
                        &xx_image_description_creator_params_v4_interface,
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
color_manager_send_supported_events (struct wl_resource *resource)
{
  xx_color_manager_v4_send_supported_intent (resource,
                                             XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL);
  xx_color_manager_v4_send_supported_feature (resource,
                                              XX_COLOR_MANAGER_V4_FEATURE_PARAMETRIC);
  xx_color_manager_v4_send_supported_feature (resource,
                                              XX_COLOR_MANAGER_V4_FEATURE_SET_LUMINANCES);
  xx_color_manager_v4_send_supported_tf_named (resource,
                                               XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_SRGB);
  xx_color_manager_v4_send_supported_tf_named (resource,
                                               XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ);
  xx_color_manager_v4_send_supported_primaries_named (resource,
                                                      XX_COLOR_MANAGER_V4_PRIMARIES_SRGB);
  xx_color_manager_v4_send_supported_primaries_named (resource,
                                                      XX_COLOR_MANAGER_V4_PRIMARIES_BT2020);
}

static const struct xx_color_manager_v4_interface
  meta_wayland_color_manager_interface =
{
  color_manager_destroy,
  color_manager_get_output,
  color_manager_get_surface,
  color_manager_get_feedback_surface,
  color_manager_new_icc_creator,
  color_manager_new_parametric_creator,
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
                                 &xx_color_manager_v4_interface,
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

  cm_output = g_hash_table_lookup (color_manager->outputs, monitor);

  if (cm_output)
    {
      GList *l;

      for (l = cm_output->resources; l; l = l->next)
        {
          struct wl_resource *resource = l->data;

          xx_color_management_output_v4_send_image_description_changed (resource);
        }
    }

  wayland_output = g_hash_table_lookup (color_manager->compositor->outputs,
                                        meta_monitor_get_spec (monitor));

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

  g_clear_pointer (&color_manager->global, wl_global_remove);
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

static void
update_enabled (MetaWaylandColorManager *color_manager)
{
  MetaWaylandCompositor *compositor = color_manager->compositor;
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);
  gboolean is_enabled =
    meta_debug_control_is_color_management_protocol_enabled (debug_control);

  if (is_enabled && color_manager->global == NULL)
    {
      color_manager->global =
        wl_global_create (compositor->wayland_display,
                          &xx_color_manager_v4_interface,
                          META_XX_COLOR_MANAGEMENT_VERSION,
                          color_manager,
                          color_management_bind);

      if (color_manager->global == NULL)
        g_error ("Failed to register a global wp_color_management object");
    }
  else if (!is_enabled)
    {
      g_clear_pointer (&color_manager->global, wl_global_destroy);
    }
}

void
meta_wayland_init_color_management (MetaWaylandCompositor *compositor)
{
  MetaDebugControl *debug_control =
    meta_context_get_debug_control (compositor->context);
  g_autoptr (MetaWaylandColorManager) color_manager = NULL;

  color_manager = meta_wayland_color_manager_new (compositor);

  g_signal_connect_data (debug_control, "notify::color-management-protocol",
                         G_CALLBACK (update_enabled),
                         color_manager, NULL,
                         G_CONNECT_SWAPPED | G_CONNECT_AFTER);

  update_enabled (color_manager);

  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-color-manager",
                          g_steal_pointer (&color_manager),
                          g_object_unref);
}
