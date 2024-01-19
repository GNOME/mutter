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

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-gles3.h"
#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-cursor-manager.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-onscreen-native.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-render-device-gbm.h"
#include "backends/native/meta-render-device-surfaceless.h"
#include "backends/native/meta-renderer-native-private.h"
#include "backends/native/meta-renderer-view-native.h"
#include "cogl/cogl.h"
#include "common/meta-cogl-drm-formats.h"
#include "core/boxes-private.h"

#ifdef HAVE_EGL_DEVICE
#include "backends/native/meta-render-device-egl-stream.h"
#endif

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

struct _MetaRendererNative
{
  MetaRenderer parent;

  MetaGpuKms *primary_gpu_kms;

  MetaGles3 *gles3;

  gboolean use_modifiers;
  gboolean send_modifiers;
  gboolean has_addfb2;

  GHashTable *gpu_datas;

  GList *pending_mode_set_views;
  gboolean pending_mode_set;

  GList *detached_onscreens;
  GList *lingering_onscreens;
  guint release_unused_gpus_idle_id;

  GList *power_save_page_flip_onscreens;
  guint power_save_page_flip_source_id;

  GHashTable *mode_set_updates;
};

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRendererNative,
                         meta_renderer_native,
                         META_TYPE_RENDERER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;

static gboolean
meta_renderer_native_ensure_gpu_data (MetaRendererNative  *renderer_native,
                                      MetaGpuKms          *gpu_kms,
                                      GError             **error);

static void
meta_renderer_native_queue_modes_reset (MetaRendererNative *renderer_native);

static void
meta_renderer_native_gpu_data_free (MetaRendererNativeGpuData *renderer_gpu_data)
{
  MetaGpuKms *gpu_kms = renderer_gpu_data->gpu_kms;

  if (renderer_gpu_data->secondary.egl_context != EGL_NO_CONTEXT)
    {
      MetaRenderDevice *render_device = renderer_gpu_data->render_device;
      EGLDisplay egl_display =
        meta_render_device_get_egl_display (render_device);
      MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
      MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);

      meta_egl_destroy_context (egl,
                                egl_display,
                                renderer_gpu_data->secondary.egl_context,
                                NULL);
    }

  if (renderer_gpu_data->crtc_needs_flush_handler_id)
    {
      g_clear_signal_handler (&renderer_gpu_data->crtc_needs_flush_handler_id,
                              meta_gpu_kms_get_kms_device (gpu_kms));
    }

  g_clear_pointer (&renderer_gpu_data->render_device, g_object_unref);
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
  MetaRenderDevice *render_device;
  MetaRenderDeviceGbm *render_device_gbm;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  render_device = renderer_gpu_data->render_device;

  if (!META_IS_RENDER_DEVICE_GBM (render_device))
    return NULL;

  render_device_gbm = META_RENDER_DEVICE_GBM (render_device);
  return meta_render_device_gbm_get_gbm_device (render_device_gbm);
}

MetaGpuKms *
meta_renderer_native_get_primary_gpu (MetaRendererNative *renderer_native)
{
  return renderer_native->primary_gpu_kms;
}

MetaDeviceFile *
meta_renderer_native_get_primary_device_file (MetaRendererNative *renderer_native)
{
  MetaGpuKms *gpu_kms = renderer_native->primary_gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;

  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  render_device = renderer_gpu_data->render_device;
  return meta_render_device_get_device_file (render_device);
}

static MetaRendererNativeGpuData *
meta_create_renderer_native_gpu_data (void)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = g_new0 (MetaRendererNativeGpuData, 1);
  renderer_gpu_data->secondary.egl_context = EGL_NO_CONTEXT;

  return renderer_gpu_data;
}

MetaEgl *
meta_renderer_native_get_egl (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);

  return meta_backend_get_egl (meta_renderer_get_backend (renderer));
}

gboolean
meta_renderer_native_send_modifiers (MetaRendererNative *renderer_native)
{
  return renderer_native->send_modifiers;
}

gboolean
meta_renderer_native_use_modifiers (MetaRendererNative *renderer_native)
{
  return renderer_native->use_modifiers;
}

gboolean
meta_renderer_native_has_addfb2 (MetaRendererNative *renderer_native)
{
  return renderer_native->has_addfb2;
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

MetaRendererNativeMode
meta_renderer_native_get_mode (MetaRendererNative *renderer_native)
{
  MetaGpuKms *primary_gpu = renderer_native->primary_gpu_kms;
  MetaRendererNativeGpuData *primary_gpu_data;

  primary_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                        primary_gpu);
  return primary_gpu_data->mode;
}

static void
meta_renderer_native_disconnect (CoglRenderer *cogl_renderer)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;

  g_free (cogl_renderer_egl);
}

static MetaKmsUpdate *
ensure_mode_set_update (MetaRendererNative *renderer_native,
                        MetaKmsDevice      *kms_device)
{
  MetaKmsUpdate *kms_update;

  kms_update = g_hash_table_lookup (renderer_native->mode_set_updates,
                                    kms_device);
  if (kms_update)
    return kms_update;

  kms_update = meta_kms_update_new (kms_device);
  g_hash_table_insert (renderer_native->mode_set_updates,
                       kms_device, kms_update);

  return kms_update;
}

