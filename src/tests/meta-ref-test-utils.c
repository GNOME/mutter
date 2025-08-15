/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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
 */

/*
 * The image difference code is originally a reformatted and simplified
 * copy of weston-test-client-helper.c from the weston repository, with
 * the following copyright and license note:
 *
 * Copyright © 2012 Intel Corporation
 * Copyright © 2015 Samsung Electronics Co., Ltd
 * Copyright 2016, 2017 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include "tests/meta-ref-test-utils.h"

#include <cairo.h>
#include <glib.h>
#include <stdint.h>

typedef struct _Range
{
  int a;
  int b;
} Range;

typedef struct _ImageIterator
{
  uint8_t *data;
  int stride;
} ImageIterator;

typedef struct _PixelDiffStat
{
  /* Pixel diff stat channel */
  struct {
    int min_diff;
    int max_diff;
  } ch[4];
} PixelDiffStat;

/**
 * range_get:
 * @range: Range to validate or NULL.
 *
 * Validate and get range.
 *
 * Returns the given range, or {0, 0} for NULL.
 *
 * Will abort if range is invalid, that is a > b.
 */
static Range
range_get (const Range *range)
{
  if (!range)
    return (Range) { 0, 0 };

  g_assert_cmpint (range->a, <=, range->b);
  return *range;
}

static void
image_iterator_init (ImageIterator   *it,
                     cairo_surface_t *image)
{
  it->stride = cairo_image_surface_get_stride (image);
  it->data = cairo_image_surface_get_data (image);

  g_assert_cmpint (cairo_image_surface_get_format (image), ==,
                   CAIRO_FORMAT_ARGB32);
}

static uint32_t *
image_iterator_get_row (ImageIterator *it,
                        int            y)
{
  return (uint32_t *) (it->data + y * it->stride);
}

static gboolean
fuzzy_match_pixels (uint32_t       pix_a,
                    uint32_t       pix_b,
                    const Range   *fuzz,
                    PixelDiffStat *diff_stat)
{
  gboolean ret = TRUE;
  int shift;
  int i;

  for (shift = 0, i = 0; i < 4; shift += 8, i++)
    {
      int val_a = (pix_a >> shift) & 0xffu;
      int val_b = (pix_b >> shift) & 0xffu;
      int d = val_b - val_a;

      if (diff_stat)
        {
          diff_stat->ch[i].min_diff = MIN (diff_stat->ch[i].min_diff, d);
          diff_stat->ch[i].max_diff = MAX (diff_stat->ch[i].max_diff, d);
        }

      if (d < fuzz->a || d > fuzz->b)
        ret = FALSE;
    }

  return ret;
}

static gboolean
compare_images (cairo_surface_t *ref_image,
                cairo_surface_t *result_image,
                const Range     *precision,
                PixelDiffStat   *diff_stat)
{
  Range fuzz = range_get (precision);
  ImageIterator it_ref;
  ImageIterator it_result;
  int x, y;
  uint32_t *pix_ref;
  uint32_t *pix_result;

  g_assert_cmpint (cairo_image_surface_get_width (ref_image), ==,
                   cairo_image_surface_get_width (result_image));
  g_assert_cmpint (cairo_image_surface_get_height (ref_image), ==,
                   cairo_image_surface_get_height (result_image));

  image_iterator_init (&it_ref, ref_image);
  image_iterator_init (&it_result, result_image);

  for (y = 0; y < cairo_image_surface_get_height (ref_image); y++)
    {
      pix_ref = image_iterator_get_row (&it_ref, y);
      pix_result = image_iterator_get_row (&it_result, y);

      for (x = 0; x < cairo_image_surface_get_width (ref_image); x++)
        {
          if (!fuzzy_match_pixels (*pix_ref, *pix_result,
                                   &fuzz, diff_stat))
            return FALSE;

          pix_ref++;
          pix_result++;
        }
    }

  return TRUE;
}

static void
depathify (char *path)
{
  int len = strlen (path);
  int i;

  for (i = 0; i < len; i++)
    {
      if (path[i] == '/')
        path[i] = '_';
    }
}

