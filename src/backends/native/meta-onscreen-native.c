/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016-2020 Red Hat
 * Copyright (c) 2018,2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-onscreen-native.h"

#include <glib/gstdio.h>
#include <drm_fourcc.h>

#include "backends/meta-egl-ext.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-drm-buffer-import.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-frame-native.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-render-device-gbm.h"
#include "backends/native/meta-render-device.h"
#include "backends/native/meta-renderer-native-gles3.h"
#include "backends/native/meta-renderer-native-private.h"
#include "backends/native/meta-egl-gbm.h"
#include "cogl/cogl.h"
#include "common/meta-cogl-drm-formats.h"
#include "common/meta-drm-format-helpers.h"

typedef enum _MetaSharedFramebufferImportStatus
{
  /* Not tried importing yet. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE,
  /* Tried before and failed. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED,
  /* Tried before and succeeded. */
  META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK
} MetaSharedFramebufferImportStatus;

typedef struct _MetaOnscreenNativeSecondaryGpuState
{
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  EGLSurface egl_surface;

  struct {
    struct gbm_surface *surface;
  } gbm;

  struct {
    MetaDrmBufferDumb *current_dumb_fb;
    MetaDrmBufferDumb *dumb_fbs[3];
  } cpu;

  gboolean noted_primary_gpu_copy_ok;
  gboolean noted_primary_gpu_copy_failed;
  MetaSharedFramebufferImportStatus import_status;
} MetaOnscreenNativeSecondaryGpuState;

typedef struct _KmsProperty
{
  gboolean invalidated;
  int64_t target_frame_counter;
  gulong signal_handler_id;
} KmsProperty;

struct _MetaOnscreenNative
{
  CoglOnscreenEgl parent;

  MetaRendererNative *renderer_native;
  MetaGpuKms *render_gpu;
  MetaOutput *output;
  MetaCrtc *crtc;

  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;

  ClutterFrame *presented_frame;
  ClutterFrame *posted_frame;
  ClutterFrame *superseded_frame;
  ClutterFrame *next_frame;

  struct {
    struct gbm_surface *surface;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLStreamKHR stream;

    MetaDrmBufferDumb *dumb_fb;
  } egl;
#endif

  gboolean needs_flush;

  gboolean frame_sync_requested;
  gboolean frame_sync_enabled;

  MetaRendererView *view;

  union {
    struct {
      KmsProperty gamma_lut;
      KmsProperty privacy_screen;
    } property;
    KmsProperty properties[2];
  };
};

G_DEFINE_TYPE (MetaOnscreenNative, meta_onscreen_native,
               COGL_TYPE_ONSCREEN_EGL)

static GQuark blit_source_quark = 0;

static void
maybe_post_next_frame (CoglOnscreen *onscreen);

static void
post_nonprimary_plane_update (MetaOnscreenNative *onscreen_native,
                              ClutterFrame       *frame,
                              MetaKmsUpdate      *kms_update);

static gboolean
init_secondary_gpu_state (MetaRendererNative  *renderer_native,
                          CoglOnscreen        *onscreen,
                          GError             **error);

static void
meta_onscreen_native_promote_posted_frame (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaFrameNative *frame_native;

  if (!onscreen_native->posted_frame)
    return;

  frame_native = meta_frame_native_from_frame (onscreen_native->posted_frame);
  if (!meta_frame_native_get_buffer (frame_native))
    {
      g_clear_pointer (&onscreen_native->posted_frame, clutter_frame_unref);
    }
  else
    {
      g_clear_pointer (&onscreen_native->presented_frame, clutter_frame_unref);
      onscreen_native->presented_frame =
        g_steal_pointer (&onscreen_native->posted_frame);
    }
}

static void
meta_onscreen_native_clear_posted_fb (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  g_clear_pointer (&onscreen_native->posted_frame, clutter_frame_unref);
}

static void
maybe_update_frame_info (MetaCrtc         *crtc,
                         CoglFrameInfo    *frame_info,
                         int64_t           time_us,
                         CoglFrameInfoFlag flags,
                         unsigned int      sequence)
{
  const MetaCrtcConfig *crtc_config;
  const MetaCrtcModeInfo *crtc_mode_info;
  float refresh_rate;

  g_return_if_fail (crtc);

  crtc_config = meta_crtc_get_config (crtc);
  if (!crtc_config)
    return;

  crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);
  refresh_rate = crtc_mode_info->refresh_rate;
  if (refresh_rate >= frame_info->refresh_rate)
    {
      frame_info->presentation_time_us = time_us;
      frame_info->refresh_rate = refresh_rate;
      frame_info->flags |= flags;
      frame_info->sequence = sequence;
    }
}

static void
meta_onscreen_native_notify_frame_complete (CoglOnscreen *onscreen)
{
  CoglFrameInfo *info;

  info = cogl_onscreen_pop_head_frame_info (onscreen);

  g_return_if_fail (info);

  _cogl_onscreen_notify_frame_sync (onscreen, info);
  _cogl_onscreen_notify_complete (onscreen, info);
  g_object_unref (info);
}

static void
notify_view_crtc_presented (MetaRendererView *view,
                            MetaKmsCrtc      *kms_crtc,
                            int64_t           time_us,
                            CoglFrameInfoFlag flags,
                            unsigned int      sequence)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (stage_view);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFrameInfo *frame_info;
  MetaCrtc *crtc;
  int64_t frame_counter;

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);

  g_return_if_fail (frame_info != NULL);

  frame_counter = cogl_frame_info_get_frame_counter (frame_info);

  for (int i = 0; i < G_N_ELEMENTS (onscreen_native->properties); i++)
    {
      KmsProperty *prop = &onscreen_native->properties[i];

      if (frame_counter >= prop->target_frame_counter)
        prop->target_frame_counter = 0;
    }

  crtc = META_CRTC (meta_crtc_kms_from_kms_crtc (kms_crtc));
  maybe_update_frame_info (crtc, frame_info, time_us, flags, sequence);

  meta_onscreen_native_notify_frame_complete (onscreen);
  meta_onscreen_native_promote_posted_frame (onscreen);
  maybe_post_next_frame (onscreen);
}

static void
page_flip_feedback_flipped (MetaKmsCrtc  *kms_crtc,
                            unsigned int  sequence,
                            unsigned int  tv_sec,
                            unsigned int  tv_usec,
                            gpointer      user_data)
{
  MetaRendererView *view = user_data;
  struct timeval page_flip_time;
  MetaKmsDevice *kms_device;
  int64_t presentation_time_us;
  CoglFrameInfoFlag flags = COGL_FRAME_INFO_FLAG_VSYNC;

  page_flip_time = (struct timeval) {
    .tv_sec = tv_sec,
    .tv_usec = tv_usec,
  };

  kms_device = meta_kms_crtc_get_device (kms_crtc);
  if (meta_kms_device_uses_monotonic_clock (kms_device))
    {
      presentation_time_us = meta_timeval_to_microseconds (&page_flip_time);
      flags |= COGL_FRAME_INFO_FLAG_HW_CLOCK;
    }
  else
    {
      /*
       * Other parts of the code assume MONOTONIC timestamps. So, if the device
       * timestamp isn't MONOTONIC, don't use it.
       */
      presentation_time_us = g_get_monotonic_time ();
    }

  notify_view_crtc_presented (view, kms_crtc,
                              presentation_time_us,
                              flags,
                              sequence);
}

static void
page_flip_feedback_ready (MetaKmsCrtc *kms_crtc,
                          gpointer     user_data)
{
  MetaRendererView *view = user_data;
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  CoglFrameInfo *frame_info;

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  meta_onscreen_native_notify_frame_complete (onscreen);
  meta_onscreen_native_promote_posted_frame (onscreen);
  maybe_post_next_frame (onscreen);
}

static void
page_flip_feedback_mode_set_fallback (MetaKmsCrtc *kms_crtc,
                                      gpointer     user_data)
{
  MetaRendererView *view = user_data;
  int64_t now_us;

  /*
   * We ended up not page flipping, thus we don't have a presentation time to
   * use. Lets use the next best thing: the current time.
   */

  now_us = g_get_monotonic_time ();

  notify_view_crtc_presented (view,
                              kms_crtc,
                              now_us,
                              COGL_FRAME_INFO_FLAG_NONE,
                              0);
}

static void
page_flip_feedback_discarded (MetaKmsCrtc  *kms_crtc,
                              gpointer      user_data,
                              const GError *error)
{
  MetaRendererView *view = user_data;
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFrameInfo *frame_info;
  int64_t frame_counter;

  /*
   * Page flipping failed, but we want to fail gracefully, so to avoid freezing
   * the frame clock, emit a symbolic flip.
   */

  if (error &&
      !g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED))

    g_warning ("Page flip discarded: %s", error->message);

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  frame_counter = cogl_frame_info_get_frame_counter (frame_info);

  for (int i = 0; i < G_N_ELEMENTS (onscreen_native->properties); i++)
    {
      KmsProperty *prop = &onscreen_native->properties[i];

      if (prop->target_frame_counter != 0 &&
          frame_counter >= prop->target_frame_counter)
        {
          prop->invalidated = TRUE;
          prop->target_frame_counter = 0;
        }
    }

  meta_onscreen_native_notify_frame_complete (onscreen);
  meta_onscreen_native_clear_posted_fb (onscreen);
  maybe_post_next_frame (onscreen);
}

static const MetaKmsPageFlipListenerVtable page_flip_listener_vtable = {
  .flipped = page_flip_feedback_flipped,
  .ready = page_flip_feedback_ready,
  .mode_set_fallback = page_flip_feedback_mode_set_fallback,
  .discarded = page_flip_feedback_discarded,
};

static MetaEgl *
meta_onscreen_native_get_egl (MetaOnscreenNative *onscreen_native)
{
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;

  return meta_renderer_native_get_egl (renderer_native);
}

#ifdef HAVE_EGL_DEVICE
static int
custom_egl_stream_page_flip (gpointer custom_page_flip_data,
                             gpointer user_data)
{
  CoglOnscreen *onscreen = custom_page_flip_data;
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererView *view = user_data;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;
  EGLDisplay *egl_display;
  EGLAttrib *acquire_attribs;
  g_autoptr (GError) error = NULL;

  acquire_attribs = (EGLAttrib[]) {
    EGL_DRM_FLIP_EVENT_DATA_NV,
    (EGLAttrib) view,
    EGL_NONE
  };

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);
  render_device = renderer_gpu_data->render_device;

  egl_display = meta_render_device_get_egl_display (render_device);
  if (!meta_egl_stream_consumer_acquire_attrib (egl,
                                                egl_display,
                                                onscreen_native->egl.stream,
                                                acquire_attribs,
                                                &error))
    {
      if (g_error_matches (error, META_EGL_ERROR, EGL_RESOURCE_BUSY_EXT))
        return -EBUSY;
      else
        return -EINVAL;
    }

  return 0;
}
#endif /* HAVE_EGL_DEVICE */

static void
clear_superseded_frame (CoglOnscreen *onscreen)
{
  CoglFrameInfo *frame_info;
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  if (onscreen_native->superseded_frame == NULL)
    return;

  g_clear_pointer (&onscreen_native->superseded_frame, clutter_frame_unref);

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;
  meta_onscreen_native_notify_frame_complete (onscreen);
}