static gboolean
meta_renderer_native_connect (CoglRenderer *cogl_renderer,
                              GError      **error)
{
  CoglRendererEGL *cogl_renderer_egl;
  MetaRendererNative *renderer_native = cogl_renderer->custom_winsys_user_data;
  MetaGpuKms *gpu_kms;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRenderDevice *render_device;

  cogl_renderer->winsys = g_new0 (CoglRendererEGL, 1);
  cogl_renderer_egl = cogl_renderer->winsys;

  gpu_kms = meta_renderer_native_get_primary_gpu (renderer_native);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  render_device = renderer_gpu_data->render_device;

  cogl_renderer_egl->platform_vtable = &_cogl_winsys_egl_vtable;
  cogl_renderer_egl->platform = renderer_gpu_data;
  cogl_renderer_egl->edpy = meta_render_device_get_egl_display (render_device);

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

gboolean
meta_renderer_native_choose_gbm_format (MetaKmsPlane    *kms_plane,
                                        MetaEgl         *egl,
                                        EGLDisplay       egl_display,
                                        EGLint          *attributes,
                                        const uint32_t  *formats,
                                        size_t           num_formats,
                                        const char      *purpose,
                                        EGLConfig       *out_config,
                                        GError         **error)
{
  int i;

  for (i = 0; i < num_formats; i++)
    {
      g_clear_error (error);

      if (kms_plane &&
          !meta_kms_plane_is_format_supported (kms_plane, formats[i]))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "KMS CRTC doesn't support format");
          continue;
        }

      if (choose_egl_config_from_gbm_format (egl,
                                             egl_display,
                                             attributes,
                                             formats[i],
                                             out_config,
                                             error))
        {
          MetaDrmFormatBuf format_string;

          meta_drm_format_to_string (&format_string, formats[i]);
          meta_topic (META_DEBUG_KMS,
                      "Using GBM format %s for primary GPU EGL %s",
                      format_string.s, purpose);

          return TRUE;
        }
    }

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
  MetaRenderer *renderer = cogl_renderer->custom_winsys_user_data;
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaEgl *egl = meta_backend_get_egl (backend);
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        static const uint32_t formats[] = {
          GBM_FORMAT_XRGB8888,
          GBM_FORMAT_ARGB8888,
        };

        return meta_renderer_native_choose_gbm_format (NULL,
                                                       egl,
                                                       egl_display,
                                                       attributes,
                                                       formats,
                                                       G_N_ELEMENTS (formats),
                                                       "fallback",
                                                       out_config,
                                                       error);
      }
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
create_dummy_pbuffer_surface (CoglRenderer  *cogl_renderer,
                              EGLDisplay     egl_display,
                              GError       **error)
{
  MetaRenderer *renderer = cogl_renderer->custom_winsys_user_data;
  MetaBackend *backend = meta_renderer_get_backend (renderer);
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
        create_dummy_pbuffer_surface (cogl_renderer,
                                      cogl_renderer_egl->edpy,
                                      error);
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
  ClutterBackend *clutter_backend;

  clutter_backend = meta_backend_get_clutter_backend (backend);
  if (!clutter_backend)
    return NULL;

  return clutter_backend_get_cogl_context (clutter_backend);
}

CoglFramebuffer *
meta_renderer_native_create_dma_buf_framebuffer (MetaRendererNative  *renderer_native,
                                                 int                  dmabuf_fd,
                                                 uint32_t             width,
                                                 uint32_t             height,
                                                 uint32_t             stride,
                                                 uint32_t             offset,
                                                 uint64_t            *modifier,
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
  CoglPixelFormat cogl_format;
  CoglEglImageFlags flags;
  CoglTexture *cogl_tex;
  CoglOffscreen *cogl_fbo;
  const MetaFormatInfo *format_info;

  format_info = meta_format_info_from_drm_format (drm_format);
  g_assert (format_info);
  cogl_format = format_info->cogl_format;

  strides[0] = stride;
  offsets[0] = offset;
  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            drm_format,
                                            1 /* n_planes */,
                                            &dmabuf_fd,
                                            strides,
                                            offsets,
                                            modifier,
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

  cogl_fbo = cogl_offscreen_new_with_texture (cogl_tex);
  g_object_unref (cogl_tex);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (cogl_fbo), error))
    {
      g_object_unref (cogl_fbo);
      return NULL;
    }

  return COGL_FRAMEBUFFER (cogl_fbo);
}

static void
configure_disabled_crtcs (MetaKmsDevice      *kms_device,
                          MetaRendererNative *renderer_native)
{
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

      kms_update = ensure_mode_set_update (renderer_native, kms_device);
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

static gboolean
is_gpu_unused (gpointer key,
               gpointer value,
               gpointer user_data)
{
  GHashTable *used_gpus = user_data;

  return !g_hash_table_contains (used_gpus, key);
}

static void
free_unused_gpu_datas (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  g_autoptr (GHashTable) used_gpus = NULL;
  GList *l;

  used_gpus = g_hash_table_new (NULL, NULL);
  g_hash_table_add (used_gpus, renderer_native->primary_gpu_kms);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MetaCrtc *crtc = meta_renderer_view_get_crtc (view);
      MetaGpu *gpu;

      gpu = meta_crtc_get_gpu (crtc);
      if (!gpu)
        continue;

      g_hash_table_add (used_gpus, gpu);
    }

  for (l = renderer_native->lingering_onscreens; l; l = l->next)
    {
      MetaOnscreenNative *onscreen_native = META_ONSCREEN_NATIVE (l->data);
      MetaCrtc *crtc = meta_onscreen_native_get_crtc (onscreen_native);

      g_hash_table_add (used_gpus, meta_crtc_get_gpu (crtc));
    }

  g_hash_table_foreach_remove (renderer_native->gpu_datas,
                               is_gpu_unused,
                               used_gpus);
}

