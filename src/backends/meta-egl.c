/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016, 2017 Red Hat Inc.
 * Copyright (C) 2018, 2019 DisplayLink (UK) Ltd.
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

#include "config.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglmesaext.h>
#include <EGL/eglplatform.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-egl.h"
#include "backends/meta-egl-ext.h"
#include "meta/util.h"

struct _MetaEgl
{
  GObject parent;
};

G_DEFINE_TYPE (MetaEgl, meta_egl, G_TYPE_OBJECT)

gboolean
meta_extensions_string_has_extensions_valist (const char   *extensions_str,
                                              const char ***missing_extensions,
                                              const char   *first_extension,
                                              va_list       var_args)
{
  char **extensions;
  const char *extension;
  size_t num_missing_extensions = 0;

  if (missing_extensions)
    *missing_extensions = NULL;

  extensions = g_strsplit (extensions_str, " ", -1);

  extension = first_extension;
  while (extension)
    {
      if (!g_strv_contains ((const char * const *) extensions, extension))
        {
          num_missing_extensions++;
          if (missing_extensions)
            {
              *missing_extensions = g_realloc_n (*missing_extensions,
                                                 num_missing_extensions + 1,
                                                 sizeof (const char *));
              (*missing_extensions)[num_missing_extensions - 1] = extension;
              (*missing_extensions)[num_missing_extensions] = NULL;
            }
          else
            {
              break;
            }
        }
      extension = va_arg (var_args, char *);
    }

  g_strfreev (extensions);

  return num_missing_extensions == 0;
}

gpointer
meta_egl_get_proc_address (MetaEgl    *egl,
                           const char *procname,
                           GError    **error)
{
  gpointer func;

  func = (gpointer) eglGetProcAddress (procname);
  if (!func)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not load symbol '%s': Not found",
                   procname);
      return NULL;
    }

  return func;
}

static void
meta_egl_init (MetaEgl *egl)
{
}

static void
meta_egl_class_init (MetaEglClass *klass)
{
}
