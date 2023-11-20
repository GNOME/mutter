/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
 * Copyright 2020 DisplayLink (UK) Ltd.
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/native/meta-cursor-renderer-native.h"

#include <string.h>
#include <gbm.h>
#include <xf86drm.h>
#include <errno.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-sprite-xcursor.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-output.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-frame-native.h"
#include "backends/native/meta-kms-cursor-manager.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-renderer-native.h"
#include "backends/native/meta-seat-native.h"
#include "core/boxes-private.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#endif

static GQuark quark_cursor_sprite = 0;

typedef struct _CursorStageView
{
  gboolean needs_emit_painted;
  gboolean has_hw_cursor;

  gboolean is_hw_cursor_valid;
} CursorStageView;

struct _MetaCursorRendererNative
{
  MetaCursorRenderer parent;
};

struct _MetaCursorRendererNativePrivate
{
  MetaBackend *backend;

  MetaCursorSprite *current_cursor;
  gulong texture_changed_handler_id;

  guint animation_timeout_id;

  gulong pointer_position_changed_in_impl_handler_id;
  gboolean input_disconnected;
  GMutex input_mutex;
  GCond input_cond;
};
typedef struct _MetaCursorRendererNativePrivate MetaCursorRendererNativePrivate;

typedef struct _MetaCursorRendererNativeGpuData
{
  gboolean hw_cursor_broken;

  uint64_t cursor_width;
  uint64_t cursor_height;
} MetaCursorRendererNativeGpuData;

typedef struct _MetaCursorNativePrivate
{
  GHashTable *gpu_states;

  struct {
    gboolean can_preprocess;
    float current_relative_scale;
    MetaMonitorTransform current_relative_transform;
  } preprocess_state;
} MetaCursorNativePrivate;

static GQuark quark_cursor_renderer_native_gpu_data = 0;
static GQuark quark_cursor_stage_view = 0;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRendererNative, meta_cursor_renderer_native, META_TYPE_CURSOR_RENDERER);

static gboolean
realize_cursor_sprite_for_crtc (MetaCursorRenderer *renderer,
                                MetaCrtcKms        *crtc_kms,
                                MetaCursorSprite   *cursor_sprite);

static void
invalidate_cursor_gpu_state (MetaCursorRenderer *cursor_renderer,
                             MetaCursorSprite   *cursor_sprite);

static CursorStageView *
get_cursor_stage_view (MetaStageView *view)
{
  return g_object_get_qdata (G_OBJECT (view),
                             quark_cursor_stage_view);
}

static CursorStageView *
ensure_cursor_stage_view (MetaStageView *view)
{
  CursorStageView *cursor_stage_view;

  cursor_stage_view = get_cursor_stage_view (view);
  if (!cursor_stage_view)
    {
      cursor_stage_view = g_new0 (CursorStageView, 1);
      cursor_stage_view->is_hw_cursor_valid = FALSE;
      g_object_set_qdata_full (G_OBJECT (view),
                               quark_cursor_stage_view,
                               cursor_stage_view,
                               g_free);
    }

  return cursor_stage_view;
}

static MetaCursorRendererNativeGpuData *
meta_cursor_renderer_native_gpu_data_from_gpu (MetaGpuKms *gpu_kms)
{
  return g_object_get_qdata (G_OBJECT (gpu_kms),
                             quark_cursor_renderer_native_gpu_data);
}

static MetaCursorRendererNativeGpuData *
meta_create_cursor_renderer_native_gpu_data (MetaGpuKms *gpu_kms)
{
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;

  cursor_renderer_gpu_data = g_new0 (MetaCursorRendererNativeGpuData, 1);
  g_object_set_qdata_full (G_OBJECT (gpu_kms),
                           quark_cursor_renderer_native_gpu_data,
                           cursor_renderer_gpu_data,
                           g_free);

  return cursor_renderer_gpu_data;
}

static void
meta_cursor_renderer_native_finalize (GObject *object)
{
  MetaCursorRendererNative *renderer = META_CURSOR_RENDERER_NATIVE (object);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (renderer);

  g_clear_signal_handler (&priv->texture_changed_handler_id,
                          priv->current_cursor);
  g_clear_object (&priv->current_cursor);
  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  G_OBJECT_CLASS (meta_cursor_renderer_native_parent_class)->finalize (object);
}

static float
calculate_cursor_crtc_sprite_scale (MetaBackend        *backend,
                                    MetaCursorSprite   *cursor_sprite,
                                    MetaLogicalMonitor *logical_monitor)
{
  if (meta_backend_is_stage_views_scaled (backend))
    {
      return (meta_logical_monitor_get_scale (logical_monitor) *
              meta_cursor_sprite_get_texture_scale (cursor_sprite));
    }
  else
    {
      return meta_cursor_sprite_get_texture_scale (cursor_sprite);
    }
}

