/*
 * Copyright 2014 Red Hat, Inc.
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
 */

#include "config.h"

#include <cogl/cogl.h>
#include <cogl/cogl-wayland-server.h>
#include <clutter/clutter.h>
#include <gbm.h>

#include "display-private.h"
#include "meta-cursor-tracker-native.h"
#include "meta-cursor-tracker-private.h"
#include "meta-monitor-manager.h"
#include "meta-cursor-private.h"

#include "wayland/meta-wayland-private.h"

struct _MetaCursorTrackerNative
{
  MetaCursorTracker parent;

  gboolean has_hw_cursor;

  int current_x, current_y;
  MetaRectangle current_rect;
  MetaRectangle previous_rect;
  gboolean previous_is_valid;

  CoglPipeline *pipeline;
  int drm_fd;
  struct gbm_device *gbm;
};

struct _MetaCursorTrackerNativeClass
{
  MetaCursorTrackerClass parent_class;
};

G_DEFINE_TYPE (MetaCursorTrackerNative, meta_cursor_tracker_native, META_TYPE_CURSOR_TRACKER);

static void
meta_cursor_tracker_native_load_cursor_pixels (MetaCursorTracker   *tracker,
                                               MetaCursorReference *cursor,
                                               uint8_t             *pixels,
                                               int                  width,
                                               int                  height,
                                               int                  rowstride,
                                               uint32_t             format)
{
  MetaCursorTrackerNative *self = META_CURSOR_TRACKER_NATIVE (tracker);

  if (!self->gbm)
    return;

  meta_cursor_reference_load_gbm_buffer (cursor,
                                         self->gbm,
                                         pixels,
                                         width, height, rowstride,
                                         format);
}

static void
meta_cursor_tracker_native_load_cursor_buffer (MetaCursorTracker   *tracker,
                                               MetaCursorReference *cursor,
                                               struct wl_resource  *buffer)
{
  struct wl_shm_buffer *shm_buffer;
  int width, height;

  width = cogl_texture_get_width (COGL_TEXTURE (cursor->image.texture));
  height = cogl_texture_get_height (COGL_TEXTURE (cursor->image.texture));

  shm_buffer = wl_shm_buffer_get (buffer);
  if (shm_buffer)
    {
      uint32_t gbm_format;
      uint8_t *pixels = wl_shm_buffer_get_data (shm_buffer);
      int rowstride = wl_shm_buffer_get_stride (shm_buffer);

      switch (wl_shm_buffer_get_format (shm_buffer))
        {
#if G_BYTE_ORDER == G_BIG_ENDIAN
        case WL_SHM_FORMAT_ARGB8888:
          gbm_format = GBM_FORMAT_ARGB8888;
          break;
        case WL_SHM_FORMAT_XRGB8888:
          gbm_format = GBM_FORMAT_XRGB8888;
          break;
#else
        case WL_SHM_FORMAT_ARGB8888:
          gbm_format = GBM_FORMAT_ARGB8888;
          break;
        case WL_SHM_FORMAT_XRGB8888:
          gbm_format = GBM_FORMAT_XRGB8888;
          break;
#endif
        default:
          g_warn_if_reached ();
          gbm_format = GBM_FORMAT_ARGB8888;
        }

      meta_cursor_tracker_native_load_cursor_pixels (tracker,
                                                     cursor,
                                                     pixels,
                                                     width,
                                                     height,
                                                     rowstride,
                                                     gbm_format);
    }
  else
    {
      MetaCursorTrackerNative *self = META_CURSOR_TRACKER_NATIVE (tracker);
      if (!self->gbm)
        return;
      meta_cursor_reference_import_gbm_buffer (cursor, self->gbm, buffer, width, height);
    }
}

static void
set_crtc_has_hw_cursor (MetaCursorTrackerNative *self,
                        MetaCRTC                *crtc,
                        gboolean                 has)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (self);

  if (has)
    {
      MetaCursorReference *displayed_cursor = tracker->displayed_cursor;
      struct gbm_bo *bo;
      union gbm_bo_handle handle;
      int width, height;
      int hot_x, hot_y;

      bo = meta_cursor_reference_get_gbm_bo (displayed_cursor, &hot_x, &hot_y);

      handle = gbm_bo_get_handle (bo);
      width = gbm_bo_get_width (bo);
      height = gbm_bo_get_height (bo);

      drmModeSetCursor2 (self->drm_fd, crtc->crtc_id, handle.u32,
                         width, height, hot_x, hot_y);
      crtc->has_hw_cursor = TRUE;
    }
  else
    {
      drmModeSetCursor2 (self->drm_fd, crtc->crtc_id, 0, 0, 0, 0, 0);
      crtc->has_hw_cursor = FALSE;
    }
}

