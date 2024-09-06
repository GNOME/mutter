/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-thread-private.h"
#include "meta-test/meta-context-test.h"

static MetaContext *test_context;

static gpointer
assert_thread_equal (MetaThreadImpl  *thread_impl,
                     gpointer         user_data,
                     GError         **error)
{
  GThread *thread = user_data;

  g_assert_true (thread == g_thread_self ());

  return NULL;
}

static void
meta_test_kms_force_user_thread_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));

  g_assert_cmpint (meta_thread_get_thread_type (META_THREAD (kms)), ==,
                   META_THREAD_TYPE_USER);
  meta_thread_run_impl_task_sync (META_THREAD (kms),
                                  assert_thread_equal, g_thread_self (),
                                  NULL);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/force-user-thread-sanity",
                   meta_test_kms_force_user_thread_sanity);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