static gboolean
release_unused_gpus_idle (gpointer user_data)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (user_data);

  renderer_native->release_unused_gpus_idle_id = 0;
  free_unused_gpu_datas (renderer_native);

  return G_SOURCE_REMOVE;
}

static void
old_onscreen_freed (gpointer  user_data,
                    GObject  *freed_onscreen)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (user_data);

  renderer_native->lingering_onscreens =
    g_list_remove (renderer_native->lingering_onscreens, freed_onscreen);

  if (!renderer_native->release_unused_gpus_idle_id)
    {
      renderer_native->release_unused_gpus_idle_id =
        g_idle_add (release_unused_gpus_idle, renderer_native);
    }
}

static void
clear_detached_onscreens (MetaRendererNative *renderer_native)
{
  GList *l;

  for (l = renderer_native->detached_onscreens; l; l = l->next)
    {
      CoglOnscreen *onscreen;

      if (!COGL_IS_ONSCREEN (l->data))
        continue;

      onscreen = COGL_ONSCREEN (l->data);
      g_object_weak_ref (G_OBJECT (onscreen),
                         old_onscreen_freed,
                         renderer_native);
      renderer_native->lingering_onscreens =
        g_list_prepend (renderer_native->lingering_onscreens, onscreen);
    }

  g_clear_list (&renderer_native->detached_onscreens,
                g_object_unref);
}

static void
mode_sets_update_result_feedback (const MetaKmsFeedback *kms_feedback,
                                  gpointer               user_data)
{
  const GError *feedback_error;

  feedback_error = meta_kms_feedback_get_error (kms_feedback);
  if (feedback_error &&
      !g_error_matches (feedback_error,
                        G_IO_ERROR,
                        G_IO_ERROR_PERMISSION_DENIED))
    g_warning ("Failed to post KMS update: %s", feedback_error->message);
}

static const MetaKmsResultListenerVtable mode_sets_result_listener_vtable = {
  .feedback = mode_sets_update_result_feedback,
};

static void
post_mode_set_updates (MetaRendererNative *renderer_native)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, renderer_native->mode_set_updates);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      MetaKmsDevice *kms_device = META_KMS_DEVICE (key);
      MetaKmsUpdate *kms_update = value;
      g_autoptr (MetaKmsFeedback) feedback = NULL;

      g_hash_table_iter_steal (&iter);

      meta_kms_update_add_result_listener (kms_update,
                                           &mode_sets_result_listener_vtable,
                                           NULL,
                                           NULL,
                                           NULL);

      feedback =
        meta_kms_device_process_update_sync (kms_device, kms_update,
                                             META_KMS_UPDATE_FLAG_MODE_SET);
    }
}

void
meta_renderer_native_post_mode_set_updates (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));

  renderer_native->pending_mode_set = FALSE;

  g_list_foreach (meta_kms_get_devices (kms),
                  (GFunc) configure_disabled_crtcs,
                  renderer_native);

  post_mode_set_updates (renderer_native);

  clear_detached_onscreens (renderer_native);

  meta_kms_notify_modes_set (kms);

  free_unused_gpu_datas (renderer_native);
}

void
meta_renderer_native_queue_mode_set_update (MetaRendererNative *renderer_native,
                                            MetaKmsUpdate      *new_kms_update)
{
  MetaKmsDevice *kms_device = meta_kms_update_get_device (new_kms_update);
  MetaKmsUpdate *kms_update;

  kms_update = g_hash_table_lookup (renderer_native->mode_set_updates,
                                    kms_device);
  if (!kms_update)
    {
      g_hash_table_insert (renderer_native->mode_set_updates,
                           kms_device, new_kms_update);
      return;
    }

  meta_kms_update_merge_from (kms_update, new_kms_update);
  meta_kms_update_free (new_kms_update);
}

