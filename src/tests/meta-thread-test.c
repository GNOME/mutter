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

#include "config.h"

#include "tests/meta-thread-test.h"

#include "backends/native/meta-thread-private.h"
#include "tests/meta-thread-impl-test.h"

struct _MetaThreadTest
{
  MetaThread parent;
};

G_DEFINE_TYPE (MetaThreadTest, meta_thread_test,
               META_TYPE_THREAD)

static void
meta_thread_test_init (MetaThreadTest *thread_test)
{
}

static void
meta_thread_test_class_init (MetaThreadTestClass *klass)
{
  MetaThreadClass *thread_class = META_THREAD_CLASS (klass);

  meta_thread_class_register_impl_type (thread_class, META_TYPE_THREAD_IMPL_TEST);
}