static void
on_monitors_changed (MetaMonitorManager      *monitors,
                     MetaCursorTrackerNative *self)
{
  MetaCRTC *crtcs;
  unsigned int i, n_crtcs;

  if (!self->has_hw_cursor)
    return;

  /* Go through the new list of monitors, find out where the cursor is */
  meta_monitor_manager_get_resources (monitors, NULL, NULL, &crtcs, &n_crtcs, NULL, NULL);

  for (i = 0; i < n_crtcs; i++)
    {
      MetaRectangle *rect = &crtcs[i].rect;
      gboolean has;

      has = meta_rectangle_overlap (&self->current_rect, rect);

      /* Need to do it unconditionally here, our tracking is
         wrong because we reloaded the CRTCs */
      set_crtc_has_hw_cursor (self, &crtcs[i], has);
    }
}

static gboolean
should_have_hw_cursor (MetaCursorTrackerNative *self)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (self);

  if (tracker->displayed_cursor)
    return (meta_cursor_reference_get_gbm_bo (tracker->displayed_cursor, NULL, NULL) != NULL);
  else
    return FALSE;
}

static void
update_hw_cursor (MetaCursorTrackerNative *self)
{
  MetaMonitorManager *monitors;
  MetaCRTC *crtcs;
  unsigned int i, n_crtcs;
  gboolean enabled;

  enabled = should_have_hw_cursor (self);
  self->has_hw_cursor = enabled;

  monitors = meta_monitor_manager_get ();
  meta_monitor_manager_get_resources (monitors, NULL, NULL, &crtcs, &n_crtcs, NULL, NULL);

  for (i = 0; i < n_crtcs; i++)
    {
      MetaRectangle *rect = &crtcs[i].rect;
      gboolean has;

      has = enabled && meta_rectangle_overlap (&self->current_rect, rect);

      if (has || crtcs[i].has_hw_cursor)
        set_crtc_has_hw_cursor (self, &crtcs[i], has);
    }
}

static void
move_hw_cursor (MetaCursorTrackerNative *self)
{
  MetaMonitorManager *monitors;
  MetaCRTC *crtcs;
  unsigned int i, n_crtcs;

  monitors = meta_monitor_manager_get ();
  meta_monitor_manager_get_resources (monitors, NULL, NULL, &crtcs, &n_crtcs, NULL, NULL);

  g_assert (self->has_hw_cursor);

  for (i = 0; i < n_crtcs; i++)
    {
      MetaRectangle *rect = &crtcs[i].rect;
      gboolean has;

      has = meta_rectangle_overlap (&self->current_rect, rect);

      if (has != crtcs[i].has_hw_cursor)
        set_crtc_has_hw_cursor (self, &crtcs[i], has);
      if (has)
        drmModeMoveCursor (self->drm_fd, crtcs[i].crtc_id,
                           self->current_rect.x - rect->x,
                           self->current_rect.y - rect->y);
    }
}

static void
queue_redraw (MetaCursorTrackerNative *self)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (self);
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  ClutterActor *stage = compositor->stage;
  cairo_rectangle_int_t clip;

  /* Clear the location the cursor was at before, if we need to. */
  if (self->previous_is_valid)
    {
      clip.x = self->previous_rect.x;
      clip.y = self->previous_rect.y;
      clip.width = self->previous_rect.width;
      clip.height = self->previous_rect.height;
      clutter_actor_queue_redraw_with_clip (stage, &clip);
      self->previous_is_valid = FALSE;
    }

  if (self->has_hw_cursor || !tracker->displayed_cursor)
    return;

  clip.x = self->current_rect.x;
  clip.y = self->current_rect.y;
  clip.width = self->current_rect.width;
  clip.height = self->current_rect.height;
  clutter_actor_queue_redraw_with_clip (stage, &clip);
}

static void
meta_cursor_tracker_native_sync_cursor (MetaCursorTracker *tracker)
{
  MetaCursorTrackerNative *self = META_CURSOR_TRACKER_NATIVE (tracker);
  MetaCursorReference *displayed_cursor;

  displayed_cursor = tracker->displayed_cursor;

  if (displayed_cursor)
    {
      CoglTexture *texture;
      int hot_x, hot_y;

      texture = meta_cursor_reference_get_cogl_texture (displayed_cursor, &hot_x, &hot_y);
      cogl_pipeline_set_layer_texture (self->pipeline, 0, texture);

      self->current_rect.x = self->current_x - hot_x;
      self->current_rect.y = self->current_y - hot_y;
      self->current_rect.width = cogl_texture_get_width (COGL_TEXTURE (texture));
      self->current_rect.height = cogl_texture_get_height (COGL_TEXTURE (texture));
    }
  else
    {
      cogl_pipeline_set_layer_texture (self->pipeline, 0, NULL);

      self->current_rect.x = 0;
      self->current_rect.y = 0;
      self->current_rect.width = 0;
      self->current_rect.height = 0;
    }

  update_hw_cursor (self);

  if (self->has_hw_cursor)
    move_hw_cursor (self);
  else
    queue_redraw (self);
}

