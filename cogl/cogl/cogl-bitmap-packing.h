/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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

/* This file is included multiple times with different definitions for
   the component_type type (either uint8_t or uint16_t). The code ends
   up exactly the same for both but we only want to end up hitting the
   16-bit path when one of the types in the conversion is > 8 bits per
   component. */

/* Unpacking to RGBA */

inline static void
G_PASTE (_cogl_unpack_a_8_, component_size) (const uint8_t *src,
                                             component_type *dst,
                                             int width)
{
  while (width-- > 0)
    {
      dst[0] = 0;
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = UNPACK_BYTE (*src);
      dst += 4;
      src++;
    }
}

inline static void
G_PASTE (_cogl_unpack_r_8_, component_size) (const uint8_t *src,
                                             component_type *dst,
                                             int width)
{
  /* FIXME: I'm not sure if this is right. It looks like Nvidia and
     Mesa handle luminance textures differently. Maybe we should
     consider just removing luminance textures for Cogl 2.0 because
     they have been removed in GL 3.0 */
  while (width-- > 0)
    {
      component_type v = UNPACK_BYTE (src[0]);
      dst[0] = v;
      dst[1] = v;
      dst[2] = v;
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src++;
    }
}

inline static void
G_PASTE (_cogl_unpack_rg_88_, component_size) (const uint8_t *src,
                                               component_type *dst,
                                               int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[0]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = 0;
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 2;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgb_888_, component_size) (const uint8_t *src,
                                                 component_type *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[0]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[2]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 3;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgr_888_, component_size) (const uint8_t *src,
                                                 component_type *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[2]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[0]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 3;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgrx_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[2]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[0]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgra_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[2]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[0]);
      dst[3] = UNPACK_BYTE (src[3]);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_xrgb_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[1]);
      dst[1] = UNPACK_BYTE (src[2]);
      dst[2] = UNPACK_BYTE (src[3]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_argb_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[1]);
      dst[1] = UNPACK_BYTE (src[2]);
      dst[2] = UNPACK_BYTE (src[3]);
      dst[3] = UNPACK_BYTE (src[0]);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_xbgr_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[3]);
      dst[1] = UNPACK_BYTE (src[2]);
      dst[2] = UNPACK_BYTE (src[1]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_abgr_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[3]);
      dst[1] = UNPACK_BYTE (src[2]);
      dst[2] = UNPACK_BYTE (src[1]);
      dst[3] = UNPACK_BYTE (src[0]);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgbx_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[0]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[2]);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_8888_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (src[0]);
      dst[1] = UNPACK_BYTE (src[1]);
      dst[2] = UNPACK_BYTE (src[2]);
      dst[3] = UNPACK_BYTE (src[3]);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgb_565_, component_size) (const uint8_t *src,
                                                 component_type *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      uint16_t v = *(const uint16_t *) src;

      dst[0] = UNPACK_5 (v >> 11);
      dst[1] = UNPACK_6 ((v >> 5) & 0x3f);
      dst[2] = UNPACK_5 (v & 0x1f);
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 2;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_4444_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      uint16_t v = *(const uint16_t *) src;

      dst[0] = UNPACK_4 (v >> 12);
      dst[1] = UNPACK_4 ((v >> 8) & 0xf);
      dst[2] = UNPACK_4 ((v >> 4) & 0xf);
      dst[3] = UNPACK_4 (v & 0xf);
      dst += 4;
      src += 2;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_5551_, component_size) (const uint8_t *src,
                                                   component_type *dst,
                                                   int width)
{
  while (width-- > 0)
    {
      uint16_t v = *(const uint16_t *) src;

      dst[0] = UNPACK_5 (v >> 11);
      dst[1] = UNPACK_5 ((v >> 6) & 0x1f);
      dst[2] = UNPACK_5 ((v >> 1) & 0x1f);
      dst[3] = UNPACK_1 (v & 1);
      dst += 4;
      src += 2;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_1010102_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[0] = UNPACK_10 (v >> 22);
      dst[1] = UNPACK_10 ((v >> 12) & 0x3ff);
      dst[2] = UNPACK_10 ((v >> 2) & 0x3ff);
      dst[3] = UNPACK_2 (v & 3);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgra_1010102_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[2] = UNPACK_10 (v >> 22);
      dst[1] = UNPACK_10 ((v >> 12) & 0x3ff);
      dst[0] = UNPACK_10 ((v >> 2) & 0x3ff);
      dst[3] = UNPACK_2 (v & 3);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_xrgb_2101010_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[3] = UNPACK_2 (0x3);
      dst[0] = UNPACK_10 ((v >> 20) & 0x3ff);
      dst[1] = UNPACK_10 ((v >> 10) & 0x3ff);
      dst[2] = UNPACK_10 (v & 0x3ff);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_argb_2101010_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[3] = UNPACK_2 (v >> 30);
      dst[0] = UNPACK_10 ((v >> 20) & 0x3ff);
      dst[1] = UNPACK_10 ((v >> 10) & 0x3ff);
      dst[2] = UNPACK_10 (v & 0x3ff);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_xbgr_2101010_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[3] = UNPACK_2 (0x3);
      dst[2] = UNPACK_10 ((v >> 20) & 0x3ff);
      dst[1] = UNPACK_10 ((v >> 10) & 0x3ff);
      dst[0] = UNPACK_10 (v & 0x3ff);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_abgr_2101010_, component_size) (const uint8_t *src,
                                                      component_type *dst,
                                                      int width)
{
  while (width-- > 0)
    {
      uint32_t v = *(const uint32_t *) src;

      dst[3] = UNPACK_2 (v >> 30);
      dst[2] = UNPACK_10 ((v >> 20) & 0x3ff);
      dst[1] = UNPACK_10 ((v >> 10) & 0x3ff);
      dst[0] = UNPACK_10 (v & 0x3ff);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgbx_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[0]);
      dst[1] = (component_type) UNPACK_SHORT (src16[1]);
      dst[2] = (component_type) UNPACK_SHORT (src16[2]);
      dst[3] = (component_type) UNPACK_SHORT (0x3C00);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[0]);
      dst[1] = (component_type) UNPACK_SHORT (src16[1]);
      dst[2] = (component_type) UNPACK_SHORT (src16[2]);
      dst[3] = (component_type) UNPACK_SHORT (src16[3]);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgrx_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[2]);
      dst[1] = (component_type) UNPACK_SHORT (src16[1]);
      dst[2] = (component_type) UNPACK_SHORT (src16[0]);
      dst[3] = (component_type) UNPACK_SHORT (0x3C00);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_bgra_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[2]);
      dst[1] = (component_type) UNPACK_SHORT (src16[1]);
      dst[2] = (component_type) UNPACK_SHORT (src16[0]);
      dst[3] = (component_type) UNPACK_SHORT (src16[3]);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_xrgb_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[1]);
      dst[1] = (component_type) UNPACK_SHORT (src16[2]);
      dst[2] = (component_type) UNPACK_SHORT (src16[3]);
      dst[3] = (component_type) UNPACK_SHORT (0x3C00);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_argb_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[1]);
      dst[1] = (component_type) UNPACK_SHORT (src16[2]);
      dst[2] = (component_type) UNPACK_SHORT (src16[3]);
      dst[3] = (component_type) UNPACK_SHORT (src16[0]);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_xbgr_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[3]);
      dst[1] = (component_type) UNPACK_SHORT (src16[2]);
      dst[2] = (component_type) UNPACK_SHORT (src16[1]);
      dst[3] = (component_type) UNPACK_SHORT (0x3C00);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_abgr_fp_16161616_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint16_t *src16 = (const uint16_t *) src;

      dst[0] = (component_type) UNPACK_SHORT (src16[3]);
      dst[1] = (component_type) UNPACK_SHORT (src16[2]);
      dst[2] = (component_type) UNPACK_SHORT (src16[1]);
      dst[3] = (component_type) UNPACK_SHORT (src16[0]);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_fp_32323232_, component_size) (const uint8_t *src,
                                                          component_type *dst,
                                                          int width)
{
  while (width-- > 0)
    {
      const uint32_t *src32 = (const uint32_t *) src;

      dst[0] = (component_type) UNPACK_FLOAT (src32[0]);
      dst[1] = (component_type) UNPACK_FLOAT (src32[1]);
      dst[2] = (component_type) UNPACK_FLOAT (src32[2]);
      dst[3] = (component_type) UNPACK_FLOAT (src32[3]);
      dst += 4;
      src += 16;
    }
}