static void
disable_hw_cursor_for_gpu (MetaGpuKms   *gpu_kms,
                           const GError *error)
{
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);

  g_warning ("Failed to set hardware cursor (%s), "
             "using OpenGL from now on",
             error->message);
  cursor_renderer_gpu_data->hw_cursor_broken = TRUE;
}

void
meta_cursor_renderer_native_prepare_frame (MetaCursorRendererNative *cursor_renderer_native,
                                           MetaRendererView         *view,
                                           ClutterFrame             *frame)
{
  MetaCursorRenderer *cursor_renderer =
    META_CURSOR_RENDERER (cursor_renderer_native);
  CursorStageView *cursor_stage_view;
  MetaCursorSprite *cursor_sprite;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor_sprite)
    return;

  cursor_stage_view = get_cursor_stage_view (META_STAGE_VIEW (view));
  if (cursor_stage_view &&
      cursor_stage_view->needs_emit_painted)
    {
      meta_cursor_renderer_emit_painted (cursor_renderer,
                                         cursor_sprite,
                                         CLUTTER_STAGE_VIEW (view));
      cursor_stage_view->needs_emit_painted = FALSE;
    }
}

static gboolean
meta_cursor_renderer_native_update_animation (MetaCursorRendererNative *native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorSprite *cursor_sprite = meta_cursor_renderer_get_cursor (renderer);

  priv->animation_timeout_id = 0;
  meta_cursor_sprite_tick_frame (cursor_sprite);
  meta_cursor_renderer_force_update (renderer);

  return G_SOURCE_REMOVE;
}

static void
maybe_schedule_cursor_sprite_animation_frame (MetaCursorRendererNative *native,
                                              MetaCursorSprite         *cursor_sprite,
                                              gboolean                  cursor_changed)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  guint delay;

  if (!cursor_changed && priv->animation_timeout_id)
    return;

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  if (cursor_sprite && meta_cursor_sprite_is_animated (cursor_sprite))
    {
      delay = meta_cursor_sprite_get_current_frame_time (cursor_sprite);

      if (delay == 0)
        return;

      priv->animation_timeout_id =
        g_timeout_add (delay,
                       (GSourceFunc) meta_cursor_renderer_native_update_animation,
                       native);
      g_source_set_name_by_id (priv->animation_timeout_id,
                               "[mutter] meta_cursor_renderer_native_update_animation");
    }
}

static void
on_cursor_sprite_texture_changed (MetaCursorSprite   *cursor_sprite,
                                  MetaCursorRenderer *cursor_renderer)
{
  invalidate_cursor_gpu_state (cursor_renderer, cursor_sprite);
}

