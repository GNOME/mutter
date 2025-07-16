/*
 * Copyright (C) 2025 Red Hat Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-color-representation.h"

#include <glib.h>
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-transaction.h"
#include "compositor/meta-multi-texture-format-private.h"

#include "color-representation-v1-server-protocol.h"

typedef struct _MetaWaylandColorRepresentationSurface
{
  MetaWaylandSurface *surface;
  gulong destroy_handler_id;
  struct wl_resource *resource;
} MetaWaylandColorRepresentationSurface;

static const struct {
  enum wp_color_representation_surface_v1_coefficients coeffs;
  enum wp_color_representation_surface_v1_range range;
  MetaMultiTextureCoefficients surface_coeffs;
} supported_coeffs[] = {
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL,
    META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_FULL,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED,
    META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_LIMITED,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL,
    META_MULTI_TEXTURE_COEFFICIENTS_BT709_FULL,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED,
    META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL,
    META_MULTI_TEXTURE_COEFFICIENTS_BT601_FULL,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED,
    META_MULTI_TEXTURE_COEFFICIENTS_BT601_LIMITED,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL,
    META_MULTI_TEXTURE_COEFFICIENTS_BT2020_FULL,
  },
  {
    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020,
    WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED,
    META_MULTI_TEXTURE_COEFFICIENTS_BT2020_LIMITED,
  },
};

static void
on_surface_destroyed (MetaWaylandSurface                    *surface,
                      MetaWaylandColorRepresentationSurface *crs)
{
  crs->surface = NULL;
}

static MetaWaylandColorRepresentationSurface *
meta_wayland_color_representation_surface_new (MetaWaylandSurface *surface,
                                               struct wl_resource *resource)
{
  MetaWaylandColorRepresentationSurface *crs;

  crs = g_new0 (MetaWaylandColorRepresentationSurface, 1);
  crs->surface = surface;
  crs->destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      crs);
  crs->resource = resource;

  return crs;
}

static void
meta_wayland_color_representation_surface_free (MetaWaylandColorRepresentationSurface *crs)
{
  if (crs->surface)
    g_clear_signal_handler (&crs->destroy_handler_id, crs->surface);
  g_free (crs);
}

gboolean
meta_wayland_color_representation_commit_check (MetaWaylandSurface *surface)
{
  MetaWaylandColorRepresentationSurface *crs =
    g_object_get_data (G_OBJECT (surface), "-meta-wayland-color-repr");
  MetaMultiTexture *tex = surface->committed_state.texture;
  MetaMultiTextureFormat format;
  const MetaMultiTextureFormatInfo *format_info;
  int hsub = 1;
  int vsub = 1;

  if (!tex || !crs)
    return TRUE;

  format = meta_multi_texture_get_format (tex);
  format_info = meta_multi_texture_format_get_info (format);

  /* we assume none of the formats have subsampled luma channels, so the
   * chroma subsampling is just the max subsampling of all planes */
  for (size_t i = 0; i < format_info->n_planes; i++)
    {
      hsub = MAX (hsub, format_info->hsub[i]);
      vsub = MAX (vsub, format_info->vsub[i]);
    }

  /* chroma subsampling location only meaningful on 4:2:0 subsampled textures.
   * 4:2:0 means chroma is subsampled horizontally and vertically by a factor
   * of 2. */
  if (surface->committed_state.chroma_loc != META_MULTI_TEXTURE_CHROMA_LOC_NONE &&
      (hsub != 2 || vsub != 2))
    {
      wl_resource_post_error (
        crs->resource,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT,
        "Commit contains a color representation with chroma location set and "
        "a buffer with a pixel format which is not 4:2:0 subsampled");
      return FALSE;
    }

  switch (surface->committed_state.coeffs)
    {
    case META_MULTI_TEXTURE_COEFFICIENTS_NONE:
    case META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_LIMITED:
    case META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_FULL:
      /* we assume all our MultiTextureFormats are either RGB or YCbCr
       * and identity is supported for both*/
      break;
    case META_MULTI_TEXTURE_COEFFICIENTS_BT709_FULL:
    case META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED:
    case META_MULTI_TEXTURE_COEFFICIENTS_BT601_FULL:
    case META_MULTI_TEXTURE_COEFFICIENTS_BT601_LIMITED:
    case META_MULTI_TEXTURE_COEFFICIENTS_BT2020_FULL:
    case META_MULTI_TEXTURE_COEFFICIENTS_BT2020_LIMITED:
      /* we assume all simple MultiTextures are RGB, everything else YCbCr
       * and the formats here only support YCbCr */
      if (meta_multi_texture_is_simple (tex))
        {
          wl_resource_post_error (
            crs->resource,
            WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT,
            "Commit contains a color representation with coefficients for a "
            "YCbCr pixel format and a buffer with an RGB pixel format");
          return FALSE;
        }
      break;
    case N_META_MULTI_TEXTURE_COEFFICIENTS:
      g_assert_not_reached ();
      break;
    }

  return TRUE;
}

