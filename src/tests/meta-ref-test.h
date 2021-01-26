/*
 * Copyright (C) 2021 Red Hat Inc.
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

#ifndef META_REF_TEST_H
#define META_REF_TEST_H

#include <glib.h>

#include "clutter/clutter/clutter.h"
#include "meta/boxes.h"

typedef enum _MetaReftestFlag
{
  META_REFTEST_FLAG_NONE = 0,
  META_REFTEST_FLAG_UPDATE_REF = 1 << 0,
} MetaReftestFlag;

void meta_ref_test_verify_view (ClutterStageView *view,
                                const char       *test_name,
                                int               test_seq_no,
                                MetaReftestFlag   flags);

MetaReftestFlag meta_ref_test_determine_ref_test_flag (void);

#endif /* META_REF_TEST_H */
