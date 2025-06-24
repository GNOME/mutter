/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *
 * Authors:
 *  Neil Roberts   <neil@linux.intel.com>
 *  Johan Bilien   <johan.bilien@nokia.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-debug.h"
#include "cogl/cogl-util.h"
#include "cogl/winsys/cogl-texture-pixmap-x11.h"
#include "cogl/winsys/cogl-texture-pixmap-x11-private.h"
#include "cogl/cogl-bitmap-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-texture-2d-sliced.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-xlib-renderer.h"
#include "cogl/cogl-xlib-renderer-private.h"
#include "cogl/cogl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/winsys/cogl-winsys.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <string.h>
#include <math.h>

G_DEFINE_FINAL_TYPE (CoglTexturePixmapX11, cogl_texture_pixmap_x11, COGL_TYPE_TEXTURE)

static CoglWinsys *
_cogl_texture_pixmap_x11_get_winsys (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglContext *ctx;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));
  return cogl_renderer_get_winsys_vtable (ctx->display->renderer) ;
}

static int
_cogl_xlib_get_damage_base (CoglContext *ctx)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (ctx->display->renderer);

  return xlib_renderer->damage_base;
}

static void
process_damage_event (CoglTexturePixmapX11 *tex_pixmap,
                      XDamageNotifyEvent *damage_event)
{
  CoglTexture *tex = COGL_TEXTURE (tex_pixmap);
  Display *display;
  enum
{ DO_NOTHING, NEEDS_SUBTRACT, NEED_BOUNDING_BOX } handle_mode;
  CoglContext *ctx;
  MtkRectangle damage_rect;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));
  display = cogl_xlib_renderer_get_display (ctx->display->renderer);

  COGL_NOTE (TEXTURE_PIXMAP, "Damage event received for %p", tex_pixmap);

  switch (tex_pixmap->damage_report_level)
    {
    case COGL_TEXTURE_PIXMAP_X11_DAMAGE_RAW_RECTANGLES:
      /* For raw rectangles we don't need do look at the damage region
         at all because the damage area is directly given in the event
         struct and the reporting of events is not affected by
         clearing the damage region */
      handle_mode = DO_NOTHING;
      break;

    case COGL_TEXTURE_PIXMAP_X11_DAMAGE_DELTA_RECTANGLES:
    case COGL_TEXTURE_PIXMAP_X11_DAMAGE_NON_EMPTY:
      /* For delta rectangles and non empty we'll query the damage
         region for the bounding box */
      handle_mode = NEED_BOUNDING_BOX;
      break;

    case COGL_TEXTURE_PIXMAP_X11_DAMAGE_BOUNDING_BOX:
      /* For bounding box we need to clear the damage region but we
         don't actually care what it was because the damage event
         itself contains the bounding box of the region */
      handle_mode = NEEDS_SUBTRACT;
      break;

    default:
      g_assert_not_reached ();
    }

  /* If the damage already covers the whole rectangle then we don't
     need to request the bounding box of the region because we're
     going to update the whole texture anyway. */
  damage_rect = MTK_RECTANGLE_INIT (0, 0,
                                    cogl_texture_get_width (tex),
                                    cogl_texture_get_height (tex));
  if (mtk_rectangle_equal (&tex_pixmap->damage_rect, &damage_rect))
    {
      if (handle_mode != DO_NOTHING)
        XDamageSubtract (display, tex_pixmap->damage, None, None);
    }
  else if (handle_mode == NEED_BOUNDING_BOX)
    {
      XserverRegion parts;
      int r_count;
      XRectangle r_bounds;
      XRectangle *r_damage;

      /* We need to extract the damage region so we can get the
         bounding box */

      parts = XFixesCreateRegion (display, 0, 0);
      XDamageSubtract (display, tex_pixmap->damage, None, parts);
      r_damage = XFixesFetchRegionAndBounds (display,
                                             parts,
                                             &r_count,
                                             &r_bounds);
      damage_rect = MTK_RECTANGLE_INIT (r_bounds.x, r_bounds.y,
                                        r_bounds.width, r_bounds.height);
      mtk_rectangle_union (&tex_pixmap->damage_rect,
                           &damage_rect,
                           &tex_pixmap->damage_rect);
      if (r_damage)
        XFree (r_damage);

      XFixesDestroyRegion (display, parts);
    }
  else
    {
      if (handle_mode == NEEDS_SUBTRACT)
        /* We still need to subtract from the damage region but we
           don't care what the region actually was */
        XDamageSubtract (display, tex_pixmap->damage, None, None);

      damage_rect = MTK_RECTANGLE_INIT (damage_event->area.x,
                                        damage_event->area.y,
                                        damage_event->area.width,
                                        damage_event->area.height);
      mtk_rectangle_union (&tex_pixmap->damage_rect,
                           &damage_rect,
                           &tex_pixmap->damage_rect);
    }

  if (tex_pixmap->winsys)
    {
      /* If we're using the texture from pixmap extension then there's no
         point in getting the region and we can just mark that the texture
         needs updating */
      CoglWinsys *winsys = _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);
      COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_damage_notify (tex_pixmap);
    }
}

