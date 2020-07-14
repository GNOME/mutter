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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/native/meta-kms-cursor-renderer.h"

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
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-renderer-native.h"
#include "core/boxes-private.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-buffer.h"
#endif

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif
#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

/* When animating a cursor, we usually call drmModeSetCursor2 once per frame.
 * Though, testing shows that we need to triple buffer the cursor buffer in
 * order to avoid glitches when animating the cursor, at least when running on
 * Intel. The reason for this might be (but is not confirmed to be) due to
 * the user space gbm_bo cache, making us reuse and overwrite the kernel side
 * buffer content before it was scanned out. To avoid this, we keep a user space
 * reference to each buffer we set until at least one frame after it was drawn.
 * In effect, this means we three active cursor gbm_bo's: one that that just has
 * been set, one that was previously set and may or may not have been scanned
 * out, and one pending that will be replaced if the cursor sprite changes.
 */
#define HW_CURSOR_BUFFER_COUNT 3

static GQuark quark_cursor_sprite = 0;

typedef struct _MetaKmsCursorRendererPrivate MetaKmsCursorRendererPrivate;
typedef struct _MetaKmsCursorRendererGpuData MetaKmsCursorRendererGpuData;
typedef struct _MetaKmsCursorGpuState MetaKmsCursorGpuState;
typedef struct _MetaKmsCursorPrivate MetaKmsCursorPrivate;

struct _MetaKmsCursorRenderer
{
  MetaCursorRenderer parent;
};

struct _MetaKmsCursorRendererPrivate
{
  MetaBackend *backend;

  gboolean hw_state_invalidated;
  gboolean has_hw_cursor;

  MetaCursorRenderer *cursor_renderer;
  MetaCursorSprite *last_cursor;
  guint animation_timeout_id;
};

struct _MetaKmsCursorRendererGpuData
{
  gboolean hw_cursor_broken;

  uint64_t cursor_width;
  uint64_t cursor_height;
};

typedef enum
{
  META_CURSOR_GBM_BO_STATE_NONE,
  META_CURSOR_GBM_BO_STATE_SET,
  META_CURSOR_GBM_BO_STATE_INVALIDATED,
} MetaCursorGbmBoState;

struct _MetaKmsCursorGpuState
{
  MetaGpu *gpu;
  guint active_bo;
  MetaCursorGbmBoState pending_bo_state;
  struct gbm_bo *bos[HW_CURSOR_BUFFER_COUNT];
};

struct _MetaKmsCursorPrivate
{
  GHashTable *gpu_states;

  struct {
    gboolean can_preprocess;
    float current_relative_scale;
    MetaMonitorTransform current_relative_transform;
  } preprocess_state;
};

static GQuark quark_kms_cursor_renderer_gpu_data = 0;

G_DEFINE_TYPE_WITH_PRIVATE (MetaKmsCursorRenderer, meta_kms_cursor_renderer, G_TYPE_OBJECT);

static void
realize_cursor_sprite (MetaKmsCursorRenderer *kms_cursor_renderer,
                       MetaCursorSprite      *cursor_sprite,
                       GList                 *gpus);

static MetaKmsCursorGpuState *
get_cursor_gpu_state (MetaKmsCursorPrivate *cursor_priv,
                      MetaGpuKms           *gpu_kms);

static MetaKmsCursorGpuState *
ensure_cursor_gpu_state (MetaKmsCursorPrivate *cursor_priv,
                         MetaGpuKms           *gpu_kms);

static void
invalidate_cursor_gpu_state (MetaCursorSprite *cursor_sprite);

static MetaKmsCursorPrivate *
ensure_cursor_priv (MetaCursorSprite *cursor_sprite);

static MetaKmsCursorPrivate *
get_cursor_priv (MetaCursorSprite *cursor_sprite);

static MetaKmsCursorRendererGpuData *
meta_kms_cursor_renderer_gpu_data_from_gpu (MetaGpuKms *gpu_kms)
{
  return g_object_get_qdata (G_OBJECT (gpu_kms),
                             quark_kms_cursor_renderer_gpu_data);
}

static MetaKmsCursorRendererGpuData *
meta_create_kms_cursor_renderer_gpu_data (MetaGpuKms *gpu_kms)
{
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;

  cursor_renderer_gpu_data = g_new0 (MetaKmsCursorRendererGpuData, 1);
  g_object_set_qdata_full (G_OBJECT (gpu_kms),
                           quark_kms_cursor_renderer_gpu_data,
                           cursor_renderer_gpu_data,
                           g_free);

  return cursor_renderer_gpu_data;
}

static void
meta_kms_cursor_renderer_finalize (GObject *object)
{
  MetaKmsCursorRenderer *renderer = META_KMS_CURSOR_RENDERER (object);
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (renderer);

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  G_OBJECT_CLASS (meta_kms_cursor_renderer_parent_class)->finalize (object);
}

static guint
get_pending_cursor_sprite_gbm_bo_index (MetaKmsCursorGpuState *cursor_gpu_state)
{
  return (cursor_gpu_state->active_bo + 1) % HW_CURSOR_BUFFER_COUNT;
}

static struct gbm_bo *
get_pending_cursor_sprite_gbm_bo (MetaKmsCursorGpuState *cursor_gpu_state)
{
  guint pending_bo;

  pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
  return cursor_gpu_state->bos[pending_bo];
}

static struct gbm_bo *
get_active_cursor_sprite_gbm_bo (MetaKmsCursorGpuState *cursor_gpu_state)
{
  return cursor_gpu_state->bos[cursor_gpu_state->active_bo];
}

static void
set_pending_cursor_sprite_gbm_bo (MetaCursorSprite *cursor_sprite,
                                  MetaGpuKms       *gpu_kms,
                                  struct gbm_bo    *bo)
{
  MetaKmsCursorPrivate *cursor_priv;
  MetaKmsCursorGpuState *cursor_gpu_state;
  guint pending_bo;

