/*
 * Copyright (C) 2018-2021 Red Hat
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

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

typedef struct _MetaThreadImpl MetaThreadImpl;

typedef enum _MetaThreadType
{
  META_THREAD_TYPE_KERNEL,
  META_THREAD_TYPE_USER,
} MetaThreadType;

#define META_TYPE_THREAD (meta_thread_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaThread, meta_thread,
                          META, THREAD,
                          GObject)

struct _MetaThreadClass
{
  GObjectClass parent_class;
};

typedef void (* MetaThreadCallback) (MetaThread *thread,
                                     gpointer    user_data);

typedef gpointer (* MetaThreadTaskFunc) (MetaThreadImpl  *thread_impl,
                                         gpointer         user_data,
                                         GError         **error);
typedef void (* MetaThreadTaskFeedbackFunc) (gpointer      retval,
                                             const GError *error,
                                             gpointer      user_data);

META_EXPORT_TEST
void meta_thread_reset_thread_type (MetaThread     *thread,
                                    MetaThreadType  thread_type);

META_EXPORT_TEST
void meta_thread_register_callback_context (MetaThread   *thread,
                                            GMainContext *main_context);

META_EXPORT_TEST
void meta_thread_unregister_callback_context (MetaThread   *thread,
                                              GMainContext *main_context);

META_EXPORT_TEST
void meta_thread_queue_callback (MetaThread         *thread,
                                 GMainContext       *main_context,
                                 MetaThreadCallback  callback,
                                 gpointer            user_data,
                                 GDestroyNotify      user_data_destroy);

META_EXPORT_TEST
void meta_thread_flush_callbacks (MetaThread *thread);

META_EXPORT_TEST
gpointer meta_thread_run_impl_task_sync (MetaThread          *thread,
                                         MetaThreadTaskFunc   func,
                                         gpointer             user_data,
                                         GError             **error);

META_EXPORT_TEST
void meta_thread_post_impl_task (MetaThread                 *thread,
                                 MetaThreadTaskFunc          func,
                                 gpointer                    user_data,
                                 GDestroyNotify              user_data_destroy,
                                 MetaThreadTaskFeedbackFunc  feedback_func,
                                 gpointer                    feedback_user_data);

META_EXPORT_TEST
MetaBackend * meta_thread_get_backend (MetaThread *thread);

META_EXPORT_TEST
const char * meta_thread_get_name (MetaThread *thread);

META_EXPORT_TEST
gboolean meta_thread_is_in_impl_task (MetaThread *thread);

gboolean meta_thread_is_waiting_for_impl_task (MetaThread *thread);

void meta_thread_inhibit_realtime_in_impl (MetaThread *thread);
void meta_thread_uninhibit_realtime_in_impl (MetaThread *thread);

#define meta_assert_in_thread_impl(thread) \
  g_assert (meta_thread_is_in_impl_task (thread))
#define meta_assert_not_in_thread_impl(thread) \
  g_assert (!meta_thread_is_in_impl_task (thread))
#define meta_assert_is_waiting_for_thread_impl_task(thread) \
  g_assert (meta_thread_is_waiting_for_impl_task (thread))