static CoglFilterReturn
_cogl_texture_pixmap_x11_filter (XEvent *event, void *data)
{
  CoglTexturePixmapX11 *tex_pixmap = data;
  int damage_base;
  CoglContext *ctx;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));
  damage_base = _cogl_xlib_get_damage_base (ctx);
  if (event->type == damage_base + XDamageNotify)
    {
      XDamageNotifyEvent *damage_event = (XDamageNotifyEvent *) event;

      if (damage_event->damage == tex_pixmap->damage)
        process_damage_event (tex_pixmap, damage_event);
    }

  return COGL_FILTER_CONTINUE;
}

static void
set_damage_object_internal (CoglContext *ctx,
                            CoglTexturePixmapX11 *tex_pixmap,
                            Damage damage,
                            CoglTexturePixmapX11ReportLevel report_level)
{
  Display *display = cogl_xlib_renderer_get_display (ctx->display->renderer);

  if (tex_pixmap->damage)
    {
      _cogl_renderer_remove_native_filter (ctx->display->renderer,
                                           (CoglNativeFilterFunc)_cogl_texture_pixmap_x11_filter,
                                           tex_pixmap);

      if (tex_pixmap->damage_owned)
        {
          XDamageDestroy (display, tex_pixmap->damage);
          tex_pixmap->damage_owned = FALSE;
        }
    }

  tex_pixmap->damage = damage;
  tex_pixmap->damage_report_level = report_level;

  if (damage)
    _cogl_renderer_add_native_filter (ctx->display->renderer,
                                      (CoglNativeFilterFunc)_cogl_texture_pixmap_x11_filter,
                                      tex_pixmap);
}

static void
cogl_texture_pixmap_x11_dispose (GObject *object)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (object);
  CoglContext *ctx;
  Display *display;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));

  if (tex_pixmap->stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    {
      g_object_unref (tex_pixmap->left);
      G_OBJECT_CLASS (cogl_texture_pixmap_x11_parent_class)->dispose (object);
      return;
    }

  display = cogl_xlib_renderer_get_display (ctx->display->renderer);

  set_damage_object_internal (ctx, tex_pixmap, 0, 0);

  if (tex_pixmap->image)
    XDestroyImage (tex_pixmap->image);

  if (tex_pixmap->shm_info.shmid != -1)
    {
      XShmDetach (display, &tex_pixmap->shm_info);
      shmdt (tex_pixmap->shm_info.shmaddr);
      shmctl (tex_pixmap->shm_info.shmid, IPC_RMID, 0);
    }

  if (tex_pixmap->tex)
    g_object_unref (tex_pixmap->tex);

  if (tex_pixmap->winsys)
    {
      CoglWinsys *winsys =
        _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);
      COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_free (tex_pixmap);
    }

  G_OBJECT_CLASS (cogl_texture_pixmap_x11_parent_class)->dispose (object);
}

static void
_cogl_texture_pixmap_x11_set_use_winsys_texture (CoglTexturePixmapX11 *tex_pixmap,
                                                 gboolean new_value)
{
  if (tex_pixmap->use_winsys_texture != new_value)
    {
      /* Notify cogl-pipeline.c that the texture's underlying GL texture
       * storage is changing so it knows it may need to bind a new texture
       * if the CoglTexture is reused with the same texture unit. */
      _cogl_pipeline_texture_storage_change_notify (COGL_TEXTURE (tex_pixmap));

      tex_pixmap->use_winsys_texture = new_value;
    }
}

static CoglTexture *
create_fallback_texture (CoglContext *ctx,
                         int width,
                         int height,
                         CoglPixelFormat internal_format)
{
  CoglTexture *tex;
  GError *skip_error = NULL;

  /* First try creating a fast-path non-sliced texture */
  tex = cogl_texture_2d_new_with_size (ctx, width, height);

  _cogl_texture_set_internal_format (tex, internal_format);

  /* TODO: instead of allocating storage here it would be better
   * if we had some api that let us just check that the size is
   * supported by the hardware so storage could be allocated
   * lazily when uploading data. */
  if (!cogl_texture_allocate (tex, &skip_error))
    {
      g_error_free (skip_error);
      g_object_unref (tex);
      tex = NULL;
    }

  if (!tex)
    {
      tex =
        cogl_texture_2d_sliced_new_with_size (ctx,
                                              width,
                                              height,
                                              COGL_TEXTURE_MAX_WASTE);
      _cogl_texture_set_internal_format (tex, internal_format);
    }

  return tex;
}


