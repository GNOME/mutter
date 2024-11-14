/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#pragma once

#include <meta/common.h>
#include <meta/meta-context.h>

typedef enum _MetaContextTestType
{
  META_CONTEXT_TEST_TYPE_HEADLESS,
  META_CONTEXT_TEST_TYPE_VKMS,
  META_CONTEXT_TEST_TYPE_TEST,
} MetaContextTestType;

typedef enum _MetaContextTestFlag
{
  META_CONTEXT_TEST_FLAG_NONE = 0,
  META_CONTEXT_TEST_FLAG_TEST_CLIENT = 1 << 0,
  META_CONTEXT_TEST_FLAG_NO_X11 = 1 << 1,
  META_CONTEXT_TEST_FLAG_NO_ANIMATIONS = 1 << 2,
} MetaContextTestFlag;

typedef enum _MetaTestRunFlags
{
  META_TEST_RUN_FLAG_NONE = 0,
  META_TEST_RUN_FLAG_CAN_SKIP = 1 << 0,
} MetaTestRunFlags;

#define META_TYPE_CONTEXT_TEST (meta_context_test_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaContextTest, meta_context_test,
                          META, CONTEXT_TEST,
                          MetaContext)

META_EXPORT
MetaContext * meta_create_test_context (MetaContextTestType type,
                                        MetaContextTestFlag flags);

META_EXPORT
int meta_context_test_run_tests (MetaContextTest  *context_test,
                                 MetaTestRunFlags  flags);

META_EXPORT
void meta_context_test_wait_for_x11_display (MetaContextTest *context_test);

META_EXPORT
void meta_context_test_set_background_color (MetaContextTest *context_test,
                                             CoglColor        color);