  cursor_priv = ensure_cursor_priv (cursor_sprite);
  cursor_gpu_state = ensure_cursor_gpu_state (cursor_priv, gpu_kms);

  pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
  cursor_gpu_state->bos[pending_bo] = bo;
  cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_SET;
}

static void
calculate_crtc_cursor_hotspot (MetaCursorSprite *cursor_sprite,
                               int              *cursor_hotspot_x,
                               int              *cursor_hotspot_y)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  int hot_x, hot_y;
  int width, height;
  float scale;
  MetaMonitorTransform transform;

  scale = cursor_priv->preprocess_state.current_relative_scale;
  transform = cursor_priv->preprocess_state.current_relative_transform;

  meta_cursor_sprite_get_hotspot (cursor_sprite, &hot_x, &hot_y);
  width = meta_cursor_sprite_get_width (cursor_sprite);
  height = meta_cursor_sprite_get_height (cursor_sprite);
  meta_monitor_transform_transform_point (transform,
                                          width, height,
                                          hot_x, hot_y,
                                          &hot_x, &hot_y);
  *cursor_hotspot_x = (int) roundf (hot_x * scale);
  *cursor_hotspot_y = (int) roundf (hot_y * scale);
}

static void
set_crtc_cursor (MetaKmsCursorRenderer *kms_renderer,
                 MetaKmsUpdate         *kms_update,
                 MetaCrtcKms           *crtc_kms,
                 int                    x,
                 int                    y,
                 MetaCursorSprite      *cursor_sprite)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_renderer);
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  MetaGpuKms *gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data =
    meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);
  MetaKmsCursorGpuState *cursor_gpu_state =
    get_cursor_gpu_state (cursor_priv, gpu_kms);
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *cursor_plane;
  struct gbm_bo *bo;
  union gbm_bo_handle handle;
  int cursor_width, cursor_height;
  MetaFixed16Rectangle src_rect;
  MetaFixed16Rectangle dst_rect;
  struct gbm_bo *crtc_bo;
  MetaKmsAssignPlaneFlag flags;
  int cursor_hotspot_x;
  int cursor_hotspot_y;
  MetaKmsPlaneAssignment *plane_assignment;

  if (cursor_gpu_state->pending_bo_state == META_CURSOR_GBM_BO_STATE_SET)
    bo = get_pending_cursor_sprite_gbm_bo (cursor_gpu_state);
  else
    bo = get_active_cursor_sprite_gbm_bo (cursor_gpu_state);

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  cursor_plane = meta_kms_device_get_cursor_plane_for (kms_device, kms_crtc);
  g_return_if_fail (cursor_plane);

  handle = gbm_bo_get_handle (bo);

  cursor_width = cursor_renderer_gpu_data->cursor_width;
  cursor_height = cursor_renderer_gpu_data->cursor_height;
  src_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (0),
    .y = meta_fixed_16_from_int (0),
    .width = meta_fixed_16_from_int (cursor_width),
    .height = meta_fixed_16_from_int (cursor_height),
  };
  dst_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (x),
    .y = meta_fixed_16_from_int (y),
    .width = meta_fixed_16_from_int (cursor_width),
    .height = meta_fixed_16_from_int (cursor_height),
  };

  flags = META_KMS_ASSIGN_PLANE_FLAG_NONE;
  crtc_bo = meta_crtc_kms_get_cursor_renderer_private (crtc_kms);
  if (!priv->hw_state_invalidated && bo == crtc_bo)
    flags |= META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED;

  plane_assignment = meta_kms_update_assign_plane (kms_update,
                                                   kms_crtc,
                                                   cursor_plane,
                                                   handle.u32,
                                                   src_rect,
                                                   dst_rect,
                                                   flags);

  calculate_crtc_cursor_hotspot (cursor_sprite,
                                 &cursor_hotspot_x,
                                 &cursor_hotspot_y);
  meta_kms_plane_assignment_set_cursor_hotspot (plane_assignment,
                                                cursor_hotspot_x,
                                                cursor_hotspot_y);

  meta_crtc_kms_set_cursor_renderer_private (crtc_kms, bo);

  if (cursor_gpu_state->pending_bo_state == META_CURSOR_GBM_BO_STATE_SET)
    {
      cursor_gpu_state->active_bo =
        (cursor_gpu_state->active_bo + 1) % HW_CURSOR_BUFFER_COUNT;
      cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_NONE;
    }
}

static void
unset_crtc_cursor (MetaKmsCursorRenderer *kms_renderer,
                   MetaKmsUpdate         *kms_update,
                   MetaCrtcKms           *crtc_kms)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_renderer);
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *cursor_plane;
  struct gbm_bo *crtc_bo;

  crtc_bo = meta_crtc_kms_get_cursor_renderer_private (crtc_kms);
  if (!priv->hw_state_invalidated && !crtc_bo)
    return;

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  cursor_plane = meta_kms_device_get_cursor_plane_for (kms_device, kms_crtc);

  if (cursor_plane)
    meta_kms_update_unassign_plane (kms_update, kms_crtc, cursor_plane);

  meta_crtc_kms_set_cursor_renderer_private (crtc_kms, NULL);
}

static float
calculate_cursor_crtc_sprite_scale (MetaCursorSprite   *cursor_sprite,
                                    MetaLogicalMonitor *logical_monitor)
{
  if (meta_is_stage_views_scaled ())
    {
      return (meta_logical_monitor_get_scale (logical_monitor) *
              meta_cursor_sprite_get_texture_scale (cursor_sprite));
    }
  else
    {
      return 1.0;
    }
}

typedef struct
{
  MetaKmsCursorRenderer *in_kms_cursor_renderer;
  MetaLogicalMonitor *in_logical_monitor;
  graphene_rect_t in_local_cursor_rect;
  MetaCursorSprite *in_cursor_sprite;
  MetaKmsUpdate *in_kms_update;

  gboolean out_painted;
} UpdateCrtcCursorData;

