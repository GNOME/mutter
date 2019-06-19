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

#define UNPACK_1(b) ((b) * ((1 << (sizeof (component_type) * 8)) - 1))
#define UNPACK_2(b) (((b) * ((1 << (sizeof (component_type) * 8)) - 1) + \
                      1) / 3)
#define UNPACK_4(b) (((b) * ((1 << (sizeof (component_type) * 8)) - 1) + \
                      7) / 15)
#define UNPACK_5(b) (((b) * ((1 << (sizeof (component_type) * 8)) - 1) + \
                      15) / 31)
#define UNPACK_6(b) (((b) * ((1 << (sizeof (component_type) * 8)) - 1) + \
                      31) / 63)
#define UNPACK_10(b) (((b) * ((1 << (sizeof (component_type) * 8)) - 1) + \
                       511) / 1023)

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
  while (width-- > 0)
    {
      dst[0] = UNPACK_BYTE (*src);
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = 0;
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
      dst[1] = UNPACK_6 ((v >> 5) & 63);
      dst[2] = UNPACK_5 (v & 31);
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
      dst[1] = UNPACK_4 ((v >> 8) & 15);
      dst[2] = UNPACK_4 ((v >> 4) & 15);
      dst[3] = UNPACK_4 (v & 15);
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
      dst[1] = UNPACK_5 ((v >> 6) & 31);
      dst[2] = UNPACK_5 ((v >> 1) & 31);
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
      dst[1] = UNPACK_10 ((v >> 12) & 1023);
      dst[2] = UNPACK_10 ((v >> 2) & 1023);
      dst[3] = UNPACK_2 (v & 3);
      dst += 4;
      src += 2;
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
      dst[1] = UNPACK_10 ((v >> 12) & 1023);
      dst[0] = UNPACK_10 ((v >> 2) & 1023);
      dst[3] = UNPACK_2 (v & 3);
      dst += 4;
      src += 2;
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
      dst[0] = UNPACK_10 ((v >> 20) & 1023);
      dst[1] = UNPACK_10 ((v >> 10) & 1023);
      dst[2] = UNPACK_10 (v & 1023);
      dst += 4;
      src += 2;
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
      dst[2] = UNPACK_10 ((v >> 20) & 1023);
      dst[1] = UNPACK_10 ((v >> 10) & 1023);
      dst[0] = UNPACK_10 (v & 1023);
      dst += 4;
      src += 2;
    }
}

