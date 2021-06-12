/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
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
 * Authors:
 *   Rob Bradford <rob@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Kristian Høgsberg (from eglkms.c)
 *   Benjamin Franzke (from eglkms.c)
 *   Robert Bragg <robert@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Neil Roberts <neil@linux.intel.com> (from cogl-winsys-egl-kms.c)
 *   Jonas Ådahl <jadahl@redhat.com>
 *
 */

#include "config.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <gbm.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/meta-gles3.h"
#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-cogl-utils.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-onscreen-native.h"
#include "backends/native/meta-renderer-native-private.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

/* added in libdrm 2.4.95 */
#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#endif

struct _MetaRendererNative
{
  MetaRenderer parent;

  MetaGpuKms *primary_gpu_kms;

  MetaGles3 *gles3;

  gboolean use_modifiers;

  GHashTable *gpu_datas;

  GList *pending_mode_set_views;
  gboolean pending_mode_set;

  GList *kept_alive_onscreens;

  GList *power_save_page_flip_onscreens;
  guint power_save_page_flip_source_id;
};

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRendererNative,
                         meta_renderer_native,
                         META_TYPE_RENDERER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;
static const CoglWinsysVtable *parent_vtable;

static void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native);

const CoglWinsysVtable *
meta_get_renderer_native_parent_vtable (void)
{
  return parent_vtable;
}

static void
meta_renderer_native_gpu_data_free (MetaRendererNativeGpuData *renderer_gpu_data)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (renderer_gpu_data->secondary.egl_context != EGL_NO_CONTEXT)
    {
      meta_egl_destroy_context (egl,
                                renderer_gpu_data->egl_display,
                                renderer_gpu_data->secondary.egl_context,
                                NULL);
    }

  if (renderer_gpu_data->egl_display != EGL_NO_DISPLAY)
    meta_egl_terminate (egl, renderer_gpu_data->egl_display, NULL);

  g_clear_pointer (&renderer_gpu_data->gbm.device, gbm_device_destroy);
  g_free (renderer_gpu_data);
}

MetaRendererNativeGpuData *
meta_renderer_native_get_gpu_data (MetaRendererNative *renderer_native,
                                   MetaGpuKms         *gpu_kms)
{
  return g_hash_table_lookup (renderer_native->gpu_datas, gpu_kms);
}

static MetaRendererNative *
meta_renderer_native_from_gpu (MetaGpuKms *gpu_kms)
{
  MetaBackend *backend = meta_gpu_get_backend (META_GPU (gpu_kms));

  return META_RENDERER_NATIVE (meta_backend_get_renderer (backend));
}

struct gbm_device *
meta_gbm_device_from_gpu (MetaGpuKms *gpu_kms)
{
  MetaRendererNative *renderer_native = meta_renderer_native_from_gpu (gpu_kms);
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);

  return renderer_gpu_data->gbm.device;
}

MetaGpuKms *
meta_renderer_native_get_primary_gpu (MetaRendererNative *renderer_native)
{
  return renderer_native->primary_gpu_kms;
}

static MetaRendererNativeGpuData *
meta_create_renderer_native_gpu_data (void)
{
  return g_new0 (MetaRendererNativeGpuData, 1);
}

MetaEgl *
meta_renderer_native_get_egl (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);

  return meta_backend_get_egl (meta_renderer_get_backend (renderer));
}

gboolean
meta_renderer_native_use_modifiers (MetaRendererNative *renderer_native)
{
  return renderer_native->use_modifiers;
}

MetaGles3 *
meta_renderer_native_get_gles3 (MetaRendererNative *renderer_native)
{
  return renderer_native->gles3;
}

gboolean
meta_renderer_native_has_pending_mode_sets (MetaRendererNative *renderer_native)
{
  return !!renderer_native->pending_mode_set_views;
}

gboolean
meta_renderer_native_has_pending_mode_set (MetaRendererNative *renderer_native)
{
  return renderer_native->pending_mode_set;
}

static void
meta_renderer_native_disconnect (CoglRenderer *cogl_renderer)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  g_free (cogl_renderer_egl);
}

static gboolean
meta_renderer_native_connect (CoglRenderer *cogl_renderer,
                              GError      **error)
{
  CoglRendererEGL *cogl_renderer_egl;
  MetaRendererNative *renderer_native = cogl_renderer->custom_winsys_user_data;
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  cogl_renderer->winsys = g_new0 (CoglRendererEGL, 1);
  cogl_renderer_egl = cogl_renderer->winsys;

  gpu_kms = meta_renderer_native_get_primary_gpu (renderer_native);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);

  cogl_renderer_egl->platform_vtable = &_cogl_winsys_egl_vtable;
  cogl_renderer_egl->platform = renderer_gpu_data;
  cogl_renderer_egl->edpy = renderer_gpu_data->egl_display;

  if (!_cogl_winsys_egl_renderer_connect_common (cogl_renderer, error))
    goto fail;

  return TRUE;

fail:
  meta_renderer_native_disconnect (cogl_renderer);

  return FALSE;
}

static int
meta_renderer_native_add_egl_config_attributes (CoglDisplay                 *cogl_display,
                                                const CoglFramebufferConfig *config,
                                                EGLint                      *attributes)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  int i = 0;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_WINDOW_BIT;
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_PBUFFER_BIT;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_STREAM_BIT_KHR;
      break;
#endif
    }

  return i;
}

static gboolean
choose_egl_config_from_gbm_format (MetaEgl       *egl,
                                   EGLDisplay     egl_display,
                                   const EGLint  *attributes,
                                   uint32_t       gbm_format,
                                   EGLConfig     *out_config,
                                   GError       **error)
{
  EGLConfig *egl_configs;
  EGLint n_configs;
  EGLint i;

  egl_configs = meta_egl_choose_all_configs (egl, egl_display,
                                             attributes,
                                             &n_configs,
                                             error);
  if (!egl_configs)
    return FALSE;

  for (i = 0; i < n_configs; i++)
    {
      EGLint visual_id;

      if (!meta_egl_get_config_attrib (egl, egl_display,
                                       egl_configs[i],
                                       EGL_NATIVE_VISUAL_ID,
                                       &visual_id,
                                       error))
        {
          g_free (egl_configs);
          return FALSE;
        }

      if ((uint32_t) visual_id == gbm_format)
        {
          *out_config = egl_configs[i];
          g_free (egl_configs);
          return TRUE;
        }
    }

  g_free (egl_configs);
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "No EGL config matching supported GBM format found");
  return FALSE;
}