static CoglDmaBufHandle *
meta_renderer_native_create_dma_buf (CoglRenderer     *cogl_renderer,
                                     CoglPixelFormat   format,
                                     uint64_t         *modifiers,
                                     int               n_modifiers,
                                     int               width,
                                     int               height,
                                     GError          **error)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        MetaRenderDevice *render_device;
        MetaDrmBufferFlags flags;
        g_autoptr (MetaDrmBuffer) buffer = NULL;
        int dmabuf_fd;
        uint32_t stride;
        uint32_t offset;
        uint32_t bpp;
        uint32_t drm_format;
        CoglFramebuffer *dmabuf_fb;
        CoglDmaBufHandle *dmabuf_handle;
        const MetaFormatInfo *format_info;

        format_info = meta_format_info_from_cogl_format (format);
        if (!format_info)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Native renderer doesn't support creating DMA buffer with format %s",
                         cogl_pixel_format_to_string (format));
            return NULL;
          }

        drm_format = format_info->drm_format;
        render_device = renderer_gpu_data->render_device;
        flags = META_DRM_BUFFER_FLAG_NONE;
        buffer = meta_render_device_allocate_dma_buf (render_device,
                                                      width, height,
                                                      drm_format,
                                                      modifiers, n_modifiers,
                                                      flags,
                                                      error);
        if (!buffer)
          return NULL;

        dmabuf_fd = meta_drm_buffer_export_fd (buffer, error);
        if (dmabuf_fd == -1)
          return NULL;

        stride = meta_drm_buffer_get_stride (buffer);
        offset = meta_drm_buffer_get_offset (buffer, 0);
        bpp = meta_drm_buffer_get_bpp (buffer);
        if (n_modifiers)
          {
            uint64_t modifier = meta_drm_buffer_get_modifier (buffer);

            dmabuf_fb =
              meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                               dmabuf_fd,
                                                               width, height,
                                                               stride,
                                                               offset,
                                                               &modifier,
                                                               drm_format,
                                                               error);
          }
        else
          {
            dmabuf_fb =
              meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                               dmabuf_fd,
                                                               width, height,
                                                               stride,
                                                               offset,
                                                               NULL,
                                                               drm_format,
                                                               error);
          }

        if (!dmabuf_fb)
          {
            close (dmabuf_fd);
            return NULL;
          }

        dmabuf_handle =
          cogl_dma_buf_handle_new (dmabuf_fb, dmabuf_fd,
                                   width, height, stride, offset, bpp,
                                   g_steal_pointer (&buffer),
                                   g_object_unref);
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
meta_renderer_native_is_dma_buf_supported (CoglRenderer *cogl_renderer)
{
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data = cogl_renderer_egl->platform;
  MetaRenderDevice *render_device = renderer_gpu_data->render_device;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return meta_render_device_is_hardware_accelerated (render_device);
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
#endif
      return FALSE;
    }

  g_assert_not_reached ();
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
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsCursorManager *kms_cursor_manager = meta_kms_get_cursor_manager (kms);
  GList *l;
  g_autoptr (GArray) crtc_layouts = NULL;

  crtc_layouts = g_array_new (FALSE, TRUE, sizeof (MetaKmsCrtcLayout));

  g_clear_list (&renderer_native->pending_mode_set_views, NULL);
  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);

      if (COGL_IS_ONSCREEN (framebuffer))
        {
          MetaOnscreenNative *onscreen_native =
            META_ONSCREEN_NATIVE (framebuffer);
          MetaCrtc *crtc;
          MetaCrtcKms *crtc_kms;
          MetaKmsCrtc *kms_crtc;
          MetaKmsPlane *kms_plane;
          MtkRectangle view_layout;
          float view_scale;
          MetaKmsCrtcLayout crtc_layout;

          crtc = meta_onscreen_native_get_crtc (onscreen_native);
          crtc_kms = META_CRTC_KMS (crtc);

          kms_plane = meta_crtc_kms_get_assigned_cursor_plane (crtc_kms);
          kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);

          clutter_stage_view_get_layout (stage_view, &view_layout);
          view_scale = clutter_stage_view_get_scale (stage_view);

          crtc_layout = (MetaKmsCrtcLayout) {
            .crtc = kms_crtc,
            .cursor_plane = kms_plane,
            .layout = GRAPHENE_RECT_INIT (view_layout.x,
                                          view_layout.y,
                                          view_layout.width,
                                          view_layout.height),
            .scale = view_scale,
          };
          g_array_append_val (crtc_layouts, crtc_layout);

          meta_onscreen_native_invalidate (onscreen_native);
          renderer_native->pending_mode_set_views =
            g_list_prepend (renderer_native->pending_mode_set_views,
                            stage_view);
        }
    }
  renderer_native->pending_mode_set = TRUE;

  meta_kms_cursor_manager_update_crtc_layout (kms_cursor_manager, crtc_layouts);

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
meta_renderer_native_create_offscreen (MetaRendererNative    *renderer_native,
                                       gint                   view_width,
                                       gint                   view_height,
                                       GError               **error)
{
  CoglContext *cogl_context =
    cogl_context_from_renderer_native (renderer_native);
  CoglOffscreen *fb;
  CoglTexture *tex;

  tex = cogl_texture_2d_new_with_size (cogl_context, view_width, view_height);
  cogl_primitive_texture_set_auto_mipmap (tex, FALSE);

  if (!cogl_texture_allocate (tex, error))
    {
      g_object_unref (tex);
      return FALSE;
    }

  fb = cogl_offscreen_new_with_texture (tex);
  g_object_unref (tex);
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

      vtable = *_cogl_winsys_egl_get_vtable ();

      vtable.id = COGL_WINSYS_ID_CUSTOM;
      vtable.name = "EGL_KMS";

      vtable.renderer_connect = meta_renderer_native_connect;
      vtable.renderer_disconnect = meta_renderer_native_disconnect;
      vtable.renderer_create_dma_buf = meta_renderer_native_create_dma_buf;
      vtable.renderer_is_dma_buf_supported =
        meta_renderer_native_is_dma_buf_supported;

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

  if (meta_crtc_native_is_transform_handled (META_CRTC_NATIVE (crtc),
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
  MetaKmsDevice *kms_device = meta_gpu_kms_get_kms_device (primary_gpu);

  if (meta_renderer_is_hardware_accelerated (renderer))
    return FALSE;

  if (!cogl_has_feature (cogl_context, COGL_FEATURE_ID_BLIT_FRAMEBUFFER))
    return FALSE;

  return meta_kms_device_prefers_shadow_buffer (kms_device);
}

static CoglFramebuffer *
create_fallback_offscreen (MetaRendererNative *renderer_native,
                           int                 width,
                           int                 height)
{
  CoglOffscreen *fallback_offscreen;
  GError *error = NULL;

  fallback_offscreen = meta_renderer_native_create_offscreen (renderer_native,
                                                              width,
                                                              height,
                                                              &error);
  if (!fallback_offscreen)
    {
      g_error ("Failed to create fallback offscreen framebuffer: %s",
               error->message);
    }

  return COGL_FRAMEBUFFER (fallback_offscreen);
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
  MtkRectangle view_layout;
  MetaRendererViewNative *view_native;
  EGLSurface egl_surface;
  GError *error = NULL;

  crtc_config = meta_crtc_get_config (crtc);
  crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);
  onscreen_width = crtc_mode_info->width;
  onscreen_height = crtc_mode_info->height;

  if (META_IS_CRTC_KMS (crtc))
    {
      MetaGpuKms *gpu_kms = META_GPU_KMS (meta_crtc_get_gpu (crtc));
      g_autoptr (MetaOnscreenNative) onscreen_native = NULL;

      if (!meta_renderer_native_ensure_gpu_data (renderer_native,
                                                 gpu_kms,
                                                 &error))
        {
          g_warning ("Failed to create secondary GPU data for %s: %s",
                      meta_gpu_kms_get_file_path (gpu_kms),
                      error->message);
          use_shadowfb = FALSE;
          framebuffer = create_fallback_offscreen (renderer_native,
                                                   onscreen_width,
                                                   onscreen_height);
        }
      else
        {
          MetaGpuKms *primary_gpu_kms = renderer_native->primary_gpu_kms;

          onscreen_native = meta_onscreen_native_new (renderer_native,
                                                      primary_gpu_kms,
                                                      output,
                                                      crtc,
                                                      cogl_context,
                                                      onscreen_width,
                                                      onscreen_height);

          if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (onscreen_native), &error))
            {
              g_warning ("Failed to allocate onscreen framebuffer for %s: %s",
                         meta_gpu_kms_get_file_path (gpu_kms),
                         error->message);
              use_shadowfb = FALSE;
              framebuffer = create_fallback_offscreen (renderer_native,
                                                       onscreen_width,
                                                       onscreen_height);
            }
          else
            {
              use_shadowfb = should_force_shadow_fb (renderer_native,
                                                     primary_gpu_kms);
              framebuffer =
                COGL_FRAMEBUFFER (g_steal_pointer (&onscreen_native));
            }
        }
    }
  else
    {
      CoglOffscreen *virtual_onscreen;

      g_assert (META_IS_CRTC_VIRTUAL (crtc));

      virtual_onscreen = meta_renderer_native_create_offscreen (renderer_native,
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
                                                         offscreen_width,
                                                         offscreen_height,
                                                         &error);
      if (!offscreen)
        g_error ("Failed to allocate back buffer texture: %s", error->message);
    }

  if (meta_backend_is_stage_views_scaled (backend))
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

  mtk_rectangle_from_graphene_rect (&crtc_config->layout,
                                    MTK_ROUNDING_STRATEGY_ROUND,
                                    &view_layout);
  view_native = g_object_new (META_TYPE_RENDERER_VIEW_NATIVE,
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
                              "vblank-duration-us", crtc_mode_info->vblank_duration_us,
                              NULL);

  if (META_IS_ONSCREEN_NATIVE (framebuffer))
    {
      CoglDisplayEGL *cogl_display_egl;
      CoglOnscreenEgl *onscreen_egl;

      meta_onscreen_native_set_view (COGL_ONSCREEN (framebuffer),
                                     META_RENDERER_VIEW (view_native));

      /* Ensure we don't point to stale surfaces when creating the offscreen */
      cogl_display_egl = cogl_display->winsys;
      onscreen_egl = COGL_ONSCREEN_EGL (framebuffer);
      egl_surface = cogl_onscreen_egl_get_egl_surface (onscreen_egl);
      _cogl_winsys_egl_make_current (cogl_display,
                                     egl_surface,
                                     egl_surface,
                                     cogl_display_egl->egl_context);
    }

  return META_RENDERER_VIEW (view_native);
}

