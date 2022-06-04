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

#include "config.h"

#include "backends/native/meta-thread-private.h"

#include <glib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-backend-types.h"
#include "backends/native/meta-thread-impl.h"

#include "meta-private-enum-types.h"

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_NAME,
  PROP_THREAD_TYPE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaThreadCallbackData
{
  MetaThreadCallback callback;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
} MetaThreadCallbackData;

typedef struct _MetaThreadCallbackSource
{
  GSource base;

  GMutex mutex;
  GCond cond;

  MetaThread *thread;
  GMainContext *main_context;
  GList *callbacks;
  gboolean needs_flush;
} MetaThreadCallbackSource;

typedef struct _MetaThreadPrivate
{
  MetaBackend *backend;
  char *name;

  GMainContext *main_context;

  MetaThreadImpl *impl;
  gboolean waiting_for_impl_task;

  GMutex callbacks_mutex;
  GHashTable *callback_sources;

  MetaThreadType thread_type;

  GThread *main_thread;

  struct {
    GThread *thread;
    GMutex init_mutex;
  } kernel;
} MetaThreadPrivate;

typedef struct _MetaThreadClassPrivate
{
  GType impl_type;
} MetaThreadClassPrivate;

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaThread, meta_thread, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaThread)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init)
                         g_type_add_class_private (g_define_type_id,
                                                   sizeof (MetaThreadClassPrivate)))

static void
meta_thread_callback_data_free (MetaThreadCallbackData *callback_data)
{
  if (callback_data->user_data_destroy)
    callback_data->user_data_destroy (callback_data->user_data);
  g_free (callback_data);
}

static void
meta_thread_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_THREAD_TYPE:
      g_value_set_enum (value, priv->thread_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_thread_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_THREAD_TYPE:
      priv->thread_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gpointer
thread_impl_func (gpointer user_data)
{
  MetaThread *thread = META_THREAD (user_data);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_mutex_lock (&priv->kernel.init_mutex);
  g_mutex_unlock (&priv->kernel.init_mutex);

  meta_thread_impl_run (priv->impl);

  return GINT_TO_POINTER (TRUE);
}

static gboolean
meta_thread_initable_init (GInitable     *initable,
                           GCancellable  *cancellable,
                           GError       **error)
{
  MetaThread *thread = META_THREAD (initable);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadClass *thread_class = META_THREAD_GET_CLASS (thread);
  MetaThreadClassPrivate *class_priv =
    G_TYPE_CLASS_GET_PRIVATE (thread_class, META_TYPE_THREAD,
                              MetaThreadClassPrivate);
  g_autoptr (GMainContext) thread_context = NULL;

  priv->main_context = g_main_context_default ();

  priv->callback_sources =
    g_hash_table_new_full (NULL, NULL,
                           NULL, (GDestroyNotify) g_source_destroy);
  meta_thread_register_callback_context (thread, priv->main_context);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      thread_context = g_main_context_ref (priv->main_context);
      break;
    case META_THREAD_TYPE_KERNEL:
      thread_context = g_main_context_new ();
      break;
    }

  g_assert (g_type_is_a (class_priv->impl_type, META_TYPE_THREAD_IMPL));
  priv->impl = g_object_new (class_priv->impl_type,
                             "thread", thread,
                             "main-context", thread_context,
                             NULL);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      break;
    case META_THREAD_TYPE_KERNEL:
      g_mutex_init (&priv->kernel.init_mutex);
      g_mutex_lock (&priv->kernel.init_mutex);
      priv->kernel.thread = g_thread_new (priv->name,
                                          thread_impl_func,
                                          thread);
      g_mutex_unlock (&priv->kernel.init_mutex);
      break;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_thread_initable_init;
}

static void
finalize_thread_user (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  while (meta_thread_impl_dispatch (priv->impl) > 0);
}

static void
finalize_thread_kernel (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  meta_thread_impl_terminate (priv->impl);
  g_thread_join (priv->kernel.thread);
  priv->kernel.thread = NULL;
  g_mutex_clear (&priv->kernel.init_mutex);
}