#undef UNPACK_1
#undef UNPACK_2
#undef UNPACK_4
#undef UNPACK_5
#undef UNPACK_6
#undef UNPACK_10

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
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      G_PASTE (_cogl_unpack_rgba_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      G_PASTE (_cogl_unpack_bgra_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      G_PASTE (_cogl_unpack_argb_8888_, component_size) (src, dst, width);
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
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      G_PASTE (_cogl_unpack_argb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      G_PASTE (_cogl_unpack_abgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_32:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    /* No support for YUV or multi-plane formats */
    case COGL_PIXEL_FORMAT_YUV:
    case COGL_PIXEL_FORMAT_YUYV:
    case COGL_PIXEL_FORMAT_YVYU:
    case COGL_PIXEL_FORMAT_UYVY:
    case COGL_PIXEL_FORMAT_VYUY:
    case COGL_PIXEL_FORMAT_AYUV:
    case COGL_PIXEL_FORMAT_XRGB8888_A8:
    case COGL_PIXEL_FORMAT_XBGR8888_A8:
    case COGL_PIXEL_FORMAT_RGBX8888_A8:
    case COGL_PIXEL_FORMAT_BGRX8888_A8:
    case COGL_PIXEL_FORMAT_RGB888_A8:
    case COGL_PIXEL_FORMAT_BGR888_A8:
    case COGL_PIXEL_FORMAT_RGB565_A8:
    case COGL_PIXEL_FORMAT_BGR565_A8:
    case COGL_PIXEL_FORMAT_NV12:
    case COGL_PIXEL_FORMAT_NV21:
    case COGL_PIXEL_FORMAT_NV16:
    case COGL_PIXEL_FORMAT_NV61:
    case COGL_PIXEL_FORMAT_NV24:
    case COGL_PIXEL_FORMAT_NV42:
    case COGL_PIXEL_FORMAT_YUV410:
    case COGL_PIXEL_FORMAT_YVU410:
    case COGL_PIXEL_FORMAT_YUV411:
    case COGL_PIXEL_FORMAT_YVU411:
    case COGL_PIXEL_FORMAT_YUV420:
    case COGL_PIXEL_FORMAT_YVU420:
    case COGL_PIXEL_FORMAT_YUV422:
    case COGL_PIXEL_FORMAT_YVU422:
    case COGL_PIXEL_FORMAT_YUV444:
    case COGL_PIXEL_FORMAT_YVU444:
      g_assert_not_reached ();
    }
}

/* Packing from RGBA */

/* Pack and round to nearest */
#define PACK_SIZE(b, max) \
  (((b) * (max) + (1 << (sizeof (component_type) * 8 - 1)) - 1) / \
   ((1 << (sizeof (component_type) * 8)) - 1))

#define PACK_1(b) PACK_SIZE (b, 1)
#define PACK_2(b) PACK_SIZE (b, 3)
#define PACK_4(b) PACK_SIZE (b, 15)
#define PACK_5(b) PACK_SIZE (b, 31)
#define PACK_6(b) PACK_SIZE (b, 63)
#define PACK_10(b) PACK_SIZE (b, 1023)

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
  while (width-- > 0)
    {
      *dst = PACK_BYTE (src[0]);
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

#undef PACK_SIZE
#undef PACK_1
#undef PACK_2
#undef PACK_4
#undef PACK_5
#undef PACK_6
#undef PACK_10

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
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      G_PASTE (_cogl_pack_rgba_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_BGRA_8888:
    case COGL_PIXEL_FORMAT_BGRA_8888_PRE:
      G_PASTE (_cogl_pack_bgra_8888_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ARGB_8888:
    case COGL_PIXEL_FORMAT_ARGB_8888_PRE:
      G_PASTE (_cogl_pack_argb_8888_, component_size) (src, dst, width);
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
    case COGL_PIXEL_FORMAT_ARGB_2101010:
    case COGL_PIXEL_FORMAT_ARGB_2101010_PRE:
      G_PASTE (_cogl_pack_argb_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_ABGR_2101010:
    case COGL_PIXEL_FORMAT_ABGR_2101010_PRE:
      G_PASTE (_cogl_pack_abgr_2101010_, component_size) (src, dst, width);
      break;
    case COGL_PIXEL_FORMAT_DEPTH_16:
    case COGL_PIXEL_FORMAT_DEPTH_32:
    case COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
    case COGL_PIXEL_FORMAT_ANY:
    /* No support for YUV or multi-plane formats */
    case COGL_PIXEL_FORMAT_YUV:
    case COGL_PIXEL_FORMAT_YUYV:
    case COGL_PIXEL_FORMAT_YVYU:
    case COGL_PIXEL_FORMAT_UYVY:
    case COGL_PIXEL_FORMAT_VYUY:
    case COGL_PIXEL_FORMAT_AYUV:
    case COGL_PIXEL_FORMAT_XRGB8888_A8:
    case COGL_PIXEL_FORMAT_XBGR8888_A8:
    case COGL_PIXEL_FORMAT_RGBX8888_A8:
    case COGL_PIXEL_FORMAT_BGRX8888_A8:
    case COGL_PIXEL_FORMAT_RGB888_A8:
    case COGL_PIXEL_FORMAT_BGR888_A8:
    case COGL_PIXEL_FORMAT_RGB565_A8:
    case COGL_PIXEL_FORMAT_BGR565_A8:
    case COGL_PIXEL_FORMAT_NV12:
    case COGL_PIXEL_FORMAT_NV21:
    case COGL_PIXEL_FORMAT_NV16:
    case COGL_PIXEL_FORMAT_NV61:
    case COGL_PIXEL_FORMAT_NV24:
    case COGL_PIXEL_FORMAT_NV42:
    case COGL_PIXEL_FORMAT_YUV410:
    case COGL_PIXEL_FORMAT_YVU410:
    case COGL_PIXEL_FORMAT_YUV411:
    case COGL_PIXEL_FORMAT_YVU411:
    case COGL_PIXEL_FORMAT_YUV420:
    case COGL_PIXEL_FORMAT_YVU420:
    case COGL_PIXEL_FORMAT_YUV422:
    case COGL_PIXEL_FORMAT_YVU422:
    case COGL_PIXEL_FORMAT_YUV444:
    case COGL_PIXEL_FORMAT_YVU444:
      g_assert_not_reached ();
    }
}
