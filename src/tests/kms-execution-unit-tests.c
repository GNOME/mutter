/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "backends/native/meta-drm-castkms.h"
#include "backends/native/meta-kms-execution.h"

static void
test_host_description (void)
{
  struct drm_castkms_execution description = {
    .version = DRM_CASTKMS_EXECUTION_VERSION,
    .profile = DRM_CASTKMS_EXECUTION_HOST_V1,
    .generation = 17,
  };
  unsigned char unaligned[sizeof (description) + 1];
  MetaKmsExecution execution;

  memcpy (unaligned + 1, &description, sizeof (description));
  execution = meta_kms_execution_parse (unaligned + 1, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_HOST);
  g_assert_cmpuint (execution.generation, ==, 17);
}

static void
test_gpu_description (void)
{
  struct drm_castkms_execution description = {
    .version = DRM_CASTKMS_EXECUTION_VERSION,
    .profile = DRM_CASTKMS_EXECUTION_GPU_V1,
    .generation = 19,
  };
  MetaKmsExecution execution;

  execution = meta_kms_execution_parse (&description, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_GPU);
  g_assert_cmpuint (execution.generation, ==, 19);
}

static void
test_invalid_description (void)
{
  struct drm_castkms_execution description = {
    .version = DRM_CASTKMS_EXECUTION_VERSION,
    .profile = DRM_CASTKMS_EXECUTION_HOST_V1,
    .generation = 1,
  };
  MetaKmsExecution execution;

  for (size_t size = 0; size < sizeof (description); size++)
    {
      execution = meta_kms_execution_parse (&description, size);
      g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
    }
  execution = meta_kms_execution_parse (NULL, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
  execution = meta_kms_execution_parse (&description, sizeof (description) + 1);
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
  description.generation = 0;
  execution = meta_kms_execution_parse (&description, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
}

static void
test_unknown_description (void)
{
  struct drm_castkms_execution description = {
    .version = DRM_CASTKMS_EXECUTION_VERSION,
    .profile = 0xffffffff,
    .generation = 23,
  };
  MetaKmsExecution execution;

  execution = meta_kms_execution_parse (&description, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
  g_assert_cmpuint (execution.generation, ==, 23);
  description.profile = DRM_CASTKMS_EXECUTION_HOST_V1;
  description.version++;
  execution = meta_kms_execution_parse (&description, sizeof (description));
  g_assert_cmpint (execution.kind, ==, META_KMS_EXECUTION_UNSUPPORTED);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/backends/native/kms/execution/host", test_host_description);
  g_test_add_func ("/backends/native/kms/execution/gpu", test_gpu_description);
  g_test_add_func ("/backends/native/kms/execution/invalid", test_invalid_description);
  g_test_add_func ("/backends/native/kms/execution/unknown", test_unknown_description);
  return g_test_run ();
}