static void
detach_onscreens (MetaRenderer *renderer)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  GList *views;
  GList *l;

  views = meta_renderer_get_views (renderer);
  for (l = views; l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *onscreen = clutter_stage_view_get_onscreen (stage_view);

      if (META_IS_ONSCREEN_NATIVE (onscreen))
        meta_onscreen_native_detach (META_ONSCREEN_NATIVE (onscreen));

      renderer_native->detached_onscreens =
        g_list_prepend (renderer_native->detached_onscreens,
                        g_object_ref (onscreen));
    }
}

static void
meta_renderer_native_rebuild_views (MetaRenderer *renderer)
{
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaRendererClass *parent_renderer_class =
    META_RENDERER_CLASS (meta_renderer_native_parent_class);

  meta_kms_discard_pending_page_flips (kms);
  g_hash_table_remove_all (renderer_native->mode_set_updates);

  detach_onscreens (renderer);

  parent_renderer_class->rebuild_views (renderer);

  meta_renderer_native_queue_modes_reset (META_RENDERER_NATIVE (renderer));
}

static void
meta_renderer_native_resume (MetaRenderer *renderer)
{
  GList *l;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      CoglFramebuffer *framebuffer;

      framebuffer = clutter_stage_view_get_onscreen (stage_view);
      if (!META_IS_ONSCREEN_NATIVE (framebuffer))
        continue;

      meta_onscreen_native_invalidate (META_ONSCREEN_NATIVE (framebuffer));
    }
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
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));
  MetaPowerSave power_save_mode;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode != META_POWER_SAVE_ON)
    return;

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);

      meta_onscreen_native_prepare_frame (onscreen, frame);
    }
}

