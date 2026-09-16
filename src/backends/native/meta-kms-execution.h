/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>
#include <stdint.h>

typedef enum
{
  META_KMS_EXECUTION_DEFAULT,
  META_KMS_EXECUTION_HOST,
  META_KMS_EXECUTION_GPU,
  META_KMS_EXECUTION_UNSUPPORTED,
} MetaKmsExecutionKind;

typedef struct
{
  MetaKmsExecutionKind kind;
  uint64_t generation;
} MetaKmsExecution;

MetaKmsExecution meta_kms_execution_parse (const void *data,
                                          size_t      size);