static gboolean
meta_renderer_native_choose_egl_config (CoglDisplay  *cogl_display,
                                        EGLint       *attributes,
                                        EGLConfig    *out_config,
                                        GError      **error)
{
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return choose_egl_config_from_gbm_format (egl,
                                                egl_display,
                                                attributes,
                                                GBM_FORMAT_XRGB8888,
                                                out_config,
                                                error);
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      *out_config = EGL_NO_CONFIG_KHR;
      return TRUE;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return meta_egl_choose_first_config (egl,
                                           egl_display,
                                           attributes,
                                           out_config,
                                           error);
#endif
    }

  return FALSE;
}

static gboolean
meta_renderer_native_setup_egl_display (CoglDisplay *cogl_display,
                                        GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRendererEGL *cogl_renderer_egl = cogl_display->renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  cogl_display_egl->platform = renderer_native;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static void
meta_renderer_native_destroy_egl_display (CoglDisplay *cogl_display)
{
}

static EGLSurface
create_dummy_pbuffer_surface (EGLDisplay egl_display,
                              GError   **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLConfig pbuffer_config;
  static const EGLint pbuffer_config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  static const EGLint pbuffer_attribs[] = {
    EGL_WIDTH, 16,
    EGL_HEIGHT, 16,
    EGL_NONE
  };

  if (!meta_egl_choose_first_config (egl, egl_display, pbuffer_config_attribs,
                                     &pbuffer_config, error))
    return EGL_NO_SURFACE;

  return meta_egl_create_pbuffer_surface (egl, egl_display,
                                          pbuffer_config, pbuffer_attribs,
                                          error);
}

static gboolean
meta_renderer_native_egl_context_created (CoglDisplay *cogl_display,
                                          GError     **error)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  if ((cogl_renderer_egl->private_features &
       COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) == 0)
    {
      cogl_display_egl->dummy_surface =
        create_dummy_pbuffer_surface (cogl_renderer_egl->edpy, error);
      if (cogl_display_egl->dummy_surface == EGL_NO_SURFACE)
        return FALSE;
    }

  if (!_cogl_winsys_egl_make_current (cogl_display,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->dummy_surface,
                                      cogl_display_egl->egl_context))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Failed to make context current");
      return FALSE;
    }

  return TRUE;
}

static void
meta_renderer_native_egl_cleanup_context (CoglDisplay *cogl_display)
{
  CoglDisplayEGL *cogl_display_egl = cogl_display->winsys;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (cogl_display_egl->dummy_surface != EGL_NO_SURFACE)
    {
      meta_egl_destroy_surface (egl,
                                cogl_renderer_egl->edpy,
                                cogl_display_egl->dummy_surface,
                                NULL);
      cogl_display_egl->dummy_surface = EGL_NO_SURFACE;
    }
}

static CoglContext *
cogl_context_from_renderer_native (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);

  return clutter_backend_get_cogl_context (clutter_backend);
}

CoglFramebuffer *
meta_renderer_native_create_dma_buf_framebuffer (MetaRendererNative  *renderer_native,
                                                 int                  dmabuf_fd,
                                                 uint32_t             width,
                                                 uint32_t             height,
                                                 uint32_t             stride,
                                                 uint32_t             offset,
                                                 uint64_t             modifier,
                                                 uint32_t             drm_format,
                                                 GError             **error)
{
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLImageKHR egl_image;
  uint32_t strides[1];
  uint32_t offsets[1];
  uint64_t modifiers[1];
  CoglPixelFormat cogl_format;
  CoglEglImageFlags flags;
  CoglTexture2D *cogl_tex;
  CoglOffscreen *cogl_fbo;
  int ret;

  ret = meta_cogl_pixel_format_from_drm_format (drm_format,
                                                &cogl_format,
                                                NULL);
  g_assert (ret);

  strides[0] = stride;
  offsets[0] = offset;
  modifiers[0] = modifier;
  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            drm_format,
                                            1 /* n_planes */,
                                            &dmabuf_fd,
                                            strides,
                                            offsets,
                                            modifiers,
                                            error);
  if (egl_image == EGL_NO_IMAGE_KHR)
    return NULL;

  flags = COGL_EGL_IMAGE_FLAG_NO_GET_DATA;
  cogl_tex = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                 width,
                                                 height,
                                                 cogl_format,
                                                 egl_image,
                                                 flags,
                                                 error);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  if (!cogl_tex)
    return NULL;

  cogl_fbo = cogl_offscreen_new_with_texture (COGL_TEXTURE (cogl_tex));
  cogl_object_unref (cogl_tex);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (cogl_fbo), error))
    {
      g_object_unref (cogl_fbo);
      return NULL;
    }

  return COGL_FRAMEBUFFER (cogl_fbo);
}

static void
configure_disabled_crtcs (MetaKmsDevice *kms_device)
{
  MetaKms *kms = meta_kms_device_get_kms (kms_device);
  GList *l;

  for (l = meta_kms_device_get_crtcs (kms_device); l; l = l->next)
    {
      MetaKmsCrtc *kms_crtc = l->data;
      MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (kms_crtc);
      MetaKmsUpdate *kms_update;

      if (meta_crtc_get_config (META_CRTC (crtc_kms)))
        continue;

      if (!meta_kms_crtc_is_active (kms_crtc))
        continue;

      kms_update = meta_kms_ensure_pending_update (kms, kms_device);
      meta_kms_update_mode_set (kms_update, kms_crtc, NULL, NULL);
    }
}

static gboolean
dummy_power_save_page_flip_cb (gpointer user_data)
{
  MetaRendererNative *renderer_native = user_data;

  g_list_foreach (renderer_native->power_save_page_flip_onscreens,
                  (GFunc) meta_onscreen_native_dummy_power_save_page_flip,
                  NULL);
  g_clear_list (&renderer_native->power_save_page_flip_onscreens,
                g_object_unref);
  renderer_native->power_save_page_flip_source_id = 0;

  return G_SOURCE_REMOVE;
}

void
meta_renderer_native_queue_power_save_page_flip (MetaRendererNative *renderer_native,
                                                 CoglOnscreen       *onscreen)
{
  const unsigned int timeout_ms = 100;

  if (!renderer_native->power_save_page_flip_source_id)
    {
      renderer_native->power_save_page_flip_source_id =
        g_timeout_add (timeout_ms,
                       dummy_power_save_page_flip_cb,
                       renderer_native);
    }

  renderer_native->power_save_page_flip_onscreens =
    g_list_prepend (renderer_native->power_save_page_flip_onscreens,
                    g_object_ref (onscreen));
}