static gboolean
update_monitor_crtc_cursor (MetaMonitor         *monitor,
                            MetaMonitorMode     *monitor_mode,
                            MetaMonitorCrtcMode *monitor_crtc_mode,
                            gpointer             user_data,
                            GError             **error)
{
  UpdateCrtcCursorData *data = user_data;
  MetaKmsCursorRenderer *kms_cursor_renderer =
    data->in_kms_cursor_renderer;
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  MetaCrtc *crtc;
  MetaMonitorTransform transform;
  const MetaCrtcModeInfo *crtc_mode_info;
  graphene_rect_t scaled_crtc_rect;
  float scale;
  int crtc_x, crtc_y;
  int crtc_width, crtc_height;

  if (meta_is_stage_views_scaled ())
    scale = meta_logical_monitor_get_scale (data->in_logical_monitor);
  else
    scale = 1.0;

  transform = meta_logical_monitor_get_transform (data->in_logical_monitor);
  transform = meta_monitor_logical_to_crtc_transform (monitor, transform);

  meta_monitor_calculate_crtc_pos (monitor, monitor_mode,
                                   monitor_crtc_mode->output,
                                   transform,
                                   &crtc_x, &crtc_y);

  crtc_mode_info = meta_crtc_mode_get_info (monitor_crtc_mode->crtc_mode);

  if (meta_monitor_transform_is_rotated (transform))
    {
      crtc_width = crtc_mode_info->height;
      crtc_height = crtc_mode_info->width;
    }
  else
    {
      crtc_width = crtc_mode_info->width;
      crtc_height = crtc_mode_info->height;
    }

  scaled_crtc_rect = (graphene_rect_t) {
    .origin = {
      .x = crtc_x / scale,
      .y = crtc_y / scale
    },
    .size = {
      .width = crtc_width / scale,
      .height = crtc_height / scale
    },
  };

  crtc = meta_output_get_assigned_crtc (monitor_crtc_mode->output);

  if (priv->has_hw_cursor &&
      graphene_rect_intersection (&scaled_crtc_rect,
                                  &data->in_local_cursor_rect,
                                  NULL))
    {
      MetaMonitorTransform inverted_transform;
      MetaRectangle cursor_rect;
      CoglTexture *texture;
      float crtc_cursor_x, crtc_cursor_y;
      float cursor_crtc_scale;
      int tex_width, tex_height;

      crtc_cursor_x = (data->in_local_cursor_rect.origin.x -
                       scaled_crtc_rect.origin.x) * scale;
      crtc_cursor_y = (data->in_local_cursor_rect.origin.y -
                       scaled_crtc_rect.origin.y) * scale;

      texture = meta_cursor_sprite_get_cogl_texture (data->in_cursor_sprite);
      tex_width = cogl_texture_get_width (texture);
      tex_height = cogl_texture_get_height (texture);

      cursor_crtc_scale =
        calculate_cursor_crtc_sprite_scale (data->in_cursor_sprite,
                                            data->in_logical_monitor);

      cursor_rect = (MetaRectangle) {
        .x = floorf (crtc_cursor_x),
        .y = floorf (crtc_cursor_y),
        .width = roundf (tex_width * cursor_crtc_scale),
        .height = roundf (tex_height * cursor_crtc_scale)
      };

      inverted_transform = meta_monitor_transform_invert (transform);
      meta_rectangle_transform (&cursor_rect,
                                inverted_transform,
                                crtc_mode_info->width,
                                crtc_mode_info->height,
                                &cursor_rect);

      set_crtc_cursor (data->in_kms_cursor_renderer,
                       data->in_kms_update,
                       META_CRTC_KMS (crtc),
                       cursor_rect.x,
                       cursor_rect.y,
                       data->in_cursor_sprite);

      data->out_painted = data->out_painted || TRUE;
    }
  else
    {
      unset_crtc_cursor (data->in_kms_cursor_renderer,
                         data->in_kms_update,
                         META_CRTC_KMS (crtc));
    }

  return TRUE;
}

static void
disable_hw_cursor_for_crtc (MetaKmsCrtc  *kms_crtc,
                            const GError *error)
{
  MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (kms_crtc);
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  MetaGpuKms *gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data =
    meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);

  g_warning ("Failed to set hardware cursor (%s), "
             "using OpenGL from now on",
             error->message);
  cursor_renderer_gpu_data->hw_cursor_broken = TRUE;
}

