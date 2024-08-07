/*
 * Copyright (C) 2020 Red Hat Inc.
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

#include "tests/monitor-transform-tests.h"

#include "mtk/mtk.h"

static void
test_transform (void)
{
  const struct
  {
    MtkMonitorTransform transform;
    MtkMonitorTransform other;
    MtkMonitorTransform expect;
  } tests[] = {
    {
      .transform = MTK_MONITOR_TRANSFORM_NORMAL,
      .other = MTK_MONITOR_TRANSFORM_90,
      .expect = MTK_MONITOR_TRANSFORM_90,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_NORMAL,
      .other = MTK_MONITOR_TRANSFORM_FLIPPED_90,
      .expect = MTK_MONITOR_TRANSFORM_FLIPPED_90,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_90,
      .other = MTK_MONITOR_TRANSFORM_90,
      .expect = MTK_MONITOR_TRANSFORM_180,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED_90,
      .other = MTK_MONITOR_TRANSFORM_90,
      .expect = MTK_MONITOR_TRANSFORM_FLIPPED_180,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED_90,
      .other = MTK_MONITOR_TRANSFORM_180,
      .expect = MTK_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED_180,
      .other = MTK_MONITOR_TRANSFORM_FLIPPED_180,
      .expect = MTK_MONITOR_TRANSFORM_NORMAL,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_NORMAL,
      .other = mtk_monitor_transform_invert (MTK_MONITOR_TRANSFORM_90),
      .expect = MTK_MONITOR_TRANSFORM_270,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED,
      .other = mtk_monitor_transform_invert (MTK_MONITOR_TRANSFORM_90),
      .expect = MTK_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED_180,
      .other = mtk_monitor_transform_invert (MTK_MONITOR_TRANSFORM_270),
      .expect = MTK_MONITOR_TRANSFORM_FLIPPED_270,
    },
    {
      .transform = MTK_MONITOR_TRANSFORM_FLIPPED_180,
      .other =
        mtk_monitor_transform_invert (MTK_MONITOR_TRANSFORM_FLIPPED_180),
      .expect = MTK_MONITOR_TRANSFORM_NORMAL,
    },
  };
  int i;
  MtkMonitorTransform transform;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      MtkMonitorTransform result;

      result = mtk_monitor_transform_transform (tests[i].transform,
                                                tests[i].other);
      g_assert_cmpint (result, ==, tests[i].expect);
    }

  for (transform = 0; transform <= MTK_MONITOR_TRANSFORM_FLIPPED_270; transform++)
    {
      MtkMonitorTransform other;
      MtkMonitorTransform result1;

      result1 =
        mtk_monitor_transform_transform (transform,
                                         mtk_monitor_transform_invert (transform));
      g_assert_cmpint (result1, ==, MTK_MONITOR_TRANSFORM_NORMAL);

      for (other = 0; other <= MTK_MONITOR_TRANSFORM_FLIPPED_270; other++)
        {
          MtkMonitorTransform result2;

          result1 = mtk_monitor_transform_transform (transform, other);
          result2 =
            mtk_monitor_transform_transform (result1,
                                             mtk_monitor_transform_invert (other));
          g_assert_cmpint (result2, ==, transform);

          result1 =
            mtk_monitor_transform_transform (mtk_monitor_transform_invert (transform),
                                             other);
          result2 = mtk_monitor_transform_transform (transform, result1);
          g_assert_cmpint (result2, ==, other);
        }
    }
}

void
init_monitor_transform_tests (void)
{
  g_test_add_func ("/util/monitor-transform/transform", test_transform);
}