void
meta_renderer_native_before_redraw (MetaRendererNative *renderer_native,
                                    MetaRendererView   *view,
                                    ClutterFrame       *frame)
{
  CoglFramebuffer *framebuffer =
    clutter_stage_view_get_onscreen (CLUTTER_STAGE_VIEW (view));

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);

      meta_onscreen_native_before_redraw (onscreen, frame);
    }
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
all_primary_planes_support_format (MetaCrtcKms *crtc_kms,
                                   uint32_t     drm_format)
{
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  gboolean supported = FALSE;
  GList *l;

  for (l = meta_kms_device_get_planes (kms_device); l; l = l->next)
    {
      MetaKmsPlane *kms_plane = l->data;

      if (meta_kms_plane_get_plane_type (kms_plane) !=
          META_KMS_PLANE_TYPE_PRIMARY)
        continue;

      if (!meta_kms_plane_is_usable_with (kms_plane, kms_crtc))
        continue;

      supported = TRUE;

      if (!meta_kms_plane_is_format_supported (kms_plane, drm_format))
        return FALSE;
    }

  return supported;
}


static gboolean
create_secondary_egl_config (MetaEgl                    *egl,
                             MetaRendererNativeGpuData  *renderer_gpu_data,
                             EGLDisplay                  egl_display,
                             EGLConfig                  *egl_config,
                             GError                    **error)
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

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      {
        MetaGpuKms *gpu_kms = renderer_gpu_data->gpu_kms;
        static const uint32_t gles3_formats[] = {
          GBM_FORMAT_ARGB2101010,
          GBM_FORMAT_ABGR2101010,
          GBM_FORMAT_RGBA1010102,
          GBM_FORMAT_BGRA1010102,
          GBM_FORMAT_XRGB8888,
          GBM_FORMAT_ARGB8888,
        };
        int i;

        for (i = 0; i < G_N_ELEMENTS (gles3_formats); i++)
          {
            g_clear_error (error);

            if (gpu_kms)
              {
                GList *l;

                for (l = meta_gpu_get_crtcs (META_GPU (gpu_kms)); l; l = l->next)
                  {
                    MetaCrtcKms *crtc_kms = META_CRTC_KMS (l->data);

                    if (!all_primary_planes_support_format (crtc_kms,
                                                            gles3_formats[i]))
                      break;
                  }

                /* If any CRTC doesn't support the format, we can't use it */
                if (l)
                  {
                    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "KMS CRTC doesn't support GBM format");
                    continue;
                  }
              }

            if (choose_egl_config_from_gbm_format (egl,
                                                   egl_display,
                                                   attributes,
                                                   gles3_formats[i],
                                                   egl_config,
                                                   error))
              {
                MetaDrmFormatBuf format_string;

                meta_drm_format_to_string (&format_string, gles3_formats[i]);
                meta_topic (META_DEBUG_KMS,
                            "Using GBM format %s for secondary GPU EGL",
                            format_string.s);

                return TRUE;
              }
          }

        return FALSE;
      }
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

static void
maybe_restore_cogl_egl_api (MetaRendererNative *renderer_native)
{
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  CoglRenderer *cogl_renderer;

  cogl_context = cogl_context_from_renderer_native (renderer_native);
  if (!cogl_context)
    return;

  cogl_display = cogl_context_get_display (cogl_context);
  cogl_renderer = cogl_display_get_renderer (cogl_display);
  cogl_renderer_bind_api (cogl_renderer);
}

static gboolean
init_secondary_gpu_data_gpu (MetaRendererNativeGpuData *renderer_gpu_data,
                             GError                   **error)
{
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;
  MetaRenderDevice *render_device = renderer_gpu_data->render_device;
  MetaEgl *egl = meta_renderer_native_get_egl (renderer_native);
  gboolean ret = FALSE;
  EGLDisplay egl_display;
  EGLConfig egl_config;
  EGLContext egl_context;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;
  const char **missing_gl_extensions;

  egl_display = meta_render_device_get_egl_display (render_device);
  if (egl_display == EGL_NO_DISPLAY)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No EGL display");
      goto out;
    }

  if (!meta_render_device_is_hardware_accelerated (render_device))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Not hardware accelerated");
      goto out;
    }

  meta_egl_bind_api (egl, EGL_OPENGL_ES_API, NULL);

  if (!create_secondary_egl_config (egl, renderer_gpu_data, egl_display,
                                    &egl_config, error))
    goto out;

  egl_context = create_secondary_egl_context (egl, egl_display, egl_config,
                                              error);
  if (egl_context == EGL_NO_CONTEXT)
    goto out;

  meta_renderer_native_ensure_gles3 (renderer_native);

  if (!meta_egl_make_current (egl,
                              egl_display,
                              EGL_NO_SURFACE,
                              EGL_NO_SURFACE,
                              egl_context,
                              error))
    {
      meta_egl_destroy_context (egl, egl_display, egl_context, NULL);
      goto out;
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

      goto out;
    }

  renderer_gpu_data->secondary.egl_context = egl_context;
  renderer_gpu_data->secondary.egl_config = egl_config;
  renderer_gpu_data->secondary.copy_mode = META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU;

  renderer_gpu_data->secondary.has_EGL_EXT_image_dma_buf_import_modifiers =
    meta_egl_has_extensions (egl, egl_display, NULL,
                             "EGL_EXT_image_dma_buf_import_modifiers",
                             NULL);
  ret = TRUE;