static void
update_hw_cursor (MetaKmsCursorRenderer *kms_renderer,
                  MetaCursorSprite      *cursor_sprite)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_renderer);
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (priv->cursor_renderer);
  MetaBackend *backend = priv->backend;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (priv->backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaKmsUpdate *kms_update;
  GList *logical_monitors;
  GList *l;
  graphene_rect_t rect;
  gboolean painted = FALSE;
  g_autoptr (MetaKmsFeedback) feedback = NULL;

  kms_update = meta_kms_ensure_pending_update (kms);

  if (cursor_sprite)
    rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);
  else
    rect = GRAPHENE_RECT_INIT_ZERO;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      UpdateCrtcCursorData data;
      GList *monitors;
      GList *k;

      data = (UpdateCrtcCursorData) {
        .in_kms_cursor_renderer = kms_renderer,
        .in_logical_monitor = logical_monitor,
        .in_local_cursor_rect = (graphene_rect_t) {
          .origin = {
            .x = rect.origin.x - logical_monitor->rect.x,
            .y = rect.origin.y - logical_monitor->rect.y
          },
          .size = rect.size
        },
        .in_cursor_sprite = cursor_sprite,
        .in_kms_update = kms_update,
      };

      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (k = monitors; k; k = k->next)
        {
          MetaMonitor *monitor = k->data;
          MetaMonitorMode *monitor_mode;

          monitor_mode = meta_monitor_get_current_mode (monitor);
          meta_monitor_mode_foreach_crtc (monitor, monitor_mode,
                                          update_monitor_crtc_cursor,
                                          &data,
                                          NULL);
        }

      painted = painted || data.out_painted;
    }

  feedback = meta_kms_post_pending_update_sync (kms);
  if (meta_kms_feedback_get_result (feedback) != META_KMS_FEEDBACK_PASSED)
    {
      for (l = meta_kms_feedback_get_failed_planes (feedback); l; l = l->next)
        {
          MetaKmsPlaneFeedback *plane_feedback = l->data;

          if (!g_error_matches (plane_feedback->error,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            {
              disable_hw_cursor_for_crtc (plane_feedback->crtc,
                                          plane_feedback->error);
            }
        }

      priv->has_hw_cursor = FALSE;
    }

  priv->hw_state_invalidated = FALSE;

  if (painted)
    meta_cursor_renderer_emit_painted (renderer, cursor_sprite);
}

static gboolean
has_valid_cursor_sprite_gbm_bo (MetaCursorSprite *cursor_sprite,
                                MetaGpuKms       *gpu_kms)
{
  MetaKmsCursorPrivate *cursor_priv;
  MetaKmsCursorGpuState *cursor_gpu_state;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (!cursor_priv)
    return FALSE;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (!cursor_gpu_state)
    return FALSE;

  switch (cursor_gpu_state->pending_bo_state)
    {
    case META_CURSOR_GBM_BO_STATE_NONE:
      return get_active_cursor_sprite_gbm_bo (cursor_gpu_state) != NULL;
    case META_CURSOR_GBM_BO_STATE_SET:
      return TRUE;
    case META_CURSOR_GBM_BO_STATE_INVALIDATED:
      return FALSE;
    }

  g_assert_not_reached ();

  return FALSE;
}

static void
set_can_preprocess (MetaCursorSprite     *cursor_sprite,
                    float                 scale,
                    MetaMonitorTransform  transform)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  cursor_priv->preprocess_state.current_relative_scale = scale;
  cursor_priv->preprocess_state.current_relative_transform = transform;
  cursor_priv->preprocess_state.can_preprocess = TRUE;

  invalidate_cursor_gpu_state (cursor_sprite);
}

static void
unset_can_preprocess (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  memset (&cursor_priv->preprocess_state,
          0,
          sizeof (cursor_priv->preprocess_state));
  cursor_priv->preprocess_state.can_preprocess = FALSE;

  invalidate_cursor_gpu_state (cursor_sprite);
}

static gboolean
get_can_preprocess (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.can_preprocess;
}

static float
get_current_relative_scale (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.current_relative_scale;
}

static MetaMonitorTransform
get_current_relative_transform (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);

  return cursor_priv->preprocess_state.current_relative_transform;
}

static void
has_cursor_plane (MetaLogicalMonitor *logical_monitor,
                  MetaMonitor        *monitor,
                  MetaOutput         *output,
                  MetaCrtc           *crtc,
                  gpointer            user_data)
{
  gboolean *has_cursor_planes = user_data;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);

  *has_cursor_planes &= !!meta_kms_device_get_cursor_plane_for (kms_device,
                                                                kms_crtc);
}

static gboolean
crtcs_has_cursor_planes (MetaKmsCursorRenderer *kms_cursor_renderer,
                         MetaCursorSprite      *cursor_sprite)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  MetaCursorRenderer *cursor_renderer = priv->cursor_renderer;
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors;
  GList *l;
  graphene_rect_t cursor_rect;

  cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle logical_monitor_layout;
      graphene_rect_t logical_monitor_rect;
      gboolean has_cursor_planes;

      logical_monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);
      logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor_layout);

      if (!graphene_rect_intersection (&cursor_rect, &logical_monitor_rect,
                                       NULL))
        continue;

      has_cursor_planes = TRUE;
      meta_logical_monitor_foreach_crtc (logical_monitor,
                                         has_cursor_plane,
                                         &has_cursor_planes);
      if (!has_cursor_planes)
        return FALSE;
    }

  return TRUE;
}

static gboolean
get_common_crtc_sprite_scale_for_logical_monitors (MetaKmsCursorRenderer *kms_cursor_renderer,
                                                   MetaCursorSprite      *cursor_sprite,
                                                   float                 *out_scale)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  MetaCursorRenderer *renderer = priv->cursor_renderer;
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  graphene_rect_t cursor_rect;
  float scale = 1.0;
  gboolean has_visible_crtc_sprite = FALSE;
  GList *logical_monitors;
  GList *l;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      graphene_rect_t logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor->rect);
      float tmp_scale;

      if (!graphene_rect_intersection (&cursor_rect,
                                       &logical_monitor_rect,
                                       NULL))
        continue;

      tmp_scale =
        calculate_cursor_crtc_sprite_scale (cursor_sprite, logical_monitor);

      if (has_visible_crtc_sprite && scale != tmp_scale)
        return FALSE;

      has_visible_crtc_sprite = TRUE;
      scale = tmp_scale;
    }

  if (!has_visible_crtc_sprite)
    return FALSE;

  *out_scale = scale;
  return TRUE;
}

static gboolean
get_common_crtc_sprite_transform_for_logical_monitors (MetaKmsCursorRenderer *kms_cursor_renderer,
                                                       MetaCursorSprite      *cursor_sprite,
                                                       MetaMonitorTransform  *out_transform)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  MetaCursorRenderer *renderer = priv->cursor_renderer;
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  graphene_rect_t cursor_rect;
  MetaMonitorTransform transform = META_MONITOR_TRANSFORM_NORMAL;
  gboolean has_visible_crtc_sprite = FALSE;
  GList *logical_monitors;
  GList *l;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      graphene_rect_t logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor->rect);
      MetaMonitorTransform logical_transform, tmp_transform;
      GList *monitors, *l_mon;

      if (!graphene_rect_intersection (&cursor_rect,
                                       &logical_monitor_rect,
                                       NULL))
        continue;

      logical_transform = meta_logical_monitor_get_transform (logical_monitor);
      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (l_mon = monitors; l_mon; l_mon = l_mon->next)
        {
          MetaMonitor *monitor = l_mon->data;

          tmp_transform = meta_monitor_transform_relative_transform (
            meta_cursor_sprite_get_texture_transform (cursor_sprite),
            meta_monitor_logical_to_crtc_transform (monitor, logical_transform));

          if (has_visible_crtc_sprite && transform != tmp_transform)
            return FALSE;

          has_visible_crtc_sprite = TRUE;
          transform = tmp_transform;
        }
    }

  if (!has_visible_crtc_sprite)
    return FALSE;

  *out_transform = transform;
  return TRUE;
}