static gboolean
meta_cursor_renderer_native_update_cursor (MetaCursorRenderer *cursor_renderer,
                                           MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *native =
    META_CURSOR_RENDERER_NATIVE (cursor_renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaBackend *backend = priv->backend;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (priv->backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
  gboolean cursor_changed;
  GList *views;
  GList *l;

  COGL_TRACE_BEGIN_SCOPED (MetaCursorRendererNative,
                           "CursorRendererNative (update cursor)");

  cursor_changed = priv->current_cursor != cursor_sprite;

  views = meta_renderer_get_views (renderer);
  g_list_foreach (views, (GFunc) ensure_cursor_stage_view, NULL);

  for (l = views; l; l = l->next)
    {
      MetaStageView *view = l->data;
      MetaRendererView *renderer_view = META_RENDERER_VIEW (view);
      MetaCrtc *crtc = meta_renderer_view_get_crtc (renderer_view);
      MetaCrtcNative *crtc_native = META_CRTC_NATIVE (crtc);
      CursorStageView *cursor_stage_view = NULL;
      gboolean has_hw_cursor = FALSE;

      cursor_stage_view = get_cursor_stage_view (view);
      g_assert (cursor_stage_view);

      if (!META_IS_CRTC_KMS (crtc) ||
          !meta_crtc_native_is_hw_cursor_supported (crtc_native))
        {
          if (cursor_stage_view->has_hw_cursor)
            {
              meta_stage_view_uninhibit_cursor_overlay (view);
              cursor_stage_view->has_hw_cursor = FALSE;
            }
          continue;
        }

      if (cursor_sprite && !meta_backend_is_hw_cursors_inhibited (backend))
        {
          meta_cursor_sprite_realize_texture (cursor_sprite);

          if (cursor_changed ||
              !cursor_stage_view->is_hw_cursor_valid)
            {
              has_hw_cursor = realize_cursor_sprite_for_crtc (cursor_renderer,
                                                              META_CRTC_KMS (crtc),
                                                              cursor_sprite);

              cursor_stage_view->is_hw_cursor_valid = TRUE;
            }
          else
            {
              has_hw_cursor =
                cursor_stage_view->is_hw_cursor_valid &&
                cursor_stage_view->has_hw_cursor;
            }

          if (has_hw_cursor)
            cursor_stage_view->needs_emit_painted = TRUE;
        }
      else
        {
          cursor_stage_view->is_hw_cursor_valid = FALSE;
          has_hw_cursor = FALSE;
        }

      if (cursor_stage_view->has_hw_cursor != has_hw_cursor)
        {
          if (has_hw_cursor)
            meta_stage_view_inhibit_cursor_overlay (view);
          else
            meta_stage_view_uninhibit_cursor_overlay (view);

          cursor_stage_view->has_hw_cursor = has_hw_cursor;

          if (!has_hw_cursor)
            {
              MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
              MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);

              meta_kms_cursor_manager_update_sprite (kms_cursor_manager,
                                                     kms_crtc,
                                                     NULL,
                                                     META_MONITOR_TRANSFORM_NORMAL,
                                                     NULL);
            }
        }
    }

  if (cursor_changed)
    {
      if (priv->current_cursor)
        {
          g_clear_signal_handler (&priv->texture_changed_handler_id,
                                  priv->current_cursor);
        }

      g_set_object (&priv->current_cursor, cursor_sprite);

      if (priv->current_cursor)
        {
          priv->texture_changed_handler_id =
            g_signal_connect (cursor_sprite, "texture-changed",
                              G_CALLBACK (on_cursor_sprite_texture_changed),
                              cursor_renderer);
        }
    }

  maybe_schedule_cursor_sprite_animation_frame (native, cursor_sprite,
                                                cursor_changed);

  return cursor_sprite && meta_cursor_sprite_get_cogl_texture (cursor_sprite);
}

static void
invalidate_cursor_gpu_state (MetaCursorRenderer *cursor_renderer,
                             MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *native =
    META_CURSOR_RENDERER_NATIVE (cursor_renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaRenderer *renderer = meta_backend_get_renderer (priv->backend);
  GList *views;
  GList *l;

  views = meta_renderer_get_views (renderer);
  for (l = views; l; l = l->next)
    {
      MetaStageView *view = l->data;
      CursorStageView *cursor_stage_view;

      cursor_stage_view = get_cursor_stage_view (view);
      cursor_stage_view->is_hw_cursor_valid = FALSE;
    }
}

static MetaDrmBuffer *
create_cursor_drm_buffer_gbm (MetaGpuKms         *gpu_kms,
                              MetaDeviceFile     *device_file,
                              struct gbm_device  *gbm_device,
                              uint8_t            *pixels,
                              int                 width,
                              int                 height,
                              int                 stride,
                              int                 cursor_width,
                              int                 cursor_height,
                              uint32_t            format,
                              GError            **error)
{
  struct gbm_bo *bo;
  uint8_t buf[4 * cursor_width * cursor_height];
  int i;
  MetaDrmBufferFlags flags;
  MetaDrmBufferGbm *buffer_gbm;

  if (!gbm_device_is_format_supported (gbm_device, format,
                                       GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Buffer format not supported");
      return NULL;
    }

  bo = gbm_bo_create (gbm_device, cursor_width, cursor_height,
                      format, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
  if (!bo)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Failed to allocate gbm_bo: %s", g_strerror (errno));
      return NULL;
    }

  memset (buf, 0, sizeof (buf));
  for (i = 0; i < height; i++)
    memcpy (buf + i * 4 * cursor_width, pixels + i * stride, width * 4);
  if (gbm_bo_write (bo, buf, cursor_width * cursor_height * 4) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Failed write to gbm_bo: %s", g_strerror (errno));
      gbm_bo_destroy (bo);
      return NULL;
    }

  flags = META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;
  buffer_gbm = meta_drm_buffer_gbm_new_take (device_file, bo, flags, error);
  if (!buffer_gbm)
    {
      gbm_bo_destroy (bo);
      return NULL;
    }

  return META_DRM_BUFFER (buffer_gbm);
}

static MetaDrmBuffer *
create_cursor_drm_buffer_dumb (MetaGpuKms      *gpu_kms,
                               MetaDeviceFile  *device_file,
                               uint8_t         *pixels,
                               int              width,
                               int              height,
                               int              stride,
                               int              cursor_width,
                               int              cursor_height,
                               uint32_t         format,
                               GError         **error)
{
  MetaDrmBufferDumb *buffer_dumb;
  int i;
  uint8_t *data;

  buffer_dumb = meta_drm_buffer_dumb_new (device_file,
                                          cursor_width, cursor_height,
                                          format,
                                          error);
  if (!buffer_dumb)
    return NULL;

  data = meta_drm_buffer_dumb_get_data (buffer_dumb);

  memset (data, 0, cursor_width * cursor_height * 4);
  for (i = 0; i < height; i++)
    memcpy (data + i * 4 * cursor_width, pixels + i * stride, width * 4);

  return META_DRM_BUFFER (buffer_dumb);
}

static MetaDrmBuffer *
create_cursor_drm_buffer (MetaGpuKms      *gpu_kms,
                          MetaDeviceFile  *device_file,
                          uint8_t         *pixels,
                          int              width,
                          int              height,
                          int              stride,
                          int              cursor_width,
                          int              cursor_height,
                          uint32_t         format,
                          GError         **error)
{
  struct gbm_device *gbm_device;

  gbm_device = meta_gbm_device_from_gpu (gpu_kms);
  if (gbm_device)
    {
      return create_cursor_drm_buffer_gbm (gpu_kms, device_file, gbm_device,
                                           pixels,
                                           width, height, stride,
                                           cursor_width, cursor_height,
                                           format,
                                           error);
    }
  else
    {
      return create_cursor_drm_buffer_dumb (gpu_kms, device_file,
                                            pixels,
                                            width, height, stride,
                                            cursor_width, cursor_height,
                                            format,
                                            error);
    }
}

static void
calculate_crtc_cursor_hotspot (MetaCursorSprite     *cursor_sprite,
                               float                 scale,
                               MetaMonitorTransform  transform,
                               graphene_point_t     *hotspot)
{
  int hot_x, hot_y;
  int width, height;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  width = meta_cursor_sprite_get_width (cursor_sprite);
  height = meta_cursor_sprite_get_height (cursor_sprite);
  meta_monitor_transform_transform_point (transform,
                                          &width, &height,
                                          &hot_x, &hot_y);
  *hotspot = GRAPHENE_POINT_INIT (hot_x * scale, hot_y * scale);
}

static gboolean
load_cursor_sprite_gbm_buffer_for_crtc (MetaCursorRendererNative *native,
                                        MetaCrtcKms              *crtc_kms,
                                        MetaCursorSprite         *cursor_sprite,
                                        uint8_t                  *pixels,
                                        uint                      width,
                                        uint                      height,
                                        int                       rowstride,
                                        float                     scale,
                                        MetaMonitorTransform      transform,
                                        uint32_t                  gbm_format)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (priv->backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (backend_native);
  MetaGpu *gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  MetaKmsCrtc *kms_crtc;
  uint64_t cursor_width, cursor_height;
  g_autoptr (MetaDrmBuffer) buffer = NULL;
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  g_autoptr (MetaDeviceFile) device_file = NULL;
  g_autoptr (GError) error = NULL;
  graphene_point_t hotspot;

  cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data)
    return FALSE;

  cursor_width = (uint64_t) cursor_renderer_gpu_data->cursor_width;
  cursor_height = (uint64_t) cursor_renderer_gpu_data->cursor_height;

  if (width > cursor_width || height > cursor_height)
    {
      meta_warning ("Invalid theme cursor size (must be at most %ux%u)",
                    (unsigned int)cursor_width, (unsigned int)cursor_height);
      return FALSE;
    }

  device_file = meta_device_pool_open (device_pool,
                                       meta_gpu_kms_get_file_path (gpu_kms),
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       &error);
  if (!device_file)
    {
      g_warning ("Failed to open '%s' for updating the cursor: %s",
                 meta_gpu_kms_get_file_path (gpu_kms),
                 error->message);
      disable_hw_cursor_for_gpu (gpu_kms, error);
      return FALSE;
    }

  buffer = create_cursor_drm_buffer (gpu_kms, device_file,
                                     pixels,
                                     width, height, rowstride,
                                     cursor_width,
                                     cursor_height,
                                     gbm_format,
                                     &error);
  if (!buffer)
    {
      g_warning ("Realizing HW cursor failed: %s", error->message);
      disable_hw_cursor_for_gpu (gpu_kms, error);
      return FALSE;
    }

  calculate_crtc_cursor_hotspot (cursor_sprite, scale, transform, &hotspot);

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  meta_kms_cursor_manager_update_sprite (kms_cursor_manager,
                                         kms_crtc,
                                         buffer,
                                         transform,
                                         &hotspot);
  return TRUE;
}

static cairo_surface_t *
scale_and_transform_cursor_sprite_cpu (uint8_t              *pixels,
                                       cairo_format_t        pixel_format,
                                       int                   width,
                                       int                   height,
                                       int                   rowstride,
                                       float                 scale,
                                       MetaMonitorTransform  transform)
{
  cairo_t *cr;
  cairo_surface_t *source_surface;
  cairo_surface_t *target_surface;
  int image_width;
  int image_height;

  image_width = ceilf (width * scale);
  image_height = ceilf (height * scale);

  target_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                               image_width,
                                               image_height);

  cr = cairo_create (target_surface);
  if (transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      cairo_translate (cr, 0.5 * image_width, 0.5 * image_height);
      switch (transform)
        {
        case META_MONITOR_TRANSFORM_90:
          cairo_rotate (cr, M_PI * 1.5);
          break;
        case META_MONITOR_TRANSFORM_180:
          cairo_rotate (cr, M_PI);
          break;
        case META_MONITOR_TRANSFORM_270:
          cairo_rotate (cr, M_PI * 0.5);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED:
          cairo_scale (cr, -1, 1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          cairo_scale (cr, -1, 1);
          cairo_rotate (cr, M_PI * 0.5);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          cairo_scale (cr, -1, 1);
          cairo_rotate (cr, M_PI);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          cairo_scale (cr, -1, 1);
          cairo_rotate (cr, M_PI * 1.5);
          break;
        case META_MONITOR_TRANSFORM_NORMAL:
          g_assert_not_reached ();
        }
      cairo_translate (cr, -0.5 * image_width, -0.5 * image_height);
    }
  cairo_scale (cr, scale, scale);

  source_surface = cairo_image_surface_create_for_data (pixels,
                                                        pixel_format,
                                                        width,
                                                        height,
                                                        rowstride);

  cairo_set_source_surface (cr, source_surface, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (source_surface);

  return target_surface;
}

static cairo_format_t
gbm_format_to_cairo_format (uint32_t gbm_format)
{
  switch (gbm_format)
    {
    case GBM_FORMAT_XRGB8888:
      return CAIRO_FORMAT_RGB24;
    default:
      g_warn_if_reached ();
      G_GNUC_FALLTHROUGH;
    case GBM_FORMAT_ARGB8888:
      return CAIRO_FORMAT_ARGB32;
    }
}

static gboolean
load_scaled_and_transformed_cursor_sprite (MetaCursorRendererNative *native,
                                           MetaCrtcKms              *crtc_kms,
                                           MetaCursorSprite         *cursor_sprite,
                                           float                     relative_scale,
                                           MetaMonitorTransform      relative_transform,
                                           uint8_t                  *data,
                                           int                       width,
                                           int                       height,
                                           int                       rowstride,
                                           uint32_t                  gbm_format)
{
  gboolean retval = FALSE;

  if (!G_APPROX_VALUE (relative_scale, 1.f, FLT_EPSILON) ||
      relative_transform != META_MONITOR_TRANSFORM_NORMAL ||
      gbm_format != GBM_FORMAT_ARGB8888)
    {
      cairo_surface_t *surface;
      cairo_format_t cairo_format;

      cairo_format = gbm_format_to_cairo_format (gbm_format);
      surface = scale_and_transform_cursor_sprite_cpu (data,
                                                       cairo_format,
                                                       width,
                                                       height,
                                                       rowstride,
                                                       relative_scale,
                                                       relative_transform);

      retval =
        load_cursor_sprite_gbm_buffer_for_crtc (native,
                                                crtc_kms,
                                                cursor_sprite,
                                                cairo_image_surface_get_data (surface),
                                                cairo_image_surface_get_width (surface),
                                                cairo_image_surface_get_width (surface),
                                                cairo_image_surface_get_stride (surface),
                                                relative_scale,
                                                relative_transform,
                                                GBM_FORMAT_ARGB8888);

      cairo_surface_destroy (surface);
    }
  else
    {
      retval = load_cursor_sprite_gbm_buffer_for_crtc (native,
                                                       crtc_kms,
                                                       cursor_sprite,
                                                       data,
                                                       width,
                                                       height,
                                                       rowstride,
                                                       relative_scale,
                                                       relative_transform,
                                                       gbm_format);
    }

  return retval;
}

#ifdef HAVE_WAYLAND
static gboolean
realize_cursor_sprite_from_wl_buffer_for_crtc (MetaCursorRenderer      *renderer,
                                               MetaCrtcKms             *crtc_kms,
                                               MetaCursorSpriteWayland *sprite_wayland)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_wayland);
  MetaGpu *gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  uint64_t cursor_width, cursor_height;
  CoglTexture *texture;
  uint width, height;
  MetaWaylandBuffer *buffer;
  struct wl_resource *buffer_resource;
  struct wl_shm_buffer *shm_buffer;

  cursor_renderer_gpu_data =
    meta_cursor_renderer_native_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data || cursor_renderer_gpu_data->hw_cursor_broken)
    return FALSE;

  buffer = meta_cursor_sprite_wayland_get_buffer (sprite_wayland);
  if (!buffer)
    return FALSE;

  buffer_resource = meta_wayland_buffer_get_resource (buffer);
  if (!buffer_resource)
    return FALSE;

  shm_buffer = wl_shm_buffer_get (buffer_resource);
  if (shm_buffer)
    {
      int rowstride = wl_shm_buffer_get_stride (shm_buffer);
      uint8_t *buffer_data;
      float relative_scale;
      MetaMonitorTransform relative_transform;
      uint32_t gbm_format;

      MetaCrtc *crtc = META_CRTC (crtc_kms);
      MetaLogicalMonitor *logical_monitor;
      MetaMonitor *monitor;
      MetaMonitorTransform logical_transform;
      gboolean retval;

      monitor = meta_output_get_monitor (meta_crtc_get_outputs (crtc)->data);
      logical_monitor = meta_monitor_get_logical_monitor (monitor);

      relative_scale = calculate_cursor_crtc_sprite_scale (priv->backend,
                                                           cursor_sprite,
                                                           logical_monitor);

      logical_transform = meta_logical_monitor_get_transform (logical_monitor);
      relative_transform = meta_monitor_transform_transform (
          meta_monitor_transform_invert (
            meta_cursor_sprite_get_texture_transform (cursor_sprite)),
          meta_monitor_logical_to_crtc_transform (monitor, logical_transform));

      wl_shm_buffer_begin_access (shm_buffer);
      buffer_data = wl_shm_buffer_get_data (shm_buffer);

      width = wl_shm_buffer_get_width (shm_buffer);
      height = wl_shm_buffer_get_height (shm_buffer);

      switch (wl_shm_buffer_get_format (shm_buffer))
        {
        case WL_SHM_FORMAT_ARGB8888:
          gbm_format = GBM_FORMAT_ARGB8888;
          break;
        case WL_SHM_FORMAT_XRGB8888:
          gbm_format = GBM_FORMAT_XRGB8888;
          break;
        default:
          g_warn_if_reached ();
          gbm_format = GBM_FORMAT_ARGB8888;
        }

      retval = load_scaled_and_transformed_cursor_sprite (native,
                                                          crtc_kms,
                                                          cursor_sprite,
                                                          relative_scale,
                                                          relative_transform,
                                                          buffer_data,
                                                          width,
                                                          height,
                                                          rowstride,
                                                          gbm_format);

      wl_shm_buffer_end_access (shm_buffer);

      return retval;
    }
  else
    {
      MetaBackendNative *backend_native = META_BACKEND_NATIVE (priv->backend);
      MetaDevicePool *device_pool =
        meta_backend_native_get_device_pool (backend_native);
      MetaKms *kms = meta_backend_native_get_kms (backend_native);
      MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
      MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
      int hot_x, hot_y;
      g_autoptr (MetaDeviceFile) device_file = NULL;
      struct gbm_device *gbm_device;
      struct gbm_bo *bo;
      g_autoptr (GError) error = NULL;
      MetaDrmBufferFlags flags;
      MetaDrmBufferGbm *buffer_gbm;

      device_file = meta_device_pool_open (device_pool,
                                           meta_gpu_kms_get_file_path (gpu_kms),
                                           META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                           &error);
      if (!device_file)
        {
          g_warning ("Failed to open '%s' for updating the cursor: %s",
                     meta_gpu_kms_get_file_path (gpu_kms),
                     error->message);
          return FALSE;
        }

      /* HW cursors have a predefined size (at least 64x64), which usually is
       * bigger than cursor theme size, so themed cursors must be padded with
       * transparent pixels to fill the overlay. This is trivial if we have CPU
       * access to the data, but it's not possible if the buffer is in GPU
       * memory (and possibly tiled too), so if we don't get the right size, we
       * fallback to GL. */
      cursor_width = (uint64_t) cursor_renderer_gpu_data->cursor_width;
      cursor_height = (uint64_t) cursor_renderer_gpu_data->cursor_height;

      texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
      width = cogl_texture_get_width (texture);
      height = cogl_texture_get_height (texture);

      if (width != cursor_width || height != cursor_height)
        {
          meta_warning ("Invalid cursor size (must be 64x64), falling back to software (GL) cursors");
          return FALSE;
        }

      gbm_device = meta_gbm_device_from_gpu (gpu_kms);
      if (!gbm_device)
        return FALSE;

      bo = gbm_bo_import (gbm_device,
                          GBM_BO_IMPORT_WL_BUFFER,
                          buffer,
                          GBM_BO_USE_CURSOR);
      if (!bo)
        {
          meta_warning ("Importing HW cursor from wl_buffer failed");
          return FALSE;
        }

      flags = META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;
      buffer_gbm = meta_drm_buffer_gbm_new_take (device_file, bo, flags,
                                                 &error);
      if (!buffer_gbm)
        {
          meta_warning ("Failed to create DRM buffer wrapper: %s",
                        error->message);
          gbm_bo_destroy (bo);
          return FALSE;
        }

      meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
      kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
      meta_kms_cursor_manager_update_sprite (kms_cursor_manager,
                                             kms_crtc,
                                             META_DRM_BUFFER (buffer_gbm),
                                             META_MONITOR_TRANSFORM_NORMAL,
                                             &GRAPHENE_POINT_INIT (hot_x, hot_y));

      return TRUE;
    }
}
#endif /* HAVE_WAYLAND */