static void
clear_kept_alive_onscreens (MetaRendererNative *renderer_native)
{
  g_clear_list (&renderer_native->kept_alive_onscreens,
                g_object_unref);
}

void
meta_renderer_native_post_mode_set_updates (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  GList *l;

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;
      MetaKmsUpdate *kms_update;
      MetaKmsUpdateFlag flags;
      g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
      const GError *feedback_error;

      configure_disabled_crtcs (kms_device);

      kms_update = meta_kms_get_pending_update (kms, kms_device);
      if (!kms_update)
        continue;

      flags = META_KMS_UPDATE_FLAG_NONE;
      kms_feedback = meta_kms_post_pending_update_sync (kms, kms_device, flags);

      switch (meta_kms_feedback_get_result (kms_feedback))
        {
        case META_KMS_FEEDBACK_PASSED:
          break;
        case META_KMS_FEEDBACK_FAILED:
          feedback_error = meta_kms_feedback_get_error (kms_feedback);
          if (!g_error_matches (feedback_error,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            g_warning ("Failed to post KMS update: %s", feedback_error->message);
          break;
        }
    }

  clear_kept_alive_onscreens (renderer_native);
}

static void
unset_disabled_crtcs (MetaBackend *backend,
                      MetaKms     *kms)
{
  GList *l;

  meta_topic (META_DEBUG_KMS, "Disabling all disabled CRTCs");

  for (l = meta_backend_get_gpus (backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      MetaKmsDevice *kms_device =
        meta_gpu_kms_get_kms_device (META_GPU_KMS (gpu));
      GList *k;
      gboolean did_mode_set = FALSE;
      MetaKmsUpdateFlag flags;
      g_autoptr (MetaKmsFeedback) kms_feedback = NULL;

      for (k = meta_gpu_get_crtcs (gpu); k; k = k->next)
        {
          MetaCrtc *crtc = k->data;
          MetaKmsUpdate *kms_update;

          if (meta_crtc_get_config (crtc))
            continue;

          kms_update = meta_kms_ensure_pending_update (kms, kms_device);
          meta_crtc_kms_set_mode (META_CRTC_KMS (crtc), kms_update);

          did_mode_set = TRUE;
        }

      if (!did_mode_set)
        continue;

      flags = META_KMS_UPDATE_FLAG_NONE;
      kms_feedback = meta_kms_post_pending_update_sync (kms,
                                                        kms_device,
                                                        flags);
      if (meta_kms_feedback_get_result (kms_feedback) !=
          META_KMS_FEEDBACK_PASSED)
        {
          const GError *error = meta_kms_feedback_get_error (kms_feedback);

          if (!g_error_matches (error, G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            g_warning ("Failed to post KMS update: %s", error->message);
        }
    }
}

static CoglDmaBufHandle *
meta_renderer_native_create_dma_buf (CoglRenderer  *cogl_renderer,
                                     int            width,
                                     int            height,
                                     GError       **error)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        CoglFramebuffer *dmabuf_fb;
        CoglDmaBufHandle *dmabuf_handle;
        struct gbm_bo *new_bo;
        int stride;
        int offset;
        int bpp;
        int dmabuf_fd = -1;

        new_bo = gbm_bo_create (renderer_gpu_data->gbm.device,
                                width, height, DRM_FORMAT_XRGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);

        if (!new_bo)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Failed to allocate buffer");
            return NULL;
          }

        dmabuf_fd = gbm_bo_get_fd (new_bo);

        if (dmabuf_fd == -1)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                         "Failed to export buffer's DMA fd: %s",
                         g_strerror (errno));
            return NULL;
          }

        stride = gbm_bo_get_stride (new_bo);
        offset = gbm_bo_get_offset (new_bo, 0);
        bpp = 4;
        dmabuf_fb =
          meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                           dmabuf_fd,
                                                           width, height,
                                                           stride,
                                                           offset,
                                                           DRM_FORMAT_MOD_LINEAR,
                                                           DRM_FORMAT_XRGB8888,
                                                           error);

        if (!dmabuf_fb)
          return NULL;

        dmabuf_handle =
          cogl_dma_buf_handle_new (dmabuf_fb, dmabuf_fd,
                                   width, height, stride, offset, bpp,
                                   new_bo,
                                   (GDestroyNotify) gbm_bo_destroy);
        g_object_unref (dmabuf_fb);
        return dmabuf_handle;
      }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
#endif
      break;
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
               "Current mode does not support exporting DMA buffers");

  return NULL;
}

static gboolean
meta_renderer_native_init_egl_context (CoglContext *cogl_context,
                                       GError     **error)
{
#ifdef HAVE_EGL_DEVICE
  CoglRenderer *cogl_renderer = cogl_context->display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
#endif

  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SWAP_BUFFERS_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT,
                  TRUE);
  COGL_FLAGS_SET (cogl_context->winsys_features,
                  COGL_WINSYS_FEATURE_MULTIPLE_ONSCREEN,
                  TRUE);

#ifdef HAVE_EGL_DEVICE
  if (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_EGL_DEVICE)
    COGL_FLAGS_SET (cogl_context->features,
                    COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL, TRUE);
#endif

  return TRUE;
}

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable = {
  .add_config_attributes = meta_renderer_native_add_egl_config_attributes,
  .choose_config = meta_renderer_native_choose_egl_config,
  .display_setup = meta_renderer_native_setup_egl_display,
  .display_destroy = meta_renderer_native_destroy_egl_display,
  .context_created = meta_renderer_native_egl_context_created,
  .cleanup_context = meta_renderer_native_egl_cleanup_context,
  .context_init = meta_renderer_native_init_egl_context
};

static void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  GList *l;

  g_clear_list (&renderer_native->pending_mode_set_views, NULL);
  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);

      if (COGL_IS_ONSCREEN (framebuffer))
        {
          renderer_native->pending_mode_set_views =
            g_list_prepend (renderer_native->pending_mode_set_views,
                            stage_view);
        }
    }
  renderer_native->pending_mode_set = TRUE;

  meta_topic (META_DEBUG_KMS, "Queue mode set");
}

void
meta_renderer_native_notify_mode_sets_reset (MetaRendererNative *renderer_native)
{
  renderer_native->pending_mode_set = FALSE;
}

