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

#include "tests/meta-thread-impl-test.h"

struct _MetaThreadImplTest
{
  MetaThreadImpl parent;
};

G_DEFINE_TYPE (MetaThreadImplTest, meta_thread_impl_test,
               META_TYPE_THREAD_IMPL)

static void
meta_thread_impl_test_init (MetaThreadImplTest *thread_impl_test)
{
}

static void
meta_thread_impl_test_class_init (MetaThreadImplTestClass *klass)
{
}