void
meta_onscreen_native_dummy_power_save_page_flip (CoglOnscreen *onscreen)
{
  clear_superseded_frame (onscreen);

  /* If the monitor woke up in the 100ms between this callback being queued
   * and dispatched, and the shell is fully idle (has nothing more to swap)
   * then we just woke to an indefinitely black screen. The only saving grace
   * here is that shells usually have multiple frames they want to display
   * soon after wakeup. But let's not assume that's always the case. Fix it
   * by displaying the last swap (which is never classified as "superseded").
   */
  maybe_post_next_frame (onscreen);
}

static void
apply_transform (MetaCrtcKms            *crtc_kms,
                 MetaKmsPlaneAssignment *kms_plane_assignment,
                 MetaKmsPlane           *kms_plane)
{
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  const MetaCrtcConfig *crtc_config;
  MtkMonitorTransform hw_transform;

  crtc_config = meta_crtc_get_config (crtc);

  hw_transform = crtc_config->transform;
  if (!meta_kms_plane_is_transform_handled (kms_plane, hw_transform))
    hw_transform = MTK_MONITOR_TRANSFORM_NORMAL;
  if (!meta_kms_plane_is_transform_handled (kms_plane, hw_transform))
    return;

  meta_kms_plane_update_set_rotation (kms_plane,
                                      kms_plane_assignment,
                                      hw_transform);
}

static void
apply_color_encoding (MetaKmsPlaneAssignment *kms_plane_assignment,
                      MetaKmsPlane           *kms_plane)
{
  if (!meta_kms_plane_is_color_encoding_handled (kms_plane,
                                                 META_KMS_PLANE_YCBCR_COLOR_ENCODING_BT709))
    return;

  meta_kms_plane_update_set_color_encoding (kms_plane,
                                            kms_plane_assignment,
                                            META_KMS_PLANE_YCBCR_COLOR_ENCODING_BT709);
}

static void
apply_color_range (MetaKmsPlaneAssignment *kms_plane_assignment,
                   MetaKmsPlane           *kms_plane)
{
  if (!meta_kms_plane_is_color_range_handled (kms_plane,
                                              META_KMS_PLANE_YCBCR_COLOR_RANGE_LIMITED))
    return;

  meta_kms_plane_update_set_color_range (kms_plane,
                                         kms_plane_assignment,
                                         META_KMS_PLANE_YCBCR_COLOR_RANGE_LIMITED);
}

static MetaKmsPlaneAssignment *
assign_primary_plane (MetaCrtcKms            *crtc_kms,
                      MetaDrmBuffer          *buffer,
                      MetaKmsUpdate          *kms_update,
                      MetaKmsAssignPlaneFlag  flags,
                      const graphene_rect_t  *src_rect,
                      const MtkRectangle     *dst_rect)
{
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  MetaFixed16Rectangle src_rect_fixed16;
  MetaKmsCrtc *kms_crtc;
  MetaKmsPlane *primary_kms_plane;
  MetaKmsPlaneAssignment *plane_assignment;

  src_rect_fixed16 = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_double (src_rect->origin.x),
    .y = meta_fixed_16_from_double (src_rect->origin.y),
    .width = meta_fixed_16_from_double (src_rect->size.width),
    .height = meta_fixed_16_from_double (src_rect->size.height),
  };

  meta_topic (META_DEBUG_KMS,
              "Assigning buffer to primary plane update on CRTC "
              "(%" G_GUINT64_FORMAT ") with src rect %f,%f %fx%f "
              "and dst rect %d,%d %dx%d",
              meta_crtc_get_id (crtc), src_rect->origin.x, src_rect->origin.y,
              src_rect->size.width, src_rect->size.height,
              dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height);

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  primary_kms_plane = meta_crtc_kms_get_assigned_primary_plane (crtc_kms);
  plane_assignment = meta_kms_update_assign_plane (kms_update,
                                                   kms_crtc,
                                                   primary_kms_plane,
                                                   buffer,
                                                   src_rect_fixed16,
                                                   *dst_rect,
                                                   flags);
  apply_transform (crtc_kms, plane_assignment, primary_kms_plane);
  apply_color_encoding (plane_assignment, primary_kms_plane);
  apply_color_range (plane_assignment, primary_kms_plane);

  return plane_assignment;
}

static gboolean
meta_onscreen_native_flip_crtc (CoglOnscreen           *onscreen,
                                ClutterFrame           *frame,
                                MetaRendererView       *view,
                                MetaCrtc               *crtc,
                                MetaKmsUpdate          *kms_update,
                                MetaKmsAssignPlaneFlag  flags,
                                const MtkRegion        *region)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaFrameNative *frame_native;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaGpuKms *gpu_kms;
  MetaDrmBuffer *buffer;
  CoglScanout *scanout;
  MetaKmsPlaneAssignment *plane_assignment;
  graphene_rect_t src_rect;
  MtkRectangle dst_rect;

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeFlipCrtcs,
                           "Meta::OnscreenNative::flip_crtc()");

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));

  g_assert (meta_gpu_kms_is_crtc_active (gpu_kms, crtc));

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      frame_native = meta_frame_native_from_frame (frame);
      buffer = meta_frame_native_get_buffer (frame_native);
      if (!buffer)
        return FALSE;

      scanout = meta_frame_native_get_scanout (frame_native);

      if (scanout)
        {
          cogl_scanout_get_src_rect (scanout, &src_rect);
          cogl_scanout_get_dst_rect (scanout, &dst_rect);
        }
      else
        {
          src_rect = (graphene_rect_t) {
            .origin.x = 0,
            .origin.y = 0,
            .size.width = meta_drm_buffer_get_width (buffer),
            .size.height = meta_drm_buffer_get_height (buffer)
          };
          dst_rect = (MtkRectangle) {
            .x = 0,
            .y = 0,
            .width = meta_drm_buffer_get_width (buffer),
            .height = meta_drm_buffer_get_height (buffer)
          };
        }

      plane_assignment = assign_primary_plane (crtc_kms,
                                               buffer,
                                               kms_update,
                                               flags,
                                               &src_rect,
                                               &dst_rect);

      if (region && !mtk_region_is_empty (region))
        meta_kms_plane_assignment_set_fb_damage (plane_assignment, region);
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      meta_kms_update_set_flushing (kms_update, kms_crtc);
      meta_kms_update_set_custom_page_flip (kms_update,
                                            custom_egl_stream_page_flip,
                                            onscreen_native);
      break;
#endif
    }

  meta_kms_update_add_page_flip_listener (kms_update,
                                          kms_crtc,
                                          &page_flip_listener_vtable,
                                          NULL,
                                          g_object_ref (view),
                                          g_object_unref);
  return TRUE;
}

static void
set_underscan (MetaOutputKms *output_kms,
               MetaKmsUpdate *kms_update)
{
  MetaOutput *output = META_OUTPUT (output_kms);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  MetaKmsConnector *kms_connector =
    meta_output_kms_get_kms_connector (output_kms);

  if (!output_info->supports_underscanning)
    return;

  if (meta_output_is_underscanning (output))
    {
      MetaCrtc *crtc;
      const MetaCrtcConfig *crtc_config;
      const MetaCrtcModeInfo *crtc_mode_info;
      uint64_t hborder, vborder;

      crtc = meta_output_get_assigned_crtc (output);
      crtc_config = meta_crtc_get_config (crtc);
      crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);

      hborder = MIN (128, (uint64_t) round (crtc_mode_info->width * 0.05));
      vborder = MIN (128, (uint64_t) round (crtc_mode_info->height * 0.05));

      g_debug ("Setting underscan of connector %s to %" G_GUINT64_FORMAT " x %" G_GUINT64_FORMAT,
               meta_kms_connector_get_name (kms_connector),
               hborder, vborder);

      meta_kms_update_set_underscanning (kms_update,
                                         kms_connector,
                                         hborder, vborder);
    }
  else
    {
      g_debug ("Unsetting underscan of connector %s",
               meta_kms_connector_get_name (kms_connector));

      meta_kms_update_unset_underscanning (kms_update,
                                           kms_connector);
    }
}

static void
set_max_bpc (MetaOutputKms *output_kms,
             MetaKmsUpdate *kms_update)
{
  MetaOutput *output = META_OUTPUT (output_kms);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  MetaKmsConnector *kms_connector =
    meta_output_kms_get_kms_connector (output_kms);
  unsigned int max_bpc;

  if (!meta_output_get_max_bpc (output, &max_bpc))
    return;

  if (output_info->max_bpc_min == 0 && output_info->max_bpc_max == 0)
    return;

  if (max_bpc < output_info->max_bpc_min || max_bpc > output_info->max_bpc_max)
    {
      g_warning ("Ignoring out of range value %u for max bpc (%u-%u)",
                 max_bpc,
                 (unsigned) output_info->max_bpc_min,
                 (unsigned) output_info->max_bpc_max);
      return;
    }

  meta_kms_update_set_max_bpc (kms_update, kms_connector, max_bpc);
}

static void
set_rgb_range (MetaOutputKms *output_kms,
               MetaKmsUpdate *kms_update)
{
  MetaOutput *output = META_OUTPUT (output_kms);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  MetaKmsConnector *kms_connector =
    meta_output_kms_get_kms_connector (output_kms);
  MetaOutputRGBRange rgb_range = meta_output_peek_rgb_range (output);

  if (rgb_range == META_OUTPUT_RGB_RANGE_AUTO &&
      !(output_info->supported_rgb_ranges & (1 << rgb_range)))
    return;

  if (!(output_info->supported_rgb_ranges & (1 << rgb_range)))
    {
      g_warning ("Ignoring unsupported RGB Range");
      return;
    }

  meta_kms_update_set_broadcast_rgb (kms_update, kms_connector, rgb_range);
}

static void
set_color_mode (MetaOutputKms *output_kms,
                MetaKmsUpdate *kms_update)
{
  MetaOutput *output = META_OUTPUT (output_kms);
  MetaKmsConnector *kms_connector =
    meta_output_kms_get_kms_connector (output_kms);
  MetaOutputHdrMetadata hdr_metadata;
  MetaOutputColorspace color_space;


  meta_output_get_color_metadata (output, &hdr_metadata, &color_space);

  if (meta_kms_connector_supports_colorspace (kms_connector))
    meta_kms_update_set_color_space (kms_update, kms_connector, color_space);

  if (meta_kms_connector_supports_hdr_metadata (kms_connector))
    meta_kms_update_set_hdr_metadata (kms_update, kms_connector, &hdr_metadata);
}

