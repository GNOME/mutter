/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010 Intel Corporation.
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

#include "cogl/cogl-xlib-renderer.h"
#include "cogl/cogl-util.h"

#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-xlib-renderer-private.h"
#include "cogl/winsys/cogl-winsys-private.h"
#include "mtk/mtk-x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrandr.h>

#include <stdlib.h>
#include <string.h>

CoglXlibRenderer *
_cogl_xlib_renderer_get_data (CoglRenderer *renderer)
{
  /* Constructs a CoglXlibRenderer struct on demand and attaches it to
     the object using user data. It's done this way instead of using a
     subclassing hierarchy in the winsys data because all EGL winsys's
     need the EGL winsys data but only one of them wants the Xlib
     data. */

  if (!cogl_renderer_get_custom_winsys_data (renderer))
    cogl_renderer_set_custom_winsys_data (renderer,  g_new0 (CoglXlibRenderer, 1));

  return cogl_renderer_get_custom_winsys_data (renderer);
}

static void
free_xlib_output (CoglXlibOutput *output)
{
  if (!output)
    return;

  g_clear_pointer (&output->name, g_free);
  g_clear_pointer (&output, g_free);
}

static gboolean
output_values_equal (CoglXlibOutput *output,
                     CoglXlibOutput *other)
{
  if (output == other)
    return TRUE;

  return memcmp ((const char *)output + G_STRUCT_OFFSET (CoglXlibOutput, x),
                 (const char *)other + G_STRUCT_OFFSET (CoglXlibOutput, x),
                 sizeof (CoglXlibOutput) - G_STRUCT_OFFSET (CoglXlibOutput, x)) == 0;
}

static int
compare_outputs (CoglXlibOutput *a,
                 CoglXlibOutput *b)
{
  return strcmp (a->name, b->name);
}

#define CSO(X) SubPixel ## X
static SubpixelOrder subpixel_map[6][6] = {
  { CSO(Unknown), CSO(None), CSO(HorizontalRGB), CSO(HorizontalBGR),
    CSO(VerticalRGB),   CSO(VerticalBGR) },   /* 0 */
  { CSO(Unknown), CSO(None), CSO(VerticalRGB),   CSO(VerticalBGR),
    CSO(HorizontalBGR), CSO(HorizontalRGB) }, /* 90 */
  { CSO(Unknown), CSO(None), CSO(HorizontalBGR), CSO(HorizontalRGB),
    CSO(VerticalBGR),   CSO(VerticalRGB) },   /* 180 */
  { CSO(Unknown), CSO(None), CSO(VerticalBGR),   CSO(VerticalRGB),
    CSO(HorizontalRGB), CSO(HorizontalBGR) }, /* 270 */
  { CSO(Unknown), CSO(None), CSO(HorizontalBGR), CSO(HorizontalRGB),
    CSO(VerticalRGB),   CSO(VerticalBGR) },   /* Reflect_X */
  { CSO(Unknown), CSO(None), CSO(HorizontalRGB), CSO(HorizontalBGR),
    CSO(VerticalBGR),   CSO(VerticalRGB) },   /* Reflect_Y */
};
#undef CSO

