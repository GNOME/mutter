/*
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#ifndef META_RENDER_DEVICE_EGL_STREAM_H
#define META_RENDER_DEVICE_EGL_STREAM_H

#include "backends/native/meta-render-device-private.h"

#define META_TYPE_RENDER_DEVICE_EGL_STREAM (meta_render_device_egl_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaRenderDeviceEglStream, meta_render_device_egl_stream,
                      META, RENDER_DEVICE_EGL_STREAM,
                      MetaRenderDevice)

MetaRenderDeviceEglStream * meta_render_device_egl_stream_new (MetaBackend     *backend,
                                                               MetaDeviceFile  *device_file,
                                                               GError         **error);

#endif /* META_RENDER_DEVICE_EGL_STREAM_H */
