/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009 Intel Corporation.
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

#include <stdlib.h>

#include "cogl/cogl-private.h"
#include "cogl/cogl-debug.h"

/* XXX: If you add a debug option, please also add an option
 * definition to cogl-debug-options.h. This will enable us - for
 * example - to emit a "help" description for the option.
 */

/* NB: Only these options get enabled if COGL_DEBUG=all is
 * used since they don't affect the behaviour of Cogl they
 * simply print out verbose information */
static const GDebugKey cogl_log_debug_keys[] = {
  { "object", COGL_DEBUG_OBJECT },
  { "slicing", COGL_DEBUG_SLICING },
  { "atlas", COGL_DEBUG_ATLAS },
  { "blend-strings", COGL_DEBUG_BLEND_STRINGS },
  { "journal", COGL_DEBUG_JOURNAL },
  { "batching", COGL_DEBUG_BATCHING },
  { "matrices", COGL_DEBUG_MATRICES },
  { "draw", COGL_DEBUG_DRAW },
  { "opengl", COGL_DEBUG_OPENGL },
  { "show-source", COGL_DEBUG_SHOW_SOURCE},
  { "framebuffer", COGL_DEBUG_FRAMEBUFFER },
  { "offscreen", COGL_DEBUG_OFFSCREEN },
  { "texture-pixmap", COGL_DEBUG_TEXTURE_PIXMAP },
  { "bitmap", COGL_DEBUG_BITMAP },
  { "clipping", COGL_DEBUG_CLIPPING },
  { "winsys", COGL_DEBUG_WINSYS },
  { "performance", COGL_DEBUG_PERFORMANCE },
  { "textures", COGL_DEBUG_TEXTURES },
};
static const int n_cogl_log_debug_keys =
  G_N_ELEMENTS (cogl_log_debug_keys);

static const GDebugKey cogl_behavioural_debug_keys[] = {
  { "rectangles", COGL_DEBUG_RECTANGLES },
  { "disable-batching", COGL_DEBUG_DISABLE_BATCHING },
  { "disable-pbos", COGL_DEBUG_DISABLE_PBOS },
  { "disable-software-transform", COGL_DEBUG_DISABLE_SOFTWARE_TRANSFORM },
  { "disable-atlas", COGL_DEBUG_DISABLE_ATLAS },
  { "disable-texturing", COGL_DEBUG_DISABLE_TEXTURING},
  { "disable-blending", COGL_DEBUG_DISABLE_BLENDING},
  { "wireframe", COGL_DEBUG_WIREFRAME},
  { "disable-software-clip", COGL_DEBUG_DISABLE_SOFTWARE_CLIP},
  { "disable-program-caches", COGL_DEBUG_DISABLE_PROGRAM_CACHES},
  { "disable-fast-read-pixel", COGL_DEBUG_DISABLE_FAST_READ_PIXEL},
  { "sync-primitive", COGL_DEBUG_SYNC_PRIMITIVE },
  { "sync-frame", COGL_DEBUG_SYNC_FRAME},
  { "stencilling", COGL_DEBUG_STENCILLING },
};
static const int n_cogl_behavioural_debug_keys =
  G_N_ELEMENTS (cogl_behavioural_debug_keys);

unsigned long _cogl_debug_flags[COGL_DEBUG_N_LONGS];
GHashTable *_cogl_debug_instances;