static gboolean
realize_cursor_sprite_from_xcursor_for_crtc (MetaCursorRenderer      *renderer,
                                             MetaCrtcKms             *crtc_kms,
                                             MetaCursorSpriteXcursor *sprite_xcursor)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_xcursor);
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  MetaLogicalMonitor *logical_monitor;
  MetaMonitor *monitor;
  MetaMonitorTransform logical_transform;
  XcursorImage *xc_image;
  float relative_scale;
  MetaMonitorTransform relative_transform;

  monitor = meta_output_get_monitor (meta_crtc_get_outputs (crtc)->data);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);

  relative_scale = calculate_cursor_crtc_sprite_scale (priv->backend,
                                                       cursor_sprite,
                                                       logical_monitor);

  logical_transform = meta_logical_monitor_get_transform (logical_monitor);
  relative_transform = meta_monitor_transform_transform (
      meta_monitor_transform_invert (
        meta_cursor_sprite_get_texture_transform (cursor_sprite)),
      meta_monitor_logical_to_crtc_transform (monitor, logical_transform));

  xc_image = meta_cursor_sprite_xcursor_get_current_image (sprite_xcursor);

  return load_scaled_and_transformed_cursor_sprite (native,
                                                    crtc_kms,
                                                    cursor_sprite,
                                                    relative_scale,
                                                    relative_transform,
                                                    (uint8_t *) xc_image->pixels,
                                                    xc_image->width,
                                                    xc_image->height,
                                                    xc_image->width * 4,
                                                    GBM_FORMAT_ARGB8888);
}

