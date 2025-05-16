/*
 * Copyright (C) 2024 Red Hat, Inc.
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
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "backends/meta-renderdoc.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-output.h"
#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-stage-native.h"
#endif
#ifdef HAVE_X11
#include "backends/x11/meta-stage-x11.h"
#endif

#include <dlfcn.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "third_party/renderdoc/renderdoc_app.h"
#pragma GCC diagnostic pop

struct _MetaRenderdoc
{
  GObject parent;

  MetaBackend *backend;
  GHashTable *queued_views;
  gboolean connected;
  RENDERDOC_API_1_1_2 *api;
};

G_DEFINE_FINAL_TYPE (MetaRenderdoc,
                     meta_renderdoc,
                     G_TYPE_OBJECT)

static void
capture_onscreen_start (MetaRenderdoc    *renderdoc,
                        ClutterStageView *stage_view,
                        CoglOnscreen     *onscreen)
{
  g_autofree char *path_template = NULL;
  gpointer device, window;

  if (!cogl_onscreen_get_window_handles (onscreen, &device, &window))
    {
      g_warning ("Getting device and window handles from onscreen failed");
      return;
    }

  path_template = g_strdup_printf ("mutter_view_%p", stage_view);

  meta_topic (META_DEBUG_BACKEND,
              "Renderdoc is starting to capture of %p %p to %s",
              device, window,
              path_template);

  renderdoc->api->SetCaptureFilePathTemplate (path_template);

  renderdoc->api->StartFrameCapture (device, window);
}

static void
capture_onscreen_end (MetaRenderdoc    *renderdoc,
                      ClutterStageView *stage_view,
                      CoglOnscreen     *onscreen)
{
  gpointer device, window;

  if (!cogl_onscreen_get_window_handles (onscreen, &device, &window))
    return;

  meta_topic (META_DEBUG_BACKEND, "Renderdoc is ending capture of %p %p",
              device, window);

  renderdoc->api->EndFrameCapture (device, window);
}

static void
meta_renderdoc_capture_start (MetaRenderdoc    *renderdoc,
                              ClutterStageView *stage_view)
{
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (renderdoc->backend);
  ClutterStageWindow *stage_window =
    clutter_backend_get_stage_window (clutter_backend);

  g_return_if_fail (renderdoc->api != NULL);

  if (!g_hash_table_contains (renderdoc->queued_views, stage_view))
    return;

#ifdef HAVE_NATIVE_BACKEND
  if (META_IS_STAGE_NATIVE (stage_window))
    {
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);

      capture_onscreen_start (renderdoc,
                              stage_view,
                              COGL_ONSCREEN (framebuffer));
    }
  else
#endif
#ifdef HAVE_X11
  if (META_IS_STAGE_X11 (stage_window))
    {
      MetaStageX11 *stage_x11 = META_STAGE_X11 (stage_window);

      capture_onscreen_start (renderdoc,
                              stage_view,
                              COGL_ONSCREEN (stage_x11->onscreen));
    }
  else
#endif
    {
      g_warning ("capturing stage of type %s is not supported",
                 G_OBJECT_TYPE_NAME (stage_window));
    }
}

static void
meta_renderdoc_capture_end (MetaRenderdoc    *renderdoc,
                            ClutterStageView *stage_view)
{
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (renderdoc->backend);
  ClutterStageWindow *stage_window =
    clutter_backend_get_stage_window (clutter_backend);

  g_return_if_fail (renderdoc->api != NULL);

  if (!g_hash_table_contains (renderdoc->queued_views, stage_view))
    return;

  g_hash_table_remove (renderdoc->queued_views, stage_view);

#ifdef HAVE_NATIVE_BACKEND
  if (META_IS_STAGE_NATIVE (stage_window))
    {
      CoglFramebuffer *framebuffer =
        clutter_stage_view_get_onscreen (stage_view);

      capture_onscreen_end (renderdoc,
                            stage_view,
                            COGL_ONSCREEN (framebuffer));
    }
  else
#endif
#ifdef HAVE_X11
  if (META_IS_STAGE_X11 (stage_window))
    {
      MetaStageX11 *stage_x11 = META_STAGE_X11 (stage_window);

      capture_onscreen_end (renderdoc,
                            stage_view,
                            COGL_ONSCREEN (stage_x11->onscreen));
    }
  else
#endif
    {
    }
}

static void
on_before_update (ClutterStage     *stage,
                  ClutterStageView *stage_view,
                  ClutterFrame     *frame,
                  MetaRenderdoc    *renderdoc)
{
  meta_renderdoc_capture_start (renderdoc, stage_view);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 ClutterFrame     *frame,
                 MetaRenderdoc    *renderdoc)
{
  meta_renderdoc_capture_end (renderdoc, stage_view);
}

static void
ensure_signals (MetaRenderdoc *renderdoc)
{
  ClutterActor *stage = meta_backend_get_stage (renderdoc->backend);

  g_return_if_fail (stage != NULL);

  if (renderdoc->connected)
    return;

  g_signal_connect_object (stage,
                           "before-update",
                           G_CALLBACK (on_before_update),
                           renderdoc,
                           G_CONNECT_DEFAULT);
  g_signal_connect_object (stage,
                           "after-update",
                           G_CALLBACK (on_after_update),
                           renderdoc,
                           G_CONNECT_DEFAULT);

  renderdoc->connected = TRUE;
}

static RENDERDOC_API_1_1_2 *
find_api (void)
{
  pRENDERDOC_GetAPI renderdoc_get_api;
  RENDERDOC_API_1_1_2 *renderdoc_api = NULL;
  void *lib;
  int ret;

  lib = dlopen ("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!lib)
    {
      meta_topic (META_DEBUG_BACKEND,
                  "No renderdoc capture support (librenderdoc.so missing)");
      return NULL;
    }

  renderdoc_get_api = dlsym (lib, "RENDERDOC_GetAPI");
  if (!renderdoc_get_api)
    {
      g_warning ("Could not get RENDERDOC_GetAPI from librenderdoc.so");
      return NULL;
    }

  ret = renderdoc_get_api (eRENDERDOC_API_Version_1_1_2,
                               (void **)&renderdoc_api);
  if (ret != 1)
    {
      g_warning ("Could not get renderdoc API version 1.1.2");
      return NULL;
    }

  meta_topic (META_DEBUG_BACKEND, "Renderdoc is ready to capture");

  return renderdoc_api;
}

static void
meta_renderdoc_dispose (GObject *object)
{
  MetaRenderdoc *renderdoc = META_RENDERDOC (object);

  g_clear_pointer (&renderdoc->queued_views, g_hash_table_unref);

  G_OBJECT_CLASS (meta_renderdoc_parent_class)->dispose (object);
}

static void
meta_renderdoc_init (MetaRenderdoc *renderdoc)
{
}

static void
meta_renderdoc_class_init (MetaRenderdocClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_renderdoc_dispose;
}

MetaRenderdoc *
meta_renderdoc_new (MetaBackend *backend)
{
  g_autoptr (MetaRenderdoc) renderdoc = NULL;

  renderdoc = g_object_new (META_TYPE_RENDERDOC, NULL);
  renderdoc->backend = backend;
  renderdoc->api = find_api ();
  renderdoc->queued_views = g_hash_table_new (NULL, NULL);

  return g_steal_pointer (&renderdoc);
}

void
meta_renderdoc_queue_capture_all (MetaRenderdoc *renderdoc)
{
  MetaRenderer *renderer = meta_backend_get_renderer (renderdoc->backend);
  GList *l;

  ensure_signals (renderdoc);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *view = CLUTTER_STAGE_VIEW (l->data);

      g_hash_table_add (renderdoc->queued_views, view);
    }
}