static void
meta_onscreen_native_set_crtc_mode (CoglOnscreen              *onscreen,
                                    MetaKmsUpdate             *kms_update,
                                    MetaRendererNativeGpuData *renderer_gpu_data)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);

  COGL_TRACE_BEGIN_SCOPED (MetaOnscreenNativeSetCrtcModes,
                           "Meta::OnscreenNative::set_crtc_mode()");

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      {
        MetaDrmBuffer *buffer;
        graphene_rect_t src_rect;
        MtkRectangle dst_rect;

        buffer = META_DRM_BUFFER (onscreen_native->egl.dumb_fb);

        src_rect = (graphene_rect_t) {
          .origin.x = 0,
          .origin.y = 0,
          .size.width = meta_drm_buffer_get_width (buffer),
          .size.height = meta_drm_buffer_get_height (buffer)
        };

        dst_rect = (MtkRectangle) {
          .x = 0,
          .y = 0,
          .width = meta_drm_buffer_get_width (buffer),
          .height = meta_drm_buffer_get_height (buffer)
        };

        assign_primary_plane (crtc_kms,
                              buffer,
                              kms_update,
                              META_KMS_ASSIGN_PLANE_FLAG_NONE,
                              &src_rect,
                              &dst_rect);
        break;
      }
#endif
    }

  meta_crtc_kms_set_mode (crtc_kms, kms_update);
  set_underscan (META_OUTPUT_KMS (onscreen_native->output), kms_update);
  set_max_bpc (META_OUTPUT_KMS (onscreen_native->output), kms_update);
  set_rgb_range (META_OUTPUT_KMS (onscreen_native->output), kms_update);
  set_color_mode (META_OUTPUT_KMS (onscreen_native->output), kms_update);
}

static void
secondary_gpu_release_dumb (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  unsigned i;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    g_clear_object (&secondary_gpu_state->cpu.dumb_fbs[i]);
}

static void
secondary_gpu_state_free (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaGpu *gpu = META_GPU (secondary_gpu_state->gpu_kms);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaEgl *egl = meta_backend_get_egl (backend);

  if (secondary_gpu_state->egl_surface != EGL_NO_SURFACE)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;
      MetaRenderDevice *render_device;
      EGLDisplay egl_display;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      render_device = renderer_gpu_data->render_device;
      egl_display = meta_render_device_get_egl_display (render_device);
      meta_egl_destroy_surface (egl,
                                egl_display,
                                secondary_gpu_state->egl_surface,
                                NULL);
    }

  g_clear_pointer (&secondary_gpu_state->gbm.surface, gbm_surface_destroy);

  secondary_gpu_release_dumb (secondary_gpu_state);

  g_free (secondary_gpu_state);
}

static MetaDrmBuffer *
import_shared_framebuffer (CoglOnscreen                        *onscreen,
                           MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                           MetaDrmBuffer                       *primary_gpu_fb)
{
  MetaRenderDevice *render_device;
  g_autoptr (GError) error = NULL;
  MetaDrmBuffer *imported_buffer;

  render_device = secondary_gpu_state->renderer_gpu_data->render_device;
  imported_buffer =
    meta_render_device_import_dma_buf (render_device,
                                       primary_gpu_fb,
                                       &error);
  if (!imported_buffer)
    {
      g_warning ("Zero-copy disabled for %s, import failed: %s",
                 meta_render_device_get_name (render_device),
                 error->message);
      secondary_gpu_state->import_status =
        META_SHARED_FRAMEBUFFER_IMPORT_STATUS_FAILED;
      return NULL;
    }

  if (secondary_gpu_state->import_status ==
      META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE)
    {
      meta_topic (META_DEBUG_KMS,
                  "Using zero-copy for %s succeeded once.",
                  meta_render_device_get_name (render_device));
    }

  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK;
  return imported_buffer;
}

static MetaDrmBuffer *
copy_shared_framebuffer_gpu (CoglOnscreen                         *onscreen,
                             MetaOnscreenNativeSecondaryGpuState  *secondary_gpu_state,
                             MetaRendererNativeGpuData            *renderer_gpu_data,
                             MetaDrmBuffer                        *primary_gpu_fb,
                             GError                              **error)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  MetaGles3 *gles3 = meta_renderer_native_get_gles3 (renderer_native);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  MetaRenderDevice *render_device;
  EGLDisplay egl_display;
  gboolean use_modifiers;
  MetaDeviceFile *device_file;
  MetaDrmBufferFlags flags;
  MetaDrmBufferGbm *buffer_gbm = NULL;
  struct gbm_bo *bo;
  EGLSync egl_sync = EGL_NO_SYNC;
  g_autofd int sync_fd = -1;
  EGLImageKHR egl_image;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferSecondaryGpu,
                           "copy_shared_framebuffer_gpu()");

  if (renderer_gpu_data->secondary.needs_explicit_sync)
    sync_fd = cogl_context_get_latest_sync_fd (cogl_context);

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);

  if (!meta_egl_make_current (egl,
                              egl_display,
                              secondary_gpu_state->egl_surface,
                              secondary_gpu_state->egl_surface,
                              renderer_gpu_data->secondary.egl_context,
                              error))
    {
      g_prefix_error (error, "Failed to make current: ");
      goto done;
    }

  if (sync_fd >= 0)
    {
      EGLAttrib attribs[3];

      attribs[0] = EGL_SYNC_NATIVE_FENCE_FD_ANDROID;
      attribs[1] = g_steal_fd (&sync_fd);
      attribs[2] = EGL_NONE;

      if (!meta_egl_create_sync (egl,
                                 egl_display,
                                 EGL_SYNC_NATIVE_FENCE_ANDROID,
                                 attribs,
                                 &egl_sync,
                                 error))
        {
          g_prefix_error (error, "Failed to create EGLSync on secondary GPU: ");
          goto done;
        }

      if (!meta_egl_wait_sync (egl,
                               egl_display,
                               egl_sync,
                               0,
                               error))
        {
          g_prefix_error (error, "Failed to wait for EGLSync on secondary GPU: ");
          goto done;
        }
    }

  buffer_gbm = META_DRM_BUFFER_GBM (primary_gpu_fb);
  bo = meta_drm_buffer_gbm_get_bo (buffer_gbm);
  egl_image = meta_egl_ensure_gbm_bo_egl_image (egl, egl_display, bo, error);

  if (!egl_image)
    {
      g_prefix_error (error, "Failed to create EGL image from buffer object for secondary GPU: ");
      goto done;
    }

  if (!meta_renderer_native_gles3_blit_shared_bo (egl,
                                                  gles3,
                                                  egl_display,
                                                  renderer_gpu_data->secondary.egl_context,
                                                  egl_image,
                                                  bo,
                                                  error))
    {
      g_prefix_error (error, "Failed to blit shared framebuffer: ");
      goto done;
    }

  if (!meta_egl_swap_buffers (egl,
                              egl_display,
                              secondary_gpu_state->egl_surface,
                              error))
    {
      g_prefix_error (error, "Failed to swap buffers: ");
      goto done;
    }

  use_modifiers = meta_renderer_native_use_modifiers (renderer_native);
  device_file = meta_render_device_get_device_file (render_device);

  flags = META_DRM_BUFFER_FLAG_NONE;
  if (!use_modifiers)
    flags |= META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;

  buffer_gbm =
    meta_drm_buffer_gbm_new_lock_front (device_file,
                                        secondary_gpu_state->gbm.surface,
                                        flags,
                                        error);
  if (!buffer_gbm)
    {
      g_prefix_error (error, "meta_drm_buffer_gbm_new_lock_front failed: ");
      goto done;
    }

  g_object_set_qdata_full (G_OBJECT (buffer_gbm),
                           blit_source_quark,
                           g_object_ref (primary_gpu_fb),
                           g_object_unref);

done:
  if (egl_sync != EGL_NO_SYNC)
    {
      g_autoptr (GError) local_error = NULL;

      if (!meta_egl_destroy_sync (egl,
                                  egl_display,
                                  egl_sync,
                                  &local_error))
        g_warning ("Failed to destroy secondary GPU EGLSync: %s", local_error->message);
    }

  _cogl_winsys_egl_ensure_current (cogl_display);

  return buffer_gbm ? META_DRM_BUFFER (buffer_gbm) : NULL;
}

static MetaDrmBufferDumb *
secondary_gpu_get_next_dumb_buffer (MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state)
{
  MetaDrmBufferDumb *current_dumb_fb;
  const int n_dumb_fbs = G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs);
  int i;

  current_dumb_fb = secondary_gpu_state->cpu.current_dumb_fb;
  for (i = 0; i < n_dumb_fbs; i++)
    {
      if (current_dumb_fb == secondary_gpu_state->cpu.dumb_fbs[i])
        return secondary_gpu_state->cpu.dumb_fbs[(i + 1) % n_dumb_fbs];
    }

  return secondary_gpu_state->cpu.dumb_fbs[0];
}

static MetaDrmBuffer *
copy_shared_framebuffer_primary_gpu (CoglOnscreen                        *onscreen,
                                     MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                                     const MtkRegion                     *region)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpuKms *primary_gpu;
  MetaRendererNativeGpuData *primary_gpu_data;
  MetaDrmBufferDumb *buffer_dumb;
  MetaDrmBuffer *buffer;
  int width, height;
  CoglFramebuffer *dmabuf_fb;
  int dmabuf_fd;
  g_autoptr (GError) error = NULL;
  const MetaFormatInfo *format_info;
  uint32_t stride;
  uint32_t offset;
  uint32_t drm_format;
  uint64_t modifier;
  int n_rectangles;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferPrimaryGpu,
                           "copy_shared_framebuffer_primary_gpu()");

  if (!secondary_gpu_state)
    return NULL;

  primary_gpu = meta_renderer_native_get_primary_gpu (renderer_native);
  primary_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native, primary_gpu);
  if (!primary_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers)
    return NULL;

  buffer_dumb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);
  buffer = META_DRM_BUFFER (buffer_dumb);

  width = meta_drm_buffer_get_width (buffer);
  height = meta_drm_buffer_get_height (buffer);
  stride = meta_drm_buffer_get_stride (buffer);
  offset = 0;
  modifier = DRM_FORMAT_MOD_LINEAR;
  drm_format = meta_drm_buffer_get_format (buffer);

  g_assert (cogl_framebuffer_get_width (framebuffer) == width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == height);

  format_info = meta_format_info_from_drm_format (drm_format);
  g_assert (format_info);

  dmabuf_fd = meta_drm_buffer_dumb_ensure_dmabuf_fd (buffer_dumb, &error);
  if (dmabuf_fd < 0)
    {
      meta_topic (META_DEBUG_KMS,
                  "Failed to create DMA buffer: %s", error->message);
      return NULL;
    }

  modifier = DRM_FORMAT_MOD_LINEAR;
  dmabuf_fb =
    meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                     width,
                                                     height,
                                                     drm_format,
                                                     1,
                                                     &dmabuf_fd,
                                                     &stride,
                                                     &offset,
                                                     &modifier,
                                                     &error);

  if (error)
    {
      meta_topic (META_DEBUG_KMS,
                  "Failed to create DMA buffer for blitting: %s",
                  error->message);
      return NULL;
    }
  /* Limit the number of individual copies to 16 */