gboolean
meta_renderer_native_pop_pending_mode_set (MetaRendererNative *renderer_native,
                                           MetaRendererView   *view)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaPowerSave power_save_mode;
  GList *link;

  g_assert (META_IS_RENDERER_VIEW (view));

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode != META_POWER_SAVE_ON)
    return FALSE;

  link = g_list_find (renderer_native->pending_mode_set_views, view);
  if (!link)
    return FALSE;

  renderer_native->pending_mode_set_views =
    g_list_delete_link (renderer_native->pending_mode_set_views, link);
  return TRUE;
}

static CoglOffscreen *
meta_renderer_native_create_offscreen (MetaRendererNative    *renderer,
                                       CoglContext           *context,
                                       gint                   view_width,
                                       gint                   view_height,
                                       GError               **error)
{
  CoglOffscreen *fb;
  CoglTexture2D *tex;

  tex = cogl_texture_2d_new_with_size (context, view_width, view_height);
  cogl_primitive_texture_set_auto_mipmap (COGL_PRIMITIVE_TEXTURE (tex), FALSE);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      cogl_object_unref (tex);
      return FALSE;
    }

  fb = cogl_offscreen_new_with_texture (COGL_TEXTURE (tex));
  cogl_object_unref (tex);
  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (fb), error))
    {
      g_object_unref (fb);
      return FALSE;
    }

  return fb;
}

static const CoglWinsysVtable *
get_native_cogl_winsys_vtable (CoglRenderer *cogl_renderer)
{
  static gboolean vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* The this winsys is a subclass of the EGL winsys so we
         start by copying its vtable */

      parent_vtable = _cogl_winsys_egl_get_vtable ();
      vtable = *parent_vtable;

      vtable.id = COGL_WINSYS_ID_CUSTOM;
      vtable.name = "EGL_KMS";

      vtable.renderer_connect = meta_renderer_native_connect;
      vtable.renderer_disconnect = meta_renderer_native_disconnect;
      vtable.renderer_create_dma_buf = meta_renderer_native_create_dma_buf;

      vtable_inited = TRUE;
    }

  return &vtable;
}

static CoglRenderer *
meta_renderer_native_create_cogl_renderer (MetaRenderer *renderer)
{
  CoglRenderer *cogl_renderer;

  cogl_renderer = cogl_renderer_new ();
  cogl_renderer_set_custom_winsys (cogl_renderer,
                                   get_native_cogl_winsys_vtable,
                                   renderer);
  return cogl_renderer;
}

static MetaMonitorTransform
calculate_view_transform (MetaMonitorManager *monitor_manager,
                          MetaLogicalMonitor *logical_monitor,
                          MetaOutput         *output,
                          MetaCrtc           *crtc)
{
  MetaMonitorTransform crtc_transform;

  crtc = meta_output_get_assigned_crtc (output);
  crtc_transform =
    meta_output_logical_to_crtc_transform (output, logical_monitor->transform);

  if (meta_monitor_manager_is_transform_handled (monitor_manager,
                                                 crtc,
                                                 crtc_transform))
    return META_MONITOR_TRANSFORM_NORMAL;
  else
    return crtc_transform;
}

static gboolean
should_force_shadow_fb (MetaRendererNative *renderer_native,
                        MetaGpuKms         *primary_gpu)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  int kms_fd;
  uint64_t prefer_shadow = 0;

  if (meta_renderer_is_hardware_accelerated (renderer))
    return FALSE;

  if (!cogl_has_feature (cogl_context, COGL_FEATURE_ID_BLIT_FRAMEBUFFER))
    return FALSE;

  kms_fd = meta_gpu_kms_get_fd (primary_gpu);
  if (drmGetCap (kms_fd, DRM_CAP_DUMB_PREFER_SHADOW, &prefer_shadow) == 0)
    {
      if (prefer_shadow)
        {
          static gboolean logged_once = FALSE;

          if (!logged_once)
            {
              g_message ("Forcing shadow framebuffer");
              logged_once = TRUE;
            }

          return TRUE;
        }
    }

  return FALSE;
}

static MetaRendererView *
meta_renderer_native_create_view (MetaRenderer       *renderer,
                                  MetaLogicalMonitor *logical_monitor,
                                  MetaOutput         *output,
                                  MetaCrtc           *crtc)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  const MetaCrtcConfig *crtc_config;
  const MetaCrtcModeInfo *crtc_mode_info;
  MetaMonitorTransform view_transform;
  g_autoptr (CoglFramebuffer) framebuffer = NULL;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  gboolean use_shadowfb;
  float scale;
  int onscreen_width;
  int onscreen_height;
  MetaRectangle view_layout;
  MetaRendererView *view;
  EGLSurface egl_surface;
  GError *error = NULL;

  crtc_config = meta_crtc_get_config (crtc);
  crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);
  onscreen_width = crtc_mode_info->width;
  onscreen_height = crtc_mode_info->height;

  if (META_IS_CRTC_KMS (crtc))
    {
      MetaOnscreenNative *onscreen_native;

      onscreen_native = meta_onscreen_native_new (renderer_native,
                                                  renderer_native->primary_gpu_kms,
                                                  output,
                                                  crtc,
                                                  cogl_context,
                                                  onscreen_width,
                                                  onscreen_height);

      if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (onscreen_native), &error))
        g_error ("Failed to allocate onscreen framebuffer: %s", error->message);

      use_shadowfb = should_force_shadow_fb (renderer_native,
                                             renderer_native->primary_gpu_kms);
      framebuffer = COGL_FRAMEBUFFER (onscreen_native);
    }
  else
    {
      CoglOffscreen *virtual_onscreen;

      g_assert (META_IS_CRTC_VIRTUAL (crtc));

      virtual_onscreen = meta_renderer_native_create_offscreen (renderer_native,
                                                                cogl_context,
                                                                onscreen_width,
                                                                onscreen_height,
                                                                &error);
      if (!virtual_onscreen)
        g_error ("Failed to allocate back buffer texture: %s", error->message);
      use_shadowfb = FALSE;
      framebuffer = COGL_FRAMEBUFFER (virtual_onscreen);
    }

  view_transform = calculate_view_transform (monitor_manager,
                                             logical_monitor,
                                             output,
                                             crtc);
  if (view_transform != META_MONITOR_TRANSFORM_NORMAL)
    {
      int offscreen_width;
      int offscreen_height;

      if (meta_monitor_transform_is_rotated (view_transform))
        {
          offscreen_width = onscreen_height;
          offscreen_height = onscreen_width;
        }
      else
        {
          offscreen_width = onscreen_width;
          offscreen_height = onscreen_height;
        }

      offscreen = meta_renderer_native_create_offscreen (renderer_native,
                                                         cogl_context,
                                                         offscreen_width,
                                                         offscreen_height,
                                                         &error);
      if (!offscreen)
        g_error ("Failed to allocate back buffer texture: %s", error->message);
    }

  if (meta_is_stage_views_scaled ())
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

  meta_rectangle_from_graphene_rect (&crtc_config->layout,
                                     META_ROUNDING_STRATEGY_ROUND,
                                     &view_layout);
  view = g_object_new (META_TYPE_RENDERER_VIEW,
                       "name", meta_output_get_name (output),
                       "stage", meta_backend_get_stage (backend),
                       "layout", &view_layout,
                       "crtc", crtc,
                       "scale", scale,
                       "framebuffer", framebuffer,
                       "offscreen", offscreen,
                       "use-shadowfb", use_shadowfb,
                       "transform", view_transform,
                       "refresh-rate", crtc_mode_info->refresh_rate,
                       NULL);

  if (META_IS_ONSCREEN_NATIVE (framebuffer))
    {
      CoglDisplayEGL *cogl_display_egl;
      CoglOnscreenEgl *onscreen_egl;

      meta_onscreen_native_set_view (COGL_ONSCREEN (framebuffer), view);

      /* Ensure we don't point to stale surfaces when creating the offscreen */
      cogl_display_egl = cogl_display->winsys;
      onscreen_egl = COGL_ONSCREEN_EGL (framebuffer);
      egl_surface = cogl_onscreen_egl_get_egl_surface (onscreen_egl);
      _cogl_winsys_egl_make_current (cogl_display,
                                     egl_surface,
                                     egl_surface,
                                     cogl_display_egl->egl_context);
    }

  return view;
}

