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

G_DEFINE_ABSTRACT_TYPE (CoglDriver, cogl_driver, G_TYPE_OBJECT)

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

int64_t
cogl_driver_timestamp_query_get_time_ns (CoglDriver         *driver,
                                         CoglTimestampQuery *query)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->timestamp_query_get_time_ns (driver, query);
}

void
cogl_driver_free_timestamp_query (CoglDriver         *driver,
                                  CoglTimestampQuery *query)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  klass->free_timestamp_query (driver, query);
}

gboolean
cogl_driver_update_features (CoglDriver  *driver,
                             CoglContext *context,
                             GError     **error)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->update_features (driver, context, error);
}

gboolean
cogl_driver_format_supports_upload (CoglDriver     *driver,
                                    CoglContext    *context,
                                    CoglPixelFormat format)
{
  CoglDriverClass *klass = COGL_DRIVER_GET_CLASS (driver);

  return klass->format_supports_upload (driver, context, format);
}

int64_t
cogl_driver_get_gpu_time_ns (CoglDriver  *driver,
                             CoglContext *context)
{
  g_return_val_if_fail (cogl_context_has_feature (context,
                                                  COGL_FEATURE_ID_TIMESTAMP_QUERY),
                        0);


  return COGL_DRIVER_GET_CLASS (driver)->get_gpu_time_ns (driver, context);
}
