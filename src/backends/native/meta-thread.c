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

#include "backends/meta-backend-private.h"
#include "backends/meta-backend-types.h"
#include "backends/native/meta-thread-impl.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaThreadCallbackData
{
  MetaThreadCallback callback;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
} MetaThreadCallbackData;

typedef struct _MetaThreadSimpleImplSource
{
  GSource source;
  MetaThread *thread;
} MetaThreadSimpleImplSource;

typedef struct _MetaThreadFdImplSource
{
  GSource source;

  gpointer fd_tag;
  MetaThread *thread;

  MetaThreadTaskFunc dispatch;
  gpointer user_data;
} MetaThreadFdImplSource;

typedef struct _MetaThreadPrivate
{
  MetaBackend *backend;

  GMainContext *main_context;

  MetaThreadImpl *impl;
  gboolean in_impl_task;
  gboolean waiting_for_impl_task;

  GList *pending_callbacks;
  guint callbacks_source_id;
} MetaThreadPrivate;

typedef struct _MetaThreadClassPrivate
{
  GType impl_type;
} MetaThreadClassPrivate;

G_DEFINE_TYPE_WITH_CODE (MetaThread, meta_thread, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaThread)
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_thread_constructed (GObject *object)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadClass *thread_class = META_THREAD_GET_CLASS (thread);
  MetaThreadClassPrivate *class_priv =
    G_TYPE_CLASS_GET_PRIVATE (thread_class, META_TYPE_THREAD,
                              MetaThreadClassPrivate);

  G_OBJECT_CLASS (meta_thread_parent_class)->constructed (object);

  priv->main_context = g_main_context_get_thread_default ();

  g_assert (g_type_is_a (class_priv->impl_type, META_TYPE_THREAD_IMPL));
  priv->impl = g_object_new (class_priv->impl_type,
                             "thread", thread,
                             NULL);
}

static void
meta_thread_finalize (GObject *object)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_clear_list (&priv->pending_callbacks,
                (GDestroyNotify) meta_thread_callback_data_free);
  g_clear_handle_id (&priv->callbacks_source_id, g_source_remove);

  g_clear_object (&priv->impl);

  G_OBJECT_CLASS (meta_thread_parent_class)->finalize (object);
}

static void
meta_thread_class_init (MetaThreadClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_thread_get_property;
  object_class->set_property = meta_thread_set_property;
  object_class->constructed = meta_thread_constructed;
  object_class->finalize = meta_thread_finalize;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_thread_init (MetaThread *thread)
{
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

int
meta_thread_flush_callbacks (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  GList *l;
  int callback_count = 0;

  meta_assert_not_in_thread_impl (thread);

  for (l = priv->pending_callbacks; l; l = l->next)
    {
      MetaThreadCallbackData *callback_data = l->data;

      callback_data->callback (thread, callback_data->user_data);
      meta_thread_callback_data_free (callback_data);
      callback_count++;
    }

  g_list_free (priv->pending_callbacks);
  priv->pending_callbacks = NULL;

  return callback_count;
}

static gboolean
callback_idle (gpointer user_data)
{
  MetaThread *thread = user_data;
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  meta_thread_flush_callbacks (thread);

  priv->callbacks_source_id = 0;
  return G_SOURCE_REMOVE;
}

void
meta_thread_queue_callback (MetaThread         *thread,
                            MetaThreadCallback  callback,
                            gpointer            user_data,
                            GDestroyNotify      user_data_destroy)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadCallbackData *callback_data;

  callback_data = g_new0 (MetaThreadCallbackData, 1);
  *callback_data = (MetaThreadCallbackData) {
    .callback = callback,
    .user_data = user_data,
    .user_data_destroy = user_data_destroy,
  };
  priv->pending_callbacks = g_list_append (priv->pending_callbacks,
                                           callback_data);
  if (!priv->callbacks_source_id)
    {
      GSource *idle_source;

      idle_source = g_idle_source_new ();
      g_source_set_callback (idle_source, callback_idle, thread, NULL);
      priv->callbacks_source_id = g_source_attach (idle_source,
                                                   priv->main_context);
      g_source_unref (idle_source);
    }
}

gpointer
meta_thread_run_impl_task_sync (MetaThread          *thread,
                                MetaThreadTaskFunc   func,
                                gpointer             user_data,
                                GError             **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  gpointer ret;

  priv->in_impl_task = TRUE;
  priv->waiting_for_impl_task = TRUE;
  ret = func (priv->impl, user_data, error);
  priv->waiting_for_impl_task = FALSE;
  priv->in_impl_task = FALSE;

  return ret;
}

static gboolean
simple_impl_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  MetaThreadSimpleImplSource *simple_impl_source =
    (MetaThreadSimpleImplSource *) source;
  MetaThread *thread = simple_impl_source->thread;
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  gboolean ret;

  priv->in_impl_task = TRUE;
  ret = callback (user_data);
  priv->in_impl_task = FALSE;

  return ret;
}