#define MAX_RECTS 16

  n_rectangles = mtk_region_num_rectangles (region);
  if (n_rectangles == 0 || n_rectangles > MAX_RECTS)
    {
      if (!cogl_framebuffer_blit (framebuffer, COGL_FRAMEBUFFER (dmabuf_fb),
                                  0, 0, 0, 0,
                                  width, height,
                                  &error))
        {
          g_object_unref (dmabuf_fb);
          return NULL;
        }
    }
  else
    {
      int i;

      for (i = 0; i < n_rectangles; ++i)
        {
          MtkRectangle rectangle = mtk_region_get_rectangle (region, i);

          if (!cogl_framebuffer_blit (framebuffer, COGL_FRAMEBUFFER (dmabuf_fb),
                                      rectangle.x, rectangle.y,
                                      rectangle.x, rectangle.y,
                                      rectangle.width, rectangle.height,
                                      &error))
            {
              g_object_unref (dmabuf_fb);
              return NULL;
            }
        }
    }

  g_object_set_qdata_full (G_OBJECT (buffer),
                           blit_source_quark,
                           g_steal_pointer (&dmabuf_fb),
                           g_object_unref);

  secondary_gpu_state->cpu.current_dumb_fb = buffer_dumb;

  return g_object_ref (buffer);
}

static MetaDrmBuffer *
copy_shared_framebuffer_cpu (CoglOnscreen                        *onscreen,
                             MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state,
                             MetaRendererNativeGpuData           *renderer_gpu_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  MetaDrmBufferDumb *buffer_dumb;
  MetaDrmBuffer *buffer;
  int width, height, stride;
  uint32_t drm_format;
  void *buffer_data;
  CoglBitmap *dumb_bitmap;
  CoglPixelFormat cogl_format;
  const MetaFormatInfo *format_info;

  COGL_TRACE_BEGIN_SCOPED (CopySharedFramebufferCpu,
                           "copy_shared_framebuffer_cpu()");

  buffer_dumb = secondary_gpu_get_next_dumb_buffer (secondary_gpu_state);
  buffer = META_DRM_BUFFER (buffer_dumb);

  width = meta_drm_buffer_get_width (buffer);
  height = meta_drm_buffer_get_height (buffer);
  stride = meta_drm_buffer_get_stride (buffer);
  drm_format = meta_drm_buffer_get_format (buffer);
  buffer_data = meta_drm_buffer_dumb_get_data (buffer_dumb);

  g_assert (cogl_framebuffer_get_width (framebuffer) == width);
  g_assert (cogl_framebuffer_get_height (framebuffer) == height);

  format_info = meta_format_info_from_drm_format (drm_format);
  g_assert (format_info);
  cogl_format = format_info->cogl_format;

  dumb_bitmap = cogl_bitmap_new_for_data (cogl_context,
                                          width,
                                          height,
                                          cogl_format,
                                          stride,
                                          buffer_data);

  if (!cogl_framebuffer_read_pixels_into_bitmap (framebuffer,
                                                 0 /* x */,
                                                 0 /* y */,
                                                 COGL_READ_PIXELS_COLOR_BUFFER,
                                                 dumb_bitmap))
    g_warning ("Failed to CPU-copy to a secondary GPU output");

  g_object_unref (dumb_bitmap);

  secondary_gpu_state->cpu.current_dumb_fb = buffer_dumb;

  return g_object_ref (buffer);
}

static MetaDrmBuffer *
update_secondary_gpu_state_pre_swap_buffers (CoglOnscreen    *onscreen,
                                             const MtkRegion *region)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaDrmBuffer *copy = NULL;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePreSwapBuffers,
                           "update_secondary_gpu_state_pre_swap_buffers()");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *renderer_gpu_data;
      MetaRenderDevice *render_device;

      renderer_gpu_data = secondary_gpu_state->renderer_gpu_data;
      render_device = renderer_gpu_data->render_device;
      switch (renderer_gpu_data->secondary.copy_mode)
        {
        case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
          /* Done after eglSwapBuffers. */
          break;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
          /* Done after eglSwapBuffers. */
          if (secondary_gpu_state->import_status ==
              META_SHARED_FRAMEBUFFER_IMPORT_STATUS_OK)
            break;
          /* prepare fallback */
          G_GNUC_FALLTHROUGH;
        case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
          if (!renderer_gpu_data->secondary.copy_mode_primary_force_cpu)
            {
              copy = copy_shared_framebuffer_primary_gpu (onscreen,
                                                          secondary_gpu_state,
                                                          region);
            }

          if (!copy)
            {
              if (!secondary_gpu_state->noted_primary_gpu_copy_failed &&
                  !renderer_gpu_data->secondary.copy_mode_primary_force_cpu)
                {
                  meta_topic (META_DEBUG_KMS,
                              "Using primary GPU to copy for %s failed once.",
                              meta_render_device_get_name (render_device));
                  secondary_gpu_state->noted_primary_gpu_copy_failed = TRUE;
                }

              copy = copy_shared_framebuffer_cpu (onscreen,
                                                  secondary_gpu_state,
                                                  renderer_gpu_data);
            }
          else if (!secondary_gpu_state->noted_primary_gpu_copy_ok)
            {
              meta_topic (META_DEBUG_KMS,
                          "Using primary GPU to copy for %s succeeded once.",
                          meta_render_device_get_name (render_device));
              secondary_gpu_state->noted_primary_gpu_copy_ok = TRUE;
            }
          break;
        }
    }

  return copy;
}

static MetaDrmBuffer *
acquire_front_buffer (CoglOnscreen   *onscreen,
                      MetaDrmBuffer  *primary_gpu_fb,
                      MetaDrmBuffer  *secondary_gpu_fb,
                      GError        **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaDrmBuffer *imported_fb;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeGpuStatePostSwapBuffers,
                           "acquire_front_buffer()");

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (!secondary_gpu_state)
    return g_object_ref (primary_gpu_fb);

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       secondary_gpu_state->gpu_kms);
  switch (renderer_gpu_data->secondary.copy_mode)
    {
    case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
      imported_fb = import_shared_framebuffer (onscreen,
                                               secondary_gpu_state,
                                               primary_gpu_fb);
      if (imported_fb)
        return imported_fb;
      /* The fallback was prepared in pre_swap_buffers and is currently
       * in secondary_gpu_fb.
       */
      renderer_gpu_data->secondary.copy_mode =
        META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY;
      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
      if (secondary_gpu_fb == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Missing secondary GPU framebuffer");
          return NULL;
        }
      return g_object_ref (secondary_gpu_fb);
    case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
      return copy_shared_framebuffer_gpu (onscreen,
                                          secondary_gpu_state,
                                          renderer_gpu_data,
                                          primary_gpu_fb,
                                          error);
    }

  g_assert_not_reached ();
  return NULL;
}

static void
ensure_crtc_modes (CoglOnscreen  *onscreen,
                   MetaKmsUpdate *kms_update)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  if (meta_renderer_native_pop_pending_mode_set (renderer_native,
                                                 onscreen_native->view))
    meta_onscreen_native_set_crtc_mode (onscreen, kms_update, renderer_gpu_data);
}

static void
swap_buffer_result_feedback (const MetaKmsFeedback *kms_feedback,
                             gpointer               user_data)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (user_data);
  const GError *error;
  CoglFrameInfo *frame_info;

  /*
   * Page flipping failed, but we want to fail gracefully, so to avoid freezing
   * the frame clock, emit a symbolic flip.
   */

  error = meta_kms_feedback_get_error (kms_feedback);
  if (!error)
    return;

  if (!g_error_matches (error,
                        META_KMS_ERROR,
                        META_KMS_ERROR_DISCARDED) &&
      !g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED))
    g_warning ("Page flip failed: %s", error->message);

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);

  /* After resuming from suspend, clear_superseded_frame might have done this
   * already and emptied the frame_info queue.
   */
  if (frame_info)
    {
      frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;
      meta_onscreen_native_notify_frame_complete (onscreen);
    }

  meta_onscreen_native_clear_posted_fb (onscreen);
}

static void
assign_next_frame (MetaOnscreenNative *onscreen_native,
                   ClutterFrame       *frame)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (onscreen_native);

  if (onscreen_native->next_frame != NULL)
    {
      clear_superseded_frame (onscreen);
      onscreen_native->superseded_frame =
        g_steal_pointer (&onscreen_native->next_frame);
    }

  onscreen_native->next_frame = clutter_frame_ref (frame);
}

static const MetaKmsResultListenerVtable swap_buffer_result_listener_vtable = {
  .feedback = swap_buffer_result_feedback,
};

static void
scanout_result_feedback (const MetaKmsFeedback *kms_feedback,
                         gpointer               user_data);

static const MetaKmsResultListenerVtable scanout_result_listener_vtable = {
  .feedback = scanout_result_feedback,
};

static void
meta_onscreen_native_swap_buffers_with_damage (CoglOnscreen    *onscreen,
                                               const MtkRegion *region,
                                               CoglFrameInfo   *frame_info,
                                               gpointer         user_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  MetaDeviceFile *render_device_file;
  ClutterFrame *frame = user_data;
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);
  CoglOnscreenClass *parent_class;
  gboolean secondary_gpu_used = FALSE;
  g_autoptr (GError) error = NULL;
  MetaDrmBufferFlags buffer_flags;
  MetaDrmBufferGbm *buffer_gbm;
  g_autoptr (MetaDrmBuffer) primary_gpu_fb = NULL;
  g_autoptr (MetaDrmBuffer) secondary_gpu_fb = NULL;
  g_autoptr (MetaDrmBuffer) buffer = NULL;

  COGL_TRACE_BEGIN_SCOPED (MetaRendererNativeSwapBuffers,
                           "Meta::OnscreenNative::swap_buffers_with_damage()");

  secondary_gpu_fb =
    update_secondary_gpu_state_pre_swap_buffers (onscreen, region);

  secondary_gpu_state = onscreen_native->secondary_gpu_state;
  if (secondary_gpu_state)
    {
      MetaRendererNativeGpuData *secondary_gpu_data;

      secondary_gpu_data =
        meta_renderer_native_get_gpu_data (renderer_native,
                                           secondary_gpu_state->gpu_kms);
      secondary_gpu_used =
        secondary_gpu_data->secondary.copy_mode ==
        META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU;
    }

  if (!secondary_gpu_used)
    cogl_onscreen_egl_maybe_create_timestamp_query (onscreen, frame_info);

  parent_class = COGL_ONSCREEN_CLASS (meta_onscreen_native_parent_class);
  parent_class->swap_buffers_with_damage (onscreen,
                                          region,
                                          frame_info,
                                          user_data);

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);
  render_device_file =
    meta_render_device_get_device_file (renderer_gpu_data->render_device);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      buffer_flags = META_DRM_BUFFER_FLAG_NONE;
      if (!meta_renderer_native_use_modifiers (renderer_native))
        buffer_flags |= META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;

      buffer_gbm =
        meta_drm_buffer_gbm_new_lock_front (render_device_file,
                                            onscreen_native->gbm.surface,
                                            buffer_flags,
                                            &error);
      if (!buffer_gbm)
        {
          g_warning ("Failed to lock front buffer on %s: %s",
                     meta_device_file_get_path (render_device_file),
                     error->message);
          goto swap_failed;
        }

      primary_gpu_fb = META_DRM_BUFFER (g_steal_pointer (&buffer_gbm));
      buffer = acquire_front_buffer (onscreen,
                                     primary_gpu_fb,
                                     secondary_gpu_fb,
                                     &error);
      if (buffer == NULL)
        {
          g_warning ("Failed to acquire front buffer: %s", error->message);
          goto swap_failed;
        }

      meta_frame_native_set_buffer (frame_native, buffer);

      if (!meta_drm_buffer_ensure_fb_id (buffer, &error))
        {
          g_warning ("Failed to ensure KMS FB ID on %s: %s",
                     meta_device_file_get_path (render_device_file),
                     error->message);
          goto swap_failed;
        }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      break;
