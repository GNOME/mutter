/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "config.h"

#include "cogl/cogl-driver-private.h"

typedef struct _CoglDriverPrivate
{
  /* Features cache */
  unsigned long features[COGL_FLAGS_N_LONGS_FOR_SIZE (_COGL_N_FEATURE_IDS)];
} CoglDriverPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglDriver, cogl_driver, G_TYPE_OBJECT)

static CoglBufferImpl *
cogl_driver_default_create_buffer_impl (CoglDriver *driver)
{
  g_assert_not_reached ();
}

static CoglTextureDriver *
cogl_driver_default_create_texture_driver (CoglDriver *driver)
{
  g_assert_not_reached ();
}

static void
cogl_driver_class_init (CoglDriverClass *klass)
{
  klass->create_buffer_impl = cogl_driver_default_create_buffer_impl;
  klass->create_texture_driver = cogl_driver_default_create_texture_driver;
}

static void
cogl_driver_init (CoglDriver *driver)
{
  CoglDriverPrivate *priv =
    cogl_driver_get_instance_private (driver);

  memset (priv->features, 0, sizeof (priv->features));

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_DISABLE_PBOS)))
    COGL_FLAGS_SET (priv->features, COGL_FEATURE_ID_PBOS, FALSE);
}

CoglBufferImpl *
cogl_driver_create_buffer_impl (CoglDriver *driver)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->create_buffer_impl (driver);
}

CoglTextureDriver *
cogl_driver_create_texture_driver (CoglDriver *driver)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->create_texture_driver (driver);
}

gboolean
cogl_driver_is_hardware_accelerated (CoglDriver *driver)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  if (klass->is_hardware_accelerated)
    return klass->is_hardware_accelerated (driver);
  else
    return FALSE;
}

const char *
cogl_driver_get_vendor (CoglDriver *driver)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->get_vendor (driver);
}

CoglGraphicsResetStatus
cogl_driver_get_graphics_reset_status (CoglDriver *driver)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->get_graphics_reset_status (driver);
}

gboolean
cogl_driver_update_features (CoglDriver    *driver,
                             CoglRenderer  *renderer,
                             GError       **error)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->update_features (driver, renderer, error);
}

gboolean
cogl_driver_format_supports_upload (CoglDriver     *driver,
                                    CoglPixelFormat format)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->format_supports_upload (driver, format);
}

gboolean
cogl_driver_has_feature (CoglDriver    *driver,
                         CoglFeatureID  feature)
{
  CoglDriverPrivate *priv =
    cogl_driver_get_instance_private (driver);

  return COGL_FLAGS_GET (priv->features, feature);
}

void
cogl_driver_set_feature (CoglDriver    *driver,
                         CoglFeatureID  feature,
                         gboolean       value)
{
  CoglDriverPrivate *priv =
    cogl_driver_get_instance_private (driver);

  COGL_FLAGS_SET (priv->features, feature, value);
}