static GSourceFuncs simple_impl_source_funcs = {
  .dispatch = simple_impl_source_dispatch,
};

GSource *
meta_thread_add_source_in_impl (MetaThread     *thread,
                                GSourceFunc     func,
                                gpointer        user_data,
                                GDestroyNotify  user_data_destroy)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  GSource *source;
  MetaThreadSimpleImplSource *simple_impl_source;

  meta_assert_in_thread_impl (thread);

  source = g_source_new (&simple_impl_source_funcs,
                         sizeof (MetaThreadSimpleImplSource));
  g_source_set_name (source, "[mutter] MetaThread simple impl");
  simple_impl_source = (MetaThreadSimpleImplSource *) source;
  simple_impl_source->thread = thread;

  g_source_set_callback (source, func, user_data, user_data_destroy);
  g_source_set_ready_time (source, 0);
  g_source_attach (source, meta_thread_impl_get_main_context (priv->impl));

  return source;
}

static gboolean
meta_thread_fd_impl_source_check (GSource *source)
{
  MetaThreadFdImplSource *fd_impl_source = (MetaThreadFdImplSource *) source;

  return g_source_query_unix_fd (source, fd_impl_source->fd_tag) & G_IO_IN;
}

static gboolean
meta_thread_fd_impl_source_dispatch (GSource     *source,
                                     GSourceFunc  callback,
                                     gpointer     user_data)
{
  MetaThreadFdImplSource *fd_impl_source = (MetaThreadFdImplSource *) source;
  MetaThread *thread = fd_impl_source->thread;
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  gpointer ret;
  GError *error = NULL;

  priv->in_impl_task = TRUE;
  ret = fd_impl_source->dispatch (priv->impl,
                                  fd_impl_source->user_data,
                                  &error);
  priv->in_impl_task = FALSE;

  if (!GPOINTER_TO_INT (ret))
    {
      g_warning ("Failed to dispatch fd source: %s", error->message);
      g_error_free (error);
    }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs fd_impl_source_funcs = {
  NULL,
  meta_thread_fd_impl_source_check,
  meta_thread_fd_impl_source_dispatch
};

GSource *
meta_thread_register_fd_in_impl (MetaThread         *thread,
                                 int                 fd,
                                 MetaThreadTaskFunc  dispatch,
                                 gpointer            user_data)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  GSource *source;
  MetaThreadFdImplSource *fd_impl_source;

  meta_assert_in_thread_impl (thread);

  source = g_source_new (&fd_impl_source_funcs,
                         sizeof (MetaThreadFdImplSource));
  g_source_set_name (source, "[mutter] MetaThread fd impl");
  fd_impl_source = (MetaThreadFdImplSource *) source;
  fd_impl_source->dispatch = dispatch;
  fd_impl_source->user_data = user_data;
  fd_impl_source->thread = thread;
  fd_impl_source->fd_tag = g_source_add_unix_fd (source, fd,
                                                 G_IO_IN | G_IO_ERR);

  g_source_attach (source, meta_thread_impl_get_main_context (priv->impl));

  return source;
}

MetaBackend *
meta_thread_get_backend (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->backend;
}

gboolean
meta_thread_is_in_impl_task (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->in_impl_task;
}

gboolean
meta_thread_is_waiting_for_impl_task (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  return priv->waiting_for_impl_task;
}