static void
keep_current_onscreens_alive (MetaRenderer *renderer)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  GList *views;
  GList *l;

  views = meta_renderer_get_views (renderer);
  for (l = views; l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (stage_view);

      renderer_native->kept_alive_onscreens =
        g_list_prepend (renderer_native->kept_alive_onscreens,
                        g_object_ref (onscreen));
    }
}

static void
meta_renderer_native_rebuild_views (MetaRenderer *renderer)
{
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaRendererClass *parent_renderer_class =
    META_RENDERER_CLASS (meta_renderer_native_parent_class);

  meta_kms_discard_pending_page_flips (kms);

  keep_current_onscreens_alive (renderer);

  parent_renderer_class->rebuild_views (renderer);

  meta_renderer_native_queue_modes_reset (META_RENDERER_NATIVE (renderer));
}

void
meta_renderer_native_prepare_frame (MetaRendererNative *renderer_native,
                                    MetaRendererView   *view,
                                    ClutterFrame       *frame)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCrtc *crtc = meta_renderer_view_get_crtc (view);
  MetaPowerSave power_save_mode;
  MetaCrtcKms *crtc_kms;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;

  if (!META_IS_CRTC_KMS (crtc))
    return;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode != META_POWER_SAVE_ON)
    return;

  crtc_kms = META_CRTC_KMS (crtc);
  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  kms_device = meta_kms_crtc_get_device (kms_crtc);

  meta_crtc_kms_maybe_set_gamma (crtc_kms, kms_device);
}

void
meta_renderer_native_finish_frame (MetaRendererNative *renderer_native,
                                   MetaRendererView   *view,
                                   ClutterFrame       *frame)
{
  if (!clutter_frame_has_result (frame))
    {
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));

      if (COGL_IS_ONSCREEN (framebuffer))
        {
          CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);

          meta_onscreen_native_finish_frame (onscreen, frame);
        }
    }
}

static gboolean
create_secondary_egl_config (MetaEgl               *egl,
                             MetaRendererNativeMode mode,
                             EGLDisplay             egl_display,
                             EGLConfig             *egl_config,
                             GError               **error)
{
  EGLint attributes[] = {
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, EGL_DONT_CARE,
    EGL_BUFFER_SIZE, EGL_DONT_CARE,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };

  switch (mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      return choose_egl_config_from_gbm_format (egl,
                                                egl_display,
                                                attributes,
                                                GBM_FORMAT_XRGB8888,
                                                egl_config,
                                                error);
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return meta_egl_choose_first_config (egl,
                                           egl_display,
                                           attributes,
                                           egl_config,
                                           error);
#endif
    }

  return FALSE;
}

static EGLContext
create_secondary_egl_context (MetaEgl   *egl,
                              EGLDisplay egl_display,
                              EGLConfig  egl_config,
                              GError   **error)
{
  EGLint attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
  };

  return meta_egl_create_context (egl,
                                  egl_display,
                                  egl_config,
                                  EGL_NO_CONTEXT,
                                  attributes,
                                  error);
}

static void
meta_renderer_native_ensure_gles3 (MetaRendererNative *renderer_native)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

  if (renderer_native->gles3)
    return;

  renderer_native->gles3 = meta_gles3_new (egl);
}

static gboolean
init_secondary_gpu_data_gpu (MetaRendererNativeGpuData *renderer_gpu_data,
                             GError                   **error)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLDisplay egl_display = renderer_gpu_data->egl_display;
  EGLConfig egl_config;
  EGLContext egl_context;
  const char **missing_gl_extensions;
  const char *renderer_str;

  if (!create_secondary_egl_config (egl, renderer_gpu_data->mode, egl_display,
                                    &egl_config, error))
    return FALSE;

  egl_context = create_secondary_egl_context (egl, egl_display, egl_config, error);
  if (egl_context == EGL_NO_CONTEXT)
    return FALSE;

  meta_renderer_native_ensure_gles3 (renderer_native);

  if (!meta_egl_make_current (egl,
                              egl_display,
                              EGL_NO_SURFACE,
                              EGL_NO_SURFACE,
                              egl_context,
                              error))
    {
      meta_egl_destroy_context (egl, egl_display, egl_context, NULL);
      return FALSE;
    }

  renderer_str = (const char *) glGetString (GL_RENDERER);
  if (g_str_has_prefix (renderer_str, "llvmpipe") ||
      g_str_has_prefix (renderer_str, "softpipe") ||
      g_str_has_prefix (renderer_str, "swrast"))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Do not want to use software renderer (%s), falling back to CPU copy path",
                   renderer_str);
      goto out_fail_with_context;
    }

  if (!meta_gles3_has_extensions (renderer_native->gles3,
                                  &missing_gl_extensions,
                                  "GL_OES_EGL_image_external",
                                  NULL))
    {
      char *missing_gl_extensions_str;

      missing_gl_extensions_str = g_strjoinv (", ",
                                              (char **) missing_gl_extensions);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing OpenGL ES extensions: %s",
                   missing_gl_extensions_str);
      g_free (missing_gl_extensions_str);
      g_free (missing_gl_extensions);

      goto out_fail_with_context;
    }

  renderer_gpu_data->secondary.is_hardware_rendering = TRUE;
  renderer_gpu_data->secondary.egl_context = egl_context;
  renderer_gpu_data->secondary.egl_config = egl_config;
  renderer_gpu_data->secondary.copy_mode = META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU;

  renderer_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers =
    meta_egl_has_extensions (egl, egl_display, NULL,
                             "EGL_EXT_image_dma_buf_import_modifiers",
                             NULL);

  return TRUE;