#endif
    }

  assign_next_frame (onscreen_native, frame);

  clutter_frame_set_result (frame,
                            CLUTTER_FRAME_RESULT_PENDING_PRESENTED);

  meta_frame_native_set_damage (frame_native, region);

  if (!secondary_gpu_used)
    {
      int sync_fd = cogl_context_get_latest_sync_fd (cogl_context);

      meta_frame_native_set_sync_fd (frame_native, g_steal_fd (&sync_fd));
    }

  maybe_post_next_frame (onscreen);
  return;

swap_failed:
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;
  meta_onscreen_native_notify_frame_complete (onscreen);
  clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
}

static void
maybe_post_next_frame (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOutputKms *output_kms = META_OUTPUT_KMS (onscreen_native->output);
  MetaKmsConnector *kms_connector =
    meta_output_kms_get_kms_connector (output_kms);
  MetaPowerSave power_save_mode;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsUpdate *kms_update;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  g_autoptr (ClutterFrame) frame = NULL;
  MetaFrameNative *frame_native;
  MtkRegion *region;
  int sync_fd;
  const MetaKmsResultListenerVtable *listener;
  MetaKmsAssignPlaneFlag flip_flags;
  gboolean is_direct_scanout;
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);

  COGL_TRACE_SCOPED_ANCHOR (MetaRendererNativePostKmsUpdate);

  if (onscreen_native->next_frame == NULL ||
      onscreen_native->posted_frame != NULL ||
      onscreen_native->view == NULL ||
      meta_kms_is_shutting_down (kms))
    return;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode != META_POWER_SAVE_ON)
    {
      meta_renderer_native_queue_power_save_page_flip (renderer_native,
                                                       onscreen);
      return;
    }

  frame = g_steal_pointer (&onscreen_native->next_frame);
  frame_native = meta_frame_native_from_frame (frame);
  region = meta_frame_native_get_damage (frame_native);

  clear_superseded_frame (onscreen);

  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (onscreen_native->crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  kms_update = meta_frame_native_ensure_kms_update (frame_native,
                                                    kms_device);

  is_direct_scanout = meta_frame_native_get_scanout (frame_native) != NULL;
  if (is_direct_scanout)
    {
      listener = &scanout_result_listener_vtable;
      flip_flags = META_KMS_ASSIGN_PLANE_FLAG_DISABLE_IMPLICIT_SYNC;
    }
  else
    {
      listener = &swap_buffer_result_listener_vtable;
      flip_flags = META_KMS_ASSIGN_PLANE_FLAG_NONE;
    }

  meta_kms_update_add_result_listener (kms_update,
                                       listener,
                                       NULL,
                                       onscreen_native,
                                       NULL);

  ensure_crtc_modes (onscreen, kms_update);
  if (!meta_onscreen_native_flip_crtc (onscreen,
                                       frame,
                                       onscreen_native->view,
                                       onscreen_native->crtc,
                                       kms_update,
                                       flip_flags,
                                       region))
    {
      kms_update = meta_frame_native_steal_kms_update (frame_native);
      post_nonprimary_plane_update (onscreen_native, frame, kms_update);
      onscreen_native->posted_frame = clutter_frame_ref (frame);
      return;
    }

  onscreen_native->posted_frame = clutter_frame_ref (frame);

  COGL_TRACE_BEGIN_ANCHORED (MetaRendererNativePostKmsUpdate,
                             "Meta::OnscreenNative::maybe_post_next_frame#post_pending_update()");

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (meta_renderer_native_has_pending_mode_sets (renderer_native))
        {
          meta_topic (META_DEBUG_KMS,
                      "Postponing primary plane composite update for CRTC %u (%s) to %s",
                      meta_kms_crtc_get_id (kms_crtc),
                      meta_kms_device_get_path (kms_device),
                      meta_kms_connector_get_name (kms_connector));

          kms_update = meta_frame_native_steal_kms_update (frame_native);
          meta_renderer_native_queue_mode_set_update (renderer_native,
                                                      kms_update);
          return;
        }
      else if (meta_renderer_native_has_pending_mode_set (renderer_native))
        {
          meta_topic (META_DEBUG_KMS, "Posting global mode set updates on %s",
                      meta_kms_device_get_path (kms_device));

          kms_update = meta_frame_native_steal_kms_update (frame_native);
          meta_renderer_native_queue_mode_set_update (renderer_native,
                                                      kms_update);

          meta_frame_native_steal_kms_update (frame_native);
          meta_renderer_native_post_mode_set_updates (renderer_native);
          return;
        }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      if (meta_renderer_native_has_pending_mode_set (renderer_native))
        {
          kms_update = meta_frame_native_steal_kms_update (frame_native);
          meta_renderer_native_queue_mode_set_update (renderer_native,
                                                      kms_update);

          meta_renderer_native_post_mode_set_updates (renderer_native);
          return;
        }
      break;
#endif
    }

  meta_topic (META_DEBUG_KMS,
              "Posting primary plane %s update for CRTC %u (%s) to %s",
              is_direct_scanout ? "direct scanout" : "composite",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device),
              meta_kms_connector_get_name (kms_connector));

  kms_update = meta_frame_native_steal_kms_update (frame_native);

  sync_fd = meta_frame_native_steal_sync_fd (frame_native);
  if (sync_fd >= 0)
    meta_kms_update_set_sync_fd (kms_update, g_steal_fd (&sync_fd));

  meta_kms_device_post_update (kms_device, kms_update,
                               META_KMS_UPDATE_FLAG_NONE);
}

gboolean
meta_onscreen_native_is_buffer_scanout_compatible (CoglOnscreen *onscreen,
                                                   CoglScanout  *scanout)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaGpuKms *gpu_kms;
  MetaKmsDevice *kms_device;
  MetaKmsCrtc *kms_crtc;
  MetaKmsUpdate *test_update;
  MetaDrmBuffer *buffer;
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
  MetaKmsFeedbackResult result;
  graphene_rect_t src_rect;
  MtkRectangle dst_rect;

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);

  test_update = meta_kms_update_new (kms_device);

  cogl_scanout_get_src_rect (scanout, &src_rect);
  cogl_scanout_get_dst_rect (scanout, &dst_rect);

  buffer = META_DRM_BUFFER (cogl_scanout_get_buffer (scanout));
  assign_primary_plane (crtc_kms,
                        buffer,
                        test_update,
                        META_KMS_ASSIGN_PLANE_FLAG_DISABLE_IMPLICIT_SYNC,
                        &src_rect,
                        &dst_rect);

  meta_topic (META_DEBUG_KMS,
              "Posting direct scanout test update for CRTC %u (%s) synchronously",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device));

  kms_feedback =
    meta_kms_device_process_update_sync (kms_device, test_update,
                                         META_KMS_UPDATE_FLAG_TEST_ONLY);

  result = meta_kms_feedback_get_result (kms_feedback);
  return result == META_KMS_FEEDBACK_PASSED;
}

static void
scanout_result_feedback (const MetaKmsFeedback *kms_feedback,
                         gpointer               user_data)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (user_data);
  CoglOnscreen *onscreen = COGL_ONSCREEN (onscreen_native);
  const GError *error;
  CoglFrameInfo *frame_info;

  error = meta_kms_feedback_get_error (kms_feedback);
  if (!error)
    return;

  if (!g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED))
    {
      ClutterStageView *view = CLUTTER_STAGE_VIEW (onscreen_native->view);
      ClutterFrame *posted_frame = onscreen_native->posted_frame;
      MetaFrameNative *posted_frame_native =
        meta_frame_native_from_frame (posted_frame);
      CoglScanout *scanout =
        meta_frame_native_get_scanout (posted_frame_native);

      g_warning ("Direct scanout page flip failed: %s", error->message);

      cogl_scanout_notify_failed (scanout, onscreen);
      if (onscreen_native->next_frame == NULL && view != NULL)
        {
          clutter_stage_view_add_redraw_clip (view, NULL);
          clutter_stage_view_schedule_update_now (view);
        }
    }

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  meta_onscreen_native_notify_frame_complete (onscreen);
  meta_onscreen_native_clear_posted_fb (onscreen);
}

static gboolean
meta_onscreen_native_direct_scanout (CoglOnscreen   *onscreen,
                                     CoglScanout    *scanout,
                                     CoglFrameInfo  *frame_info,
                                     gpointer        user_data,
                                     GError        **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaGpuKms *render_gpu = onscreen_native->render_gpu;
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  ClutterFrame *frame = user_data;
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         render_gpu);

  g_warn_if_fail (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_GBM);

  assign_next_frame (onscreen_native, frame);

  meta_frame_native_set_scanout (frame_native, scanout);
  meta_frame_native_set_buffer (frame_native,
                                META_DRM_BUFFER (cogl_scanout_get_buffer (scanout)));

  frame_info->cpu_time_before_buffer_swap_us = g_get_monotonic_time ();

  if (cogl_context_has_feature (cogl_context, COGL_FEATURE_ID_TIMESTAMP_QUERY))
    frame_info->has_valid_gpu_rendering_duration = TRUE;

  clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_PENDING_PRESENTED);

  maybe_post_next_frame (onscreen);
  return TRUE;
}

static gboolean
meta_onscreen_native_get_window_handles (CoglOnscreen *onscreen,
                                         gpointer     *device_out,
                                         gpointer     *window_out)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *cogl_display_egl = cogl_context->display->winsys;
  gpointer window = NULL;

  if (onscreen_native->gbm.surface)
    window = onscreen_native->gbm.surface;
#ifdef HAVE_EGL_DEVICE
  else if (onscreen_native->egl.stream)
    window = onscreen_native->egl.stream;
#endif

  if (!window)
    return FALSE;

  *device_out = cogl_display_egl->egl_context;
  *window_out = window;
  return TRUE;
}

static void
add_onscreen_frame_info (MetaCrtc     *crtc,
                         ClutterFrame *frame)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterStageWindow *stage_window = _clutter_stage_get_window (stage);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererView *view = meta_renderer_get_view_for_crtc (renderer, crtc);

  meta_stage_impl_add_onscreen_frame_info (META_STAGE_IMPL (stage_window),
                                           CLUTTER_STAGE_VIEW (view),
                                           frame);
}

void
meta_onscreen_native_request_frame_sync (MetaOnscreenNative *onscreen_native,
                                         gboolean            enabled)
{
  onscreen_native->frame_sync_requested = enabled;
}

gboolean
meta_onscreen_native_is_frame_sync_enabled (MetaOnscreenNative *onscreen_native)
{
  return onscreen_native->frame_sync_enabled;
}

