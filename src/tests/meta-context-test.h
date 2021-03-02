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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_CONTEXT_TEST_H
#define META_CONTEXT_TEST_H

#include "core/meta-context-private.h"

typedef enum _MetaContextTestType
{
#ifdef HAVE_NATIVE_BACKEND
  META_CONTEXT_TEST_TYPE_HEADLESS,
#endif
  META_CONTEXT_TEST_TYPE_NESTED,
} MetaContextTestType;

#define META_TYPE_CONTEXT_TEST (meta_context_test_get_type ())
G_DECLARE_FINAL_TYPE (MetaContextTest, meta_context_test,
                      META, CONTEXT_TEST,
                      MetaContext)

MetaContext * meta_create_test_context (MetaContextTestType type);

int meta_context_test_run_tests (MetaContextTest *context_test);

#endif /* META_CONTEXT_TEST_H */