static void
update_outputs (CoglRenderer *renderer,
                gboolean notify)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  XRRScreenResources *resources;
  gboolean error = FALSE;
  GList *new_outputs = NULL;
  GList *l, *m;
  gboolean changed = FALSE;
  int i;

  xlib_renderer->outputs_update_serial = XNextRequest (xlib_renderer->xdpy);

  resources = XRRGetScreenResources (xlib_renderer->xdpy,
                                     DefaultRootWindow (xlib_renderer->xdpy));

  mtk_x11_error_trap_push (xlib_renderer->xdpy);

  for (i = 0; resources && i < resources->ncrtc && !error; i++)
    {
      XRRCrtcInfo *crtc_info = NULL;
      XRROutputInfo *output_info = NULL;
      CoglXlibOutput *output;
      float refresh_rate = 0;
      int j;

      crtc_info = XRRGetCrtcInfo (xlib_renderer->xdpy,
                                  resources, resources->crtcs[i]);
      if (crtc_info == NULL)
        {
          error = TRUE;
          goto next;
        }

      if (crtc_info->mode == None)
        goto next;

      for (j = 0; j < resources->nmode; j++)
        {
          if (resources->modes[j].id == crtc_info->mode)
            refresh_rate = (resources->modes[j].dotClock /
                            ((float)resources->modes[j].hTotal *
                             resources->modes[j].vTotal));
        }

      output_info = XRRGetOutputInfo (xlib_renderer->xdpy,
                                      resources,
                                      crtc_info->outputs[0]);
      if (output_info == NULL)
        {
          error = TRUE;
          goto next;
        }

      output = g_new0 (CoglXlibOutput, 1);
      output->name = g_strdup (output_info->name);
      output->x = crtc_info->x;
      output->y = crtc_info->y;
      output->width = crtc_info->width;
      output->height = crtc_info->height;
      if ((crtc_info->rotation & (RR_Rotate_90 | RR_Rotate_270)) != 0)
        {
          output->mm_width = output_info->mm_height;
          output->mm_height = output_info->mm_width;
        }
      else
        {
          output->mm_width = output_info->mm_width;
          output->mm_height = output_info->mm_height;
        }

      output->refresh_rate = refresh_rate;
      output->subpixel_order = output_info->subpixel_order;

      /* Handle the effect of rotation and reflection on subpixel order (ugh) */
      for (j = 0; j < 6; j++)
        {
          if ((crtc_info->rotation & (1 << j)) != 0)
            output->subpixel_order = subpixel_map[j][output->subpixel_order];
        }

      new_outputs = g_list_prepend (new_outputs, output);

    next:
      g_clear_pointer (&crtc_info, XRRFreeCrtcInfo);
      g_clear_pointer (&output_info, XRRFreeOutputInfo);
    }

  XFree (resources);

  if (!error)
    {
      new_outputs = g_list_sort (new_outputs, (GCompareFunc)compare_outputs);

      l = new_outputs;
      m = xlib_renderer->outputs;

      while (l || m)
        {
          int cmp;
          CoglXlibOutput *output_l = l ? (CoglXlibOutput *)l->data : NULL;
          CoglXlibOutput *output_m = m ? (CoglXlibOutput *)m->data : NULL;

          if (l && m)
            cmp = compare_outputs (output_l, output_m);
          else if (l)
            cmp = -1;
          else
            cmp = 1;

          if (cmp == 0)
            {
              GList *m_next = m->next;

              if (!output_values_equal (output_l, output_m))
                {
                  xlib_renderer->outputs =
                    g_list_remove_link (xlib_renderer->outputs, m);

                  xlib_renderer->outputs =
                    g_list_insert_before (xlib_renderer->outputs,
                                          m_next,
                                          g_steal_pointer (&l->data));

                  g_clear_pointer (&output_m, free_xlib_output);

                  changed = TRUE;
                }

              l = l->next;
              m = m_next;
            }
          else if (cmp < 0)
            {
              xlib_renderer->outputs =
                g_list_insert_before (xlib_renderer->outputs,
                                      m,
                                      g_steal_pointer (&l->data));
              changed = TRUE;
              l = l->next;
            }
          else
            {
              GList *m_next = m->next;
              xlib_renderer->outputs =
                g_list_remove_link (xlib_renderer->outputs, m);
              g_clear_pointer (&output_m, free_xlib_output);
              changed = TRUE;
              m = m_next;
            }
        }
    }

  g_list_free_full (new_outputs, (GDestroyNotify) free_xlib_output);
  mtk_x11_error_trap_pop (xlib_renderer->xdpy);

  if (changed)
    {
      const CoglWinsysVtable *winsys = cogl_renderer_get_winsys_vtable (renderer);

      if (notify)
        COGL_NOTE (WINSYS, "Outputs changed:");
      else
        COGL_NOTE (WINSYS, "Outputs:");

      for (l = xlib_renderer->outputs; l; l = l->next)
        {
          CoglXlibOutput *output = l->data;
          const char *subpixel_string;

          switch (output->subpixel_order)
            {
            case SubPixelUnknown:
            default:
              subpixel_string = "unknown";
              break;
            case SubPixelNone:
              subpixel_string = "none";
              break;
            case SubPixelHorizontalRGB:
              subpixel_string = "horizontal_rgb";
              break;
            case SubPixelHorizontalBGR:
              subpixel_string = "horizontal_bgr";
              break;
            case SubPixelVerticalRGB:
              subpixel_string = "vertical_rgb";
              break;
            case SubPixelVerticalBGR:
              subpixel_string = "vertical_bgr";
              break;
            }

          COGL_NOTE (WINSYS,
                     " %10s: +%d+%dx%dx%d mm=%dx%d dpi=%.1fx%.1f "
                     "subpixel_order=%s refresh_rate=%.3f",
                     output->name,
                     output->x, output->y, output->width, output->height,
                     output->mm_width, output->mm_height,
                     output->width / (output->mm_width / 25.4),
                     output->height / (output->mm_height / 25.4),
                     subpixel_string,
                     output->refresh_rate);
        }

      if (notify && winsys->renderer_outputs_changed != NULL)
        winsys->renderer_outputs_changed (renderer);
    }
}

