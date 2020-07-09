/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010,2011  Intel Corporation.
 *               2011 Giovanni Campagna <scampa.giovanni@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.

 * Authors:
 *  Matthew Allum
 *  Emmanuele Bassi
 *  Robert Bragg
 *  Neil Roberts
 */

#include "clutter-build-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>

#include "clutter-backend-eglnative.h"

/* This is a Cogl based backend */
#include "cogl/clutter-stage-cogl.h"

#include "clutter-debug.h"
#include "clutter-private.h"
#include "clutter-main.h"
#include "clutter-stage-private.h"

G_DEFINE_TYPE (ClutterBackendEglNative, clutter_backend_egl_native, CLUTTER_TYPE_BACKEND);

static void
clutter_backend_egl_native_class_init (ClutterBackendEglNativeClass *klass)
{
}

static void
clutter_backend_egl_native_init (ClutterBackendEglNative *backend_egl_native)
{
}

ClutterBackend *
clutter_backend_egl_native_new (void)
{
  return g_object_new (CLUTTER_TYPE_BACKEND_EGL_NATIVE, NULL);
}