static gboolean
should_have_hw_cursor (MetaKmsCursorRenderer *kms_cursor_renderer,
                       MetaCursorSprite      *cursor_sprite,
                       GList                 *gpus)
{
  MetaMonitorTransform transform;
  float scale;
  GList *l;

  if (!cursor_sprite)
    return FALSE;

  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;
      MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;

      cursor_renderer_gpu_data =
        meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);
      if (!cursor_renderer_gpu_data)
        return FALSE;

      if (cursor_renderer_gpu_data->hw_cursor_broken)
        return FALSE;

      if (!has_valid_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms))
        return FALSE;
    }

  if (!crtcs_has_cursor_planes (kms_cursor_renderer, cursor_sprite))
    return FALSE;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (kms_cursor_renderer,
                                                          cursor_sprite,
                                                          &scale))
    return FALSE;

  if (!get_common_crtc_sprite_transform_for_logical_monitors (kms_cursor_renderer,
                                                              cursor_sprite,
                                                              &transform))
    return FALSE;

  if (G_APPROX_VALUE (scale, 1.f, FLT_EPSILON) &&
      transform == META_MONITOR_TRANSFORM_NORMAL)
    return TRUE;
  else
    return get_can_preprocess (cursor_sprite);

  return TRUE;
}

static GList *
calculate_cursor_sprite_gpus (MetaKmsCursorRenderer *kms_cursor_renderer,
                              MetaCursorSprite      *cursor_sprite)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  MetaCursorRenderer *renderer = priv->cursor_renderer;
  MetaBackend *backend = priv->backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *gpus = NULL;
  GList *logical_monitors;
  GList *l;
  graphene_rect_t cursor_rect;

  cursor_rect = meta_cursor_renderer_calculate_rect (renderer, cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle logical_monitor_layout;
      graphene_rect_t logical_monitor_rect;
      GList *monitors, *l_mon;

      logical_monitor_layout =
        meta_logical_monitor_get_layout (logical_monitor);
      logical_monitor_rect =
        meta_rectangle_to_graphene_rect (&logical_monitor_layout);

      if (!graphene_rect_intersection (&cursor_rect, &logical_monitor_rect,
                                       NULL))
        continue;

      monitors = meta_logical_monitor_get_monitors (logical_monitor);
      for (l_mon = monitors; l_mon; l_mon = l_mon->next)
        {
          MetaMonitor *monitor = l_mon->data;
          MetaGpu *gpu;

          gpu = meta_monitor_get_gpu (monitor);
          if (!g_list_find (gpus, gpu))
            gpus = g_list_prepend (gpus, gpu);
        }
    }

  return gpus;
}

gboolean
meta_kms_cursor_renderer_update_cursor (MetaKmsCursorRenderer *kms_cursor_renderer,
                                        MetaCursorSprite      *cursor_sprite)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  g_autoptr (GList) gpus = NULL;

  if (cursor_sprite)
    {
      gpus = calculate_cursor_sprite_gpus (kms_cursor_renderer, cursor_sprite);
      realize_cursor_sprite (kms_cursor_renderer, cursor_sprite, gpus);
    }

  priv->has_hw_cursor = should_have_hw_cursor (kms_cursor_renderer,
                                               cursor_sprite, gpus);
  update_hw_cursor (kms_cursor_renderer, cursor_sprite);

  return priv->has_hw_cursor;
}

static void
unset_crtc_cursor_renderer_privates (MetaGpu       *gpu,
                                     struct gbm_bo *bo)
{
  GList *l;

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtcKms *crtc_kms = META_CRTC_KMS (l->data);
      struct gbm_bo *crtc_bo;

      crtc_bo = meta_crtc_kms_get_cursor_renderer_private (crtc_kms);
      if (bo == crtc_bo)
        meta_crtc_kms_set_cursor_renderer_private (crtc_kms, NULL);
    }
}

static void
cursor_gpu_state_free (MetaKmsCursorGpuState *cursor_gpu_state)
{
  int i;
  struct gbm_bo *active_bo;

  active_bo = get_active_cursor_sprite_gbm_bo (cursor_gpu_state);
  if (active_bo)
    unset_crtc_cursor_renderer_privates (cursor_gpu_state->gpu, active_bo);

  for (i = 0; i < HW_CURSOR_BUFFER_COUNT; i++)
    g_clear_pointer (&cursor_gpu_state->bos[i], gbm_bo_destroy);
  g_free (cursor_gpu_state);
}

static MetaKmsCursorGpuState *
get_cursor_gpu_state (MetaKmsCursorPrivate *cursor_priv,
                      MetaGpuKms           *gpu_kms)
{
  return g_hash_table_lookup (cursor_priv->gpu_states, gpu_kms);
}

static MetaKmsCursorGpuState *
ensure_cursor_gpu_state (MetaKmsCursorPrivate *cursor_priv,
                         MetaGpuKms           *gpu_kms)
{
  MetaKmsCursorGpuState *cursor_gpu_state;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (cursor_gpu_state)
    return cursor_gpu_state;

  cursor_gpu_state = g_new0 (MetaKmsCursorGpuState, 1);
  cursor_gpu_state->gpu = META_GPU (gpu_kms);
  g_hash_table_insert (cursor_priv->gpu_states, gpu_kms, cursor_gpu_state);

  return cursor_gpu_state;
}

