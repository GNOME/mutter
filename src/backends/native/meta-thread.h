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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_THREAD_H
#define META_THREAD_H

#include <glib-object.h>

#include "backends/meta-backend-types.h"

typedef struct _MetaThreadImpl MetaThreadImpl;

#define META_TYPE_THREAD (meta_thread_get_type ())
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

void meta_thread_queue_callback (MetaThread         *thread,
                                 MetaThreadCallback  callback,
                                 gpointer            user_data,
                                 GDestroyNotify      user_data_destroy);

int meta_thread_flush_callbacks (MetaThread *thread);

gpointer meta_thread_run_impl_task_sync (MetaThread          *thread,
                                         MetaThreadTaskFunc   func,
                                         gpointer             user_data,
                                         GError             **error);

GSource * meta_thread_add_source_in_impl (MetaThread     *thread,
                                          GSourceFunc     func,
                                          gpointer        user_data,
                                          GDestroyNotify  user_data_destroy);

GSource * meta_thread_register_fd_in_impl (MetaThread         *thread,
                                           int                 fd,
                                           MetaThreadTaskFunc  dispatch,
                                           gpointer            user_data);

MetaBackend * meta_thread_get_backend (MetaThread *thread);

gboolean meta_thread_is_in_impl_task (MetaThread *thread);

gboolean meta_thread_is_waiting_for_impl_task (MetaThread *thread);

#define meta_assert_in_thread_impl(thread) \
  g_assert (meta_thread_is_in_impl_task (thread))
#define meta_assert_not_in_thread_impl(thread) \
  g_assert (!meta_thread_is_in_impl_task (thread))
#define meta_assert_is_waiting_for_thread_impl_task(thread) \
  g_assert (meta_thread_is_waiting_for_impl_task (thread))

#endif /* META_THREAD_H */