static gboolean
realize_cursor_sprite_for_crtc (MetaCursorRenderer *renderer,
                                MetaCrtcKms        *crtc_kms,
                                MetaCursorSprite   *cursor_sprite)
{
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);

  meta_topic (META_DEBUG_KMS,
              "Realizing HW cursor for cursor sprite for CRTC %u (%s)",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device));

  COGL_TRACE_BEGIN_SCOPED (CursorRendererNativeRealize,
                           "Cursor Renderer Native (realize for crtc)");
  if (META_IS_CURSOR_SPRITE_XCURSOR (cursor_sprite))
    {
      MetaCursorSpriteXcursor *sprite_xcursor =
        META_CURSOR_SPRITE_XCURSOR (cursor_sprite);

      return realize_cursor_sprite_from_xcursor_for_crtc (renderer,
                                                          crtc_kms,
                                                          sprite_xcursor);
    }
#ifdef HAVE_WAYLAND
  else if (META_IS_CURSOR_SPRITE_WAYLAND (cursor_sprite))
    {
      MetaCursorSpriteWayland *sprite_wayland =
        META_CURSOR_SPRITE_WAYLAND (cursor_sprite);

      return realize_cursor_sprite_from_wl_buffer_for_crtc (renderer,
                                                            crtc_kms,
                                                            sprite_wayland);
    }