static void
ensure_expected_format (cairo_surface_t **ref_image)
{
  int width, height;
  cairo_surface_t *target;
  cairo_t *cr;

  if (cairo_image_surface_get_format (*ref_image) ==
      CAIRO_FORMAT_ARGB32)
    return;

  width = cairo_image_surface_get_width (*ref_image);
  height = cairo_image_surface_get_height (*ref_image);
  target = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);

  cr = cairo_create (target);
  cairo_set_source_surface (cr, *ref_image, 0.0, 0.0);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_destroy (*ref_image);
  *ref_image = target;
}

/*
 * Tint a color:
 * @src Source pixel as x8r8g8b8.
 * @add The tint as x8r8g8b8, x8 must be zero; r8, g8 and b8 must be
 * no greater than 0xc0 to avoid overflow to another channel.
 * Returns: The tinted pixel color as x8r8g8b8, x8 guaranteed to be 0xff.
 *
 * The source pixel RGB values are divided by 4, and then the tint is added.
 * To achieve colors outside of the range of src, a tint color channel must be
 * at least 0x40. (0xff / 4 = 0x3f, 0xff - 0x3f = 0xc0)
 */
static uint32_t
tint (uint32_t src,
      uint32_t add)
{
  uint32_t v;

  v = ((src & 0xfcfcfcfc) >> 2) | 0xff000000;

  return v + add;
}

static cairo_surface_t *
visualize_difference (cairo_surface_t *ref_image,
                      cairo_surface_t *result_image,
                      const Range     *precision)
{
  Range fuzz = range_get (precision);
  int width, height;
  cairo_surface_t *diff_image;
  cairo_t *cr;
  ImageIterator it_ref;
  ImageIterator it_result;
  ImageIterator it_diff;
  int y;

  width = cairo_image_surface_get_width (ref_image);
  height = cairo_image_surface_get_height (ref_image);

  diff_image = cairo_surface_create_similar_image (ref_image,
                                                   CAIRO_FORMAT_ARGB32,
                                                   width,
                                                   height);
  cr = cairo_create (diff_image);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);
  cairo_paint (cr);
  cairo_set_source_surface (cr, ref_image, 0.0, 0.0);
  cairo_set_operator (cr, CAIRO_OPERATOR_HSL_LUMINOSITY);
  cairo_paint (cr);
  cairo_destroy (cr);

  image_iterator_init (&it_ref, ref_image);
  image_iterator_init (&it_result, result_image);
  image_iterator_init (&it_diff, diff_image);

  for (y = 0; y < cairo_image_surface_get_height (ref_image); y++)
    {
      uint32_t *ref_pixel;
      uint32_t *result_pixel;
      uint32_t *diff_pixel;
      int x;

      ref_pixel = image_iterator_get_row (&it_ref, y);
      result_pixel = image_iterator_get_row (&it_result, y);
      diff_pixel = image_iterator_get_row (&it_diff, y);

      for (x = 0; x < cairo_image_surface_get_width (ref_image); x++)
        {
          if (fuzzy_match_pixels (*ref_pixel, *result_pixel,
                                  &fuzz, NULL))
            *diff_pixel = tint (*diff_pixel, 0x00008000); /* green */
          else
            *diff_pixel = tint (*diff_pixel, 0x00c00000); /* red */

          ref_pixel++;
          result_pixel++;
          diff_pixel++;
        }
    }

  return diff_image;
}