static void
unset_pending_color_representation (MetaWaylandSurface *surface)
{
  MetaWaylandSurfaceState *pending;

  g_return_if_fail (surface != NULL);

  pending = meta_wayland_surface_get_pending_state (surface);
  if (!pending)
    return;

  pending->premult = META_MULTI_TEXTURE_ALPHA_MODE_NONE;
  pending->coeffs = META_MULTI_TEXTURE_COEFFICIENTS_NONE;
  pending->chroma_loc = META_MULTI_TEXTURE_CHROMA_LOC_NONE;

  pending->has_new_premult = TRUE;
  pending->has_new_coeffs = TRUE;
  pending->has_new_chroma_loc = TRUE;
}

static void
color_representation_surface_destructor (struct wl_resource *resource)
{
  MetaWaylandColorRepresentationSurface *crs =
    wl_resource_get_user_data (resource);

  if (crs->surface)
    {
      unset_pending_color_representation (crs->surface);
      g_object_set_data (G_OBJECT (crs->surface),
                         "-meta-wayland-color-repr",
                         NULL);
    }

  meta_wayland_color_representation_surface_free (crs);
}

static void
color_representation_surface_destroy (struct wl_client   *client,
                                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_representation_surface_set_alpha_mode (struct wl_client                                   *client,
                                             struct wl_resource                                 *resource,
                                             enum wp_color_representation_surface_v1_alpha_mode  alpha_mode)

{
  MetaWaylandColorRepresentationSurface *crs =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = crs->surface;
  MetaWaylandSurfaceState *pending;
  MetaMultiTextureAlphaMode premult = META_MULTI_TEXTURE_ALPHA_MODE_NONE;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  switch (alpha_mode)
    {
    case WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL:
      premult = META_MULTI_TEXTURE_ALPHA_MODE_PREMULT_ELECTRICAL;
      break;
    case WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT:
      premult = META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT;
      break;
    default:
      wl_resource_post_error (
        resource,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE,
        "Unsupported alpha mode");
      break;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->premult = premult;
  pending->has_new_premult = TRUE;
}

static void
color_representation_surface_set_coefficients_and_range (struct wl_client                                     *client,
                                                         struct wl_resource                                   *resource,
                                                         enum wp_color_representation_surface_v1_coefficients  coeffs,
                                                         enum wp_color_representation_surface_v1_range         range)

{
  MetaWaylandColorRepresentationSurface *crs =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = crs->surface;
  MetaWaylandSurfaceState *pending;
  MetaMultiTextureCoefficients surface_coeffs =
    META_MULTI_TEXTURE_COEFFICIENTS_NONE;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  for (size_t i = 0; i < G_N_ELEMENTS (supported_coeffs); i++)
    {
      if (supported_coeffs[i].coeffs != coeffs ||
          supported_coeffs[i].range != range)
        continue;

      surface_coeffs = supported_coeffs[i].surface_coeffs;
      break;
    }

  if (surface_coeffs == META_MULTI_TEXTURE_COEFFICIENTS_NONE)
    {
      wl_resource_post_error (
        resource,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS,
        "Unsupported coefficients");
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->coeffs = surface_coeffs;
  pending->has_new_coeffs = TRUE;
}

static void
color_representation_surface_set_chroma_location (struct wl_client                                        *client,
                                                  struct wl_resource                                      *resource,
                                                  enum wp_color_representation_surface_v1_chroma_location  chroma_loc)

{
  MetaWaylandColorRepresentationSurface *crs =
    wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = crs->surface;
  MetaWaylandSurfaceState *pending;
  MetaMultiTextureChromaLoc loc = META_MULTI_TEXTURE_CHROMA_LOC_NONE;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                              "Underlying surface object has been destroyed");
      return;
    }

  switch (chroma_loc)
    {
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_0:
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_1:
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_2:
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_3:
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_4:
    case WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_5:
      loc = META_MULTI_TEXTURE_CHROMA_LOC_DEFINED;
      break;
    default:
      wl_resource_post_error (resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "Invalid chroma location");
      break;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->chroma_loc = loc;
  pending->has_new_chroma_loc = TRUE;
}

static const struct wp_color_representation_surface_v1_interface
  color_representation_surface_implementation =
{
  color_representation_surface_destroy,
  color_representation_surface_set_alpha_mode,
  color_representation_surface_set_coefficients_and_range,
  color_representation_surface_set_chroma_location,
};

static void
color_representation_manager_destroy (struct wl_client   *client,
                                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
color_representation_manager_get_surface (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            id,
                                          struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandColorRepresentationSurface *crs;
  struct wl_resource *color_repr_resource;

  crs = g_object_get_data (G_OBJECT (surface), "-meta-wayland-color-repr");

  if (crs)
    {
      wl_resource_post_error (
        resource,
        WP_COLOR_REPRESENTATION_MANAGER_V1_ERROR_SURFACE_EXISTS,
        "a wp_color_representation_v1 object already exists for this surface");
      return;
    }

  color_repr_resource =
    wl_resource_create (client,
                        &wp_color_representation_surface_v1_interface,
                        wl_resource_get_version (resource),
                        id);

  crs = meta_wayland_color_representation_surface_new (surface,
                                                       color_repr_resource);

  wl_resource_set_implementation (color_repr_resource,
                                  &color_representation_surface_implementation,
                                  crs,
                                  color_representation_surface_destructor);

  g_object_set_data (G_OBJECT (surface), "-meta-wayland-color-repr", crs);
}

static const struct wp_color_representation_manager_v1_interface
  color_representation_manager_implementation =
{
  color_representation_manager_destroy,
  color_representation_manager_get_surface,
};

static void
send_supported (struct wl_resource *resource)
{
  wp_color_representation_manager_v1_send_supported_alpha_mode (
    resource,
    WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL);

  wp_color_representation_manager_v1_send_supported_alpha_mode (
    resource,
    WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);

  for (size_t i = 0; i < G_N_ELEMENTS (supported_coeffs); i++)
    {
      wp_color_representation_manager_v1_send_supported_coefficients_and_ranges (
        resource,
        supported_coeffs[i].coeffs,
        supported_coeffs[i].range);
    }

  wp_color_representation_manager_v1_send_done (resource);
}

static void
color_representation_manager_bind (struct wl_client *client,
                                   void             *data,
                                   uint32_t          version,
                                   uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_color_representation_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &color_representation_manager_implementation,
                                  NULL, NULL);

  send_supported (resource);
}

void
meta_wayland_init_color_representation (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &wp_color_representation_manager_v1_interface,
                         META_WP_COLOR_REPRESENTATION_VERSION,
                         compositor,
                         color_representation_manager_bind))
    g_warning ("Failed to create wp_color_representation_manager_v1 global");
}
