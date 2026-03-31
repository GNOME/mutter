/*
 * Copyright (C) 2017-2026 Red Hat Inc.
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
 */

#include "config.h"

#include "backends/meta-stream.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-eis.h"
#include "backends/meta-eis.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-render-device.h"
#include "backends/native/meta-renderer-native-private.h"
#include "common/meta-cogl-drm-formats.h"

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_CURSOR_MODE,
  PROP_IS_CONFIGURED,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  READY,
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaStreamPrivate
{
  MetaBackend *backend;

  MetaEis *eis;
  char *mapping_id;

  MetaStreamCursorMode cursor_mode;
  gboolean is_configured;

  MetaStreamSource *source;
} MetaStreamPrivate;

G_DEFINE_TYPE_WITH_CODE (MetaStream, meta_stream, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaStream))

static MetaStreamSource *
meta_stream_create_source (MetaStream  *stream,
                           GError     **error)
{
  return META_STREAM_GET_CLASS (stream)->create_source (stream, error);
}

static void
meta_stream_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaStream *stream = META_STREAM (object);
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_CURSOR_MODE:
      priv->cursor_mode = g_value_get_uint (value);
      break;
    case PROP_IS_CONFIGURED:
      priv->is_configured = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaStream *stream = META_STREAM (object);
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_CURSOR_MODE:
      g_value_set_uint (value, priv->cursor_mode);
      break;
    case PROP_IS_CONFIGURED:
      g_value_set_boolean (value, priv->is_configured);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_dispose (GObject *object)
{
  MetaStream *stream = META_STREAM (object);
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  if (priv->source)
    meta_stream_stop (stream);

  if (priv->mapping_id)
    {
      meta_eis_release_mapping_id (priv->eis, priv->mapping_id);
      g_clear_pointer (&priv->mapping_id, g_free);
    }

  G_OBJECT_CLASS (meta_stream_parent_class)->dispose (object);
}

static void
meta_stream_class_init (MetaStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_stream_dispose;
  object_class->set_property = meta_stream_set_property;
  object_class->get_property = meta_stream_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CURSOR_MODE] =
    g_param_spec_uint ("cursor-mode", NULL, NULL,
                       META_STREAM_CURSOR_MODE_HIDDEN,
                       META_STREAM_CURSOR_MODE_METADATA,
                       META_STREAM_CURSOR_MODE_HIDDEN,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IS_CONFIGURED] =
    g_param_spec_boolean ("is-configured", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[READY] = g_signal_new ("ready",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void
meta_stream_init (MetaStream *stream)
{
}

static void
on_stream_source_closed (MetaStreamSource *source,
                         MetaStream       *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  if (priv->source)
    meta_stream_stop (stream);
}

static void
on_stream_source_ready (MetaStreamSource *source,
                        MetaStream       *stream)
{
  g_signal_emit (stream, signals[READY], 0);
}

MetaBackend *
meta_stream_get_backend (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return priv->backend;
}

gboolean
meta_stream_start (MetaStream  *stream,
                   GError     **error)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);
  MetaStreamSource *source;

  g_return_val_if_fail (!priv->source, FALSE);

  source = meta_stream_create_source (stream, error);
  if (!source)
    return FALSE;

  priv->source = source;
  g_signal_connect (source, "ready",
                    G_CALLBACK (on_stream_source_ready), stream);
  g_signal_connect (source, "closed",
                    G_CALLBACK (on_stream_source_closed), stream);

  return TRUE;
}

void
meta_stream_stop (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  g_clear_object (&priv->source);

  g_signal_emit (stream, signals[CLOSED], 0);
}

MetaStreamSource *
meta_stream_get_source (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return priv->source;
}

MetaStreamCursorMode
meta_stream_get_cursor_mode (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return priv->cursor_mode;
}

const char *
meta_stream_get_mapping_id (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return priv->mapping_id;
}

void
meta_stream_map_input (MetaStream *stream,
                       MetaEis    *eis)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);
  const char *mapping_id;

  g_return_if_fail (!priv->eis);

  priv->eis = eis;

  mapping_id = meta_eis_acquire_mapping_id (priv->eis);
  priv->mapping_id = g_strdup (mapping_id);
}

gboolean
meta_stream_transform_position (MetaStream *stream,
                                double      stream_x,
                                double      stream_y,
                                double     *x,
                                double     *y)
{
  MetaStreamClass *klass = META_STREAM_GET_CLASS (stream);

  return klass->transform_position (stream, stream_x, stream_y, x, y);
}

gboolean
meta_stream_is_configured (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return priv->is_configured;
}

gboolean
meta_stream_is_started (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  return !!priv->source;
}

void
meta_stream_notify_is_configured (MetaStream *stream)
{
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);

  priv->is_configured = TRUE;
  g_object_notify_by_pspec (G_OBJECT (stream), obj_props[PROP_IS_CONFIGURED]);
}