/* Tries to allocate enough shared mem to handle a full size
 * update size of the X Pixmap. */
static void
try_alloc_shm (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexture *tex = COGL_TEXTURE (tex_pixmap);
  XImage *dummy_image;
  CoglContext *ctx;
  Display *display;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));
  display = cogl_xlib_renderer_get_display (ctx->display->renderer);

  if (!XShmQueryExtension (display))
    return;

  /* We are creating a dummy_image so we can have Xlib calculate
   * image->bytes_per_line - including any magic padding it may
   * want - for the largest possible ximage we might need to use
   * when handling updates to the texture.
   *
   * Note: we pass a NULL shminfo here, but that has no bearing
   * on the setup of the XImage, except that ximage->obdata will
   * == NULL.
   */
  dummy_image =
    XShmCreateImage (display,
                     tex_pixmap->visual,
                     tex_pixmap->depth,
                     ZPixmap,
                     NULL,
                     NULL, /* shminfo, */
                     cogl_texture_get_width (tex),
                     cogl_texture_get_height (tex));
  if (!dummy_image)
    goto failed_image_create;

  tex_pixmap->shm_info.shmid = shmget (IPC_PRIVATE,
                                       dummy_image->bytes_per_line
                                       * dummy_image->height,
                                       IPC_CREAT | 0777);
  if (tex_pixmap->shm_info.shmid == -1)
    goto failed_shmget;

  tex_pixmap->shm_info.shmaddr = shmat (tex_pixmap->shm_info.shmid, 0, 0);
  if (tex_pixmap->shm_info.shmaddr == (void *) -1)
    goto failed_shmat;

  tex_pixmap->shm_info.readOnly = False;

  if (XShmAttach (display, &tex_pixmap->shm_info) == 0)
    goto failed_xshmattach;

  XDestroyImage (dummy_image);

  return;

 failed_xshmattach:
  g_warning ("XShmAttach failed");
  shmdt (tex_pixmap->shm_info.shmaddr);

 failed_shmat:
  g_warning ("shmat failed");
  shmctl (tex_pixmap->shm_info.shmid, IPC_RMID, 0);

 failed_shmget:
  g_warning ("shmget failed");
  XDestroyImage (dummy_image);

 failed_image_create:
  tex_pixmap->shm_info.shmid = -1;
}

/* Given a set of red, green and blue component masks, a depth and
 * bits per pixel this function tries to determine a corresponding
 * CoglPixelFormat.
 *
 * The depth is measured in bits not including padding for un-used
 * alpha. The bits per pixel (bpp) does include padding for un-used
 * alpha.
 *
 * This function firstly aims to match formats with RGB ordered
 * components and only considers alpha coming first, in the most
 * significant bits. If the function fails to match then it recurses
 * by either switching the r and b masks around to check for BGR
 * ordered formats or it recurses with the masks shifted to check for
 * formats where the alpha component is the least significant bits.
 */
static CoglPixelFormat
_cogl_util_pixel_format_from_masks_real (unsigned long r_mask,
                                         unsigned long g_mask,
                                         unsigned long b_mask,
                                         int depth, int bpp,
                                         gboolean check_bgr,
                                         gboolean check_afirst,
                                         int recursion_depth)
{
  CoglPixelFormat image_format;

  if (depth == 24 && bpp == 24 &&
      r_mask == 0xff0000 && g_mask == 0xff00 && b_mask == 0xff)
    {
      return COGL_PIXEL_FORMAT_RGB_888;
    }
  else if ((depth == 24 || depth == 32) && bpp == 32 &&
           r_mask == 0xff0000 && g_mask == 0xff00 && b_mask == 0xff)
    {
      return COGL_PIXEL_FORMAT_ARGB_8888_PRE;
    }
  else if ((depth == 30 || depth == 32) &&
           r_mask == 0x3ff00000 && g_mask == 0xffc00 && b_mask == 0x3ff)
    {
      return COGL_PIXEL_FORMAT_ARGB_2101010_PRE;
    }
  else if (depth == 16 && bpp == 16 &&
           r_mask == 0xf800 && g_mask == 0x7e0 && b_mask == 0x1f)
    {
      return COGL_PIXEL_FORMAT_RGB_565;
    }

  if (recursion_depth == 2)
    return 0;

  /* Check for BGR ordering if we didn't find a match */
  if (check_bgr)
    {
      image_format =
        _cogl_util_pixel_format_from_masks_real (b_mask, g_mask, r_mask,
                                                 depth, bpp,
                                                 FALSE,
                                                 TRUE,
                                                 recursion_depth + 1);
      if (image_format)
        return image_format ^ COGL_BGR_BIT;
    }

  /* Check for alpha in the least significant bits if we still
   * haven't found a match... */
  if (check_afirst && depth != bpp)
    {
      int shift = bpp - depth;

      image_format =
        _cogl_util_pixel_format_from_masks_real (r_mask >> shift,
                                                 g_mask >> shift,
                                                 b_mask >> shift,
                                                 depth, bpp,
                                                 TRUE,
                                                 FALSE,
                                                 recursion_depth + 1);
      if (image_format)
        return image_format ^ COGL_AFIRST_BIT;
    }

  return 0;
}


