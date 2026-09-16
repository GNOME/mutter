/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include "backends/native/meta-kms-execution.h"

#include <string.h>

#include "backends/native/meta-drm-castkms.h"

MetaKmsExecution
meta_kms_execution_parse (const void *data,
                          size_t      size)
{
  MetaKmsExecution result = { .kind = META_KMS_EXECUTION_UNSUPPORTED };
  struct drm_castkms_execution description;

  if (!data || size != sizeof (description))
    return result;

  memcpy (&description, data, sizeof (description));
  if (description.version != DRM_CASTKMS_EXECUTION_VERSION ||
      description.generation == 0)
    return result;

  result.generation = description.generation;
  if (description.profile == DRM_CASTKMS_EXECUTION_HOST_V1)
    result.kind = META_KMS_EXECUTION_HOST;
  else if (description.profile == DRM_CASTKMS_EXECUTION_GPU_V1)
    result.kind = META_KMS_EXECUTION_GPU;

  return result;
}