out_fail_with_context:
  meta_egl_make_current (egl,
                         egl_display,
                         EGL_NO_SURFACE,
                         EGL_NO_SURFACE,
                         EGL_NO_CONTEXT,
                         NULL);
  meta_egl_destroy_context (egl, egl_display, egl_context, NULL);

  return FALSE;
}

static void
init_secondary_gpu_data_cpu (MetaRendererNativeGpuData *renderer_gpu_data)
{
  renderer_gpu_data->secondary.is_hardware_rendering = FALSE;

  /* First try ZERO, it automatically falls back to PRIMARY as needed */
  renderer_gpu_data->secondary.copy_mode =
    META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO;
}

static void
init_secondary_gpu_data (MetaRendererNativeGpuData *renderer_gpu_data)
{
  GError *error = NULL;

  if (init_secondary_gpu_data_gpu (renderer_gpu_data, &error))
    return;

  g_message ("Failed to initialize accelerated iGPU/dGPU framebuffer sharing: %s",
             error->message);
  g_error_free (error);

  init_secondary_gpu_data_cpu (renderer_gpu_data);
}

static gboolean
gpu_kms_is_hardware_rendering (MetaRendererNative *renderer_native,
                               MetaGpuKms         *gpu_kms)
{
  MetaRendererNativeGpuData *data;

  data = meta_renderer_native_get_gpu_data (renderer_native, gpu_kms);
  return data->secondary.is_hardware_rendering;
}

static EGLDisplay
init_gbm_egl_display (MetaRendererNative  *renderer_native,
                      struct gbm_device   *gbm_device,
                      GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLDisplay egl_display;

  if (!meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_MESA_platform_gbm",
                                NULL) &&
      !meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_KHR_platform_gbm",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing extension for GBM renderer: EGL_KHR_platform_gbm");
      return EGL_NO_DISPLAY;
    }

  egl_display = meta_egl_get_platform_display (egl,
                                               EGL_PLATFORM_GBM_KHR,
                                               gbm_device, NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!meta_egl_initialize (egl, egl_display, error))
    return EGL_NO_DISPLAY;

  return egl_display;
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_gbm (MetaRendererNative  *renderer_native,
                              MetaGpuKms          *gpu_kms,
                              GError             **error)
{
  struct gbm_device *gbm_device;
  int kms_fd;
  MetaRendererNativeGpuData *renderer_gpu_data;
  g_autoptr (GError) local_error = NULL;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  gbm_device = gbm_create_device (kms_fd);
  if (!gbm_device)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create gbm device: %s", g_strerror (errno));
      return NULL;
    }

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->gbm.device = gbm_device;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_GBM;

  renderer_gpu_data->egl_display = init_gbm_egl_display (renderer_native,
                                                         gbm_device,
                                                         &local_error);
  if (renderer_gpu_data->egl_display == EGL_NO_DISPLAY)
    {
      g_debug ("GBM EGL init for %s failed: %s",
               meta_gpu_kms_get_file_path (gpu_kms),
               local_error->message);

      init_secondary_gpu_data_cpu (renderer_gpu_data);
      return renderer_gpu_data;
    }

  init_secondary_gpu_data (renderer_gpu_data);
  return renderer_gpu_data;
}

static EGLDisplay
init_surfaceless_egl_display (MetaRendererNative  *renderer_native,
                              GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  EGLDisplay egl_display;

  if (!meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_MESA_platform_surfaceless",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing EGL platform required for surfaceless context: "
                   "EGL_MESA_platform_surfaceless");
      return EGL_NO_DISPLAY;
    }

  egl_display = meta_egl_get_platform_display (egl,
                                               EGL_PLATFORM_SURFACELESS_MESA,
                                               EGL_DEFAULT_DISPLAY,
                                               NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!meta_egl_initialize (egl, egl_display, error))
    return EGL_NO_DISPLAY;

  if (!meta_egl_has_extensions (egl, egl_display, NULL,
                                "EGL_KHR_no_config_context",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing EGL extension required for surfaceless context: "
                   "EGL_KHR_no_config_context");
      return EGL_NO_DISPLAY;
    }

  return egl_display;
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_surfaceless (MetaRendererNative  *renderer_native,
                                      GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;
  EGLDisplay egl_display;

  egl_display = init_surfaceless_egl_display (renderer_native, error);
  if (egl_display == EGL_NO_DISPLAY)
    return NULL;

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_SURFACELESS;
  renderer_gpu_data->egl_display = egl_display;

  return renderer_gpu_data;
}