static void
invalidate_cursor_gpu_state (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv = get_cursor_priv (cursor_sprite);
  GHashTableIter iter;
  MetaKmsCursorGpuState *cursor_gpu_state;

  g_hash_table_iter_init (&iter, cursor_priv->gpu_states);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &cursor_gpu_state))
    {
      guint pending_bo;
      pending_bo = get_pending_cursor_sprite_gbm_bo_index (cursor_gpu_state);
      g_clear_pointer (&cursor_gpu_state->bos[pending_bo],
                       gbm_bo_destroy);
      cursor_gpu_state->pending_bo_state = META_CURSOR_GBM_BO_STATE_INVALIDATED;
    }
}

static void
on_cursor_sprite_texture_changed (MetaCursorSprite *cursor_sprite)
{
  invalidate_cursor_gpu_state (cursor_sprite);
}

static void
cursor_priv_free (MetaKmsCursorPrivate *cursor_priv)
{
  g_hash_table_destroy (cursor_priv->gpu_states);
  g_free (cursor_priv);
}

static MetaKmsCursorPrivate *
get_cursor_priv (MetaCursorSprite *cursor_sprite)
{
  return g_object_get_qdata (G_OBJECT (cursor_sprite), quark_cursor_sprite);
}

static MetaKmsCursorPrivate *
ensure_cursor_priv (MetaCursorSprite *cursor_sprite)
{
  MetaKmsCursorPrivate *cursor_priv;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (cursor_priv)
    return cursor_priv;

  cursor_priv = g_new0 (MetaKmsCursorPrivate, 1);
  cursor_priv->gpu_states =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) cursor_gpu_state_free);
  g_object_set_qdata_full (G_OBJECT (cursor_sprite),
                           quark_cursor_sprite,
                           cursor_priv,
                           (GDestroyNotify) cursor_priv_free);

  g_signal_connect (cursor_sprite, "texture-changed",
                    G_CALLBACK (on_cursor_sprite_texture_changed), NULL);

  unset_can_preprocess (cursor_sprite);

  return cursor_priv;
}

static void
load_cursor_sprite_gbm_buffer_for_gpu (MetaKmsCursorRenderer *kms_renderer,
                                       MetaGpuKms            *gpu_kms,
                                       MetaCursorSprite      *cursor_sprite,
                                       uint8_t               *pixels,
                                       uint                   width,
                                       uint                   height,
                                       int                    rowstride,
                                       uint32_t               gbm_format)
{
  uint64_t cursor_width, cursor_height;
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;
  struct gbm_device *gbm_device;

  cursor_renderer_gpu_data =
    meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data)
    return;

  cursor_width = (uint64_t) cursor_renderer_gpu_data->cursor_width;
  cursor_height = (uint64_t) cursor_renderer_gpu_data->cursor_height;

  if (width > cursor_width || height > cursor_height)
    {
      meta_warning ("Invalid theme cursor size (must be at most %ux%u)",
                    (unsigned int)cursor_width, (unsigned int)cursor_height);
      return;
    }

  gbm_device = meta_gbm_device_from_gpu (gpu_kms);
  if (gbm_device_is_format_supported (gbm_device, gbm_format,
                                      GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE))
    {
      struct gbm_bo *bo;
      uint8_t buf[4 * cursor_width * cursor_height];
      uint i;

      bo = gbm_bo_create (gbm_device, cursor_width, cursor_height,
                          gbm_format, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
      if (!bo)
        {
          meta_warning ("Failed to allocate HW cursor buffer");
          return;
        }

      memset (buf, 0, sizeof(buf));
      for (i = 0; i < height; i++)
        memcpy (buf + i * 4 * cursor_width, pixels + i * rowstride, width * 4);
      if (gbm_bo_write (bo, buf, cursor_width * cursor_height * 4) != 0)
        {
          meta_warning ("Failed to write cursors buffer data: %s",
                        g_strerror (errno));
          gbm_bo_destroy (bo);
          return;
        }

      set_pending_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms, bo);
    }
  else
    {
      meta_warning ("HW cursor for format %d not supported", gbm_format);
    }
}