/* Match a CoglPixelFormat according to channel masks, color depth,
 * bits per pixel and byte order. These information are provided by
 * the Visual and XImage structures.
 *
 * If no specific pixel format could be found, COGL_PIXEL_FORMAT_ANY
 * is returned.
 */
static CoglPixelFormat
_cogl_util_pixel_format_from_masks (unsigned long r_mask,
                                    unsigned long g_mask,
                                    unsigned long b_mask,
                                    int depth, int bpp,
                                    gboolean byte_order_is_lsb_first)
{
  CoglPixelFormat image_format =
    _cogl_util_pixel_format_from_masks_real (r_mask, g_mask, b_mask,
                                             depth, bpp,
                                             TRUE,
                                             TRUE,
                                             0);

  if (!image_format)
    {
      const char *byte_order[] = { "MSB first", "LSB first" };
      g_warning ("Could not find a matching pixel format for red mask=0x%lx,"
                 "green mask=0x%lx, blue mask=0x%lx at depth=%d, bpp=%d "
                 "and byte order=%s\n", r_mask, g_mask, b_mask, depth, bpp,
                 byte_order[!!byte_order_is_lsb_first]);
      return 0;
    }

  /* If the image is in little-endian then the order in memory is
     reversed */
  if (byte_order_is_lsb_first &&
      _cogl_pixel_format_is_endian_dependant (image_format))
    {
      image_format ^= COGL_BGR_BIT;
      if (image_format & COGL_A_BIT)
        image_format ^= COGL_AFIRST_BIT;
    }

  return image_format;
}