static CoglFilterReturn
randr_filter (XEvent *event,
              void   *data)
{
  CoglRenderer *renderer = data;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);

  if (xlib_renderer->randr_base != -1 &&
      (event->xany.type == xlib_renderer->randr_base + RRScreenChangeNotify ||
       event->xany.type == xlib_renderer->randr_base + RRNotify) &&
      event->xany.serial >= xlib_renderer->outputs_update_serial)
    update_outputs (renderer, TRUE);

  return COGL_FILTER_CONTINUE;
}

gboolean
_cogl_xlib_renderer_connect (CoglRenderer *renderer, GError **error)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  int damage_error;
  int randr_error;

  g_return_val_if_fail (xlib_renderer->xdpy != NULL, FALSE);

  /* Check whether damage events are supported on this display */
  if (!XDamageQueryExtension (xlib_renderer->xdpy,
                              &xlib_renderer->damage_base,
                              &damage_error))
    xlib_renderer->damage_base = -1;

  /* Check whether randr is supported on this display */
  if (!XRRQueryExtension (xlib_renderer->xdpy,
                          &xlib_renderer->randr_base,
                          &randr_error))
    xlib_renderer->randr_base = -1;

  XRRSelectInput(xlib_renderer->xdpy,
                 DefaultRootWindow (xlib_renderer->xdpy),
                 RRScreenChangeNotifyMask
                 | RRCrtcChangeNotifyMask
                 | RROutputPropertyNotifyMask);
  update_outputs (renderer, FALSE);

  _cogl_renderer_add_native_filter (renderer,
                                    (CoglNativeFilterFunc)randr_filter,
                                    renderer);

  return TRUE;
}

void
_cogl_xlib_renderer_disconnect (CoglRenderer *renderer)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);

  g_list_free_full (xlib_renderer->outputs, (GDestroyNotify) free_xlib_output);
  xlib_renderer->outputs = NULL;
}

Display *
cogl_xlib_renderer_get_display (CoglRenderer *renderer)
{
  CoglXlibRenderer *xlib_renderer;

  g_return_val_if_fail (COGL_IS_RENDERER (renderer), NULL);

  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);

  return xlib_renderer->xdpy;
}

float
_cogl_xlib_renderer_refresh_rate_for_rectangle (CoglRenderer *renderer,
                                                int           x,
                                                int           y,
                                                int           width,
                                                int           height)
{
  CoglXlibRenderer *xlib_renderer = _cogl_xlib_renderer_get_data (renderer);
  int max_overlap = 0;
  CoglXlibOutput *max_overlapped = NULL;
  GList *l;
  int xa1 = x, xa2 = x + width;
  int ya1 = y, ya2 = y + height;

  for (l = xlib_renderer->outputs; l; l = l->next)
    {
      CoglXlibOutput *output = l->data;
      int xb1 = output->x, xb2 = output->x + output->width;
      int yb1 = output->y, yb2 = output->y + output->height;

      int overlap_x = MIN(xa2, xb2) - MAX(xa1, xb1);
      int overlap_y = MIN(ya2, yb2) - MAX(ya1, yb1);

      if (overlap_x > 0 && overlap_y > 0)
        {
          int overlap = overlap_x * overlap_y;
          if (overlap > max_overlap)
            {
              max_overlap = overlap;
              max_overlapped = output;
            }
        }
    }

  if (max_overlapped)
    return max_overlapped->refresh_rate;
  else
    return 0.0;
}