out:
  maybe_restore_cogl_egl_api (renderer_native);
  cogl_context = cogl_context_from_renderer_native (renderer_native);
  if (cogl_context)
    {
      cogl_display = cogl_context_get_display (cogl_context);
      _cogl_winsys_egl_ensure_current (cogl_display);
    }
  return ret;
}

static void
init_secondary_gpu_data_cpu (MetaRendererNativeGpuData *renderer_gpu_data)
{
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
  return meta_render_device_is_hardware_accelerated (data->render_device);
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_gbm (MetaRendererNative *renderer_native,
                              MetaRenderDevice   *render_device,
                              MetaGpuKms         *gpu_kms)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_GBM;
  renderer_gpu_data->render_device = render_device;
  renderer_gpu_data->gpu_kms = gpu_kms;

  init_secondary_gpu_data (renderer_gpu_data);
  return renderer_gpu_data;
}

static MetaRendererNativeGpuData *
create_renderer_gpu_data_surfaceless (MetaRendererNative  *renderer_native,
                                      GError             **error)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaRenderDeviceSurfaceless *render_device_surfaceless;
  MetaRendererNativeGpuData *renderer_gpu_data;

  render_device_surfaceless = meta_render_device_surfaceless_new (backend,
                                                                  error);
  if (!render_device_surfaceless)
    return NULL;

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_SURFACELESS;
  renderer_gpu_data->render_device =
    META_RENDER_DEVICE (render_device_surfaceless);

  return renderer_gpu_data;
}

#ifdef HAVE_EGL_DEVICE
static MetaRendererNativeGpuData *
create_renderer_gpu_data_egl_device (MetaRendererNative  *renderer_native,
                                     MetaRenderDevice    *render_device,
                                     MetaGpuKms          *gpu_kms)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = meta_create_renderer_native_gpu_data ();
  renderer_gpu_data->renderer_native = renderer_native;
  renderer_gpu_data->mode = META_RENDERER_NATIVE_MODE_EGL_DEVICE;
  renderer_gpu_data->render_device = render_device;
  renderer_gpu_data->gpu_kms = gpu_kms;

  return renderer_gpu_data;
}
#endif /* HAVE_EGL_DEVICE */

static void
on_crtc_needs_flush (MetaKmsDevice *kms_device,
                     MetaKmsCrtc   *kms_crtc,
                     MetaRenderer  *renderer)
{
  MetaCrtc *crtc = META_CRTC (meta_crtc_kms_from_kms_crtc (kms_crtc));
  MetaRendererView *view;

  view = meta_renderer_get_view_for_crtc (renderer, crtc);
  if (view)
    clutter_stage_view_schedule_update (CLUTTER_STAGE_VIEW (view));
}

static MetaRendererNativeGpuData *
meta_renderer_native_create_renderer_gpu_data (MetaRendererNative  *renderer_native,
                                               MetaGpuKms          *gpu_kms,
                                               GError             **error)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  const char *device_path;
  MetaRenderDevice *render_device;
  MetaRendererNativeGpuData *renderer_gpu_data;

  if (!gpu_kms)
    return create_renderer_gpu_data_surfaceless (renderer_native, error);

  device_path = meta_gpu_kms_get_file_path (gpu_kms);
  render_device = meta_backend_native_take_render_device (backend_native,
                                                          device_path,
                                                          error);
  if (!render_device)
    {
      return NULL;
    }

  if (META_IS_RENDER_DEVICE_GBM (render_device))
    {
      renderer_gpu_data = create_renderer_gpu_data_gbm (renderer_native,
                                                        render_device,
                                                        gpu_kms);
    }
#ifdef HAVE_EGL_DEVICE
  else if (META_IS_RENDER_DEVICE_EGL_STREAM (render_device))
    {
      renderer_gpu_data = create_renderer_gpu_data_egl_device (renderer_native,
                                                               render_device,
                                                               gpu_kms);
    }
#endif
  else
    {
      g_assert_not_reached ();
      return NULL;
    }

  renderer_gpu_data->crtc_needs_flush_handler_id =
    g_signal_connect (meta_gpu_kms_get_kms_device (gpu_kms),
                      "crtc-needs-flush",
                      G_CALLBACK (on_crtc_needs_flush),
                      renderer_native);
  return renderer_gpu_data;
}