static void
_cogl_texture_pixmap_x11_update_image_texture (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexture *tex = COGL_TEXTURE (tex_pixmap);
  Display *display;
  Visual *visual;
  CoglContext *ctx;
  CoglPixelFormat image_format;
  XImage *image;
  int src_x, src_y;
  int x, y, width, height;
  int bpp;
  int offset;
  GError *ignore = NULL;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));
  display = cogl_xlib_renderer_get_display (ctx->display->renderer);
  visual = tex_pixmap->visual;

  /* If the damage region is empty then there's nothing to do */
  if (tex_pixmap->damage_rect.x == tex_pixmap->damage_rect.width)
    return;

  x = tex_pixmap->damage_rect.x;
  y = tex_pixmap->damage_rect.y;
  width = tex_pixmap->damage_rect.width;
  height = tex_pixmap->damage_rect.height;

  /* We lazily create the texture the first time it is needed in case
     this texture can be entirely handled using the GLX texture
     instead */
  if (tex_pixmap->tex == NULL)
    {
      CoglPixelFormat texture_format;

      texture_format = (tex_pixmap->depth >= 32
                        ? COGL_PIXEL_FORMAT_RGBA_8888_PRE
                        : COGL_PIXEL_FORMAT_RGB_888);

      tex_pixmap->tex = create_fallback_texture (ctx,
                                                 cogl_texture_get_width (tex),
                                                 cogl_texture_get_height (tex),
                                                 texture_format);
    }

  if (tex_pixmap->image == NULL)
    {
      /* If we also haven't got a shm segment then this must be the
         first time we've tried to update, so lets try allocating shm
         first */
      if (tex_pixmap->shm_info.shmid == -1)
        try_alloc_shm (tex_pixmap);

      if (tex_pixmap->shm_info.shmid == -1)
        {
          COGL_NOTE (TEXTURE_PIXMAP, "Updating %p using XGetImage", tex_pixmap);

          /* We'll fallback to using a regular XImage. We'll download
             the entire area instead of a sub region because presumably
             if this is the first update then the entire pixmap is
             needed anyway and it saves trying to manually allocate an
             XImage at the right size */
          tex_pixmap->image = XGetImage (display,
                                         tex_pixmap->pixmap,
                                         0, 0,
                                         cogl_texture_get_width (tex),
                                         cogl_texture_get_height (tex),
                                         AllPlanes, ZPixmap);
          image = tex_pixmap->image;
          src_x = x;
          src_y = y;
        }
      else
        {
          COGL_NOTE (TEXTURE_PIXMAP, "Updating %p using XShmGetImage",
                     tex_pixmap);

          /* Create a temporary image using the beginning of the
             shared memory segment and the right size for the region
             we want to update. We need to reallocate the XImage every
             time because there is no XShmGetSubImage. */
          image = XShmCreateImage (display,
                                   tex_pixmap->visual,
                                   tex_pixmap->depth,
                                   ZPixmap,
                                   NULL,
                                   &tex_pixmap->shm_info,
                                   width,
                                   height);
          image->data = tex_pixmap->shm_info.shmaddr;
          src_x = 0;
          src_y = 0;

          XShmGetImage (display, tex_pixmap->pixmap, image, x, y, AllPlanes);
        }
    }
  else
    {
      COGL_NOTE (TEXTURE_PIXMAP, "Updating %p using XGetSubImage", tex_pixmap);

      image = tex_pixmap->image;
      src_x = x;
      src_y = y;

      XGetSubImage (display,
                    tex_pixmap->pixmap,
                    x, y, width, height,
                    AllPlanes, ZPixmap,
                    image,
                    x, y);
    }

  image_format =
    _cogl_util_pixel_format_from_masks (visual->red_mask,
                                        visual->green_mask,
                                        visual->blue_mask,
                                        image->depth,
                                        image->bits_per_pixel,
                                        image->byte_order == LSBFirst);
  g_return_if_fail (cogl_pixel_format_get_n_planes (image_format) == 1);

  bpp = cogl_pixel_format_get_bytes_per_pixel (image_format, 0);
  offset = image->bytes_per_line * src_y + bpp * src_x;

  _cogl_texture_set_region (tex_pixmap->tex,
                            width,
                            height,
                            image_format,
                            image->bytes_per_line,
                            ((const uint8_t *) image->data) + offset,
                            x, y,
                            0, /* level */
                            &ignore);

  /* If we have a shared memory segment then the XImage would be a
     temporary one with no data allocated so we can just XFree it */
  if (tex_pixmap->shm_info.shmid != -1)
    XFree (image);

  memset (&tex_pixmap->damage_rect, 0, sizeof (MtkRectangle));
}

static void
_cogl_texture_pixmap_x11_update (CoglTexturePixmapX11 *tex_pixmap,
                                 gboolean needs_mipmap)
{
  CoglTexturePixmapStereoMode stereo_mode = tex_pixmap->stereo_mode;
  if (stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    tex_pixmap = tex_pixmap->left;

  if (tex_pixmap->winsys)
    {
      CoglWinsys *winsys =
        _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);

      if (COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_update (tex_pixmap, stereo_mode, needs_mipmap))
        {
          _cogl_texture_pixmap_x11_set_use_winsys_texture (tex_pixmap, TRUE);
          return;
        }
    }

  /* If it didn't work then fallback to using XGetImage. This may be
     temporary */
  _cogl_texture_pixmap_x11_set_use_winsys_texture (tex_pixmap, FALSE);

  _cogl_texture_pixmap_x11_update_image_texture (tex_pixmap);
}

static CoglTexture *
_cogl_texture_pixmap_x11_get_texture (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapX11 *original_pixmap = tex_pixmap;
  CoglTexture *tex;
  int i;
  CoglTexturePixmapStereoMode stereo_mode = tex_pixmap->stereo_mode;

  if (stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    tex_pixmap = tex_pixmap->left;

  /* We try getting the texture twice, once without flushing the
     updates and once with. If pre_paint has been called already then
     we should have a good idea of which texture to use so we don't
     want to mess with that by ensuring the updates. However, if we
     couldn't find a texture then we'll just make a best guess by
     flushing without expecting mipmap support and try again. This
     would happen for example if an application calls
     get_gl_texture before the first paint */

  for (i = 0; i < 2; i++)
    {
      if (tex_pixmap->use_winsys_texture)
        {
          CoglWinsys *winsys =
            _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);
          tex = COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_get_texture (tex_pixmap, stereo_mode);
        }
      else
        tex = tex_pixmap->tex;

      if (tex)
        return tex;

      _cogl_texture_pixmap_x11_update (original_pixmap, FALSE);
    }

  g_assert_not_reached ();

  return NULL;
}

static gboolean
_cogl_texture_pixmap_x11_allocate (CoglTexture *tex,
                                   GError **error)
{
  return TRUE;
}