#endif
  else
    {
      return FALSE;
    }
}

static void
meta_cursor_renderer_native_class_init (MetaCursorRendererNativeClass *klass)
{
  MetaCursorRendererClass *renderer_class = META_CURSOR_RENDERER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_renderer_native_finalize;
  renderer_class->update_cursor = meta_cursor_renderer_native_update_cursor;

  quark_cursor_sprite = g_quark_from_static_string ("-meta-cursor-native");
  quark_cursor_renderer_native_gpu_data =
    g_quark_from_static_string ("-meta-cursor-renderer-native-gpu-data");
  quark_cursor_stage_view =
    g_quark_from_static_string ("-meta-cursor-stage-view-native");
}

static void
on_monitors_changed (MetaMonitorManager       *monitors,
                     MetaCursorRendererNative *native)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);

  meta_cursor_renderer_force_update (renderer);
}

static void
init_hw_cursor_support_for_gpu (MetaGpuKms *gpu_kms)
{
  MetaKmsDevice *kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  MetaCursorRendererNativeGpuData *cursor_renderer_gpu_data;
  uint64_t width, height;

  cursor_renderer_gpu_data =
    meta_create_cursor_renderer_native_gpu_data (gpu_kms);

  if (!meta_kms_device_get_cursor_size (kms_device, &width, &height))
    {
      width = 64;
      height = 64;
    }

  cursor_renderer_gpu_data->cursor_width = width;
  cursor_renderer_gpu_data->cursor_height = height;
}