static void
maybe_update_frame_sync (MetaOnscreenNative *onscreen_native,
                         ClutterFrame       *frame)
{
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  const MetaKmsCrtcState *crtc_state =
    meta_kms_crtc_get_current_state (kms_crtc);
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (onscreen_native->view);
  ClutterFrameClock *frame_clock =
    clutter_stage_view_get_frame_clock (stage_view);
  ClutterFrameClockMode frame_clock_mode;
  MetaKmsUpdate *kms_update;
  gboolean frame_sync_enabled = FALSE;

  if (meta_output_is_vrr_enabled (onscreen_native->output))
    frame_sync_enabled = onscreen_native->frame_sync_requested;

  if (frame_sync_enabled != onscreen_native->frame_sync_enabled)
    {
      frame_clock_mode = frame_sync_enabled ? CLUTTER_FRAME_CLOCK_MODE_VARIABLE :
                                              CLUTTER_FRAME_CLOCK_MODE_FIXED;
      clutter_frame_clock_set_mode (frame_clock, frame_clock_mode);
      onscreen_native->frame_sync_enabled = frame_sync_enabled;
    }

  if (crtc_state->vrr.supported &&
      frame_sync_enabled != crtc_state->vrr.enabled)
    {
      kms_update = meta_frame_native_ensure_kms_update (frame_native, kms_device);
      meta_kms_update_set_vrr (kms_update, kms_crtc, frame_sync_enabled);
    }
}

void
meta_onscreen_native_before_redraw (CoglOnscreen *onscreen,
                                    ClutterFrame *frame)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  if (meta_get_debug_paint_flags () & META_DEBUG_PAINT_SYNC_CURSOR_PRIMARY)
    {
      MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
      MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);

      meta_kms_device_await_flush (meta_kms_crtc_get_device (kms_crtc), kms_crtc);
    }

  maybe_update_frame_sync (onscreen_native, frame);
}

void
meta_onscreen_native_prepare_frame (CoglOnscreen *onscreen,
                                    ClutterFrame *frame)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaOutputKms *output_kms = META_OUTPUT_KMS (onscreen_native->output);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);;
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);
  int64_t target_frame_counter = cogl_onscreen_get_frame_counter (onscreen);

  if (onscreen_native->property.gamma_lut.invalidated)
    {
      const MetaGammaLut *gamma;
      MetaKmsUpdate *kms_update;

      kms_update = meta_frame_native_ensure_kms_update (frame_native,
                                                        kms_device);

      gamma = meta_crtc_kms_peek_gamma_lut (crtc_kms);
      meta_kms_update_set_crtc_gamma (kms_update, kms_crtc, gamma);
      onscreen_native->property.gamma_lut.invalidated = FALSE;
      onscreen_native->property.gamma_lut.target_frame_counter =
        target_frame_counter;
    }

  if (onscreen_native->property.privacy_screen.invalidated)
    {
      MetaKmsConnector *kms_connector =
        meta_output_kms_get_kms_connector (output_kms);
      MetaKmsUpdate *kms_update;
      gboolean enabled;

      kms_update = meta_frame_native_ensure_kms_update (frame_native,
                                                        kms_device);

      enabled = meta_output_is_privacy_screen_enabled (onscreen_native->output);
      meta_kms_update_set_privacy_screen (kms_update, kms_connector, enabled);
      onscreen_native->property.privacy_screen.invalidated = FALSE;
      onscreen_native->property.privacy_screen.target_frame_counter =
        target_frame_counter;
    }
}

static void
finish_frame_result_feedback (const MetaKmsFeedback *kms_feedback,
                              gpointer               user_data)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (user_data);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  const GError *error;
  CoglFrameInfo *frame_info;

  error = meta_kms_feedback_get_error (kms_feedback);
  if (!error)
    return;

  if (!g_error_matches (error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED) &&
      !g_error_matches (error,
                        META_KMS_ERROR,
                        META_KMS_ERROR_EMPTY_UPDATE))
    g_warning ("Cursor update failed: %s", error->message);

  frame_info = cogl_onscreen_peek_head_frame_info (onscreen);
  if (!frame_info)
    {
      g_warning ("The feedback callback was called, but there was no frame info");
      return;
    }

  frame_info->flags |= COGL_FRAME_INFO_FLAG_SYMBOLIC;

  meta_onscreen_native_notify_frame_complete (onscreen);
  g_clear_pointer (&onscreen_native->posted_frame, clutter_frame_unref);
}

static const MetaKmsResultListenerVtable finish_frame_result_listener_vtable = {
  .feedback = finish_frame_result_feedback,
};

void
meta_onscreen_native_finish_frame (CoglOnscreen *onscreen,
                                   ClutterFrame *frame)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);
  MetaKmsUpdate *kms_update;

  onscreen_native->needs_flush |= meta_kms_device_handle_flush (kms_device,
                                                                kms_crtc);

  if (!meta_frame_native_has_kms_update (frame_native))
    {
      if (!onscreen_native->needs_flush ||
          onscreen_native->posted_frame != NULL)
        {
          clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
          return;
        }
    }

  if (onscreen_native->posted_frame != NULL &&
      onscreen_native->next_frame == NULL)
    {
      g_return_if_fail (meta_frame_native_has_kms_update (frame_native));
      assign_next_frame (onscreen_native, frame);
      clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_PENDING_PRESENTED);
      return;
    }

  kms_update = meta_frame_native_steal_kms_update (frame_native);

  if (onscreen_native->posted_frame != NULL &&
      onscreen_native->next_frame != NULL)
    {
      MetaFrameNative *next_frame_native;
      MetaKmsUpdate *next_kms_update;

      g_return_if_fail (kms_update);

      next_frame_native =
        meta_frame_native_from_frame (onscreen_native->next_frame);
      next_kms_update =
        meta_frame_native_ensure_kms_update (next_frame_native, kms_device);
      meta_kms_update_merge_from (next_kms_update, kms_update);
      meta_kms_update_free (kms_update);
      clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
      return;
    }

  if (!kms_update)
    {
      kms_update = meta_kms_update_new (kms_device);
      g_warn_if_fail (onscreen_native->needs_flush);
    }

  if (onscreen_native->needs_flush)
    {
      meta_kms_update_set_flushing (kms_update, kms_crtc);
      onscreen_native->needs_flush = FALSE;
    }

  post_nonprimary_plane_update (onscreen_native, frame, kms_update);
  onscreen_native->posted_frame = clutter_frame_ref (frame);

  clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_PENDING_PRESENTED);
}

static void
post_nonprimary_plane_update (MetaOnscreenNative *onscreen_native,
                              ClutterFrame       *frame,
                              MetaKmsUpdate      *kms_update)
{
  MetaCrtc *crtc = onscreen_native->crtc;
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  g_autoptr (MetaKmsFeedback) kms_feedback = NULL;

  meta_kms_update_add_result_listener (kms_update,
                                       &finish_frame_result_listener_vtable,
                                       NULL,
                                       onscreen_native,
                                       NULL);

  meta_kms_update_add_page_flip_listener (kms_update,
                                          kms_crtc,
                                          &page_flip_listener_vtable,
                                          NULL,
                                          g_object_ref (onscreen_native->view),
                                          g_object_unref);
  add_onscreen_frame_info (crtc, frame);

  meta_topic (META_DEBUG_KMS,
              "Posting non-primary plane update for CRTC %u (%s)",
              meta_kms_crtc_get_id (kms_crtc),
              meta_kms_device_get_path (kms_device));

  meta_kms_update_set_flushing (kms_update, kms_crtc);
  meta_kms_device_post_update (kms_device, kms_update,
                               META_KMS_UPDATE_FLAG_NONE);
}

static void
discard_pending_swap (ClutterFrame **frame)
{
  if (frame && *frame)
    {
      MetaFrameNative *frame_native;
      MetaKmsUpdate *kms_update;

      frame_native = meta_frame_native_from_frame (*frame);
      kms_update = meta_frame_native_steal_kms_update (frame_native);
      g_clear_pointer (&kms_update, meta_kms_update_free);
      g_clear_pointer (frame, clutter_frame_unref);
    }
}

void
meta_onscreen_native_discard_pending_swaps (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  discard_pending_swap (&onscreen_native->superseded_frame);
  discard_pending_swap (&onscreen_native->next_frame);
}

static gboolean
should_surface_be_sharable (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);

  if (META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc)) ==
      onscreen_native->render_gpu)
    return FALSE;
  else
    return TRUE;
}

static uint32_t
get_gbm_format_from_egl (MetaEgl    *egl,
                         EGLDisplay  egl_display,
                         EGLConfig   egl_config)
{
  uint32_t gbm_format;
  EGLint native_visual_id;

  if (meta_egl_get_config_attrib (egl,
                                  egl_display,
                                  egl_config,
                                  EGL_NATIVE_VISUAL_ID,
                                  &native_visual_id,
                                  NULL))
    gbm_format = (uint32_t) native_visual_id;
  else
    g_assert_not_reached ();

  return gbm_format;
}

static GArray *
get_supported_kms_modifiers (MetaCrtcKms *crtc_kms,
                             uint32_t     format)
{
  MetaKmsPlane *plane = meta_crtc_kms_get_assigned_primary_plane (crtc_kms);
  GArray *crtc_mods;

  g_return_val_if_fail (plane, NULL);

  crtc_mods = meta_kms_plane_get_modifiers_for_format (plane, format);
  if (!crtc_mods)
    return NULL;

  return g_array_copy (crtc_mods);
}

static GArray *
get_supported_egl_modifiers (CoglOnscreen *onscreen,
                             MetaCrtcKms  *crtc_kms,
                             uint32_t      format)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaGpu *gpu;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;
  GArray *modifiers;
  g_autoptr (GError) error = NULL;

  gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));
  render_device = renderer_gpu_data->render_device;

  modifiers = meta_render_device_query_drm_modifiers (render_device, format,
                                                      COGL_DRM_MODIFIER_FILTER_NONE,
                                                      &error);
  if (!modifiers)
    {
      g_warning ("Failed to query DMABUF modifiers: %s", error->message);
      return NULL;
    }

  return modifiers;
}

static GArray *
get_supported_modifiers (CoglOnscreen *onscreen,
                         uint32_t      format)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaGpu *gpu;
  g_autoptr (GArray) modifiers = NULL;

  gpu = meta_crtc_get_gpu (META_CRTC (crtc_kms));
  if (gpu == META_GPU (onscreen_native->render_gpu))
    modifiers = get_supported_kms_modifiers (crtc_kms, format);
  else
    modifiers = get_supported_egl_modifiers (onscreen, crtc_kms, format);

  return g_steal_pointer (&modifiers);
}

static GArray *
get_supported_kms_formats (CoglOnscreen *onscreen)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaKmsPlane *plane = meta_crtc_kms_get_assigned_primary_plane (crtc_kms);

  return meta_kms_plane_copy_drm_format_list (plane);
}

