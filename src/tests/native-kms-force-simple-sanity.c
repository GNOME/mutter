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
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device-simple.h"
#include "meta-test/meta-context-test.h"

static MetaContext *test_context;

static void
meta_test_kms_force_simple_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  GList *l;

  g_assert_nonnull (meta_kms_get_devices (kms));

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *device = META_KMS_DEVICE (l->data);
      MetaKmsImplDevice *impl_device;

      impl_device = meta_kms_device_get_impl_device (device);
      g_assert_true (META_IS_KMS_IMPL_DEVICE_SIMPLE (impl_device));
    }
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/force-simple-sanity",
                   meta_test_kms_force_simple_sanity);
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