inline static void
G_PASTE (_cogl_unpack_r_16_, component_size) (const uint8_t *src,
                                              component_type *dst,
                                              int width)
{
  while (width-- > 0)
    {
      const uint16_t *v = (const uint16_t *) src;

      dst[0] = UNPACK_16 (v[0]);
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 2;
    }
}

inline static void
G_PASTE (_cogl_unpack_rg_1616_, component_size) (const uint8_t *src,
                                                 component_type *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      const uint16_t *v = (const uint16_t *) src;

      dst[0] = UNPACK_16 (v[0]);
      dst[1] = UNPACK_16 (v[1]);
      dst[2] = 0;
      dst[3] = UNPACK_BYTE (255);
      dst += 4;
      src += 4;
    }
}

inline static void
G_PASTE (_cogl_unpack_rgba_16161616_, component_size) (const uint8_t  *src,
                                                       component_type *dst,
                                                       int             width)
{
  while (width-- > 0)
    {
      const uint16_t *v = (const uint16_t *) src;

      dst[0] = UNPACK_16 (v[0]);
      dst[1] = UNPACK_16 (v[1]);
      dst[2] = UNPACK_16 (v[2]);
      dst[3] = UNPACK_16 (v[3]);
      dst += 4;
      src += 8;
    }
}

