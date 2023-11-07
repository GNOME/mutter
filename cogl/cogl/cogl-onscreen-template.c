/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
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
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-template-private.h"

#include <stdlib.h>

G_DEFINE_TYPE (CoglOnscreenTemplate, cogl_onscreen_template, G_TYPE_OBJECT);

static void
cogl_onscreen_template_init (CoglOnscreenTemplate *onscreen_template)
{
}

static void
cogl_onscreen_template_class_init (CoglOnscreenTemplateClass *class)
{
}

CoglOnscreenTemplate *
cogl_onscreen_template_new (CoglSwapChain *swap_chain)
{
  CoglOnscreenTemplate *onscreen_template = g_object_new (COGL_TYPE_ONSCREEN_TEMPLATE, NULL);
  char *user_config;

  onscreen_template->config.swap_chain = swap_chain;
  if (swap_chain)
    g_object_ref (swap_chain);
  else
    onscreen_template->config.swap_chain = cogl_swap_chain_new ();

  onscreen_template->config.need_stencil = TRUE;
  onscreen_template->config.samples_per_pixel = 0;

  user_config = getenv ("COGL_POINT_SAMPLES_PER_PIXEL");
  if (user_config)
    {
      unsigned long samples_per_pixel = strtoul (user_config, NULL, 10);
      if (samples_per_pixel != ULONG_MAX)
        onscreen_template->config.samples_per_pixel =
          samples_per_pixel;
    }

  return onscreen_template;
}

void
cogl_onscreen_template_set_samples_per_pixel (
                                        CoglOnscreenTemplate *onscreen_template,
                                        int samples_per_pixel)
{
  onscreen_template->config.samples_per_pixel = samples_per_pixel;
}

void
cogl_onscreen_template_set_stereo_enabled (
					   CoglOnscreenTemplate *onscreen_template,
					   gboolean enabled)
{
  onscreen_template->config.stereo_enabled = enabled;
}