static gboolean
_cogl_texture_pixmap_x11_set_region (CoglTexture *tex,
                                     int src_x,
                                     int src_y,
                                     int dst_x,
                                     int dst_y,
                                     int dst_width,
                                     int dst_height,
                                     int level,
                                     CoglBitmap *bmp,
                                     GError **error)
{
  /* This doesn't make much sense for texture from pixmap so it's not
     supported */
  g_set_error_literal (error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_UNSUPPORTED,
                       "Explicitly setting a region of a TFP texture "
                       "unsupported");
  return FALSE;
}

static gboolean
_cogl_texture_pixmap_x11_get_data (CoglTexture *tex,
                                   CoglPixelFormat format,
                                   int rowstride,
                                   uint8_t *data)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  return cogl_texture_get_data (child_tex, format, rowstride, data);
}

static void
_cogl_texture_pixmap_x11_foreach_sub_texture_in_region
                                  (CoglTexture                *tex,
                                   float                       virtual_tx_1,
                                   float                       virtual_ty_1,
                                   float                       virtual_tx_2,
                                   float                       virtual_ty_2,
                                   CoglTextureForeachCallback  callback,
                                   void                       *user_data)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  cogl_texture_foreach_in_region (child_tex,
                                  virtual_tx_1,
                                  virtual_ty_1,
                                  virtual_tx_2,
                                  virtual_ty_2,
                                  COGL_PIPELINE_WRAP_MODE_REPEAT,
                                  COGL_PIPELINE_WRAP_MODE_REPEAT,
                                  callback,
                                  user_data);
}

static gboolean
_cogl_texture_pixmap_x11_is_sliced (CoglTexture *tex)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  return cogl_texture_is_sliced (child_tex);
}

static gboolean
_cogl_texture_pixmap_x11_can_hardware_repeat (CoglTexture *tex)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  return _cogl_texture_can_hardware_repeat (child_tex);
}

static void
_cogl_texture_pixmap_x11_transform_coords_to_gl (CoglTexture *tex,
                                                 float       *s,
                                                 float       *t)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  COGL_TEXTURE_GET_CLASS (child_tex)->transform_coords_to_gl (child_tex, s, t);
}

static CoglTransformResult
_cogl_texture_pixmap_x11_transform_quad_coords_to_gl (CoglTexture *tex,
                                                      float       *coords)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  return COGL_TEXTURE_GET_CLASS (child_tex)->transform_quad_coords_to_gl (child_tex,
                                                                          coords);
}

static gboolean
_cogl_texture_pixmap_x11_get_gl_texture (CoglTexture *tex,
                                         GLuint      *out_gl_handle,
                                         GLenum      *out_gl_target)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  return cogl_texture_get_gl_texture (child_tex,
                                      out_gl_handle,
                                      out_gl_target);
}

static void
_cogl_texture_pixmap_x11_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                                         GLenum min_filter,
                                                         GLenum mag_filter)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  _cogl_texture_gl_flush_legacy_texobj_filters (child_tex,
                                                min_filter, mag_filter);
}

static void
_cogl_texture_pixmap_x11_pre_paint (CoglTexture *tex,
                                    CoglTexturePrePaintFlags flags)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex;

  _cogl_texture_pixmap_x11_update (tex_pixmap,
                                   !!(flags & COGL_TEXTURE_NEEDS_MIPMAP));

  child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  _cogl_texture_pre_paint (child_tex, flags);
}

static void
_cogl_texture_pixmap_x11_ensure_non_quad_rendering (CoglTexture *tex)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
    COGL_TEXTURE_GET_CLASS (child_tex)->ensure_non_quad_rendering (child_tex);
}

static void
_cogl_texture_pixmap_x11_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                            GLenum wrap_mode_s,
                                                            GLenum wrap_mode_t)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  _cogl_texture_gl_flush_legacy_texobj_wrap_modes (child_tex,
                                                   wrap_mode_s,
                                                   wrap_mode_t);
}

static CoglPixelFormat
_cogl_texture_pixmap_x11_get_format (CoglTexture *tex)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  /* Forward on to the child texture */
  return cogl_texture_get_format (child_tex);
}

static GLenum
_cogl_texture_pixmap_x11_get_gl_format (CoglTexture *tex)
{
  CoglTexturePixmapX11 *tex_pixmap = COGL_TEXTURE_PIXMAP_X11 (tex);
  CoglTexture *child_tex = _cogl_texture_pixmap_x11_get_texture (tex_pixmap);

  return _cogl_texture_gl_get_format (child_tex);
}

