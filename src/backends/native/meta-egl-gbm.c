/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016, 2017 Red Hat Inc.
 * Copyright (C) 2018, 2019 DisplayLink (UK) Ltd.
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <drm_fourcc.h>

#include "backends/native/meta-egl-gbm.h"

typedef struct _GbmBoUserData
{
  EGLImageKHR egl_image;

  MetaEgl *egl;
  EGLDisplay egl_display;
} GbmBoUserData;

static EGLImageKHR
create_gbm_bo_egl_image (MetaEgl        *egl,
                         EGLDisplay      egl_display,
                         struct gbm_bo  *shared_bo,
                         GError        **error)
{
  g_autofd int shared_bo_fd = -1;
  unsigned int width;
  unsigned int height;
  uint32_t i, n_planes;
  uint32_t *strides;
  uint32_t *offsets;
  uint64_t *modifiers;
  int *fds;
  uint32_t format;
  EGLImageKHR egl_image;
  gboolean use_modifiers;

  shared_bo_fd = gbm_bo_get_fd (shared_bo);
  if (shared_bo_fd < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to export gbm_bo: %s", strerror (errno));
      return FALSE;
    }

  width = gbm_bo_get_width (shared_bo);
  height = gbm_bo_get_height (shared_bo);
  format = gbm_bo_get_format (shared_bo);

  n_planes = gbm_bo_get_plane_count (shared_bo);
  fds = g_alloca (sizeof (*fds) * n_planes);
  strides = g_alloca (sizeof (*strides) * n_planes);
  offsets = g_alloca (sizeof (*offsets) * n_planes);
  modifiers = g_alloca (sizeof (*modifiers) * n_planes);

  for (i = 0; i < n_planes; i++)
    {
      strides[i] = gbm_bo_get_stride_for_plane (shared_bo, i);
      offsets[i] = gbm_bo_get_offset (shared_bo, i);
      modifiers[i] = gbm_bo_get_modifier (shared_bo);
      fds[i] = shared_bo_fd;
    }

  /* Workaround for https://gitlab.gnome.org/GNOME/mutter/issues/18 */
  if (modifiers[0] == DRM_FORMAT_MOD_LINEAR ||
      modifiers[0] == DRM_FORMAT_MOD_INVALID)
    use_modifiers = FALSE;
  else
    use_modifiers = TRUE;

  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            format,
                                            n_planes,
                                            fds,
                                            strides,
                                            offsets,
                                            use_modifiers ? modifiers : NULL,
                                            error);

  return egl_image;
}

static void
free_gbm_bo_egl_image (struct gbm_bo *bo,
                       void          *data)
{
  GbmBoUserData *user_data = data;
  g_autoptr (GError) error = NULL;

  if (!meta_egl_destroy_image (user_data->egl,
                               user_data->egl_display,
                               user_data->egl_image,
                               &error))
    {
      g_warning ("Could not destroy EGLImage attached to GBM BO: %s", error->message);
    }

  g_free (data);
}

EGLImageKHR
meta_egl_ensure_gbm_bo_egl_image (MetaEgl        *egl,
                                  EGLDisplay      egl_display,
                                  struct gbm_bo  *bo,
                                  GError        **error)
{
  GbmBoUserData *bo_user_data = NULL;

  bo_user_data = gbm_bo_get_user_data (bo);

  if (!bo_user_data)
    {
      EGLImageKHR egl_image = EGL_NO_IMAGE;

      egl_image = create_gbm_bo_egl_image (egl,
                                           egl_display,
                                           bo,
                                           error);

      if (!egl_image)
        return EGL_NO_IMAGE;

      bo_user_data = g_new0 (GbmBoUserData, 1);
      bo_user_data->egl = egl;
      bo_user_data->egl_display = egl_display;
      bo_user_data->egl_image = egl_image;

      gbm_bo_set_user_data (bo, bo_user_data, free_gbm_bo_egl_image);
    }

  return bo_user_data->egl_image;
}