static gboolean
choose_onscreen_egl_config (CoglOnscreen  *onscreen,
                            EGLConfig     *out_config,
                            GError       **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  EGLDisplay egl_display = cogl_renderer_egl->edpy;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (onscreen_native->crtc);
  MetaKmsPlane *kms_plane = meta_crtc_kms_get_assigned_primary_plane (crtc_kms);
  EGLint attrs[MAX_EGL_CONFIG_ATTRIBS];
  static const uint32_t alphaless_10bpc_formats[] = {
    GBM_FORMAT_XRGB2101010,
    GBM_FORMAT_XBGR2101010,
    GBM_FORMAT_RGBX1010102,
    GBM_FORMAT_BGRX1010102,
  };
  static const uint32_t default_formats[] = {
    GBM_FORMAT_ARGB2101010,
    GBM_FORMAT_ABGR2101010,
    GBM_FORMAT_RGBA1010102,
    GBM_FORMAT_BGRA1010102,
    GBM_FORMAT_XBGR8888,
    GBM_FORMAT_ABGR8888,
    GBM_FORMAT_RGBX8888,
    GBM_FORMAT_RGBA8888,
    GBM_FORMAT_BGRX8888,
    GBM_FORMAT_BGRA8888,
    GBM_FORMAT_XRGB8888,
    GBM_FORMAT_ARGB8888,
  };

  g_return_val_if_fail (META_IS_KMS_PLANE (kms_plane), FALSE);

  cogl_display_egl_determine_attributes (cogl_display,
                                         attrs);

  /* Secondary GPU contexts use GLES3, which doesn't guarantee that 10 bpc
   * formats without alpha are renderable
   */
  if (!should_surface_be_sharable (onscreen) &&
      meta_renderer_native_choose_gbm_format (kms_plane,
                                              egl,
                                              egl_display,
                                              attrs,
                                              alphaless_10bpc_formats,
                                              G_N_ELEMENTS (alphaless_10bpc_formats),
                                              "surface",
                                              out_config,
                                              error))
    return TRUE;

  if (meta_renderer_native_choose_gbm_format (kms_plane,
                                              egl,
                                              egl_display,
                                              attrs,
                                              default_formats,
                                              G_N_ELEMENTS (default_formats),
                                              "surface",
                                              out_config,
                                              error))
    return TRUE;

  return FALSE;
}

static gboolean
create_surfaces_gbm (CoglOnscreen        *onscreen,
                     int                  width,
                     int                  height,
                     struct gbm_surface **gbm_surface,
                     EGLSurface          *egl_surface,
                     GError             **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRenderDeviceGbm *render_device_gbm;
  struct gbm_device *gbm_device;
  struct gbm_surface *new_gbm_surface = NULL;
  EGLNativeWindowType egl_native_window;
  gboolean should_be_sharable;
  EGLSurface new_egl_surface;
  EGLConfig egl_config;
  uint32_t format;
  GArray *modifiers;

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);
  render_device_gbm = META_RENDER_DEVICE_GBM (renderer_gpu_data->render_device);
  gbm_device = meta_render_device_gbm_get_gbm_device (render_device_gbm);

  should_be_sharable = should_surface_be_sharable (onscreen);

  if (!(cogl_renderer_egl->private_features &
        COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT) ||
      !choose_onscreen_egl_config (onscreen, &egl_config, error))
    egl_config = cogl_display_egl->egl_config;

  format = get_gbm_format_from_egl (egl,
                                    cogl_renderer_egl->edpy,
                                    egl_config);

  if (meta_renderer_native_use_modifiers (renderer_native))
    {
      if (should_be_sharable)
        {
          modifiers = g_array_sized_new (FALSE, FALSE, sizeof (uint64_t), 1);
          g_array_set_size (modifiers, 1);
          ((uint64_t *) modifiers->data)[0] = DRM_FORMAT_MOD_LINEAR;
        }
      else
        {
          modifiers = get_supported_modifiers (onscreen, format);
        }
    }
  else
    {
      modifiers = NULL;
    }

  if (modifiers)
    {
      new_gbm_surface =
        gbm_surface_create_with_modifiers (gbm_device,
                                           width, height, format,
                                           (uint64_t *) modifiers->data,
                                           modifiers->len);
      g_array_free (modifiers, TRUE);
    }

  if (!new_gbm_surface)
    {
      uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

      if (should_be_sharable)
        flags |= GBM_BO_USE_LINEAR;

      new_gbm_surface = gbm_surface_create (gbm_device,
                                            width, height,
                                            format,
                                            flags);
    }

  if (!new_gbm_surface)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to allocate surface: %s", g_strerror (errno));
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) new_gbm_surface;
  new_egl_surface =
    meta_egl_create_window_surface (egl,
                                    cogl_renderer_egl->edpy,
                                    egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (new_egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (new_gbm_surface);
      return FALSE;
    }

  *gbm_surface = new_gbm_surface;
  *egl_surface = new_egl_surface;

  return TRUE;
}

#ifdef HAVE_EGL_DEVICE
static gboolean
create_surfaces_egl_device (CoglOnscreen  *onscreen,
                            int            width,
                            int            height,
                            EGLStreamKHR  *out_egl_stream,
                            EGLSurface    *out_egl_surface,
                            GError       **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRenderDevice *render_device;
  MetaEgl *egl =
    meta_renderer_native_get_egl (renderer_gpu_data->renderer_native);
  EGLDisplay egl_display;
  EGLConfig egl_config;
  EGLStreamKHR egl_stream;
  EGLSurface egl_surface;
  EGLint num_layers;
  EGLOutputLayerEXT output_layer;
  EGLAttrib output_attribs[3];
  EGLint stream_attribs[] = {
    EGL_STREAM_FIFO_LENGTH_KHR, 0,
    EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
    EGL_NONE
  };
  EGLint stream_producer_attribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);
  egl_stream = meta_egl_create_stream (egl, egl_display, stream_attribs, error);
  if (egl_stream == EGL_NO_STREAM_KHR)
    return FALSE;

  output_attribs[0] = EGL_DRM_CRTC_EXT;
  output_attribs[1] = meta_crtc_get_id (onscreen_native->crtc);
  output_attribs[2] = EGL_NONE;

  if (!meta_egl_get_output_layers (egl, egl_display,
                                   output_attribs,
                                   &output_layer, 1, &num_layers,
                                   error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  if (num_layers < 1)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unable to find output layers.");
      return FALSE;
    }

  if (!meta_egl_stream_consumer_output (egl, egl_display,
                                        egl_stream, output_layer,
                                        error))
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  egl_config = cogl_display_egl->egl_config;
  egl_surface = meta_egl_create_stream_producer_surface (egl,
                                                         egl_display,
                                                         egl_config,
                                                         egl_stream,
                                                         stream_producer_attribs,
                                                         error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      meta_egl_destroy_stream (egl, egl_display, egl_stream, NULL);
      return FALSE;
    }

  *out_egl_stream = egl_stream;
  *out_egl_surface = egl_surface;

  return TRUE;
}
#endif /* HAVE_EGL_DEVICE */

void
meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                               MetaRendererView *view)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  onscreen_native->view = view;
}

static gboolean
meta_onscreen_native_allocate (CoglFramebuffer  *framebuffer,
                               GError          **error)
{
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  MetaRendererNativeGpuData *renderer_gpu_data;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  int width;
  int height;
#ifdef HAVE_EGL_DEVICE
  MetaRenderDevice *render_device;
  MetaDrmBuffer *dumb_buffer;
  EGLStreamKHR egl_stream;
#endif
  CoglFramebufferClass *parent_class;

  if (META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc)) !=
      onscreen_native->render_gpu)
    {
      if (!init_secondary_gpu_state (onscreen_native->renderer_native,
                                     onscreen, error))
        return FALSE;
    }

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (onscreen_native->renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      if (!create_surfaces_gbm (onscreen,
                                width, height,
                                &gbm_surface,
                                &egl_surface,
                                error))
        return FALSE;

      onscreen_native->gbm.surface = gbm_surface;
      cogl_onscreen_egl_set_egl_surface (onscreen_egl, egl_surface);
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      render_device = renderer_gpu_data->render_device;
      dumb_buffer = meta_render_device_allocate_dumb_buf (render_device,
                                                          width, height,
                                                          DRM_FORMAT_XRGB8888,
                                                          error);
      if (!dumb_buffer)
        return FALSE;

      onscreen_native->egl.dumb_fb = META_DRM_BUFFER_DUMB (dumb_buffer);

      if (!create_surfaces_egl_device (onscreen,
                                       width, height,
                                       &egl_stream,
                                       &egl_surface,
                                       error))
        return FALSE;

      onscreen_native->egl.stream = egl_stream;
      cogl_onscreen_egl_set_egl_surface (onscreen_egl, egl_surface);
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  parent_class = COGL_FRAMEBUFFER_CLASS (meta_onscreen_native_parent_class);
  return parent_class->allocate (framebuffer, error);
}

static gboolean
init_secondary_gpu_state_gpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
  MetaRenderDevice *render_device;
  MetaRenderDeviceGbm *render_device_gbm;
  struct gbm_device *gbm_device;
  EGLDisplay egl_display;
  int width, height;
  EGLNativeWindowType egl_native_window;
  struct gbm_surface *gbm_surface;
  EGLSurface egl_surface;
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaGpuKms *gpu_kms;
  uint32_t format;

  render_device = renderer_gpu_data->render_device;
  egl_display = meta_render_device_get_egl_display (render_device);
  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);
  format = get_gbm_format_from_egl (egl,
                                    egl_display,
                                    renderer_gpu_data->secondary.egl_config);

  render_device_gbm = META_RENDER_DEVICE_GBM (render_device);
  gbm_device = meta_render_device_gbm_get_gbm_device (render_device_gbm);
  gbm_surface = gbm_surface_create (gbm_device,
                                    width, height,
                                    format,
                                    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

  if (!gbm_surface)
    {
      gbm_surface = gbm_surface_create (gbm_device,
                                        width, height,
                                        format,
                                        0);
    }

  if (!gbm_surface)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create gbm_surface: %s", g_strerror (errno));
      return FALSE;
    }

  egl_native_window = (EGLNativeWindowType) gbm_surface;
  egl_surface =
    meta_egl_create_window_surface (egl,
                                    egl_display,
                                    renderer_gpu_data->secondary.egl_config,
                                    egl_native_window,
                                    NULL,
                                    error);
  if (egl_surface == EGL_NO_SURFACE)
    {
      gbm_surface_destroy (gbm_surface);
      return FALSE;
    }

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gbm.surface = gbm_surface;
  secondary_gpu_state->egl_surface = egl_surface;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static uint32_t
