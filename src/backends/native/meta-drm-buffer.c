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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "config.h"

#include "backends/native/meta-drm-buffer-private.h"

#include <drm_fourcc.h>

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms-private.h"

#define INVALID_FB_ID 0U

enum
{
  PROP_0,

  PROP_DEVICE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaDrmBufferPrivate
{
  MetaKmsDevice *device;
  uint32_t fb_id;
} MetaDrmBufferPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaDrmBuffer, meta_drm_buffer,
                                     G_TYPE_OBJECT)

MetaKmsDevice *
meta_drm_buffer_get_device (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  return priv->device;
}

gboolean
meta_drm_buffer_ensure_fb_in_impl (MetaDrmBuffer        *buffer,
                                   gboolean              use_modifiers,
                                   const MetaDrmFbArgs  *fb_args,
                                   GError              **error)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);
  MetaKmsDevice *device = priv->device;
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  int fd;
  MetaDrmFormatBuf tmp;
  uint32_t fb_id;

  fd = meta_kms_impl_device_get_fd (impl_device);

  if (use_modifiers && fb_args->modifiers[0] != DRM_FORMAT_MOD_INVALID)
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
                       G_IO_ERROR_FAILED,
                       "drmModeAddFB does not support format '%s' (0x%x)",
                       meta_drm_format_to_string (&tmp, fb_args->format),
                       fb_args->format);
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

  return TRUE;
}

typedef struct
{
  MetaDrmBuffer *buffer;
  gboolean use_modifiers;
  MetaDrmFbArgs fb_args;
} AddFbData;

static gpointer
add_fb_in_impl (MetaKmsImpl  *impl,
                gpointer      user_data,
                GError      **error)
{
  AddFbData *data = user_data;

  if (meta_drm_buffer_ensure_fb_in_impl (data->buffer,
                                         data->use_modifiers,
                                         &data->fb_args,
                                         error))
    return GINT_TO_POINTER (TRUE);
  else
    return GINT_TO_POINTER (FALSE);
}

gboolean
meta_drm_buffer_ensure_fb_id (MetaDrmBuffer        *buffer,
                              gboolean              use_modifiers,
                              const MetaDrmFbArgs  *fb_args,
                              GError              **error)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);
  AddFbData data;

  data = (AddFbData) {
    .buffer = buffer,
    .use_modifiers = use_modifiers,
    .fb_args = *fb_args,
  };

  if (!meta_kms_run_impl_task_sync (meta_kms_device_get_kms (priv->device),
                                    add_fb_in_impl,
                                    &data,
                                    error))
    return FALSE;

  return TRUE;
}

typedef struct
{
  MetaKmsDevice *device;
  uint32_t fb_id;
} RmFbData;

static gpointer
rm_fb_in_impl (MetaKmsImpl  *impl,
               gpointer      user_data,
               GError      **error)
{
  RmFbData *data = user_data;
  MetaKmsDevice *device = data->device;
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  uint32_t fb_id = data->fb_id;
  int fd;
  int ret;

  fd = meta_kms_impl_device_get_fd (impl_device);
  ret = drmModeRmFB (fd, fb_id);
  if (ret != 0)
    g_warning ("drmModeRmFB: %s", g_strerror (-ret));

  return GINT_TO_POINTER (TRUE);
}

static void
meta_drm_buffer_release_fb_id (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);
  RmFbData data;

  data = (RmFbData) {
    .device = priv->device,
    .fb_id = priv->fb_id,
  };

  meta_kms_run_impl_task_sync (meta_kms_device_get_kms (priv->device),
                               rm_fb_in_impl,
                               &data,
                               NULL);
  priv->fb_id = 0;
}

uint32_t
meta_drm_buffer_get_fb_id (MetaDrmBuffer *buffer)
{
  MetaDrmBufferPrivate *priv = meta_drm_buffer_get_instance_private (buffer);

  return priv->fb_id;
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

uint32_t
meta_drm_buffer_get_format (MetaDrmBuffer *buffer)
{
  return META_DRM_BUFFER_GET_CLASS (buffer)->get_format (buffer);
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
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
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
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
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

  G_OBJECT_CLASS (meta_drm_buffer_parent_class)->finalize (object);
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
  object_class->finalize = meta_drm_buffer_finalize;

  obj_props[PROP_DEVICE] =
    g_param_spec_object ("device",
                         "device",
                         "MetaKmsDevice",
                         META_TYPE_KMS_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