static void
meta_thread_finalize (GObject *object)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      finalize_thread_user (thread);
      break;
    case META_THREAD_TYPE_KERNEL:
      finalize_thread_kernel (thread);
      break;
    }

  meta_thread_flush_callbacks (thread);
  meta_thread_unregister_callback_context (thread, priv->main_context);

  g_warn_if_fail (g_hash_table_size (priv->callback_sources) == 0);
  g_clear_object (&priv->impl);
  g_clear_pointer (&priv->name, g_free);

  g_mutex_clear (&priv->callbacks_mutex);

  G_OBJECT_CLASS (meta_thread_parent_class)->finalize (object);
}

static void
meta_thread_class_init (MetaThreadClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_thread_get_property;
  object_class->set_property = meta_thread_set_property;
  object_class->finalize = meta_thread_finalize;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "name",
                         "Name of thread",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_THREAD_TYPE] =
    g_param_spec_enum ("thread-type",
                       "thread-type",
                       "Type of thread",
                       META_TYPE_THREAD_TYPE,
                       META_THREAD_TYPE_KERNEL,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_thread_init (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_mutex_init (&priv->callbacks_mutex);
  priv->main_thread = g_thread_self ();
}

void
meta_thread_class_register_impl_type (MetaThreadClass *thread_class,
                                      GType            impl_type)
{
  MetaThreadClassPrivate *class_priv =
    G_TYPE_CLASS_GET_PRIVATE (thread_class, META_TYPE_THREAD,
                              MetaThreadClassPrivate);

  g_assert (class_priv->impl_type == G_TYPE_INVALID);
  class_priv->impl_type = impl_type;
}

static int
dispatch_callbacks (MetaThread *thread,
                    GList      *pending_callbacks)
{
  int callback_count = 0;
  GList *l;

  for (l = pending_callbacks; l; l = l->next)
    {
      MetaThreadCallbackData *callback_data = l->data;

      callback_data->callback (thread, callback_data->user_data);
      meta_thread_callback_data_free (callback_data);
      callback_count++;
    }

  return callback_count;
}

void
meta_thread_flush_callbacks (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GList) pending_callbacks = NULL;
  MetaThreadCallbackSource *main_callback_source;
  g_autoptr (GList) callback_sources = NULL;
  GList *l;

  g_assert (!g_main_context_get_thread_default ());

  while (TRUE)
    {
      gboolean needs_reflush = FALSE;

      g_mutex_lock (&priv->callbacks_mutex);
      main_callback_source = g_hash_table_lookup (priv->callback_sources,
                                                  priv->main_context);
      pending_callbacks = g_steal_pointer (&main_callback_source->callbacks);
      callback_sources = g_hash_table_get_values (priv->callback_sources);
      g_mutex_unlock (&priv->callbacks_mutex);

      if (dispatch_callbacks (thread, pending_callbacks) > 0)
        needs_reflush = TRUE;

      g_list_foreach (callback_sources, (GFunc) g_source_ref, NULL);
      for (l = callback_sources; l; l = l->next)
        {
          MetaThreadCallbackSource *callback_source = l->data;

          if (callback_source == main_callback_source)
            continue;

          g_mutex_lock (&callback_source->mutex);
          while (callback_source->needs_flush)
            {
              needs_reflush = TRUE;
              g_cond_wait (&callback_source->cond, &callback_source->mutex);
            }
          g_mutex_unlock (&callback_source->mutex);
        }
      g_list_foreach (callback_sources, (GFunc) g_source_unref, NULL);

      if (!needs_reflush)
        break;
    }
}

static gboolean
callback_source_prepare (GSource *source,
                         int     *timeout)
{
  MetaThreadCallbackSource *callback_source =
    (MetaThreadCallbackSource *) source;
  MetaThread *thread = callback_source->thread;
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  gboolean retval;

  *timeout = -1;

  g_mutex_lock (&priv->callbacks_mutex);
  retval = !!callback_source->callbacks;
  g_mutex_unlock (&priv->callbacks_mutex);

  return retval;
}