void
meta_ref_test_verify (MetaRefTestAdaptor  adaptor,
                      gpointer            adaptor_data,
                      const char         *test_name_unescaped,
                      int                 test_seq_no,
                      MetaReftestFlag     flags)
{
  cairo_surface_t *image;
  const char *dist_dir;
  g_autofree char *test_name = NULL;
  g_autofree char *ref_image_path = NULL;
  cairo_surface_t *ref_image;
  cairo_status_t ref_status;
  gboolean maybe_update;

  image = adaptor (adaptor_data);

  test_name = g_strdup (test_name_unescaped + 1);
  depathify (test_name);

  dist_dir = g_test_get_dir (G_TEST_DIST);
  ref_image_path = g_strdup_printf ("%s/ref-tests/%s_%d.ref.png",
                                    dist_dir,
                                    test_name, test_seq_no);

  ref_image = cairo_image_surface_create_from_png (ref_image_path);
  g_assert_nonnull (ref_image);
  ref_status = cairo_surface_status (ref_image);

  g_assert_true (ref_status == CAIRO_STATUS_FILE_NOT_FOUND ||
                 ref_status == CAIRO_STATUS_SUCCESS);

  if (ref_status == CAIRO_STATUS_FILE_NOT_FOUND)
    {
      maybe_update = ((flags & META_REFTEST_FLAG_UPDATE_REF) ||
                      (flags & META_REFTEST_FLAG_ENSURE_REF));
    }
  else
    {
      maybe_update = !!(flags & META_REFTEST_FLAG_UPDATE_REF);
    }

  if (maybe_update)
    {
      if (ref_status == CAIRO_STATUS_SUCCESS)
        ensure_expected_format (&ref_image);

      if (ref_status == CAIRO_STATUS_SUCCESS &&
          cairo_image_surface_get_width (ref_image) ==
          cairo_image_surface_get_width (image) &&
          cairo_image_surface_get_height (ref_image) ==
          cairo_image_surface_get_height (image) &&
          compare_images (ref_image, image, NULL, NULL))
        {
          g_message ("Not updating '%s', it didn't change.", ref_image_path);
        }
      else
        {
          g_message ("Updating '%s'.", ref_image_path);
          g_assert_cmpint (cairo_surface_write_to_png (image, ref_image_path),
                           ==,
                           CAIRO_STATUS_SUCCESS);
        }
    }
  else
    {
      const Range gl_fuzz = { -3, 4 };
      PixelDiffStat diff_stat = {};

      g_assert_cmpint (ref_status, ==, CAIRO_STATUS_SUCCESS);
      ensure_expected_format (&ref_image);

      if (!compare_images (ref_image, image, &gl_fuzz,
                           &diff_stat))
        {
          cairo_surface_t *diff_image;
          const char *ref_test_result_dir;
          g_autofree char *ref_image_copy_path = NULL;
          g_autofree char *result_image_path = NULL;
          g_autofree char *diff_image_path = NULL;
          int ret;

          diff_image = visualize_difference (ref_image, image,
                                             &gl_fuzz);

          ref_test_result_dir = g_getenv ("MUTTER_REF_TEST_RESULT_DIR");
          g_assert_nonnull (ref_test_result_dir);
          ref_image_copy_path =
            g_strdup_printf ("%s/%s_%d.ref.png",
                             ref_test_result_dir,
                             test_name, test_seq_no);
          result_image_path =
            g_strdup_printf ("%s/%s_%d.result.png",
                             ref_test_result_dir,
                             test_name, test_seq_no);
          diff_image_path =
            g_strdup_printf ("%s/%s_%d.diff.png",
                             ref_test_result_dir,
                             test_name, test_seq_no);

          ret = g_mkdir_with_parents (ref_test_result_dir, 0755);
          if (ret == -1)
            {
              g_error ("Failed to create directory %s: %s",
                       ref_test_result_dir,
                       g_strerror (errno));
            }

          g_assert_cmpint (cairo_surface_write_to_png (ref_image,
                                                       ref_image_copy_path),
                           ==,
                           CAIRO_STATUS_SUCCESS);
          g_assert_cmpint (cairo_surface_write_to_png (image,
                                                       result_image_path),
                           ==,
                           CAIRO_STATUS_SUCCESS);
          g_assert_cmpint (cairo_surface_write_to_png (diff_image,
                                                       diff_image_path),
                           ==,
                           CAIRO_STATUS_SUCCESS);

          g_critical ("Pixel difference exceeds limits "
                      "(min: [%d, %d, %d, %d], "
                      "max: [%d, %d, %d, %d])\n"
                      "See %s %s %s for details.",
                      diff_stat.ch[0].min_diff,
                      diff_stat.ch[1].min_diff,
                      diff_stat.ch[2].min_diff,
                      diff_stat.ch[3].min_diff,
                      diff_stat.ch[0].max_diff,
                      diff_stat.ch[1].max_diff,
                      diff_stat.ch[2].max_diff,
                      diff_stat.ch[3].max_diff,
                      ref_image_copy_path,
                      result_image_path,
                      diff_image_path);
        }
      else
        {
          g_message ("Image matched the reference image '%s'.", ref_image_path);
        }
    }

  cairo_surface_destroy (image);
  cairo_surface_destroy (ref_image);
}