static const char *
renderer_data_mode_to_string (MetaRendererNativeMode mode)
{
  switch (mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return "gbm";
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      return "surfaceless";
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return "egldevice";
#endif
    }

  g_assert_not_reached ();
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

  if (gpu_kms)
    {
      g_message ("Created %s renderer for '%s'",
                 renderer_data_mode_to_string (renderer_gpu_data->mode),
                 meta_gpu_kms_get_file_path (gpu_kms));
    }
  else
    {
      g_message ("Created %s renderer without GPU",
                 renderer_data_mode_to_string (renderer_gpu_data->mode));
    }

  g_hash_table_insert (renderer_native->gpu_datas,
                       gpu_kms,
                       renderer_gpu_data);

  return TRUE;
}

static gboolean
meta_renderer_native_ensure_gpu_data (MetaRendererNative  *renderer_native,
                                      MetaGpuKms          *gpu_kms,
                                      GError             **error)
{
  MetaRendererNativeGpuData *renderer_gpu_data;

  renderer_gpu_data = g_hash_table_lookup (renderer_native->gpu_datas, gpu_kms);
  if (renderer_gpu_data)
    return TRUE;

  return create_renderer_gpu_data (renderer_native, gpu_kms, error);
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
on_power_save_mode_changed (MetaMonitorManager        *monitor_manager,
                            MetaPowerSaveChangeReason  reason,
                            MetaRendererNative        *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaPowerSave power_save_mode;

  power_save_mode = meta_monitor_manager_get_power_save_mode (monitor_manager);
  if (power_save_mode == META_POWER_SAVE_ON &&
      reason == META_POWER_SAVE_CHANGE_REASON_MODE_CHANGE)
    meta_renderer_native_queue_modes_reset (renderer_native);
  else
    meta_kms_discard_pending_page_flips (kms);
}

void
meta_renderer_native_unset_modes (MetaRendererNative *renderer_native)
{
  MetaRenderer *renderer = META_RENDERER (renderer_native);
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  GList *l;

  meta_topic (META_DEBUG_KMS, "Unsetting all CRTC modes");

  g_hash_table_remove_all (renderer_native->mode_set_updates);

  for (l = meta_backend_get_gpus (backend); l; l = l->next)
    {
      MetaGpu *gpu = l->data;
      MetaKmsDevice *kms_device =
        meta_gpu_kms_get_kms_device (META_GPU_KMS (gpu));
      GList *k;
      g_autoptr (MetaKmsFeedback) kms_feedback = NULL;
      MetaKmsUpdate *kms_update = NULL;

      for (k = meta_gpu_get_crtcs (gpu); k; k = k->next)
        {
          MetaCrtc *crtc = k->data;

          g_warn_if_fail (!meta_crtc_get_config (crtc));

          kms_update = ensure_mode_set_update (renderer_native, kms_device);
          meta_crtc_kms_set_mode (META_CRTC_KMS (crtc), kms_update);
        }
    }

  post_mode_set_updates (renderer_native);
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
  MetaRenderDevice *render_device;

  gpu_kms = choose_primary_gpu_unchecked (backend, renderer_native);
  renderer_gpu_data = meta_renderer_native_get_gpu_data (renderer_native,
                                                         gpu_kms);
  render_device = renderer_gpu_data->render_device;
  if (meta_render_device_get_egl_display (render_device) == EGL_NO_DISPLAY)
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
      MetaKmsDevice *kms_device;
      MetaKmsDeviceFlag flags;
      const char *kms_modifiers_debug_env;

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

      kms_device = meta_gpu_kms_get_kms_device (renderer_native->primary_gpu_kms);
      flags = meta_kms_device_get_flags (kms_device);
      renderer_native->has_addfb2 = !!(flags & META_KMS_DEVICE_FLAG_HAS_ADDFB2);

      kms_modifiers_debug_env = g_getenv ("MUTTER_DEBUG_USE_KMS_MODIFIERS");
      if (kms_modifiers_debug_env)
        {
          renderer_native->use_modifiers =
            g_strcmp0 (kms_modifiers_debug_env, "1") == 0;
        }
      else
        {
          renderer_native->use_modifiers =
            !(flags & META_KMS_DEVICE_FLAG_DISABLE_MODIFIERS) &&
            renderer_native->has_addfb2;
        }

      meta_topic (META_DEBUG_KMS, "Usage of KMS modifiers is %s",
                  renderer_native->use_modifiers ? "enabled" : "disabled");

      kms_modifiers_debug_env = g_getenv ("MUTTER_DEBUG_SEND_KMS_MODIFIERS");
      if (kms_modifiers_debug_env)
        {
          renderer_native->send_modifiers =
            g_strcmp0 (kms_modifiers_debug_env, "1") == 0;
        }
      else
        {
          renderer_native->send_modifiers =
            !(flags & META_KMS_DEVICE_FLAG_DISABLE_CLIENT_MODIFIERS);
        }

      meta_topic (META_DEBUG_KMS, "Sending KMS modifiers to clients is %s",
                  renderer_native->send_modifiers ? "enabled" : "disabled");
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

  g_clear_list (&renderer_native->power_save_page_flip_onscreens,
                g_object_unref);
  g_clear_handle_id (&renderer_native->power_save_page_flip_source_id,
                     g_source_remove);

  g_list_free (renderer_native->pending_mode_set_views);
  g_hash_table_unref (renderer_native->mode_set_updates);

  g_clear_handle_id (&renderer_native->release_unused_gpus_idle_id,
                     g_source_remove);
  clear_detached_onscreens (renderer_native);

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
  renderer_native->mode_set_updates =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_kms_update_free);
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
  renderer_class->resume = meta_renderer_native_resume;
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
