/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016-2020 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2018 Canonical Ltd.
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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "config.h"

#include "backends/native/meta-drm-buffer-private.h"

#include <drm_fourcc.h>

#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-utils.h"
#include "common/meta-drm-format-helpers.h"

#include "meta-private-enum-types.h"

#define INVALID_FB_ID 0U

enum
{
  PROP_0,

  PROP_DEVICE_FILE,
  PROP_FLAGS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaDrmBufferPrivate
{
  MetaDeviceFile *device_file;
  MetaDrmBufferFlags flags;

  uint32_t fb_id;
  uint32_t handle;
} MetaDrmBufferPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaDrmBuffer, meta_drm_buffer,
                                     G_TYPE_OBJECT)

MetaDeviceFile *
meta_drm_buffer_get_device_file (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  return priv->device_file;
}

gboolean
meta_drm_buffer_ensure_fb_id (MetaDrmBuffer  *buffer,
                              GError        **error)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  if (priv->fb_id)
    return TRUE;

  return META_DRM_BUFFER_GET_CLASS (buffer)->ensure_fb_id (buffer, error);
}

gboolean
meta_drm_buffer_do_ensure_fb_id (MetaDrmBuffer        *buffer,
                                 const MetaDrmFbArgs  *fb_args,
                                 GError              **error)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);
  int fd;
  MetaDrmFormatBuf tmp;
  uint32_t fb_id;

  fd = meta_device_file_get_fd (priv->device_file);

  if (!(priv->flags & META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS) &&
      fb_args->modifiers[0] != DRM_FORMAT_MOD_INVALID)
    {
      if (drmModeAddFB2WithModifiers (fd,
                                      fb_args->width,
                                      fb_args->height,
                                      fb_args->format,
                                      fb_args->handles,
                                      fb_args->strides,
                                      fb_args->offsets,
                                      fb_args->modifiers,
                                      &fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB2WithModifiers failed: %s",
                       g_strerror (errno));
          return FALSE;
        }
    }
  else if (drmModeAddFB2 (fd,
                          fb_args->width,
                          fb_args->height,
                          fb_args->format,
                          fb_args->handles,
                          fb_args->strides,
                          fb_args->offsets,
                          &fb_id,
                          0))
    {
      if (fb_args->format != DRM_FORMAT_XRGB8888)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB2 failed (%s) and drmModeAddFB cannot be "
                       "used as a fallback because format=0x%x (%s).",
                       g_strerror (errno),
                       fb_args->format,
                       meta_drm_format_to_string (&tmp, fb_args->format));
          return FALSE;
        }

      if (drmModeAddFB (fd,
                        fb_args->width,
                        fb_args->height,
                        24,
                        32,
                        fb_args->strides[0],
                        fb_args->handles[0],
                        &fb_id))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB failed: %s",
                       g_strerror (errno));
          return FALSE;
        }
    }

  priv->fb_id = fb_id;
  priv->handle = fb_args->handle;

  return TRUE;
}

static void
meta_drm_buffer_release_fb_id (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);
  int fd;
  int ret;

  fd = meta_device_file_get_fd (priv->device_file);
  ret = drmModeCloseFB (fd, priv->fb_id);
  if (ret == -EINVAL)
    ret = drmModeRmFB (fd, priv->fb_id);
  if (ret != 0)
    g_warning ("drmModeRmFB: %s", g_strerror (-ret));

  priv->fb_id = 0;
}

int
meta_drm_buffer_export_fd (MetaDrmBuffer  *buffer,
                           GError        **error)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->export_fd (buffer, error);
}

uint32_t
meta_drm_buffer_get_fb_id (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  return priv->fb_id;
}

uint32_t
meta_drm_buffer_get_handle (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  return priv->handle;
}

int
meta_drm_buffer_get_width (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_width (buffer);
}

int
meta_drm_buffer_get_height (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_height (buffer);
}

int
meta_drm_buffer_get_stride (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_stride (buffer);
}

int
meta_drm_buffer_get_bpp (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_bpp (buffer);
}

uint32_t
meta_drm_buffer_get_format (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_format (buffer);
}

int
meta_drm_buffer_get_offset (MetaDrmBuffer *buffer,
                            int            plane)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_offset (buffer, plane);
}

uint64_t
meta_drm_buffer_get_modifier (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_modifier (buffer);
}

static void
meta_drm_buffer_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (object);
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  switch (prop_id)
    {
    case PROP_DEVICE_FILE:
      g_value_set_pointer (value, priv->device_file);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, priv->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_drm_buffer_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (object);
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  switch (prop_id)
    {
    case PROP_DEVICE_FILE:
      priv->device_file = g_value_get_pointer (value);
      break;
    case PROP_FLAGS:
      priv->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_drm_buffer_finalize (GObject *object)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (object);
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  if (priv->fb_id != INVALID_FB_ID)
    meta_drm_buffer_release_fb_id (buffer);
  meta_device_file_release (priv->device_file);

  G_OBJECT_CLASS (meta_drm_buffer_parent_class)->finalize (object);
}

static void
meta_drm_buffer_constructed (GObject *object)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (object);
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  meta_device_file_acquire (priv->device_file);

  G_OBJECT_CLASS (meta_drm_buffer_parent_class)->constructed (object);
}

static void
meta_drm_buffer_init (MetaDrmBuffer *buffer)
{
}

static void
meta_drm_buffer_class_init (MetaDrmBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_drm_buffer_get_property;
  object_class->set_property = meta_drm_buffer_set_property;
  object_class->constructed = meta_drm_buffer_constructed;
  object_class->finalize = meta_drm_buffer_finalize;

  obj_props[PROP_DEVICE_FILE] =
    g_param_spec_pointer ("device-file", NULL, NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        META_TYPE_DRM_BUFFER_FLAGS,
                        META_DRM_BUFFER_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