gboolean
meta_stream_get_preferred_modifier (MetaStream      *stream,
                                    CoglPixelFormat  format,
                                    GArray          *modifiers,
                                    int              width,
                                    int              height,
                                    uint64_t        *preferred_modifier)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaStreamPrivate *priv = meta_stream_get_instance_private (stream);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (priv->backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer =
    cogl_context_get_renderer (cogl_context);
  CoglRendererEGL *cogl_renderer_egl =
    cogl_renderer_get_winsys_data (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data =
    cogl_renderer_egl->platform;
  MetaRenderDevice *render_device =
    renderer_gpu_data->render_device;
  MetaRendererNative *renderer_native =
    renderer_gpu_data->renderer_native;
  int dmabuf_fd;
  uint32_t stride;
  uint32_t offset;
  g_autoptr (GError) error = NULL;
  const MetaFormatInfo *format_info;
  gboolean use_implicit_modifier;

  g_assert (cogl_renderer_is_dma_buf_supported (cogl_renderer));

  format_info = meta_format_info_from_cogl_format (format);
  g_assert (format_info);

  while (modifiers->len > 0)
    {
      g_autoptr (MetaDrmBuffer) dmabuf = NULL;
      g_autoptr (CoglFramebuffer) fb = NULL;

      if ((modifiers->len > 1) ||
          (g_array_index (modifiers, uint64_t, 0) != DRM_FORMAT_MOD_INVALID))
        use_implicit_modifier = FALSE;
      else
        use_implicit_modifier = TRUE;

      dmabuf = meta_render_device_allocate_dma_buf (render_device,
                                                    width, height,
                                                    format_info->drm_format,
                                                    (uint64_t *) modifiers->data,
                                                    use_implicit_modifier ? 0 : modifiers->len,
                                                    META_DRM_BUFFER_FLAG_NONE,
                                                    &error);
      if (!dmabuf)
        break;

      stride = meta_drm_buffer_get_stride (dmabuf);
      offset = meta_drm_buffer_get_offset_for_plane (dmabuf, 0);

      dmabuf_fd = meta_drm_buffer_export_fd (dmabuf, &error);
      if (dmabuf_fd == -1)
        break;

      if (use_implicit_modifier)
        {
          *preferred_modifier = DRM_FORMAT_MOD_INVALID;
          fb = meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                                width, height,
                                                                format_info->drm_format,
                                                                1,
                                                                &dmabuf_fd,
                                                                &stride,
                                                                &offset,
                                                                NULL,
                                                                &error);
        }
      else
        {
          *preferred_modifier = meta_drm_buffer_get_modifier (dmabuf);
          fb = meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                                width, height,
                                                                format_info->drm_format,
                                                                1,
                                                                &dmabuf_fd,
                                                                &stride,
                                                                &offset,
                                                                preferred_modifier,
                                                                &error);
        }
      close (dmabuf_fd);

      if (!fb)
        {
          int i;

          g_clear_error (&error);

          for (i = 0; i < modifiers->len; i++)
            {
              if (g_array_index (modifiers, uint64_t, i) == *preferred_modifier)
                {
                  g_array_remove_index (modifiers, i);
                  break;
                }
            }
        }
      else
        {
          return TRUE;
        }
    }
#endif

  g_array_set_size (modifiers, 0);
  return FALSE;
}

GArray *
meta_stream_query_modifiers (MetaStream      *stream,
                             CoglPixelFormat  format)
{
  MetaBackend *backend =
    meta_stream_get_backend (stream);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer =
    cogl_context_get_renderer (cogl_context);
  g_autoptr (GError) error = NULL;
  GArray *modifiers;
  uint64_t modifier;

  if (!cogl_renderer_is_dma_buf_supported (cogl_renderer))
    return g_array_new (FALSE, FALSE, sizeof (uint64_t));

  modifiers =
    cogl_renderer_query_drm_modifiers (cogl_renderer,
                                       format,
                                       COGL_DRM_MODIFIER_FILTER_SINGLE_PLANE |
                                       COGL_DRM_MODIFIER_FILTER_NOT_EXTERNAL_ONLY,
                                       &error);
  if (!modifiers)
    {
      meta_topic (META_DEBUG_SCREEN_CAST,
                  "Failed to query drm buffer modifiers: %s", error->message);
      modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));
    }

  modifier = cogl_renderer_get_implicit_drm_modifier (cogl_renderer);
  g_array_append_val (modifiers, modifier);

  return modifiers;
}

CoglDmaBufHandle *
meta_stream_create_dma_buf_handle (MetaStream      *stream,
                                   CoglPixelFormat  format,
                                   uint64_t         modifier,
                                   int              width,
                                   int              height)
{
  MetaBackend *backend = meta_stream_get_backend (stream);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer = cogl_context_get_renderer (cogl_context);
  g_autoptr (GError) error = NULL;
  CoglDmaBufHandle *dmabuf_handle;
  int n_modifiers;

  g_return_val_if_fail (cogl_renderer_is_dma_buf_supported (cogl_renderer), NULL);

  if (cogl_renderer_is_implicit_drm_modifier (cogl_renderer, modifier))
    n_modifiers = 0;
  else
    n_modifiers = 1;

  dmabuf_handle = cogl_renderer_create_dma_buf (cogl_renderer,
                                                format,
                                                &modifier, n_modifiers,
                                                width, height,
                                                &error);
  return dmabuf_handle;
}