static void
cogl_texture_pixmap_x11_class_init (CoglTexturePixmapX11Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  CoglTextureClass *texture_class = COGL_TEXTURE_CLASS (klass);

  gobject_class->dispose = cogl_texture_pixmap_x11_dispose;

  texture_class->allocate = _cogl_texture_pixmap_x11_allocate;
  texture_class->set_region = _cogl_texture_pixmap_x11_set_region;
  texture_class->get_data = _cogl_texture_pixmap_x11_get_data;
  texture_class->foreach_sub_texture_in_region = _cogl_texture_pixmap_x11_foreach_sub_texture_in_region;
  texture_class->is_sliced = _cogl_texture_pixmap_x11_is_sliced;
  texture_class->can_hardware_repeat = _cogl_texture_pixmap_x11_can_hardware_repeat;
  texture_class->transform_coords_to_gl = _cogl_texture_pixmap_x11_transform_coords_to_gl;
  texture_class->transform_quad_coords_to_gl = _cogl_texture_pixmap_x11_transform_quad_coords_to_gl;
  texture_class->get_gl_texture = _cogl_texture_pixmap_x11_get_gl_texture;
  texture_class->gl_flush_legacy_texobj_filters = _cogl_texture_pixmap_x11_gl_flush_legacy_texobj_filters;
  texture_class->pre_paint = _cogl_texture_pixmap_x11_pre_paint;
  texture_class->ensure_non_quad_rendering = _cogl_texture_pixmap_x11_ensure_non_quad_rendering;
  texture_class->gl_flush_legacy_texobj_wrap_modes = _cogl_texture_pixmap_x11_gl_flush_legacy_texobj_wrap_modes;
  texture_class->get_format = _cogl_texture_pixmap_x11_get_format;
  texture_class->get_gl_format = _cogl_texture_pixmap_x11_get_gl_format;
}

static void
cogl_texture_pixmap_x11_init (CoglTexturePixmapX11 *self)
{
}

uint32_t
cogl_texture_pixmap_x11_error_quark (void)
{
  return g_quark_from_static_string ("cogl-texture-pixmap-error-quark");
}

static CoglTexture *
_cogl_texture_pixmap_x11_new (CoglContext *ctx,
                              uint32_t pixmap,
                              gboolean automatic_updates,
                              CoglTexturePixmapStereoMode stereo_mode,
                              GError **error)
{
  CoglTexturePixmapX11 *tex_pixmap;
  Display *display = cogl_xlib_renderer_get_display (ctx->display->renderer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  Window pixmap_root_window;
  int pixmap_x, pixmap_y;
  unsigned int pixmap_width, pixmap_height;
  unsigned int pixmap_border_width;
  unsigned int pixmap_depth;
  CoglPixelFormat internal_format;
  XWindowAttributes window_attributes;
  int damage_base;
  CoglWinsys *winsys;

  if (!XGetGeometry (display, pixmap, &pixmap_root_window,
                     &pixmap_x, &pixmap_y,
                     &pixmap_width, &pixmap_height,
                     &pixmap_border_width, &pixmap_depth))
    {
      g_set_error_literal (error,
                           COGL_TEXTURE_PIXMAP_X11_ERROR,
                           COGL_TEXTURE_PIXMAP_X11_ERROR_X11,
                           "Unable to query pixmap size");
      return NULL;
    }

  /* Note: the detailed pixel layout doesn't matter here, we are just
   * interested in RGB vs RGBA... */
  internal_format = (pixmap_depth >= 32
                     ? COGL_PIXEL_FORMAT_RGBA_8888_PRE
                     : COGL_PIXEL_FORMAT_RGB_888);

  tex_pixmap = g_object_new (COGL_TYPE_TEXTURE_PIXMAP_X11,
                             "context", ctx,
                             "texture-driver", cogl_driver_create_texture_driver (driver),
                             "width", pixmap_width,
                             "height", pixmap_height,
                             "format", internal_format,
                             NULL);

  tex_pixmap->depth = pixmap_depth;
  tex_pixmap->pixmap = pixmap;
  tex_pixmap->stereo_mode = stereo_mode;
  tex_pixmap->left = NULL;
  tex_pixmap->image = NULL;
  tex_pixmap->shm_info.shmid = -1;
  tex_pixmap->tex = NULL;
  tex_pixmap->damage_owned = FALSE;
  tex_pixmap->damage = 0;

  /* We need a visual to use for shared memory images so we'll query
     it from the pixmap's root window */
  if (!XGetWindowAttributes (display, pixmap_root_window, &window_attributes))
    {
      g_free (tex_pixmap);
      g_set_error_literal (error,
                           COGL_TEXTURE_PIXMAP_X11_ERROR,
                           COGL_TEXTURE_PIXMAP_X11_ERROR_X11,
                           "Unable to query root window attributes");
      return NULL;
    }

  tex_pixmap->visual = window_attributes.visual;

  /* If automatic updates are requested and the Xlib connection
     supports damage events then we'll register a damage object on the
     pixmap */
  damage_base = _cogl_xlib_get_damage_base (ctx);
  if (automatic_updates && damage_base >= 0)
    {
      Damage damage = XDamageCreate (display,
                                     pixmap,
                                     XDamageReportBoundingBox);
      set_damage_object_internal (ctx,
                                  tex_pixmap,
                                  damage,
                                  COGL_TEXTURE_PIXMAP_X11_DAMAGE_BOUNDING_BOX);
      tex_pixmap->damage_owned = TRUE;
    }

  /* Assume the entire pixmap is damaged to begin with */
  tex_pixmap->damage_rect.x = 0;
  tex_pixmap->damage_rect.width = pixmap_width;
  tex_pixmap->damage_rect.y = 0;
  tex_pixmap->damage_rect.height = pixmap_height;

  winsys = _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);
  if (COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_create)
    {
      tex_pixmap->use_winsys_texture =
        COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_create (tex_pixmap);
    }
  else
    tex_pixmap->use_winsys_texture = FALSE;

  if (!tex_pixmap->use_winsys_texture)
    tex_pixmap->winsys = NULL;

  _cogl_texture_set_allocated (COGL_TEXTURE (tex_pixmap), internal_format,
                               pixmap_width, pixmap_height);

  return COGL_TEXTURE (tex_pixmap);
}