#ifdef HAVE_EGL_DEVICE
static const char *
get_drm_device_file (MetaEgl     *egl,
                     EGLDeviceEXT device,
                     GError     **error)
{
  if (!meta_egl_egl_device_has_extensions (egl, device,
                                           NULL,
                                           "EGL_EXT_device_drm",
                                           NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing required EGLDevice extension EGL_EXT_device_drm");
      return NULL;
    }

  return meta_egl_query_device_string (egl, device,
                                       EGL_DRM_DEVICE_FILE_EXT,
                                       error);
}

static EGLDeviceEXT
find_egl_device (MetaRendererNative  *renderer_native,
                 MetaGpuKms          *gpu_kms,
                 GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  const char **missing_extensions;
  EGLint num_devices;
  EGLDeviceEXT *devices;
  const char *kms_file_path;
  EGLDeviceEXT device;
  EGLint i;

  if (!meta_egl_has_extensions (egl,
                                EGL_NO_DISPLAY,
                                &missing_extensions,
                                "EGL_EXT_device_base",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return EGL_NO_DEVICE_EXT;
    }

  if (!meta_egl_query_devices (egl, 0, NULL, &num_devices, error))
    return EGL_NO_DEVICE_EXT;

  devices = g_new0 (EGLDeviceEXT, num_devices);
  if (!meta_egl_query_devices (egl, num_devices, devices, &num_devices,
                               error))
    {
      g_free (devices);
      return EGL_NO_DEVICE_EXT;
    }

  kms_file_path = meta_gpu_kms_get_file_path (gpu_kms);

  device = EGL_NO_DEVICE_EXT;
  for (i = 0; i < num_devices; i++)
    {
      const char *egl_device_drm_path;

      g_clear_error (error);

      egl_device_drm_path = get_drm_device_file (egl, devices[i], error);
      if (!egl_device_drm_path)
        continue;

      if (g_str_equal (egl_device_drm_path, kms_file_path))
        {
          device = devices[i];
          break;
        }
    }
  g_free (devices);

  if (device == EGL_NO_DEVICE_EXT)
    {
      if (!*error)
        g_set_error (error, G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to find matching EGLDeviceEXT");
      return EGL_NO_DEVICE_EXT;
    }

  return device;
}

static EGLDisplay
get_egl_device_display (MetaRendererNative  *renderer_native,
                        MetaGpuKms          *gpu_kms,
                        EGLDeviceEXT         egl_device,
                        GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  int kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  EGLint platform_attribs[] = {
    EGL_DRM_MASTER_FD_EXT, kms_fd,
    EGL_NONE
  };

  return meta_egl_get_platform_display (egl, EGL_PLATFORM_DEVICE_EXT,
                                        (void *) egl_device,
                                        platform_attribs,
                                        error);
}

static int
count_drm_devices (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);

  return g_list_length (meta_backend_get_gpus (backend));
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_egl_device (MetaRendererNative  *renderer_native,
                                     MetaGpuKms          *gpu_kms,
                                     GError             **error)
{
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  const char **missing_extensions;
  EGLDeviceEXT egl_device;
  EGLDisplay egl_display;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (count_drm_devices (renderer_native) != 1)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "EGLDevice currently only works with single GPU systems");
      return NULL;
    }

  egl_device = find_egl_device (renderer_native, gpu_kms, error);
  if (egl_device == EGL_NO_DEVICE_EXT)
    return NULL;

  egl_display = get_egl_device_display (renderer_native, gpu_kms,
                                        egl_device, error);
  if (egl_display == EGL_NO_DISPLAY)
    return NULL;

  if (!meta_egl_initialize (egl, egl_display, error))
    return NULL;

  if (!meta_egl_has_extensions (egl,
                                egl_display,
                                &missing_extensions,
                                "EGL_NV_output_drm_flip_event",
                                "EGL_EXT_output_base",
                                "EGL_EXT_output_drm",
                                "EGL_KHR_stream",
                                "EGL_KHR_stream_producer_eglsurface",
                                "EGL_EXT_stream_consumer_egloutput",
                                "EGL_EXT_stream_acquire_mode",
                                NULL))
    {
      char *missing_extensions_str;

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      meta_egl_terminate (egl, egl_display, NULL);
      g_free (missing_extensions_str);
      g_free (missing_extensions);
      return NULL;
    }

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->egl.device = egl_device;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_EGL_DEVICE;
  renderer_gpu_data->egl_display = egl_display;

  return renderer_gpu_data;
}
#endif /* HAVE_EGL_DEVICE */

static MetaRendererNativeGpuData *
meta_renderer_native_create_renderer_gpu_data (MetaRendererNative  *renderer_native,
                                               MetaGpuKms          *gpu_kms,
                                               GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;
  GError *gbm_error = NULL;
#ifdef HAVE_EGL_DEVICE
  GError *egl_device_error = NULL;
#endif

  if (!gpu_kms)
    return create_renderer_gpu_data_surfaceless (renderer_native, error);

#ifdef HAVE_EGL_DEVICE
  /* Try to initialize the EGLDevice backend first. Whenever we use a
   * non-NVIDIA GPU, the EGLDevice enumeration function won't find a match, and
   * we'll fall back to GBM (which will always succeed as it has a software
   * rendering fallback)
   */
  renderer_gpu_data = create_renderer_gpu_data_egl_device (renderer_native,
                                                           gpu_kms,
                                                           &egl_device_error);
  if (renderer_gpu_data)
    return renderer_gpu_data;
#endif

  renderer_gpu_data = create_renderer_gpu_data_gbm (renderer_native,
                                                    gpu_kms,
                                                    &gbm_error);
  if (renderer_gpu_data)
    {
#ifdef HAVE_EGL_DEVICE
      g_error_free (egl_device_error);
#endif
      return renderer_gpu_data;
    }

  g_set_error (error, G_IO_ERROR,
               G_IO_ERROR_FAILED,
               "Failed to initialize renderer: "
               "%s"
#ifdef HAVE_EGL_DEVICE
               ", %s"
#endif
               , gbm_error->message
#ifdef HAVE_EGL_DEVICE
               , egl_device_error->message
#endif
  );

  g_error_free (gbm_error);
#ifdef HAVE_EGL_DEVICE
  g_error_free (egl_device_error);
#endif

  return NULL;
}

static gboolean
create_renderer_gpu_data (MetaRendererNative  *renderer_native,
                          MetaGpuKms          *gpu_kms,
                          GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data =
    meta_renderer_native_create_renderer_gpu_data (renderer_native,
                                                   gpu_kms,
                                                   error);
  if (!renderer_gpu_data)
    return FALSE;

  g_hash_table_insert (renderer_native->gpu_datas,
                       gpu_kms,
                       renderer_gpu_data);

  return TRUE;
}

static void
on_gpu_added (MetaBackendNative  *backend_native,
              MetaGpuKms         *gpu_kms,
              MetaRendererNative *renderer_native)
{
  MetaBackend *backend = META_BACKEND (backend_native);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context_get_display (cogl_context);
  GError *error = NULL;

  if (!create_renderer_gpu_data (renderer_native, gpu_kms, &error))
    {
      g_warning ("on_gpu_added: could not create gpu_data for gpu %s: %s",
                 meta_gpu_kms_get_file_path (gpu_kms), error->message);
      g_clear_error (&error);
    }

  _cogl_winsys_egl_ensure_current (cogl_display);
}

static void
on_power_save_mode_changed (MetaMonitorManager *monitor_manager,
                            MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaPowerSave power_save_mode;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode == META_POWER_SAVE_ON)
    meta_renderer_native_queue_modes_reset (renderer_native);
  else
    meta_kms_discard_pending_page_flips (kms);
}

