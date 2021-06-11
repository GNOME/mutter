/*
 * Copyright (C) 2018-2020 Red Hat
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

#ifndef META_THREAD_IMPL_H
#define META_THREAD_IMPL_H

#include <glib-object.h>

#include "backends/native/meta-thread.h"
#include "core/util-private.h"

typedef struct _MetaThread MetaThread;

#define META_TYPE_THREAD_IMPL (meta_thread_impl_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaThreadImpl, meta_thread_impl,
                          META, THREAD_IMPL, GObject)

struct _MetaThreadImplClass
{
  GObjectClass parent_class;
};

typedef enum _MetaThreadTaskFeedbackType
{
  META_THREAD_TASK_FEEDBACK_TYPE_CALLBACK,
  META_THREAD_TASK_FEEDBACK_TYPE_IMPL,
} MetaThreadTaskFeedbackType;

typedef struct _MetaThreadTask MetaThreadTask;

META_EXPORT_TEST
MetaThread * meta_thread_impl_get_thread (MetaThreadImpl *thread_impl);

GMainContext * meta_thread_impl_get_main_context (MetaThreadImpl *thread_impl);

META_EXPORT_TEST
GSource * meta_thread_impl_add_source (MetaThreadImpl *thread_impl,
                                       GSourceFunc     func,
                                       gpointer        user_data,
                                       GDestroyNotify  user_data_destroy);

META_EXPORT_TEST
GSource * meta_thread_impl_register_fd (MetaThreadImpl     *thread_impl,
                                        int                 fd,
                                        MetaThreadTaskFunc  dispatch,
                                        gpointer            user_data);

void meta_thread_impl_queue_task (MetaThreadImpl *thread_impl,
                                  MetaThreadTask *task);

int meta_thread_impl_dispatch (MetaThreadImpl *thread_impl);

gboolean meta_thread_impl_is_in_impl (MetaThreadImpl *thread_impl);

MetaThreadTask * meta_thread_task_new (MetaThreadTaskFunc         func,
                                       gpointer                   user_data,
                                       MetaThreadTaskFeedbackFunc feedback_func,
                                       gpointer                   feedback_user_data,
                                       MetaThreadTaskFeedbackType feedback_type);

void meta_thread_task_free (MetaThreadTask *task);

#endif /* META_THREAD_IMPL_H */
