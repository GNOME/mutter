/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "compositor/meta-compositor-native.h"

#include "compositor/meta-compositor-view-native.h"

struct _MetaCompositorNative
{
  MetaCompositorServer parent;
};

G_DEFINE_TYPE (MetaCompositorNative, meta_compositor_native,
               META_TYPE_COMPOSITOR_SERVER)

static void
meta_compositor_native_before_paint (MetaCompositor     *compositor,
                                     MetaCompositorView *compositor_view)
{
  MetaCompositorViewNative *compositor_view_native =
    META_COMPOSITOR_VIEW_NATIVE (compositor_view);
  MetaCompositorClass *parent_class;

#ifdef HAVE_WAYLAND
  meta_compositor_view_native_maybe_assign_scanout (compositor_view_native,
                                                    compositor);
#endif

  meta_compositor_view_native_maybe_update_frame_sync_surface (compositor_view_native,
                                                               compositor);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_native_parent_class);
  parent_class->before_paint (compositor, compositor_view);
}

static MetaCompositorView *
meta_compositor_native_create_view (MetaCompositor   *compositor,
                                    ClutterStageView *stage_view)
{
  MetaCompositorViewNative *compositor_view_native;

  compositor_view_native = meta_compositor_view_native_new (stage_view);

  return META_COMPOSITOR_VIEW (compositor_view_native);
}

MetaCompositorNative *
meta_compositor_native_new (MetaDisplay *display,
                            MetaBackend *backend)
{
  return g_object_new (META_TYPE_COMPOSITOR_NATIVE,
                       "display", display,
                       "backend", backend,
                       NULL);
}

static void
meta_compositor_native_init (MetaCompositorNative *compositor_native)
{
}

static void
meta_compositor_native_class_init (MetaCompositorNativeClass *klass)
{
  MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

  compositor_class->before_paint = meta_compositor_native_before_paint;
  compositor_class->create_view = meta_compositor_native_create_view;
}
