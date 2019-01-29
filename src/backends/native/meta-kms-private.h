/*
 * Copyright (C) 2019 Red Hat
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

#ifndef META_KMS_PRIVATE_H
#define META_KMS_PRIVATE_H

#include "backends/native/meta-kms.h"

#include "backends/native/meta-kms-types.h"

typedef gboolean (* MetaKmsImplTaskFunc) (MetaKmsImpl  *impl,
                                          gpointer      user_data,
                                          GError      **error);

gboolean meta_kms_run_impl_task_sync (MetaKms              *kms,
                                      MetaKmsImplTaskFunc   func,
                                      gpointer              user_data,
                                      GError              **error);

gboolean meta_kms_in_impl_task (MetaKms *kms);

#define meta_assert_in_kms_impl(kms) \
  g_assert (meta_kms_in_impl_task (kms))
#define meta_assert_not_in_kms_impl(kms) \
  g_assert (!meta_kms_in_impl_task (kms))

#endif /* META_KMS_PRIVATE_H */