inline static void
G_PASTE (_cogl_unpack_, component_size) (CoglPixelFormat format,
                                         const uint8_t *src,
                                         component_type *dst,
                                         int width)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
      G_PASTE (_cogl_unpack_a_8_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_R_8:
      G_PASTE (_cogl_unpack_r_8_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RG_88:
      G_PASTE (_cogl_unpack_rg_88_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGB_888:
      G_PASTE (_cogl_unpack_rgb_888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGR_888:
      G_PASTE (_cogl_unpack_bgr_888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBX_8888:
      G_PASTE (_cogl_unpack_rgbx_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      G_PASTE (_cogl_unpack_rgba_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRX_8888:
      G_PASTE (_cogl_unpack_bgrx_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      G_PASTE (_cogl_unpack_bgra_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_8888:
      G_PASTE (_cogl_unpack_xrgb_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      G_PASTE (_cogl_unpack_argb_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_8888:
      G_PASTE (_cogl_unpack_xbgr_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
      G_PASTE (_cogl_unpack_abgr_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGB_565:
      G_PASTE (_cogl_unpack_rgb_565_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
      G_PASTE (_cogl_unpack_rgba_4444_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      G_PASTE (_cogl_unpack_rgba_5551_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
      G_PASTE (_cogl_unpack_rgba_1010102_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
      G_PASTE (_cogl_unpack_bgra_1010102_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_2101010:
      G_PASTE (_cogl_unpack_xrgb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      G_PASTE (_cogl_unpack_argb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_2101010:
      G_PASTE (_cogl_unpack_xbgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      G_PASTE (_cogl_unpack_abgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
      G_PASTE (_cogl_unpack_rgbx_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
      G_PASTE (_cogl_unpack_rgba_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
      G_PASTE (_cogl_unpack_bgrx_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
      G_PASTE (_cogl_unpack_bgra_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
      G_PASTE (_cogl_unpack_xrgb_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
      G_PASTE (_cogl_unpack_argb_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
      G_PASTE (_cogl_unpack_xbgr_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
      G_PASTE (_cogl_unpack_abgr_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      G_PASTE (_cogl_unpack_rgba_fp_32323232_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_R_16:
      G_PASTE (_cogl_unpack_r_16_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RG_1616:
      G_PASTE (_cogl_unpack_rg_1616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      G_PASTE (_cogl_unpack_rgba_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
    }
}

/* Packing from RGBA */

inline static void
G_PASTE (_cogl_pack_a_8_, component_size) (const component_type *src,
                                           uint8_t *dst,
                                           int width)
{
  while (width-- > 0)
    {
      *dst = PACK_BYTE (src[3]);
      src += 4;
      dst++;
    }
}

inline static void
G_PASTE (_cogl_pack_r_8_, component_size) (const component_type *src,
                                           uint8_t *dst,
                                           int width)
{
  /* FIXME: I'm not sure if this is right. It looks like Nvidia and
     Mesa handle luminance textures differently. Maybe we should
     consider just removing luminance textures for Cogl 2.0 because
     they have been removed in GL 3.0 */
  while (width-- > 0)
    {
      component_type v = (src[0] + src[1] + src[2]) / 3;
      *dst = PACK_BYTE (v);
      src += 4;
      dst++;
    }
}

inline static void
G_PASTE (_cogl_pack_rg_88_, component_size) (const component_type *src,
                                             uint8_t *dst,
                                             int width)
{
  while (width-- > 0)
    {
      dst[0] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      src += 4;
      dst += 2;
    }
}

inline static void
G_PASTE (_cogl_pack_rgb_888_, component_size) (const component_type *src,
                                               uint8_t *dst,
                                               int width)
{
  while (width-- > 0)
    {
      dst[0] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[2] = PACK_BYTE (src[2]);
      src += 4;
      dst += 3;
    }
}

inline static void
G_PASTE (_cogl_pack_bgr_888_, component_size) (const component_type *src,
                                               uint8_t *dst,
                                               int width)
{
  while (width-- > 0)
    {
      dst[2] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[0] = PACK_BYTE (src[2]);
      src += 4;
      dst += 3;
    }
}

inline static void
G_PASTE (_cogl_pack_bgrx_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[2] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[0] = PACK_BYTE (src[2]);
      dst[3] = 255;
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_bgra_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[2] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[0] = PACK_BYTE (src[2]);
      dst[3] = PACK_BYTE (src[3]);
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_xrgb_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[1] = PACK_BYTE (src[0]);
      dst[2] = PACK_BYTE (src[1]);
      dst[3] = PACK_BYTE (src[2]);
      dst[0] = 255;
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_argb_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[1] = PACK_BYTE (src[0]);
      dst[2] = PACK_BYTE (src[1]);
      dst[3] = PACK_BYTE (src[2]);
      dst[0] = PACK_BYTE (src[3]);
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_xbgr_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[3] = PACK_BYTE (src[0]);
      dst[2] = PACK_BYTE (src[1]);
      dst[1] = PACK_BYTE (src[2]);
      dst[0] = 255;
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_abgr_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[3] = PACK_BYTE (src[0]);
      dst[2] = PACK_BYTE (src[1]);
      dst[1] = PACK_BYTE (src[2]);
      dst[0] = PACK_BYTE (src[3]);
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_rgbx_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[0] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[2] = PACK_BYTE (src[2]);
      dst[3] = 255;
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_8888_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      dst[0] = PACK_BYTE (src[0]);
      dst[1] = PACK_BYTE (src[1]);
      dst[2] = PACK_BYTE (src[2]);
      dst[3] = PACK_BYTE (src[3]);
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_rgb_565_, component_size) (const component_type *src,
                                               uint8_t *dst,
                                               int width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      *v = ((PACK_5 (src[0]) << 11) |
            (PACK_6 (src[1]) << 5) |
            PACK_5 (src[2]));
      src += 4;
      dst += 2;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_4444_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      *v = ((PACK_4 (src[0]) << 12) |
            (PACK_4 (src[1]) << 8) |
            (PACK_4 (src[2]) << 4) |
            PACK_4 (src[3]));
      src += 4;
      dst += 2;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_5551_, component_size) (const component_type *src,
                                                 uint8_t *dst,
                                                 int width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      *v = ((PACK_5 (src[0]) << 11) |
            (PACK_5 (src[1]) << 6) |
            (PACK_5 (src[2]) << 1) |
            PACK_1 (src[3]));
      src += 4;
      dst += 2;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_1010102_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((PACK_10 (src[0]) << 22) |
            (PACK_10 (src[1]) << 12) |
            (PACK_10 (src[2]) << 2) |
            PACK_2 (src[3]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_bgra_1010102_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((PACK_10 (src[2]) << 22) |
            (PACK_10 (src[1]) << 12) |
            (PACK_10 (src[0]) << 2) |
            PACK_2 (src[3]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_xrgb_2101010_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((0x3 << 30) |
            (PACK_10 (src[0]) << 20) |
            (PACK_10 (src[1]) << 10) |
            PACK_10 (src[2]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_argb_2101010_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((PACK_2 (src[3]) << 30) |
            (PACK_10 (src[0]) << 20) |
            (PACK_10 (src[1]) << 10) |
            PACK_10 (src[2]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_xbgr_2101010_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((0x3 << 30) |
            (PACK_10 (src[2]) << 20) |
            (PACK_10 (src[1]) << 10) |
            PACK_10 (src[0]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_abgr_2101010_, component_size) (const component_type *src,
                                                    uint8_t *dst,
                                                    int width)
{
  while (width-- > 0)
    {
      uint32_t *v = (uint32_t *) dst;

      *v = ((PACK_2 (src[3]) << 30) |
            (PACK_10 (src[2]) << 20) |
            (PACK_10 (src[1]) << 10) |
            PACK_10 (src[0]));
      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_rgbx_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[0]);
      dst16[1] = PACK_SHORT (src[1]);
      dst16[2] = PACK_SHORT (src[2]);
      dst16[3] = 0x3C00;
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[0]);
      dst16[1] = PACK_SHORT (src[1]);
      dst16[2] = PACK_SHORT (src[2]);
      dst16[3] = PACK_SHORT (src[3]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_bgrx_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[2]);
      dst16[1] = PACK_SHORT (src[1]);
      dst16[2] = PACK_SHORT (src[0]);
      dst16[3] = 0x3C00;
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_bgra_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[2]);
      dst16[1] = PACK_SHORT (src[1]);
      dst16[2] = PACK_SHORT (src[0]);
      dst16[3] = PACK_SHORT (src[3]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_xrgb_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = 0x3C00;
      dst16[1] = PACK_SHORT (src[0]);
      dst16[2] = PACK_SHORT (src[1]);
      dst16[3] = PACK_SHORT (src[2]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_argb_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[3]);
      dst16[1] = PACK_SHORT (src[0]);
      dst16[2] = PACK_SHORT (src[1]);
      dst16[3] = PACK_SHORT (src[2]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_xbgr_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = 0x3C00;
      dst16[1] = PACK_SHORT (src[2]);
      dst16[2] = PACK_SHORT (src[1]);
      dst16[3] = PACK_SHORT (src[0]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_abgr_fp_16161616_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint16_t *dst16 = (uint16_t *) dst;

      dst16[0] = PACK_SHORT (src[3]);
      dst16[1] = PACK_SHORT (src[2]);
      dst16[2] = PACK_SHORT (src[1]);
      dst16[3] = PACK_SHORT (src[0]);
      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_fp_32323232_, component_size) (const component_type *src,
                                                        uint8_t *dst,
                                                        int width)
{
  while (width-- > 0)
    {
      uint32_t *dst32 = (uint32_t *) dst;

      dst32[0] = PACK_FLOAT (src[0]);
      dst32[1] = PACK_FLOAT (src[1]);
      dst32[2] = PACK_FLOAT (src[2]);
      dst32[3] = PACK_FLOAT (src[3]);
      src += 4;
      dst += 16;
    }
}

inline static void
G_PASTE (_cogl_pack_r_16_, component_size) (const component_type *src,
                                             uint8_t *dst,
                                             int width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      v[0] = PACK_16 (src[0]);

      src += 4;
      dst += 2;
    }
}

inline static void
G_PASTE (_cogl_pack_rg_1616_, component_size) (const component_type *src,
                                             uint8_t *dst,
                                             int width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      v[0] = PACK_16 (src[0]);
      v[1] = PACK_16 (src[1]);

      src += 4;
      dst += 4;
    }
}

inline static void
G_PASTE (_cogl_pack_rgba_16161616_, component_size) (const component_type *src,
                                                     uint8_t              *dst,
                                                     int                   width)
{
  while (width-- > 0)
    {
      uint16_t *v = (uint16_t *) dst;

      v[0] = PACK_16 (src[0]);
      v[1] = PACK_16 (src[1]);
      v[2] = PACK_16 (src[2]);
      v[3] = PACK_16 (src[3]);

      src += 4;
      dst += 8;
    }
}

inline static void
G_PASTE (_cogl_pack_, component_size) (CoglPixelFormat format,
                                       const component_type *src,
                                       uint8_t *dst,
                                       int width)
{
  switch (format)
    {
    case COGL_PIXEL_FORMAT_A_8:
      G_PASTE (_cogl_pack_a_8_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_R_8:
      G_PASTE (_cogl_pack_r_8_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RG_88:
      G_PASTE (_cogl_pack_rg_88_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGB_888:
      G_PASTE (_cogl_pack_rgb_888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGR_888:
      G_PASTE (_cogl_pack_bgr_888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBX_8888:
      G_PASTE (_cogl_pack_rgbx_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      G_PASTE (_cogl_pack_rgba_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRX_8888:
      G_PASTE (_cogl_pack_bgrx_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      G_PASTE (_cogl_pack_bgra_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_8888:
      G_PASTE (_cogl_pack_xrgb_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      G_PASTE (_cogl_pack_argb_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_8888:
      G_PASTE (_cogl_pack_xbgr_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_8888:
    case COGL_PIXEL_FORMAT_ABGR_8888_PRE:
      G_PASTE (_cogl_pack_abgr_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGB_565:
      G_PASTE (_cogl_pack_rgb_565_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_4444:
    case COGL_PIXEL_FORMAT_RGBA_4444_PRE:
      G_PASTE (_cogl_pack_rgba_4444_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_5551:
    case COGL_PIXEL_FORMAT_RGBA_5551_PRE:
      G_PASTE (_cogl_pack_rgba_5551_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_1010102:
    case COGL_PIXEL_FORMAT_RGBA_1010102_PRE:
      G_PASTE (_cogl_pack_rgba_1010102_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_1010102:
    case COGL_PIXEL_FORMAT_BGRA_1010102_PRE:
      G_PASTE (_cogl_pack_bgra_1010102_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_2101010:
      G_PASTE (_cogl_pack_xrgb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      G_PASTE (_cogl_pack_argb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_2101010:
      G_PASTE (_cogl_pack_xbgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      G_PASTE (_cogl_pack_abgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBX_FP_16161616:
      G_PASTE (_cogl_pack_rgbx_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616:
    case COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE:
      G_PASTE (_cogl_pack_rgba_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRX_FP_16161616:
      G_PASTE (_cogl_pack_bgrx_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616:
    case COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE:
      G_PASTE (_cogl_pack_bgra_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XRGB_FP_16161616:
      G_PASTE (_cogl_pack_xrgb_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616:
    case COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE:
      G_PASTE (_cogl_pack_argb_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_XBGR_FP_16161616:
      G_PASTE (_cogl_pack_xbgr_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616:
    case COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE:
      G_PASTE (_cogl_pack_abgr_fp_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232:
    case COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE:
      G_PASTE (_cogl_pack_rgba_fp_32323232_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_R_16:
      G_PASTE (_cogl_pack_r_16_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RG_1616:
      G_PASTE (_cogl_pack_rg_1616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_RGBA_16161616:
    case COGL_PIXEL_FORMAT_RGBA_16161616_PRE:
      G_PASTE (_cogl_pack_rgba_16161616_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    case COGL_PIXEL_FORMAT_YUV:
      g_assert_not_reached ();
    }
}