static void
on_gpu_added_for_cursor (MetaBackend *backend,
                         MetaGpuKms  *gpu_kms)
{
  init_hw_cursor_support_for_gpu (gpu_kms);
}

typedef struct _CursorKmsImplState
{
  graphene_point_t sprite_hotspot;
  graphene_rect_t sprite_rect;
} CursorKmsImplState;

static void
on_pointer_position_changed_in_input_impl (MetaSeatImpl           *seat_impl,
                                           const graphene_point_t *position,
                                           MetaBackend            *backend)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);

  meta_kms_cursor_manager_position_changed_in_input_impl (kms_cursor_manager,
                                                          position);
}

static gboolean
connect_seat_signals_in_input_impl (gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (g_task_get_task_data (task));
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaBackend *backend = priv->backend;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
  ClutterInputDevice *device;
  graphene_point_t position;

  priv->pointer_position_changed_in_impl_handler_id =
    g_signal_connect (seat_impl, "pointer-position-changed-in-impl",
                      G_CALLBACK (on_pointer_position_changed_in_input_impl),
                      backend);


  device = meta_seat_impl_get_pointer (seat_impl);
  meta_seat_impl_query_state (seat_impl, device, NULL, &position, NULL);
  meta_kms_cursor_manager_position_changed_in_input_impl (kms_cursor_manager,
                                                          &position);

  g_task_return_boolean (task, TRUE);

  return G_SOURCE_REMOVE;
}

