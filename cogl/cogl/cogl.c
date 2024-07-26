/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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
 *
 */

#include "config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "cogl/cogl-cpu-caps.h"
#include "cogl/cogl-debug.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-bitmap-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-offscreen.h"
#include "cogl/winsys/cogl-winsys-private.h"
#include "cogl/cogl-mutter.h"

gboolean
_cogl_check_extension (const char *name, char * const *ext)
{
  while (*ext)
    if (!strcmp (name, *ext))
      return TRUE;
    else
      ext++;

  return FALSE;
}

uint32_t
_cogl_driver_error_quark (void)
{
  return g_quark_from_static_string ("cogl-driver-error-quark");
}

uint32_t
_cogl_system_error_quark (void)
{
  return g_quark_from_static_string ("cogl-system-error-quark");
}

void
cogl_init (void)
{
  static gboolean initialized = FALSE;

  if (initialized == FALSE)
    {
      _cogl_debug_check_environment ();
      cogl_init_cpu_caps ();
      initialized = TRUE;
    }
}
