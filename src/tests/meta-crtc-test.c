/*
 * Copyright (C) 2016-2024 Red Hat, Inc.
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
 */

#include "config.h"

#include "tests/meta-crtc-test.h"

#define GAMMA_SIZE 256

G_DEFINE_TYPE (MetaCrtcTest, meta_crtc_test, META_TYPE_CRTC_NATIVE)

static size_t
meta_crtc_test_get_gamma_lut_size (MetaCrtc *crtc)
{
  MetaCrtcTest *crtc_test = META_CRTC_TEST (crtc);

  return crtc_test->gamma.size;
}

static MetaGammaLut *
meta_crtc_test_get_gamma_lut (MetaCrtc *crtc)
{
  MetaCrtcTest *crtc_test = META_CRTC_TEST (crtc);
  MetaGammaLut *lut;

  g_assert_cmpint (crtc_test->gamma.size, >, 0);

  lut = g_new0 (MetaGammaLut, 1);
  lut->size = crtc_test->gamma.size;
  lut->red = g_memdup2 (crtc_test->gamma.red,
                        lut->size * sizeof (uint16_t));
  lut->green = g_memdup2 (crtc_test->gamma.green,
                          lut->size * sizeof (uint16_t));
  lut->blue = g_memdup2 (crtc_test->gamma.blue,
                         lut->size * sizeof (uint16_t));
  return lut;
}

static void
meta_crtc_test_set_gamma_lut (MetaCrtc           *crtc,
                              const MetaGammaLut *lut)
{
  MetaCrtcTest *crtc_test = META_CRTC_TEST (crtc);

  g_assert_cmpint (crtc_test->gamma.size, ==, lut->size);

  g_free (crtc_test->gamma.red);
  g_free (crtc_test->gamma.green);
  g_free (crtc_test->gamma.blue);

  crtc_test->gamma.red = g_memdup2 (lut->red,
                                    sizeof (uint16_t) * lut->size);
  crtc_test->gamma.green = g_memdup2 (lut->green,
                                      sizeof (uint16_t) * lut->size);
  crtc_test->gamma.blue = g_memdup2 (lut->blue,
                                     sizeof (uint16_t) * lut->size);
}

static gboolean
meta_crtc_test_is_transform_handled (MetaCrtcNative      *crtc_native,
                                     MtkMonitorTransform  monitor_transform)
{
  MetaCrtcTest *crtc_test = META_CRTC_TEST (crtc_native);

  return crtc_test->handles_transforms;
}

static gboolean
meta_crtc_test_is_hw_cursor_supported (MetaCrtcNative *crtc_native)
{
  return FALSE;
}

static int64_t
meta_crtc_test_get_deadline_evasion (MetaCrtcNative *crtc_native)
{
  return 0;
}

static void
meta_crtc_test_finalize (GObject *object)
{
  MetaCrtcTest *crtc_test = META_CRTC_TEST (object);

  g_free (crtc_test->gamma.red);
  g_free (crtc_test->gamma.green);
  g_free (crtc_test->gamma.blue);

  G_OBJECT_CLASS (meta_crtc_test_parent_class)->finalize (object);
}

static void
meta_crtc_test_class_init (MetaCrtcTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCrtcClass *crtc_class = META_CRTC_CLASS (klass);
  MetaCrtcNativeClass *crtc_native_class = META_CRTC_NATIVE_CLASS (klass);

  object_class->finalize = meta_crtc_test_finalize;

  crtc_class->get_gamma_lut_size = meta_crtc_test_get_gamma_lut_size;
  crtc_class->get_gamma_lut = meta_crtc_test_get_gamma_lut;
  crtc_class->set_gamma_lut = meta_crtc_test_set_gamma_lut;

  crtc_native_class->is_transform_handled =
    meta_crtc_test_is_transform_handled;
  crtc_native_class->is_hw_cursor_supported =
    meta_crtc_test_is_hw_cursor_supported;
  crtc_native_class->get_deadline_evasion =
    meta_crtc_test_get_deadline_evasion;
}

static void
meta_crtc_test_init (MetaCrtcTest *crtc_test)
{
  int i;

  crtc_test->gamma.size = GAMMA_SIZE;
  crtc_test->gamma.red = g_new0 (uint16_t, GAMMA_SIZE);
  crtc_test->gamma.green = g_new0 (uint16_t, GAMMA_SIZE);
  crtc_test->gamma.blue = g_new0 (uint16_t, GAMMA_SIZE);

  for (i = 0; i < GAMMA_SIZE; i++)
    {
      uint16_t gamma;

      gamma = (uint16_t) (((float) i / GAMMA_SIZE) * UINT16_MAX);
      crtc_test->gamma.red[i] = gamma;
      crtc_test->gamma.green[i] = gamma;
      crtc_test->gamma.blue[i] = gamma;
    }

  crtc_test->handles_transforms = TRUE;
}

void
meta_crtc_test_disable_gamma_lut (MetaCrtcTest *crtc_test)
{
  crtc_test->gamma.size = 0;
  g_clear_pointer (&crtc_test->gamma.red, g_free);
  g_clear_pointer (&crtc_test->gamma.green, g_free);
  g_clear_pointer (&crtc_test->gamma.blue, g_free);
}

void
meta_crtc_test_set_is_transform_handled (MetaCrtcTest *crtc_test,
                                         gboolean      handles_transforms)
{
  crtc_test->handles_transforms = handles_transforms;
}
