/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat Inc.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglmesaext.h>
#include <glib-object.h>

#define META_TYPE_EGL (meta_egl_get_type ())
G_DECLARE_FINAL_TYPE (MetaEgl, meta_egl, META, EGL, GObject)


gboolean
meta_extensions_string_has_extensions_valist (const char   *extensions_str,
                                              const char ***missing_extensions,
                                              const char   *first_extension,
                                              va_list       var_args);

gpointer meta_egl_get_proc_address (MetaEgl    *egl,
                                    const char *procname,
                                    GError    **error);