static gboolean
disconnect_seat_signals_in_input_impl (gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (g_task_get_task_data (task));
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaSeatImpl *seat_impl = g_task_get_source_object (task);

  g_clear_signal_handler (&priv->pointer_position_changed_in_impl_handler_id,
                          seat_impl);
  g_task_return_boolean (task, TRUE);

  g_mutex_lock (&priv->input_mutex);
  priv->input_disconnected = TRUE;
  g_cond_signal (&priv->input_cond);
  g_mutex_unlock (&priv->input_mutex);

  return G_SOURCE_REMOVE;
}

static void
query_cursor_position_in_kms_impl (float    *x,
                                   float    *y,
                                   gpointer  user_data)
{
  ClutterSeat *seat = user_data;
  ClutterInputDevice *device;
  graphene_point_t position;

  device = clutter_seat_get_pointer (seat);
  clutter_seat_query_state (seat, device, NULL, &position, NULL);
  *x = position.x;
  *y = position.y;
}

static void
init_hw_cursor_support (MetaCursorRendererNative *cursor_renderer_native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  MetaBackend *backend = priv->backend;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
  ClutterSeat *seat;
  MetaSeatNative *seat_native;
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (priv->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      init_hw_cursor_support_for_gpu (gpu_kms);
    }

  seat = meta_backend_get_default_seat (priv->backend);
  seat_native = META_SEAT_NATIVE (seat);
  meta_seat_native_run_impl_task (seat_native,
                                  connect_seat_signals_in_input_impl,
                                  cursor_renderer_native, NULL);

  meta_kms_cursor_manager_set_query_func (kms_cursor_manager,
                                          query_cursor_position_in_kms_impl,
                                          seat);
}

static void
on_started (MetaContext              *context,
            MetaCursorRendererNative *cursor_renderer_native)
{
  if (g_strcmp0 (getenv ("MUTTER_DEBUG_DISABLE_HW_CURSORS"), "1"))
    init_hw_cursor_support (cursor_renderer_native);
  else
    g_message ("Disabling hardware cursors because MUTTER_DEBUG_DISABLE_HW_CURSORS is set");
}

static void
on_prepare_shutdown (MetaContext              *context,
                     MetaCursorRendererNative *cursor_renderer_native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  ClutterSeat *seat;
  MetaSeatNative *seat_native;

  g_mutex_init (&priv->input_mutex);
  g_cond_init (&priv->input_cond);
  priv->input_disconnected = FALSE;

  seat = meta_backend_get_default_seat (priv->backend);
  seat_native = META_SEAT_NATIVE (seat);
  meta_seat_native_run_impl_task (seat_native,
                                  disconnect_seat_signals_in_input_impl,
                                  cursor_renderer_native, NULL);

  g_mutex_lock (&priv->input_mutex);
  while (!priv->input_disconnected)
    g_cond_wait (&priv->input_cond, &priv->input_mutex);
  g_mutex_unlock (&priv->input_mutex);

  g_mutex_clear (&priv->input_mutex);
  g_cond_clear (&priv->input_cond);
}

MetaCursorRendererNative *
meta_cursor_renderer_native_new (MetaBackend        *backend,
                                 ClutterInputDevice *device)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRendererNative *cursor_renderer_native;
  MetaCursorRendererNativePrivate *priv;
  MetaSeatNative *seat =
    META_SEAT_NATIVE (meta_backend_get_default_seat (backend));

  g_assert (seat);

  cursor_renderer_native = g_object_new (META_TYPE_CURSOR_RENDERER_NATIVE,
                                         "backend", backend,
                                         "device", device,
                                         NULL);
  priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           cursor_renderer_native, 0);
  g_signal_connect (backend, "gpu-added",
                    G_CALLBACK (on_gpu_added_for_cursor), NULL);
  g_signal_connect (meta_backend_get_context (backend),
                    "started",
                    G_CALLBACK (on_started),
                    cursor_renderer_native);
  g_signal_connect (meta_backend_get_context (backend),
                    "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown),
                    cursor_renderer_native);

  priv->backend = backend;

  return cursor_renderer_native;
}

static void
meta_cursor_renderer_native_init (MetaCursorRendererNative *native)
{
}