static gboolean
is_cursor_hw_state_valid (MetaCursorSprite *cursor_sprite,
                          MetaGpuKms       *gpu_kms)
{
  MetaKmsCursorPrivate *cursor_priv;
  MetaKmsCursorGpuState *cursor_gpu_state;

  cursor_priv = get_cursor_priv (cursor_sprite);
  if (!cursor_priv)
    return FALSE;

  cursor_gpu_state = get_cursor_gpu_state (cursor_priv, gpu_kms);
  if (!cursor_gpu_state)
    return FALSE;

  switch (cursor_gpu_state->pending_bo_state)
    {
    case META_CURSOR_GBM_BO_STATE_SET:
    case META_CURSOR_GBM_BO_STATE_NONE:
      return TRUE;
    case META_CURSOR_GBM_BO_STATE_INVALIDATED:
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

static gboolean
is_cursor_scale_and_transform_valid (MetaKmsCursorRenderer *kms_cursor_renderer,
                                     MetaCursorSprite      *cursor_sprite)
{
  MetaMonitorTransform transform;
  float scale;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (kms_cursor_renderer,
                                                          cursor_sprite,
                                                          &scale))
    return FALSE;

  if (!get_common_crtc_sprite_transform_for_logical_monitors (kms_cursor_renderer,
                                                              cursor_sprite,
                                                              &transform))
    return FALSE;

  return (scale == get_current_relative_scale (cursor_sprite) &&
          transform == get_current_relative_transform (cursor_sprite));
}

static cairo_surface_t *
scale_and_transform_cursor_sprite_cpu (uint8_t              *pixels,
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
          cairo_scale (cr, 1, -1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_90:
          cairo_rotate (cr, M_PI * 1.5);
          cairo_scale (cr, -1, 1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_180:
          cairo_rotate (cr, M_PI);
          cairo_scale (cr, 1, -1);
          break;
        case META_MONITOR_TRANSFORM_FLIPPED_270:
          cairo_rotate (cr, M_PI * 0.5);
          cairo_scale (cr, -1, 1);
          break;
        case META_MONITOR_TRANSFORM_NORMAL:
          g_assert_not_reached ();
        }
      cairo_translate (cr, -0.5 * image_width, -0.5 * image_height);
    }
  cairo_scale (cr, scale, scale);

  source_surface = cairo_image_surface_create_for_data (pixels,
                                                        CAIRO_FORMAT_ARGB32,
                                                        width,
                                                        height,
                                                        rowstride);

  cairo_set_source_surface (cr, source_surface, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (source_surface);

  return target_surface;
}

static void
load_scaled_and_transformed_cursor_sprite (MetaKmsCursorRenderer *kms_renderer,
                                           MetaGpuKms            *gpu_kms,
                                           MetaCursorSprite      *cursor_sprite,
                                           float                  relative_scale,
                                           MetaMonitorTransform   relative_transform,
                                           uint8_t               *data,
                                           int                    width,
                                           int                    height,
                                           int                    rowstride,
                                           uint32_t               gbm_format)
{
  if (!G_APPROX_VALUE (relative_scale, 1.f, FLT_EPSILON) ||
      relative_transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      cairo_surface_t *surface;

      surface = scale_and_transform_cursor_sprite_cpu (data,
                                                       width,
                                                       height,
                                                       rowstride,
                                                       relative_scale,
                                                       relative_transform);

      load_cursor_sprite_gbm_buffer_for_gpu (kms_renderer,
                                             gpu_kms,
                                             cursor_sprite,
                                             cairo_image_surface_get_data (surface),
                                             cairo_image_surface_get_width (surface),
                                             cairo_image_surface_get_width (surface),
                                             cairo_image_surface_get_stride (surface),
                                             gbm_format);

      cairo_surface_destroy (surface);
    }
  else
    {
      load_cursor_sprite_gbm_buffer_for_gpu (kms_renderer,
                                             gpu_kms,
                                             cursor_sprite,
                                             data,
                                             width,
                                             height,
                                             rowstride,
                                             gbm_format);
    }
}

#ifdef HAVE_WAYLAND
static void
realize_cursor_sprite_from_wl_buffer_for_gpu (MetaKmsCursorRenderer   *kms_cursor_renderer,
                                              MetaGpuKms              *gpu_kms,
                                              MetaCursorSpriteWayland *sprite_wayland)
{
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_wayland);
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;
  uint64_t cursor_width, cursor_height;
  CoglTexture *texture;
  uint width, height;
  MetaWaylandBuffer *buffer;
  struct wl_resource *buffer_resource;
  struct wl_shm_buffer *shm_buffer;

  cursor_renderer_gpu_data =
    meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data || cursor_renderer_gpu_data->hw_cursor_broken)
    return;

  if (is_cursor_hw_state_valid (cursor_sprite, gpu_kms) &&
      is_cursor_scale_and_transform_valid (kms_cursor_renderer, cursor_sprite))
    return;

  buffer = meta_cursor_sprite_wayland_get_buffer (sprite_wayland);
  if (!buffer)
    return;

  buffer_resource = meta_wayland_buffer_get_resource (buffer);
  if (!buffer_resource)
    return;

  ensure_cursor_priv (cursor_sprite);

  shm_buffer = wl_shm_buffer_get (buffer_resource);
  if (shm_buffer)
    {
      int rowstride = wl_shm_buffer_get_stride (shm_buffer);
      uint8_t *buffer_data;
      float relative_scale;
      MetaMonitorTransform relative_transform;
      uint32_t gbm_format;

      if (!get_common_crtc_sprite_scale_for_logical_monitors (kms_cursor_renderer,
                                                              cursor_sprite,
                                                              &relative_scale))
        {
          unset_can_preprocess (cursor_sprite);
          return;
        }

      if (!get_common_crtc_sprite_transform_for_logical_monitors (kms_cursor_renderer,
                                                                  cursor_sprite,
                                                                  &relative_transform))
        {
          unset_can_preprocess (cursor_sprite);
          return;
        }

      set_can_preprocess (cursor_sprite,
                          relative_scale,
                          relative_transform);

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

      load_scaled_and_transformed_cursor_sprite (kms_cursor_renderer,
                                                 gpu_kms,
                                                 cursor_sprite,
                                                 relative_scale,
                                                 relative_transform,
                                                 buffer_data,
                                                 width,
                                                 height,
                                                 rowstride,
                                                 gbm_format);

      wl_shm_buffer_end_access (shm_buffer);
    }
  else
    {
      struct gbm_device *gbm_device;
      struct gbm_bo *bo;

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
          return;
        }

      gbm_device = meta_gbm_device_from_gpu (gpu_kms);
      bo = gbm_bo_import (gbm_device,
                          GBM_BO_IMPORT_WL_BUFFER,
                          buffer,
                          GBM_BO_USE_CURSOR);
      if (!bo)
        {
          meta_warning ("Importing HW cursor from wl_buffer failed");
          return;
        }

      unset_can_preprocess (cursor_sprite);

      set_pending_cursor_sprite_gbm_bo (cursor_sprite, gpu_kms, bo);
    }
}
#endif

