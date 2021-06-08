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

#include "config.h"

#include "backends/native/meta-thread-impl.h"

#include <glib-object.h>

#include "backends/native/meta-thread.h"

enum
{
  PROP_0,

  PROP_THREAD,
  PROP_MAIN_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaThreadImplSource
{
  GSource base;
  MetaThreadImpl *thread_impl;
} MetaThreadImplSource;

typedef struct _MetaThreadImplPrivate
{
  MetaThread *thread;

  gboolean in_impl_task;

  GMainContext *thread_context;
  GSource *impl_source;
  GAsyncQueue *task_queue;
} MetaThreadImplPrivate;

struct _MetaThreadTask
{
  MetaThreadTaskFunc func;
  gpointer user_data;

  MetaThreadTaskFeedbackFunc feedback_func;
  gpointer feedback_user_data;
  MetaThreadTaskFeedbackType feedback_type;

  gpointer retval;
  GError *error;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaThreadImpl, meta_thread_impl, G_TYPE_OBJECT)

static void
meta_thread_impl_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  switch (prop_id)
    {
    case PROP_THREAD:
      g_value_set_object (value, priv->thread);
      break;
    case PROP_MAIN_CONTEXT:
      g_value_set_boxed (value, priv->thread_context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_thread_impl_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  switch (prop_id)
    {
    case PROP_THREAD:
      priv->thread = g_value_get_object (value);
      break;
    case PROP_MAIN_CONTEXT:
      priv->thread_context = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
impl_source_prepare (GSource *source,
                     int     *timeout)
{
  MetaThreadImplSource *impl_source = (MetaThreadImplSource *) source;
  MetaThreadImpl *thread_impl = impl_source->thread_impl;
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  g_assert (g_source_get_context (source) == priv->thread_context);

  *timeout = -1;

  return g_async_queue_length (priv->task_queue) > 0;
}

static gboolean
impl_source_check (GSource *source)
{
  MetaThreadImplSource *impl_source = (MetaThreadImplSource *) source;
  MetaThreadImpl *thread_impl = impl_source->thread_impl;
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  g_assert (g_source_get_context (source) == priv->thread_context);

  return g_async_queue_length (priv->task_queue) > 0;
}

static gboolean
impl_source_dispatch (GSource     *source,
                      GSourceFunc  callback,
                      gpointer     user_data)
{
  MetaThreadImplSource *impl_source = (MetaThreadImplSource *) source;
  MetaThreadImpl *thread_impl = impl_source->thread_impl;
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  g_assert (g_source_get_context (source) == priv->thread_context);

  meta_thread_impl_dispatch (thread_impl);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs impl_source_funcs = {
  .prepare = impl_source_prepare,
  .check = impl_source_check,
  .dispatch = impl_source_dispatch,
};

static void
meta_thread_impl_constructed (GObject *object)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  GSource *source;
  MetaThreadImplSource *impl_source;

  source = g_source_new (&impl_source_funcs, sizeof (MetaThreadImplSource));
  impl_source = (MetaThreadImplSource *) source;
  impl_source->thread_impl = thread_impl;
  g_source_attach (source, priv->thread_context);
  g_source_unref (source);

  priv->impl_source = source;

  priv->task_queue = g_async_queue_new ();

  G_OBJECT_CLASS (meta_thread_impl_parent_class)->constructed (object);
}

static void
meta_thread_impl_finalize (GObject *object)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  g_clear_pointer (&priv->impl_source, g_source_destroy);
  g_clear_pointer (&priv->task_queue, g_async_queue_unref);
  g_clear_pointer (&priv->thread_context, g_main_context_unref);

  G_OBJECT_CLASS (meta_thread_impl_parent_class)->finalize (object);
}

static void
meta_thread_impl_class_init (MetaThreadImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_thread_impl_get_property;
  object_class->set_property = meta_thread_impl_set_property;
  object_class->constructed = meta_thread_impl_constructed;
  object_class->finalize = meta_thread_impl_finalize;

  obj_props[PROP_THREAD] =
    g_param_spec_object ("thread",
                         "thread",
                         "MetaThread",
                         META_TYPE_THREAD,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MAIN_CONTEXT] =
    g_param_spec_boxed ("main-context",
                        "main-context",
                        "GMainContext",
                        G_TYPE_MAIN_CONTEXT,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_thread_impl_init (MetaThreadImpl *thread_impl)
{
}

MetaThread *
meta_thread_impl_get_thread (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  return priv->thread;
}

GMainContext *
meta_thread_impl_get_main_context (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  return priv->thread_context;
}

MetaThreadTask *
meta_thread_task_new (MetaThreadTaskFunc         func,
                      gpointer                   user_data,
                      MetaThreadTaskFeedbackFunc feedback_func,
                      gpointer                   feedback_user_data,
                      MetaThreadTaskFeedbackType feedback_type)
{
  MetaThreadTask *task;

  task = g_new0 (MetaThreadTask, 1);
  *task = (MetaThreadTask) {
    .func = func,
    .user_data = user_data,
    .feedback_func = feedback_func,
    .feedback_user_data = feedback_user_data,
    .feedback_type = feedback_type,
  };

  return task;
}

void
meta_thread_task_free (MetaThreadTask *task)
{
  g_clear_error (&task->error);
  g_free (task);
}

typedef struct _MetaThreadImplIdleSource
{
  GSource source;
  MetaThreadImpl *thread_impl;
} MetaThreadImplIdleSource;

static gboolean
impl_idle_source_dispatch (GSource     *source,
                           GSourceFunc  callback,
                           gpointer     user_data)
{
  MetaThreadImplIdleSource *impl_idle_source =
    (MetaThreadImplIdleSource *) source;
  MetaThreadImpl *thread_impl = impl_idle_source->thread_impl;
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  gboolean ret;

  priv->in_impl_task = TRUE;
  ret = callback (user_data);
  priv->in_impl_task = FALSE;

  return ret;
}

static GSourceFuncs impl_idle_source_funcs = {
  .dispatch = impl_idle_source_dispatch,
};

GSource *
meta_thread_impl_add_source (MetaThreadImpl *thread_impl,
                             GSourceFunc     func,
                             gpointer        user_data,
                             GDestroyNotify  user_data_destroy)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  GSource *source;
  MetaThreadImplIdleSource *impl_idle_source;

  meta_assert_in_thread_impl (priv->thread);

  source = g_source_new (&impl_idle_source_funcs,
                         sizeof (MetaThreadImplIdleSource));
  g_source_set_name (source, "[mutter] MetaThreadImpl idle source");
  impl_idle_source = (MetaThreadImplIdleSource *) source;
  impl_idle_source->thread_impl = thread_impl;

  g_source_set_callback (source, func, user_data, user_data_destroy);
  g_source_set_ready_time (source, 0);
  g_source_attach (source, priv->thread_context);

  return source;
}

typedef struct _MetaThreadImplFdSource
{
  GSource source;

  gpointer fd_tag;
  MetaThreadImpl *thread_impl;

  MetaThreadTaskFunc dispatch;
  gpointer user_data;
} MetaThreadImplFdSource;

static gboolean
meta_thread_impl_fd_source_check (GSource *source)
{
  MetaThreadImplFdSource *impl_fd_source = (MetaThreadImplFdSource *) source;

  return g_source_query_unix_fd (source, impl_fd_source->fd_tag) & G_IO_IN;
}

static gpointer
dispatch_task_func (MetaThreadImpl      *thread_impl,
                    MetaThreadTaskFunc   dispatch,
                    gpointer             user_data,
                    GError             **error)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  gpointer retval = NULL;

  priv->in_impl_task = TRUE;
  retval = dispatch (thread_impl, user_data, error);
  priv->in_impl_task = FALSE;

  return retval;
}

static gboolean
meta_thread_impl_fd_source_dispatch (GSource     *source,
                                     GSourceFunc  callback,
                                     gpointer     user_data)
{
  MetaThreadImplFdSource *impl_fd_source = (MetaThreadImplFdSource *) source;
  MetaThreadImpl *thread_impl = impl_fd_source->thread_impl;
  gpointer retval;
  GError *error = NULL;

  retval = dispatch_task_func (thread_impl,
                               impl_fd_source->dispatch,
                               impl_fd_source->user_data,
                               &error);

  if (!GPOINTER_TO_INT (retval))
    {
      g_warning ("Failed to dispatch fd source: %s", error->message);
      g_error_free (error);
    }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs impl_fd_source_funcs = {
  NULL,
  meta_thread_impl_fd_source_check,
  meta_thread_impl_fd_source_dispatch
};

GSource *
meta_thread_impl_register_fd (MetaThreadImpl     *thread_impl,
                              int                 fd,
                              MetaThreadTaskFunc  dispatch,
                              gpointer            user_data)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  GSource *source;
  MetaThreadImplFdSource *impl_fd_source;

  meta_assert_in_thread_impl (priv->thread);

  source = g_source_new (&impl_fd_source_funcs,
                         sizeof (MetaThreadImplFdSource));
  g_source_set_name (source, "[mutter] MetaThreadImpl fd source");
  impl_fd_source = (MetaThreadImplFdSource *) source;
  impl_fd_source->dispatch = dispatch;
  impl_fd_source->user_data = user_data;
  impl_fd_source->thread_impl = thread_impl;
  impl_fd_source->fd_tag = g_source_add_unix_fd (source, fd,
                                                 G_IO_IN | G_IO_ERR);

  g_source_attach (source, priv->thread_context);

  return source;
}

gboolean
meta_thread_impl_is_in_impl (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  return priv->in_impl_task;
}

static void
invoke_task_feedback (MetaThread *thread,
                      gpointer    user_data)
{
  MetaThreadTask *task = user_data;

  meta_assert_not_in_thread_impl (thread);

  task->feedback_func (task->retval, task->error, task->feedback_user_data);
}

void
meta_thread_impl_dispatch (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);
  MetaThreadTask *task;
  gpointer retval;
  g_autoptr (GError) error = NULL;

  task = g_async_queue_pop (priv->task_queue);

  priv->in_impl_task = TRUE;
  retval = task->func (thread_impl, task->user_data, &error);

  if (task->feedback_func)
    {
      switch (task->feedback_type)
        {
        case META_THREAD_TASK_FEEDBACK_TYPE_IMPL:
          task->feedback_func (retval, error, task->feedback_user_data);
          break;
        case META_THREAD_TASK_FEEDBACK_TYPE_CALLBACK:
          task->retval = retval;
          task->error = g_steal_pointer (&error);
          meta_thread_queue_callback (priv->thread,
                                      invoke_task_feedback,
                                      g_steal_pointer (&task),
                                      (GDestroyNotify) meta_thread_task_free);
          break;
        }
    }

  g_clear_pointer (&task, meta_thread_task_free);

  priv->in_impl_task = FALSE;
}

void
meta_thread_impl_queue_task (MetaThreadImpl *thread_impl,
                             MetaThreadTask *task)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  g_async_queue_push (priv->task_queue, task);
  g_main_context_wakeup (priv->thread_context);
}