void
meta_renderer_native_reset_modes (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);

  unset_disabled_crtcs (backend, kms);
}

static MetaGpuKms *
choose_primary_gpu_unchecked (MetaBackend        *backend,
                              MetaRendererNative *renderer_native)
{
  GList *gpus = meta_backend_get_gpus (backend);
  GList *l;
  int allow_sw;

  /*
   * Check first hardware rendering devices, and if none found,
   * then software rendering devices.
   */
  for (allow_sw = 0; allow_sw < 2; allow_sw++)
  {
    /* First check if one was explicitly configured. */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);
        MetaKmsDevice *kms_device = meta_gpu_kms_get_kms_device (gpu_kms);

        if (meta_kms_device_get_flags (kms_device) &
            META_KMS_DEVICE_FLAG_PREFERRED_PRIMARY)
          {
            g_message ("GPU %s selected primary given udev rule",
                       meta_gpu_kms_get_file_path (gpu_kms));
            return gpu_kms;
          }
      }

    /* Prefer a platform device */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (meta_gpu_kms_is_platform_device (gpu_kms) &&
            (allow_sw == 1 ||
             gpu_kms_is_hardware_rendering (renderer_native, gpu_kms)))
          {
            g_message ("Integrated GPU %s selected as primary",
                       meta_gpu_kms_get_file_path (gpu_kms));
            return gpu_kms;
          }
      }

    /* Otherwise a device we booted with */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (meta_gpu_kms_is_boot_vga (gpu_kms) &&
            (allow_sw == 1 ||
             gpu_kms_is_hardware_rendering (renderer_native, gpu_kms)))
          {
            g_message ("Boot VGA GPU %s selected as primary",
                       meta_gpu_kms_get_file_path (gpu_kms));
            return gpu_kms;
          }
      }

    /* Fall back to any device */
    for (l = gpus; l; l = l->next)
      {
        MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

        if (allow_sw == 1 ||
            gpu_kms_is_hardware_rendering (renderer_native, gpu_kms))
          {
            g_message ("GPU %s selected as primary",
                       meta_gpu_kms_get_file_path (gpu_kms));
            return gpu_kms;
          }
      }
  }

  g_assert_not_reached ();
  return NULL;
}

static MetaGpuKms *
choose_primary_gpu (MetaBackend         *backend,
                    MetaRendererNative  *renderer_native,
                    GError             **error)
{
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;

  gpu_kms = choose_primary_gpu_unchecked (backend, renderer_native);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  if (renderer_gpu_data->egl_display == EGL_NO_DISPLAY)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The GPU %s chosen as primary is not supported by EGL.",
                   meta_gpu_kms_get_file_path (gpu_kms));
      return NULL;
    }

  return gpu_kms;
}

static gboolean
meta_renderer_native_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (initable);
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  GList *gpus;
  GList *l;

  gpus = meta_backend_get_gpus (backend);
  if (gpus)
    {
      const char *use_kms_modifiers_debug_env;

      for (l = gpus; l; l = l->next)
        {
          MetaGpuKms *gpu_kms = META_GPU_KMS (l->data);

          if (!create_renderer_gpu_data (renderer_native, gpu_kms, error))
            return FALSE;
        }

      renderer_native->primary_gpu_kms = choose_primary_gpu (backend,
                                                             renderer_native,
                                                             error);
      if (!renderer_native->primary_gpu_kms)
        return FALSE;

      use_kms_modifiers_debug_env = g_getenv ("MUTTER_DEBUG_USE_KMS_MODIFIERS");
      if (use_kms_modifiers_debug_env)
        {
          renderer_native->use_modifiers =
            g_strcmp0 (use_kms_modifiers_debug_env, "1") == 0;
        }
      else
        {
          renderer_native->use_modifiers =
            !meta_gpu_kms_disable_modifiers (renderer_native->primary_gpu_kms);
        }

      meta_topic (META_DEBUG_KMS, "Usage of KMS modifiers is %s",
                  renderer_native->use_modifiers ? "enabled" : "disabled");
    }
  else
    {
      if (!create_renderer_gpu_data (renderer_native, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_renderer_native_initable_init;
}

static void
meta_renderer_native_finalize (GObject *object)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);

  clear_kept_alive_onscreens (renderer_native);

  g_clear_list (&renderer_native->power_save_page_flip_onscreens,
                g_object_unref);
  g_clear_handle_id (&renderer_native->power_save_page_flip_source_id,
                     g_source_remove);

  g_list_free (renderer_native->pending_mode_set_views);

  g_hash_table_destroy (renderer_native->gpu_datas);
  g_clear_object (&renderer_native->gles3);

  G_OBJECT_CLASS (meta_renderer_native_parent_class)->finalize (object);
}

static void
meta_renderer_native_constructed (GObject *object)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (object);
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (meta_settings_is_experimental_feature_enabled (
        settings, META_EXPERIMENTAL_FEATURE_KMS_MODIFIERS))
    renderer_native->use_modifiers = TRUE;

  g_signal_connect (backend, "gpu-added",
                    G_CALLBACK (on_gpu_added), renderer_native);
  g_signal_connect (monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (on_power_save_mode_changed), renderer_native);

  G_OBJECT_CLASS (meta_renderer_native_parent_class)->constructed (object);
}

static void
meta_renderer_native_init (MetaRendererNative *renderer_native)
{
  renderer_native->gpu_datas =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_renderer_native_gpu_data_free);
}

static void
meta_renderer_native_class_init (MetaRendererNativeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  object_class->finalize = meta_renderer_native_finalize;
  object_class->constructed = meta_renderer_native_constructed;

  renderer_class->create_cogl_renderer = meta_renderer_native_create_cogl_renderer;
  renderer_class->create_view = meta_renderer_native_create_view;
  renderer_class->rebuild_views = meta_renderer_native_rebuild_views;
}

MetaRendererNative *
meta_renderer_native_new (MetaBackendNative  *backend_native,
                          GError            **error)
{
  return g_initable_new (META_TYPE_RENDERER_NATIVE,
                         NULL,
                         error,
                         "backend", backend_native,
                         NULL);
}