static void
realize_cursor_sprite_from_xcursor_for_gpu (MetaKmsCursorRenderer   *kms_cursor_renderer,
                                            MetaGpuKms              *gpu_kms,
                                            MetaCursorSpriteXcursor *sprite_xcursor)
{
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_xcursor);
  XcursorImage *xc_image;
  float relative_scale;
  MetaMonitorTransform relative_transform;

  ensure_cursor_priv (cursor_sprite);

  cursor_renderer_gpu_data =
    meta_kms_cursor_renderer_gpu_data_from_gpu (gpu_kms);
  if (!cursor_renderer_gpu_data || cursor_renderer_gpu_data->hw_cursor_broken)
    return;

  if (is_cursor_hw_state_valid (cursor_sprite, gpu_kms) &&
      is_cursor_scale_and_transform_valid (kms_cursor_renderer, cursor_sprite))
    return;

  if (!get_common_crtc_sprite_scale_for_logical_monitors (kms_cursor_renderer,
                                                          cursor_sprite,
                                                          &relative_scale))
    {
      unset_can_preprocess (cursor_sprite);
      return;
    }

  if (!get_common_crtc_sprite_transform_for_logical_monitors (kms_cursor_renderer,
                                                              cursor_sprite,
                                                              &relative_transform))
    {
      unset_can_preprocess (cursor_sprite);
      return;
    }

  set_can_preprocess (cursor_sprite,
                      relative_scale,
                      relative_transform);

  xc_image = meta_cursor_sprite_xcursor_get_current_image (sprite_xcursor);

  load_scaled_and_transformed_cursor_sprite (kms_cursor_renderer,
                                             gpu_kms,
                                             cursor_sprite,
                                             relative_scale,
                                             relative_transform,
                                             (uint8_t *) xc_image->pixels,
                                             xc_image->width,
                                             xc_image->height,
                                             xc_image->width * 4,
                                             GBM_FORMAT_ARGB8888);
}

static void
realize_cursor_sprite_for_gpu (MetaKmsCursorRenderer *kms_cursor_renderer,
                               MetaGpuKms            *gpu_kms,
                               MetaCursorSprite      *cursor_sprite)
{
  if (META_IS_CURSOR_SPRITE_XCURSOR (cursor_sprite))
    {
      MetaCursorSpriteXcursor *sprite_xcursor =
        META_CURSOR_SPRITE_XCURSOR (cursor_sprite);

      realize_cursor_sprite_from_xcursor_for_gpu (kms_cursor_renderer,
                                                  gpu_kms,
                                                  sprite_xcursor);
    }
#ifdef HAVE_WAYLAND
  else if (META_IS_CURSOR_SPRITE_WAYLAND (cursor_sprite))
    {
      MetaCursorSpriteWayland *sprite_wayland =
        META_CURSOR_SPRITE_WAYLAND (cursor_sprite);

      realize_cursor_sprite_from_wl_buffer_for_gpu (kms_cursor_renderer,
                                                    gpu_kms,
                                                    sprite_wayland);
    }
#endif
}

static void
realize_cursor_sprite (MetaKmsCursorRenderer *kms_cursor_renderer,
                       MetaCursorSprite      *cursor_sprite,
                       GList                 *gpus)
{
  GList *l;

  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      realize_cursor_sprite_for_gpu (kms_cursor_renderer, gpu_kms,
                                     cursor_sprite);
    }
}

static void
meta_kms_cursor_renderer_class_init (MetaKmsCursorRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_cursor_renderer_finalize;

  quark_cursor_sprite = g_quark_from_static_string ("-meta-kms-cursor");
  quark_kms_cursor_renderer_gpu_data =
    g_quark_from_static_string ("-meta-kms-cursor-renderer-gpu-data");
}

static void
init_hw_cursor_support_for_gpu (MetaGpuKms *gpu_kms)
{
  MetaKmsDevice *kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  MetaKmsCursorRendererGpuData *cursor_renderer_gpu_data;
  struct gbm_device *gbm_device;
  uint64_t width, height;

  gbm_device = meta_gbm_device_from_gpu (gpu_kms);
  if (!gbm_device)
    return;

  cursor_renderer_gpu_data =
    meta_create_kms_cursor_renderer_gpu_data (gpu_kms);

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

static void
init_hw_cursor_support (MetaKmsCursorRenderer *kms_cursor_renderer)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (priv->backend);
  for (l = gpus; l; l = l->next)
    {
      MetaGpuKms *gpu_kms = l->data;

      init_hw_cursor_support_for_gpu (gpu_kms);
    }
}

MetaKmsCursorRenderer *
meta_kms_cursor_renderer_new (MetaBackend *backend)
{
  MetaKmsCursorRenderer *kms_cursor_renderer;
  MetaKmsCursorRendererPrivate *priv;

  kms_cursor_renderer = g_object_new (META_TYPE_KMS_CURSOR_RENDERER, NULL);
  priv = meta_kms_cursor_renderer_get_instance_private (kms_cursor_renderer);

  g_signal_connect (backend, "gpu-added",
                    G_CALLBACK (on_gpu_added_for_cursor), NULL);

  priv->backend = backend;
  priv->hw_state_invalidated = TRUE;

  init_hw_cursor_support (kms_cursor_renderer);

  return kms_cursor_renderer;
}

static void
meta_kms_cursor_renderer_init (MetaKmsCursorRenderer *kms_renderer)
{
}

void
meta_kms_cursor_renderer_invalidate_state (MetaKmsCursorRenderer *kms_renderer)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_renderer);

  priv->hw_state_invalidated = TRUE;
}

void
meta_kms_cursor_renderer_set_cursor_renderer (MetaKmsCursorRenderer *kms_renderer,
                                              MetaCursorRenderer    *renderer)
{
  MetaKmsCursorRendererPrivate *priv =
    meta_kms_cursor_renderer_get_instance_private (kms_renderer);
  MetaCursorRenderer *old_renderer =
    priv->cursor_renderer ? g_object_ref (priv->cursor_renderer) : NULL;

  if (g_set_object (&priv->cursor_renderer, renderer))
    {
      if (old_renderer)
        {
          meta_cursor_renderer_native_set_kms_cursor_renderer (META_CURSOR_RENDERER_NATIVE (old_renderer),
                                                               NULL);
        }
    }

  if (old_renderer)
    g_object_unref (old_renderer);
}