pick_secondary_gpu_framebuffer_format_for_cpu (CoglOnscreen *onscreen)
{
  /*
   * cogl_framebuffer_read_pixels_into_bitmap () supported formats in
   * preference order. Ideally these should depend on the render buffer
   * format copy_shared_framebuffer_cpu () will be reading from but
   * alpha channel ignored.
   */
  static const uint32_t preferred_formats[] =
    {
      /*
       * DRM_FORMAT_XBGR8888 a.k.a GL_RGBA, GL_UNSIGNED_BYTE on
       * little-endian is possibly the most optimized glReadPixels
       * output format. glReadPixels cannot avoid manufacturing an alpha
       * channel if the render buffer does not have one and converting
       * to ABGR8888 may be more optimized than ARGB8888.
       */
      DRM_FORMAT_XBGR8888,
      /* The rest are other fairly commonly used formats in OpenGL. */
      DRM_FORMAT_XRGB8888,
    };
  g_autoptr (GArray) formats = NULL;
  size_t k;
  unsigned int i;
  uint32_t drm_format;

  formats = get_supported_kms_formats (onscreen);

  /* Check if any of our preferred formats are supported. */
  for (k = 0; k < G_N_ELEMENTS (preferred_formats); k++)
    {
      g_assert (meta_format_info_from_drm_format (preferred_formats[k]));

      for (i = 0; i < formats->len; i++)
        {
          drm_format = g_array_index (formats, uint32_t, i);

          if (drm_format == preferred_formats[k])
            return drm_format;
        }
    }

  /*
   * Otherwise just pick an arbitrary format we recognize. The formats
   * list is not in any specific order and we don't know any better
   * either.
   */
  for (i = 0; i < formats->len; i++)
    {
      drm_format = g_array_index (formats, uint32_t, i);

      if (meta_format_info_from_drm_format (drm_format))
        return drm_format;
    }

  return DRM_FORMAT_INVALID;
}

static gboolean
init_secondary_gpu_state_cpu_copy_mode (MetaRendererNative         *renderer_native,
                                        CoglOnscreen               *onscreen,
                                        MetaRendererNativeGpuData  *renderer_gpu_data,
                                        GError                    **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaOnscreenNativeSecondaryGpuState *secondary_gpu_state;
  MetaRenderDevice *render_device;
  MetaGpuKms *gpu_kms;
  int width, height;
  unsigned int i;
  uint32_t drm_format;
  MetaDrmFormatBuf tmp;

  drm_format = pick_secondary_gpu_framebuffer_format_for_cpu (onscreen);
  if (drm_format == DRM_FORMAT_INVALID)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not find a suitable pixel format in CPU copy mode");
      return FALSE;
    }

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);

  gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (onscreen_native->crtc));
  render_device = renderer_gpu_data->render_device;
  meta_topic (META_DEBUG_KMS,
              "Secondary GPU %s using DRM format '%s' (0x%x) for a %dx%d output.",
              meta_render_device_get_name (render_device),
              meta_drm_format_to_string (&tmp, drm_format),
              drm_format,
              width, height);

  secondary_gpu_state = g_new0 (MetaOnscreenNativeSecondaryGpuState, 1);
  secondary_gpu_state->renderer_gpu_data = renderer_gpu_data;
  secondary_gpu_state->gpu_kms = gpu_kms;
  secondary_gpu_state->egl_surface = EGL_NO_SURFACE;

  for (i = 0; i < G_N_ELEMENTS (secondary_gpu_state->cpu.dumb_fbs); i++)
    {
      MetaDrmBuffer *dumb_buffer;

      dumb_buffer = meta_render_device_allocate_dumb_buf (render_device,
                                                          width, height,
                                                          drm_format,
                                                          error);
      if (!dumb_buffer)
        {
          secondary_gpu_state_free (secondary_gpu_state);
          return FALSE;
        }

      secondary_gpu_state->cpu.dumb_fbs[i] = META_DRM_BUFFER_DUMB (dumb_buffer);
    }

  /*
   * This function initializes everything needed for
   * META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO as well.
   */
  secondary_gpu_state->import_status =
    META_SHARED_FRAMEBUFFER_IMPORT_STATUS_NONE;

  onscreen_native->secondary_gpu_state = secondary_gpu_state;

  return TRUE;
}

static gboolean
init_secondary_gpu_state (MetaRendererNative  *renderer_native,
                          CoglOnscreen        *onscreen,
                          GError             **error)
{
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaGpu *gpu = meta_crtc_get_gpu (onscreen_native->crtc);
  MetaRendererNativeGpuData *renderer_gpu_data;
  g_autoptr (GError) local_error = NULL;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         META_GPU_KMS (gpu));

  switch (renderer_gpu_data->secondary.copy_mode)
    {
    case META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU:
      if (init_secondary_gpu_state_gpu_copy_mode (renderer_native,
                                                  onscreen,
                                                  renderer_gpu_data,
                                                  &local_error))
        return TRUE;

      g_warning ("Secondary GPU initialization failed (%s). "
                 "Falling back to GPU-less mode instead, so the "
                 "secondary monitor may be slow to update.",
                 local_error->message);

      renderer_gpu_data->secondary.copy_mode =
        META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO;

      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO:
      /*
       * Initialize also the primary copy mode, so that if zero-copy
       * path fails, which is quite likely, we can simply continue
       * with the primary copy path on the very first frame.
       */
      G_GNUC_FALLTHROUGH;
    case META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY:
      if (!init_secondary_gpu_state_cpu_copy_mode (renderer_native,
                                                   onscreen,
                                                   renderer_gpu_data,
                                                   error))
        return FALSE;
      break;
    }

  return TRUE;
}

void
meta_onscreen_native_invalidate (MetaOnscreenNative *onscreen_native)
{
  const MetaOutputInfo *output_info =
    meta_output_get_info (onscreen_native->output);

  if (meta_crtc_get_gamma_lut_size (onscreen_native->crtc) > 0)
    onscreen_native->property.gamma_lut.invalidated = TRUE;
  if (output_info->supports_privacy_screen)
    onscreen_native->property.privacy_screen.invalidated = TRUE;
}

static void
on_gamma_lut_changed (MetaCrtc           *crtc,
                      MetaOnscreenNative *onscreen_native)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (onscreen_native->view);

  onscreen_native->property.gamma_lut.invalidated = TRUE;
  clutter_stage_view_schedule_update (stage_view);
}

static void
on_privacy_screen_enabled_changed (MetaOutput         *output,
                                   GParamSpec         *pspec,
                                   MetaOnscreenNative *onscreen_native)
{
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (onscreen_native->view);

  onscreen_native->property.privacy_screen.invalidated = TRUE;
  clutter_stage_view_schedule_update (stage_view);
}

MetaOnscreenNative *
meta_onscreen_native_new (MetaRendererNative *renderer_native,
                          MetaGpuKms         *render_gpu,
                          MetaOutput         *output,
                          MetaCrtc           *crtc,
                          CoglContext        *cogl_context,
                          int                 width,
                          int                 height)
{
  MetaOnscreenNative *onscreen_native;
  CoglFramebufferDriverConfig driver_config;
  const MetaOutputInfo *output_info = meta_output_get_info (output);

  driver_config = (CoglFramebufferDriverConfig) {
    .type = COGL_FRAMEBUFFER_DRIVER_TYPE_BACK,
  };
  onscreen_native = g_object_new (META_TYPE_ONSCREEN_NATIVE,
                                  "context", cogl_context,
                                  "driver-config", &driver_config,
                                  "width", width,
                                  "height", height,
                                  NULL);

  onscreen_native->renderer_native = renderer_native;
  onscreen_native->render_gpu = render_gpu;

  g_set_object (&onscreen_native->output, output);
  g_set_object (&onscreen_native->crtc, crtc);

  if (meta_crtc_get_gamma_lut_size (crtc) > 0)
    {
      onscreen_native->property.gamma_lut.invalidated = TRUE;
      onscreen_native->property.gamma_lut.signal_handler_id =
        g_signal_connect (crtc, "gamma-lut-changed",
                          G_CALLBACK (on_gamma_lut_changed),
                          onscreen_native);
    }

  if (output_info->supports_privacy_screen)
    {
      onscreen_native->property.privacy_screen.invalidated = TRUE;
      onscreen_native->property.privacy_screen.signal_handler_id =
        g_signal_connect (output, "notify::is-privacy-screen-enabled",
                          G_CALLBACK (on_privacy_screen_enabled_changed),
                          onscreen_native);
    }

  return onscreen_native;
}

static void
clear_invalidation_handlers (MetaOnscreenNative *onscreen_native)
{
  g_clear_signal_handler (&onscreen_native->property.gamma_lut.signal_handler_id,
                          onscreen_native->crtc);
  g_clear_signal_handler (&onscreen_native->property.privacy_screen.signal_handler_id,
                          onscreen_native->output);
}

static void
meta_onscreen_native_dispose (GObject *object)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
  MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (onscreen);
  MetaRendererNative *renderer_native = onscreen_native->renderer_native;
  MetaRendererNativeGpuData *renderer_gpu_data;

  meta_onscreen_native_detach (onscreen_native);

  meta_onscreen_native_discard_pending_swaps (onscreen);
  g_clear_pointer (&onscreen_native->posted_frame, clutter_frame_unref);
  g_clear_pointer (&onscreen_native->presented_frame, clutter_frame_unref);

  renderer_gpu_data =
    meta_renderer_native_get_gpu_data (renderer_native,
                                       onscreen_native->render_gpu);
  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      g_assert_not_reached ();
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      g_clear_object (&onscreen_native->egl.dumb_fb);

      if (onscreen_native->egl.stream != EGL_NO_STREAM_KHR)
        {
          MetaEgl *egl = meta_onscreen_native_get_egl (onscreen_native);
          CoglContext *cogl_context = cogl_framebuffer_get_context (framebuffer);
          CoglRenderer *cogl_renderer = cogl_context->display->renderer;
          CoglRendererEGL *cogl_renderer_egl = cogl_renderer_get_winsys (cogl_renderer);

          meta_egl_destroy_stream (egl,
                                   cogl_renderer_egl->edpy,
                                   onscreen_native->egl.stream,
                                   NULL);
          onscreen_native->egl.stream = EGL_NO_STREAM_KHR;
        }
      break;
#endif /* HAVE_EGL_DEVICE */
    }

  G_OBJECT_CLASS (meta_onscreen_native_parent_class)->dispose (object);

  g_clear_pointer (&onscreen_native->gbm.surface, gbm_surface_destroy);
  g_clear_pointer (&onscreen_native->secondary_gpu_state,
                   secondary_gpu_state_free);

  g_clear_object (&onscreen_native->output);
  g_clear_object (&onscreen_native->crtc);
}

static void
meta_onscreen_native_init (MetaOnscreenNative *onscreen_native)
{
}

static void
meta_onscreen_native_class_init (MetaOnscreenNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferClass *framebuffer_class = COGL_FRAMEBUFFER_CLASS (klass);
  CoglOnscreenClass *onscreen_class = COGL_ONSCREEN_CLASS (klass);

  object_class->dispose = meta_onscreen_native_dispose;

  framebuffer_class->allocate = meta_onscreen_native_allocate;

  onscreen_class->swap_buffers_with_damage =
    meta_onscreen_native_swap_buffers_with_damage;
  onscreen_class->direct_scanout = meta_onscreen_native_direct_scanout;
  onscreen_class->get_window_handles = meta_onscreen_native_get_window_handles;

  blit_source_quark = g_quark_from_static_string ("Blit source");
}

MetaCrtc *
meta_onscreen_native_get_crtc (MetaOnscreenNative *onscreen_native)
{
  return onscreen_native->crtc;
}

void
meta_onscreen_native_detach (MetaOnscreenNative *onscreen_native)
{
  clear_invalidation_handlers (onscreen_native);
  onscreen_native->view = NULL;
}