static gboolean
callback_source_dispatch (GSource     *source,
                          GSourceFunc  callback,
                          gpointer     user_data)
{
  MetaThreadCallbackSource *callback_source =
    (MetaThreadCallbackSource *) source;
  MetaThread *thread = callback_source->thread;
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GList) pending_callbacks = NULL;

  g_mutex_lock (&priv->callbacks_mutex);
  pending_callbacks = g_steal_pointer (&callback_source->callbacks);
  g_mutex_unlock (&priv->callbacks_mutex);

  dispatch_callbacks (thread, pending_callbacks);

  g_mutex_lock (&priv->callbacks_mutex);

  if (callback_source->callbacks)
    {
      g_source_set_ready_time (source, 0);
    }
  else
    {
      g_source_set_ready_time (source, -1);

      g_mutex_lock (&callback_source->mutex);
      callback_source->needs_flush = FALSE;
      g_cond_signal (&callback_source->cond);
      g_mutex_unlock (&callback_source->mutex);
    }

  g_mutex_unlock (&priv->callbacks_mutex);

  return G_SOURCE_CONTINUE;
}

static void
callback_source_finalize (GSource *source)
{
  MetaThreadCallbackSource *callback_source =
    (MetaThreadCallbackSource *) source;

  g_list_free_full (callback_source->callbacks,
                    (GDestroyNotify) meta_thread_callback_data_free);

  g_cond_clear (&callback_source->cond);
  g_mutex_clear (&callback_source->mutex);
}

static GSourceFuncs callback_source_funcs = {
  .prepare = callback_source_prepare,
  .dispatch = callback_source_dispatch,
  .finalize = callback_source_finalize,
};

void
meta_thread_register_callback_context (MetaThread   *thread,
                                       GMainContext *main_context)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  GSource *source;
  MetaThreadCallbackSource *callback_source;

  source = g_source_new (&callback_source_funcs,
                         sizeof (MetaThreadCallbackSource));
  callback_source = (MetaThreadCallbackSource *) source;
  g_mutex_init (&callback_source->mutex);
  g_cond_init (&callback_source->cond);
  callback_source->thread = thread;
  callback_source->main_context = main_context;

  g_source_set_ready_time (&callback_source->base, -1);
  g_source_set_priority (source, G_PRIORITY_HIGH + 1);
  g_source_attach (source, main_context);
  g_source_unref (source);

  g_hash_table_insert (priv->callback_sources,
                       main_context,
                       callback_source);
}

void
meta_thread_unregister_callback_context (MetaThread   *thread,
                                         GMainContext *main_context)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_hash_table_remove (priv->callback_sources, main_context);
}

void
meta_thread_queue_callback (MetaThread         *thread,
                            GMainContext       *main_context,
                            MetaThreadCallback  callback,
                            gpointer            user_data,
                            GDestroyNotify      user_data_destroy)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GMutexLocker) locker;
  MetaThreadCallbackSource *callback_source;
  MetaThreadCallbackData *callback_data;

  if (!main_context)
    main_context = g_main_context_default ();

  locker = g_mutex_locker_new (&priv->callbacks_mutex);

  callback_source = g_hash_table_lookup (priv->callback_sources, main_context);
  g_return_if_fail (callback_source);

  callback_data = g_new0 (MetaThreadCallbackData, 1);
  *callback_data = (MetaThreadCallbackData) {
    .callback = callback,
    .user_data = user_data,
    .user_data_destroy = user_data_destroy,
  };

  g_mutex_lock (&callback_source->mutex);
  callback_source->needs_flush = TRUE;
  callback_source->callbacks = g_list_append (callback_source->callbacks,
                                              callback_data);
  g_source_set_ready_time (&callback_source->base, 0);
  g_mutex_unlock (&callback_source->mutex);
}

typedef struct _MetaSyncTaskData
{
  gboolean done;
  GError *error;
  gpointer retval;
  struct {
    GMutex mutex;
    GCond cond;
  } kernel;
} MetaSyncTaskData;

static void
sync_task_done_user_in_impl (gpointer        retval,
                             const GError   *error,
                             gpointer        user_data)
{
  MetaSyncTaskData *data = user_data;

  data->done = TRUE;
  data->retval = retval;
  data->error = error ? g_error_copy (error) : NULL;
}

