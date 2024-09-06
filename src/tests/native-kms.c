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
#include "backends/native/meta-kms.h"
#include "meta-test/meta-context-test.h"
//#include "tests/meta-kms-test-utils.h"

static MetaContext *test_context;

static gpointer
assert_not_thread (MetaThreadImpl  *thread_impl,
                   gpointer         user_data,
                   GError         **error)
{
  GThread **thread_to_check = user_data;

  g_assert_true (g_steal_pointer (thread_to_check) != g_thread_self ());

  return NULL;
}

static gpointer
assert_thread (MetaThreadImpl  *thread_impl,
               gpointer         user_data,
               GError         **error)
{
  GThread **thread_to_check = user_data;

  g_assert_true (g_steal_pointer (thread_to_check) == g_thread_self ());

  return NULL;
}

static void
meta_test_kms_inhibit_kernel_thread (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaThread *thread = META_THREAD (kms);
  GThread *main_thread;
  GThread *test_thread;

  main_thread = g_thread_self ();

  test_thread = main_thread;
  meta_thread_post_impl_task (thread, assert_not_thread, &test_thread, NULL,
                              NULL, NULL);

  meta_kms_inhibit_kernel_thread (kms);
  g_assert_null (test_thread);

  test_thread = main_thread;
  meta_thread_post_impl_task (thread, assert_thread, &test_thread, NULL,
                              NULL, NULL);

  meta_kms_uninhibit_kernel_thread (kms);
  g_assert_null (test_thread);

  test_thread = main_thread;
  meta_thread_post_impl_task (thread, assert_not_thread, &test_thread, NULL,
                              NULL, NULL);

  while (test_thread)
    g_main_context_iteration (NULL, TRUE);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/inhibit-kernel-thread",
                   meta_test_kms_inhibit_kernel_thread);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                              META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
