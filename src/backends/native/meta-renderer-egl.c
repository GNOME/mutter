/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include "backends/native/meta-renderer-egl.h"

struct _MetaRendererEGL
{
  CoglRendererEgl parent_instance;
};

G_DEFINE_FINAL_TYPE (MetaRendererEGL, meta_renderer_egl, COGL_TYPE_RENDERER_EGL)

static void
meta_renderer_egl_class_init (MetaRendererEGLClass *klass)
{
}

static void
meta_renderer_egl_init (MetaRendererEGL *renderer_egl)
{
}

MetaRendererEGL *
meta_renderer_egl_new (void)
{
  return g_object_new (META_TYPE_RENDERER_EGL, NULL);
}