static gpointer
run_impl_task_sync_user (MetaThread          *thread,
                         MetaThreadTaskFunc   func,
                         gpointer             user_data,
                         GError             **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadTask *task;
  MetaSyncTaskData data = { 0 };

  task = meta_thread_task_new (func, user_data, NULL,
                               sync_task_done_user_in_impl, &data,
                               meta_thread_impl_get_main_context (priv->impl));
  meta_thread_impl_queue_task (priv->impl, task);

  priv->waiting_for_impl_task = TRUE;
  while (!data.done)
    meta_thread_impl_dispatch (priv->impl);
  priv->waiting_for_impl_task = FALSE;

  if (error)
    *error = data.error;
  else
    g_clear_error (&data.error);

  return data.retval;
}

static void
sync_task_done_kernel_in_impl (gpointer      retval,
                               const GError *error,
                               gpointer      user_data)
{
  MetaSyncTaskData *data = user_data;

  g_mutex_lock (&data->kernel.mutex);
  data->done = TRUE;
  data->retval = retval;
  data->error = error ? g_error_copy (error) : NULL;
  g_cond_signal (&data->kernel.cond);
  g_mutex_unlock (&data->kernel.mutex);
}

static gpointer
run_impl_task_sync_kernel (MetaThread          *thread,
                           MetaThreadTaskFunc   func,
                           gpointer             user_data,
                           GError             **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadTask *task;
  MetaSyncTaskData data = { 0 };

  g_mutex_init (&data.kernel.mutex);
  g_cond_init (&data.kernel.cond);

  g_mutex_lock (&data.kernel.mutex);
  priv->waiting_for_impl_task = TRUE;

  task = meta_thread_task_new (func, user_data, NULL,
                               sync_task_done_kernel_in_impl, &data,
                               meta_thread_impl_get_main_context (priv->impl));
  meta_thread_impl_queue_task (priv->impl, task);

  while (!data.done)
    g_cond_wait (&data.kernel.cond, &data.kernel.mutex);
  priv->waiting_for_impl_task = FALSE;
  g_mutex_unlock (&data.kernel.mutex);

  g_mutex_clear (&data.kernel.mutex);
  g_cond_clear (&data.kernel.cond);

  if (error)
    *error = data.error;
  else
    g_clear_error (&data.error);

  return data.retval;
}

gpointer
meta_thread_run_impl_task_sync (MetaThread          *thread,
                                MetaThreadTaskFunc   func,
                                gpointer             user_data,
                                GError             **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      if (priv->main_thread == g_thread_self ())
        return run_impl_task_sync_user (thread, func, user_data, error);
      else
        return run_impl_task_sync_kernel (thread, func, user_data, error);
    case META_THREAD_TYPE_KERNEL:
      return run_impl_task_sync_kernel (thread, func, user_data, error);
    }

  g_assert_not_reached ();
}

void
meta_thread_post_impl_task (MetaThread                 *thread,
                            MetaThreadTaskFunc          func,
                            gpointer                    user_data,
                            GDestroyNotify              user_data_destroy,
                            MetaThreadTaskFeedbackFunc  feedback_func,
                            gpointer                    feedback_user_data)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadTask *task;

  task = meta_thread_task_new (func, user_data, user_data_destroy,
                               feedback_func, feedback_user_data,
                               g_main_context_get_thread_default ());
  meta_thread_impl_queue_task (priv->impl, task);
}

MetaBackend *
meta_thread_get_backend (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->backend;
}

const char *
meta_thread_get_name (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->name;
}

MetaThreadType
meta_thread_get_thread_type (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->thread_type;
}

GThread *
meta_thread_get_thread (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_assert (priv->thread_type == META_THREAD_TYPE_KERNEL);

  return priv->kernel.thread;
}

gboolean
meta_thread_is_in_impl_task (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return meta_thread_impl_is_in_impl (priv->impl);
}

gboolean
meta_thread_is_waiting_for_impl_task (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->waiting_for_impl_task;
}