static void
_cogl_parse_debug_string_for_keys (const char *value,
                                   gboolean enable,
                                   const GDebugKey *keys,
                                   unsigned int nkeys)
{
  int long_num, key_num;

  /* g_parse_debug_string expects the value field in GDebugKey to be a
     mask in an unsigned int but the flags are stored in an array of
     multiple longs so we need to build a separate array for each
     possible unsigned int */

  for (long_num = 0; long_num < COGL_DEBUG_N_LONGS; long_num++)
    {
      int int_num;

      for (int_num = 0;
           int_num < sizeof (unsigned long) / sizeof (unsigned int);
           int_num++)
        {
          GDebugKey keys_for_int[sizeof (unsigned int) * 8];
          int nkeys_for_int = 0;

          for (key_num = 0; key_num < nkeys; key_num++)
            {
              int long_index = COGL_FLAGS_GET_INDEX (keys[key_num].value);
              int int_index = (keys[key_num].value %
                               (sizeof (unsigned long) * 8) /
                               (sizeof (unsigned int) * 8));

              if (long_index == long_num && int_index == int_num)
                {
                  keys_for_int[nkeys_for_int] = keys[key_num];
                  keys_for_int[nkeys_for_int].value =
                    COGL_FLAGS_GET_MASK (keys[key_num].value) >>
                    (int_num * sizeof (unsigned int) * 8);
                  nkeys_for_int++;
                }
            }

          if (nkeys_for_int > 0)
            {
              unsigned long mask =
                ((unsigned long) g_parse_debug_string (value,
                                                       keys_for_int,
                                                       nkeys_for_int)) <<
                (int_num * sizeof (unsigned int) * 8);

              if (enable)
                _cogl_debug_flags[long_num] |= mask;
              else
                _cogl_debug_flags[long_num] &= ~mask;
            }
        }
    }
}

void
_cogl_parse_debug_string (const char *value,
                          gboolean    enable)
{
  /* We don't want to let g_parse_debug_string handle "all" because
   * literally enabling all the debug options wouldn't be useful to
   * anyone; instead the all option enables all non behavioural
   * options.
   */
  if (strcmp (value, "all") == 0 ||
      strcmp (value, "verbose") == 0)
    {
      int i;
      for (i = 0; i < n_cogl_log_debug_keys; i++)
        if (enable)
          COGL_DEBUG_SET_FLAG (cogl_log_debug_keys[i].value);
        else
          COGL_DEBUG_CLEAR_FLAG (cogl_log_debug_keys[i].value);
    }
  else if (g_ascii_strcasecmp (value, "help") == 0)
    {
      g_printerr ("\n\n%28s\n", "Supported debug values:");
#define OPT(MASK_NAME, GROUP, NAME, NAME_FORMATTED, DESCRIPTION) \
      g_printerr ("%28s %s\n", NAME ":", DESCRIPTION);
      g_printerr ("\n%28s\n", "Special debug values:");
      OPT (IGNORED, "ignored", "all", "ignored", \
           "Enables all non-behavioural debug options");
      OPT (IGNORED, "ignored", "verbose", "ignored", \
           "Enables all non-behavioural debug options");
#undef OPT

      g_printerr ("\n"
                  "%28s\n"
                  " COGL_DISABLE_GL_EXTENSIONS: %s\n"
                  "   COGL_OVERRIDE_GL_VERSION: %s\n",
                  "Additional environment variables:",
                  "Comma-separated list of GL extensions to pretend are disabled",
                  "Override the GL version that Cogl will assume the driver supports");
      exit (1);
    }
  else
    {
      _cogl_parse_debug_string_for_keys (value,
                                         enable,
                                         cogl_log_debug_keys,
                                         n_cogl_log_debug_keys);
      _cogl_parse_debug_string_for_keys (value,
                                         enable,
                                         cogl_behavioural_debug_keys,
                                         n_cogl_behavioural_debug_keys);
    }
}

void
_cogl_debug_check_environment (void)
{
  const char *env_string;

  env_string = g_getenv ("COGL_DEBUG");
  if (env_string != NULL)
    {
      _cogl_parse_debug_string (env_string,
                                TRUE /* enable the flags */);
      env_string = NULL;
    }

  env_string = g_getenv ("COGL_NO_DEBUG");
  if (env_string != NULL)
    {
      _cogl_parse_debug_string (env_string,
                                FALSE /* disable the flags */);
      env_string = NULL;
    }
}
