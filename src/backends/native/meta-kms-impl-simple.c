/*
 * Copyright (C) 2018-2019 Red Hat
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
 */

#include "config.h"

#include "backends/native/meta-kms-impl-simple.h"

#include <gbm.h>

struct _MetaKmsImplSimple
{
  MetaKmsImpl parent;
};

G_DEFINE_TYPE (MetaKmsImplSimple, meta_kms_impl_simple,
               META_TYPE_KMS_IMPL)

MetaKmsImplSimple *
meta_kms_impl_simple_new (MetaKms  *kms,
                          GError  **error)
{
  return g_object_new (META_TYPE_KMS_IMPL_SIMPLE,
                       "kms", kms,
                       NULL);
}

static void
meta_kms_impl_simple_init (MetaKmsImplSimple *impl_simple)
{
}

static void
meta_kms_impl_simple_class_init (MetaKmsImplSimpleClass *klass)
{
}