static void
meta_cursor_tracker_native_get_pointer (MetaCursorTracker   *tracker,
                                        int                 *x,
                                        int                 *y,
                                        ClutterModifierType *mods)
{
  ClutterDeviceManager *cmanager;
  ClutterInputDevice *cdevice;
  ClutterPoint point;

  /* On wayland we can't use GDK, because that only sees the events we
   * forward to xwayland.
   */
  cmanager = clutter_device_manager_get_default ();
  cdevice = clutter_device_manager_get_core_device (cmanager, CLUTTER_POINTER_DEVICE);

  clutter_input_device_get_coords (cdevice, NULL, &point);
  if (x)
    *x = point.x;
  if (y)
    *y = point.y;
  if (mods)
    *mods = clutter_input_device_get_modifier_state (cdevice);
}

static void
meta_cursor_tracker_native_finalize (GObject *object)
{
  MetaCursorTrackerNative *self = META_CURSOR_TRACKER_NATIVE (object);

  if (self->pipeline)
    cogl_object_unref (self->pipeline);
  if (self->gbm)
    gbm_device_destroy (self->gbm);

  G_OBJECT_CLASS (meta_cursor_tracker_native_parent_class)->finalize (object);
}

static void
meta_cursor_tracker_native_class_init (MetaCursorTrackerNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCursorTrackerClass *cursor_tracker_class = META_CURSOR_TRACKER_CLASS (klass);

  object_class->finalize = meta_cursor_tracker_native_finalize;

  cursor_tracker_class->get_pointer = meta_cursor_tracker_native_get_pointer;
  cursor_tracker_class->sync_cursor = meta_cursor_tracker_native_sync_cursor;
  cursor_tracker_class->load_cursor_pixels = meta_cursor_tracker_native_load_cursor_pixels;
  cursor_tracker_class->load_cursor_buffer = meta_cursor_tracker_native_load_cursor_buffer;
}

static void
meta_cursor_tracker_native_init (MetaCursorTrackerNative *self)
{
  MetaWaylandCompositor *compositor;
  CoglContext *ctx;
  MetaMonitorManager *monitors;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  self->pipeline = cogl_pipeline_new (ctx);

  compositor = meta_wayland_compositor_get_default ();
  compositor->seat->cursor_tracker = META_CURSOR_TRACKER (self);
  meta_cursor_tracker_native_update_position (self,
                                              wl_fixed_to_int (compositor->seat->pointer.x),
                                              wl_fixed_to_int (compositor->seat->pointer.y));

#if defined(CLUTTER_WINDOWING_EGL)
  if (clutter_check_windowing_backend (CLUTTER_WINDOWING_EGL))
    {
      CoglRenderer *cogl_renderer = cogl_display_get_renderer (cogl_context_get_display (ctx));
      self->drm_fd = cogl_kms_renderer_get_kms_fd (cogl_renderer);
      self->gbm = gbm_create_device (self->drm_fd);
    }
#endif

  monitors = meta_monitor_manager_get ();
  g_signal_connect_object (monitors, "monitors-changed",
                           G_CALLBACK (on_monitors_changed), self, 0);
}

void
meta_cursor_tracker_native_update_position (MetaCursorTrackerNative *self,
                                            int                      new_x,
                                            int                      new_y)
{
  self->current_x = new_x;
  self->current_y = new_y;

  _meta_cursor_tracker_sync_cursor (META_CURSOR_TRACKER (self));
}

void
meta_cursor_tracker_native_paint (MetaCursorTrackerNative *self)
{
  MetaCursorTracker *tracker = META_CURSOR_TRACKER (self);

  if (self->has_hw_cursor || !tracker->displayed_cursor)
    return;

  cogl_framebuffer_draw_rectangle (cogl_get_draw_framebuffer (),
                                   self->pipeline,
                                   self->current_rect.x,
                                   self->current_rect.y,
                                   self->current_rect.x +
                                   self->current_rect.width,
                                   self->current_rect.y +
                                   self->current_rect.height);

  self->previous_rect = self->current_rect;
  self->previous_is_valid = TRUE;
}

void
meta_cursor_tracker_native_force_update (MetaCursorTrackerNative *self)
{
  _meta_cursor_tracker_sync_cursor (META_CURSOR_TRACKER (self));
}
