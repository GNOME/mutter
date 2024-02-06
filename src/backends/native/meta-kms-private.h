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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "backends/native/meta-kms.h"

#include <gudev/gudev.h>

#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-types-private.h"

void meta_kms_queue_callback (MetaKms            *kms,
                              GMainContext       *main_context,
                              MetaThreadCallback  callback,
                              gpointer            user_data,
                              GDestroyNotify      user_data_destroy);

void meta_kms_queue_result_callback (MetaKms               *kms,
                                     MetaKmsResultListener *listener);

gpointer meta_kms_run_impl_task_sync (MetaKms             *kms,
                                      MetaThreadTaskFunc   func,
                                      gpointer             user_data,
                                      GError             **error);

META_EXPORT_TEST
MetaKmsResourceChanges meta_kms_update_states_sync (MetaKms     *kms,
                                                    GUdevDevice *udev_device);

gboolean meta_kms_in_impl_task (MetaKms *kms);

gboolean meta_kms_is_waiting_for_impl_task (MetaKms *kms);

void meta_kms_emit_resources_changed (MetaKms                *kms,
                                      MetaKmsResourceChanges  changes);

#define meta_assert_in_kms_impl(kms) \
  g_assert (meta_kms_in_impl_task (kms))
#define meta_assert_not_in_kms_impl(kms) \
  g_assert (!meta_kms_in_impl_task (kms))
#define meta_assert_is_waiting_for_kms_impl_task(kms) \
  g_assert (meta_kms_is_waiting_for_impl_task (kms))