CoglTexture *
cogl_texture_pixmap_x11_new (CoglContext *ctxt,
                             uint32_t pixmap,
                             gboolean automatic_updates,
                             GError **error)

{
  return _cogl_texture_pixmap_x11_new (ctxt, pixmap,
                                       automatic_updates, COGL_TEXTURE_PIXMAP_MONO,
                                       error);
}

CoglTexture *
cogl_texture_pixmap_x11_new_left (CoglContext *ctxt,
                                  uint32_t pixmap,
                                  gboolean automatic_updates,
                                  GError **error)
{
  return _cogl_texture_pixmap_x11_new (ctxt, pixmap,
                                       automatic_updates, COGL_TEXTURE_PIXMAP_LEFT,
                                       error);
}

CoglTexture *
cogl_texture_pixmap_x11_new_right (CoglTexturePixmapX11 *tfp_left)
{
  CoglTexture *texture_left = COGL_TEXTURE (tfp_left);
  CoglContext *ctx = cogl_texture_get_context (texture_left);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglTexturePixmapX11 *tfp_right;
  CoglPixelFormat internal_format;

  g_return_val_if_fail (tfp_left->stereo_mode == COGL_TEXTURE_PIXMAP_LEFT, NULL);

  internal_format = (tfp_left->depth >= 32
                     ? COGL_PIXEL_FORMAT_RGBA_8888_PRE
                     : COGL_PIXEL_FORMAT_RGB_888);

  tfp_right = g_object_new (COGL_TYPE_TEXTURE_PIXMAP_X11,
                            "context", ctx,
                            "texture-driver", cogl_driver_create_texture_driver (driver),
                            "width", cogl_texture_get_width (texture_left),
                            "height", cogl_texture_get_height (texture_left),
                            "format", internal_format,
                            NULL);
  tfp_right->stereo_mode = COGL_TEXTURE_PIXMAP_RIGHT;
  tfp_right->left = g_object_ref (tfp_left);

  _cogl_texture_set_allocated (COGL_TEXTURE (tfp_right), internal_format,
                               cogl_texture_get_width (texture_left),
                               cogl_texture_get_height (texture_left));

  return COGL_TEXTURE (tfp_right);
}

void
cogl_texture_pixmap_x11_update_area (CoglTexturePixmapX11 *tex_pixmap,
                                     const MtkRectangle   *area)
{
  /* We'll queue the update for both the GLX texture and the regular
     texture because we can't determine which will be needed until we
     actually render something */

  if (tex_pixmap->stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    tex_pixmap = tex_pixmap->left;

  if (tex_pixmap->winsys)
    {
      CoglWinsys *winsys = _cogl_texture_pixmap_x11_get_winsys (tex_pixmap);
      COGL_WINSYS_GET_CLASS (winsys)->texture_pixmap_x11_damage_notify (tex_pixmap);
    }
  mtk_rectangle_union (&tex_pixmap->damage_rect,
                       area,
                       &tex_pixmap->damage_rect);
}

gboolean
cogl_texture_pixmap_x11_is_using_tfp_extension (CoglTexturePixmapX11 *tex_pixmap)
{
  if (tex_pixmap->stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    tex_pixmap = tex_pixmap->left;

  return !!tex_pixmap->winsys;
}
