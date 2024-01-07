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

#include "config.h"

#include "backends/native/meta-thread-private.h"

#include <glib.h>
#include <sys/resource.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-backend-types.h"
#include "backends/native/meta-thread-impl.h"

#include "meta-dbus-rtkit1.h"
#include "meta-private-enum-types.h"

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_NAME,
  PROP_THREAD_TYPE,
  PROP_WANTS_REALTIME,

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
  gboolean wants_realtime;
  gboolean waiting_for_impl_task;
  GSource *wrapper_source;

  GMutex callbacks_mutex;
  GHashTable *callback_sources;

  MetaThreadType thread_type;

  GThread *main_thread;

  struct {
    MetaDBusRealtimeKit1 *rtkit_proxy;
    GThread *thread;
    pid_t thread_id;
    GMutex init_mutex;
    int realtime_inhibit_count;
    gboolean is_realtime;
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
    case PROP_WANTS_REALTIME:
      g_value_set_boolean (value, priv->wants_realtime);
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
    case PROP_WANTS_REALTIME:
      priv->wants_realtime = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GVariant *
get_rtkit_property (MetaDBusRealtimeKit1  *rtkit_proxy,
                    const char            *property_name,
                    GError               **error)
{
  GDBusConnection *connection;
  g_autoptr (GVariant) prop_value = NULL;
  g_autoptr (GVariant) property_variant = NULL;

  /* The following is a fall back path for a RTKit daemon that doesn't support
   * org.freedesktop.DBus.Properties.GetAll. See
   * <https://github.com/heftig/rtkit/pull/30>.
   */
  connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (rtkit_proxy));
  prop_value =
    g_dbus_connection_call_sync (connection,
                                 "org.freedesktop.RealtimeKit1",
                                 "/org/freedesktop/RealtimeKit1",
                                 "org.freedesktop.DBus.Properties",
                                 "Get",
                                 g_variant_new ("(ss)",
                                                "org.freedesktop.RealtimeKit1",
                                                property_name),
                                 G_VARIANT_TYPE ("(v)"),
                                 G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                 -1, NULL, error);
  if (!prop_value)
    return NULL;

  g_variant_get (prop_value, "(v)", &property_variant);
  return g_steal_pointer (&property_variant);
}

static gboolean
ensure_realtime_kit_proxy (MetaThread  *thread,
                           GError     **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (MetaDBusRealtimeKit1) rtkit_proxy = NULL;
  g_autoptr (GError) local_error = NULL;

  if (priv->kernel.rtkit_proxy)
    return TRUE;

  rtkit_proxy =
    meta_dbus_realtime_kit1_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                    "org.freedesktop.RealtimeKit1",
                                                    "/org/freedesktop/RealtimeKit1",
                                                    NULL,
                                                    &local_error);
  if (!rtkit_proxy)
    {
      g_dbus_error_strip_remote_error (local_error);
      g_propagate_prefixed_error (error, g_steal_pointer (&local_error),
                                  "Failed to acquire RTKit D-Bus proxy: ");
      return FALSE;
    }

  priv->kernel.rtkit_proxy = g_steal_pointer (&rtkit_proxy);
  return TRUE;
}

static gboolean
request_realtime_scheduling (MetaThread  *thread,
                             GError     **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GError) local_error = NULL;
  int64_t rttime;
  struct rlimit rl;
  uint32_t priority;

  if (!ensure_realtime_kit_proxy (thread, error))
    return FALSE;

  priority = meta_dbus_realtime_kit1_get_max_realtime_priority (priv->kernel.rtkit_proxy);
  if (priority == 0)
    {
      g_autoptr (GVariant) priority_variant = NULL;

      priority_variant = get_rtkit_property (priv->kernel.rtkit_proxy,
                                             "MaxRealtimePriority",
                                             error);
      if (!priority_variant)
        return FALSE;

      priority = g_variant_get_int32 (priority_variant);
    }

  if (priority == 0)
    g_warning ("Maximum real time scheduling priority is 0");

  rttime = meta_dbus_realtime_kit1_get_rttime_usec_max (priv->kernel.rtkit_proxy);
  if (rttime == 0)
    {
      g_autoptr (GVariant) rttime_variant = NULL;

      rttime_variant = get_rtkit_property (priv->kernel.rtkit_proxy,
                                           "RTTimeUSecMax",
                                           error);
      if (!rttime_variant)
        return FALSE;

      rttime = g_variant_get_int64 (rttime_variant);
    }

  meta_topic (META_DEBUG_BACKEND,
              "Setting soft and hard RLIMIT_RTTIME limit to %lu", rttime);
  rl.rlim_cur = rttime;
  rl.rlim_max = rttime;

  if (setrlimit (RLIMIT_RTTIME, &rl) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to set RLIMIT_RTTIME: %s", g_strerror (errno));
      return FALSE;
    }

  meta_topic (META_DEBUG_BACKEND, "Setting '%s' thread real time priority to %d",
              priv->name, priority);
  if (!meta_dbus_realtime_kit1_call_make_thread_realtime_sync (priv->kernel.rtkit_proxy,
                                                               priv->kernel.thread_id,
                                                               priority,
                                                               NULL,
                                                               &local_error))
    {
      g_dbus_error_strip_remote_error (local_error);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

static gboolean
request_normal_scheduling (MetaThread  *thread,
                           GError     **error)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GError) local_error = NULL;

  if (!ensure_realtime_kit_proxy (thread, error))
    return FALSE;

  meta_topic (META_DEBUG_BACKEND, "Setting '%s' thread to normal priority", priv->name);
  if (!meta_dbus_realtime_kit1_call_make_thread_high_priority_sync (priv->kernel.rtkit_proxy,
                                                                    priv->kernel.thread_id,
                                                                    0 /* "normal" nice value */,
                                                                    NULL,
                                                                    &local_error))
    {
      g_dbus_error_strip_remote_error (local_error);
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  return TRUE;
}

static gboolean
should_use_realtime_scheduling_in_impl (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  gboolean should_use_realtime_scheduling = FALSE;

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      break;
    case META_THREAD_TYPE_KERNEL:
      if (priv->wants_realtime && priv->kernel.realtime_inhibit_count == 0)
        should_use_realtime_scheduling = TRUE;
      break;
    }

  return should_use_realtime_scheduling;
}

static void
sync_realtime_scheduling_in_impl (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GError) error = NULL;
  gboolean should_be_realtime;

  should_be_realtime = should_use_realtime_scheduling_in_impl (thread);

  if (should_be_realtime == priv->kernel.is_realtime)
    return;

  if (should_be_realtime)
    {
      if (!request_realtime_scheduling (thread, &error))
        {
          g_warning ("Failed to make thread '%s' realtime scheduled: %s",
                     priv->name, error->message);
        }
      else
        {
          meta_topic (META_DEBUG_BACKEND, "Made thread '%s' real-time scheduled", priv->name);
          priv->kernel.is_realtime = TRUE;
        }
    }
  else
    {
      if (!request_normal_scheduling (thread, &error))
        {
          g_warning ("Failed to make thread '%s' normally scheduled: %s",
                     priv->name, error->message);
        }
      else
        {
          meta_topic (META_DEBUG_BACKEND, "Made thread '%s' normally scheduled", priv->name);
          priv->kernel.is_realtime = FALSE;
        }
    }
}

static gpointer
thread_impl_func (gpointer user_data)
{
  MetaThread *thread = META_THREAD (user_data);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadImpl *impl = priv->impl;
  MetaThreadImplRunFlags run_flags = META_THREAD_IMPL_RUN_FLAG_NONE;
  GMainContext *thread_context = meta_thread_impl_get_main_context (impl);
#ifdef HAVE_PROFILER
  MetaContext *context = meta_backend_get_context (priv->backend);
  MetaProfiler *profiler = meta_context_get_profiler (context);
#endif

  g_mutex_lock (&priv->kernel.init_mutex);
  g_mutex_unlock (&priv->kernel.init_mutex);

  g_main_context_push_thread_default (thread_context);

#ifdef HAVE_PROFILER
  meta_profiler_register_thread (profiler, thread_context, priv->name);
#endif

  priv->kernel.thread_id = gettid ();
  priv->kernel.realtime_inhibit_count = 0;
  priv->kernel.is_realtime = FALSE;

  sync_realtime_scheduling_in_impl (thread);

  if (priv->kernel.is_realtime)
    {
      g_message ("Made thread '%s' realtime scheduled", priv->name);
      run_flags |= META_THREAD_IMPL_RUN_FLAG_REALTIME;
    }

  meta_thread_impl_run (impl, run_flags);

#ifdef HAVE_PROFILER
  meta_profiler_unregister_thread (profiler, thread_context);
#endif

  g_main_context_pop_thread_default (thread_context);

  return GINT_TO_POINTER (TRUE);
}

typedef struct _WrapperSource
{
  GSource base;

  GMainContext *thread_main_context;

  GPollFD fds[256];
  gpointer fd_tags[256];
  int n_fds;
  int priority;
} WrapperSource;

static gboolean
wrapper_source_prepare (GSource *source,
                        int     *timeout)
{
  WrapperSource *wrapper_source = (WrapperSource *) source;
  int ret;
  int old_n_fds = wrapper_source->n_fds;
  GPollFD old_fds[wrapper_source->n_fds];
  int i;

  ret = g_main_context_prepare (wrapper_source->thread_main_context,
                                &wrapper_source->priority);

  if (old_n_fds > 0)
    memcpy (old_fds, wrapper_source->fds, sizeof (GPollFD) * old_n_fds);

  wrapper_source->n_fds =
    g_main_context_query (wrapper_source->thread_main_context,
                          INT_MAX,
                          timeout,
                          wrapper_source->fds,
                          G_N_ELEMENTS (wrapper_source->fds));

  if (wrapper_source->n_fds == old_n_fds &&
      old_n_fds > 0 &&
      memcmp (old_fds, wrapper_source->fds, old_n_fds * sizeof (GPollFD)) == 0)
    return ret;

  for (i = 0; i < old_n_fds; i++)
    g_source_remove_unix_fd (source, wrapper_source->fd_tags[i]);

  for (i = 0; i < wrapper_source->n_fds; i++)
    {
      wrapper_source->fd_tags[i] =
        g_source_add_unix_fd (source,
                              wrapper_source->fds[i].fd,
                              wrapper_source->fds[i].events);
    }

  return ret;
}

static gboolean
wrapper_source_check (GSource *source)
{
  WrapperSource *wrapper_source = (WrapperSource *) source;
  GIOCondition all_revents = 0;
  int i;

  for (i = 0; i < wrapper_source->n_fds; i++)
    {
      GIOCondition revents;

      revents = g_source_query_unix_fd (source, wrapper_source->fd_tags[i]);
      wrapper_source->fds[i].revents = revents;
      all_revents |= revents;
    }

  return !!all_revents;
}

static gboolean
wrapper_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  WrapperSource *wrapper_source = (WrapperSource *) source;

  g_source_set_priority (source, MIN (0, wrapper_source->priority));

  if (g_main_context_check (wrapper_source->thread_main_context,
                            wrapper_source->priority,
                            wrapper_source->fds,
                            wrapper_source->n_fds))
    g_main_context_dispatch (wrapper_source->thread_main_context);

  return G_SOURCE_CONTINUE;
}

static void
wrapper_source_finalize (GSource *source)
{
}

static GSourceFuncs wrapper_source_funcs = {
  .prepare = wrapper_source_prepare,
  .check = wrapper_source_check,
  .dispatch = wrapper_source_dispatch,
  .finalize = wrapper_source_finalize,
};

static void
wrap_main_context (MetaThread   *thread,
                   GMainContext *thread_main_context)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GSource) source = NULL;
  WrapperSource *wrapper_source;
  g_autofree char *name = NULL;

  if (!g_main_context_acquire (thread_main_context))
    g_return_if_reached ();

  source = g_source_new (&wrapper_source_funcs,
                         sizeof (WrapperSource));
  name = g_strdup_printf ("[mutter] MetaThread '%s' wrapper source",
                          meta_thread_get_name (thread));
  g_source_set_name (source, name);
  wrapper_source = (WrapperSource *) source;
  wrapper_source->thread_main_context = thread_main_context;
  g_source_set_ready_time (source, -1);
  g_source_attach (source, NULL);

  priv->wrapper_source = source;
}

static void
unwrap_main_context (MetaThread   *thread,
                     GMainContext *thread_main_context)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  g_main_context_release (thread_main_context);
  g_clear_pointer (&priv->wrapper_source, g_source_destroy);
}

static void
start_thread (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      wrap_main_context (thread,
                         meta_thread_impl_get_main_context (priv->impl));
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

  thread_context = g_main_context_new ();

  g_assert (g_type_is_a (class_priv->impl_type, META_TYPE_THREAD_IMPL));
  priv->impl = g_object_new (class_priv->impl_type,
                             "thread", thread,
                             "main-context", thread_context,
                             NULL);

  start_thread (thread);

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

  meta_thread_impl_terminate (priv->impl);
  while (meta_thread_impl_dispatch (priv->impl) > 0);
  unwrap_main_context (thread, meta_thread_impl_get_main_context (priv->impl));
}

static void
finalize_thread_kernel (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  meta_thread_impl_terminate (priv->impl);
  g_thread_join (priv->kernel.thread);
  priv->kernel.thread = NULL;
  priv->kernel.thread_id = 0;

  g_clear_object (&priv->kernel.rtkit_proxy);

  g_mutex_clear (&priv->kernel.init_mutex);
}

static void
tear_down_thread (MetaThread *thread)
{
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
}

static void
meta_thread_finalize (GObject *object)
{
  MetaThread *thread = META_THREAD (object);
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  tear_down_thread (thread);

  meta_thread_unregister_callback_context (thread, priv->main_context);

  g_clear_object (&priv->impl);
  g_clear_pointer (&priv->name, g_free);

  g_warn_if_fail (g_hash_table_size (priv->callback_sources) == 0);
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
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  obj_props[PROP_THREAD_TYPE] =
    g_param_spec_enum ("thread-type", NULL, NULL,
                       META_TYPE_THREAD_TYPE,
                       META_THREAD_TYPE_KERNEL,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  obj_props[PROP_WANTS_REALTIME] =
    g_param_spec_boolean ("wants-realtime", NULL, NULL,
                          FALSE,
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

void
meta_thread_reset_thread_type (MetaThread     *thread,
                               MetaThreadType  thread_type)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  g_autoptr (GMainContext) thread_context = NULL;

  if (priv->thread_type == thread_type)
    return;

  tear_down_thread (thread);
  g_assert (!priv->wrapper_source);

  priv->thread_type = thread_type;

  start_thread (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      g_assert (priv->wrapper_source);
      break;
    case META_THREAD_TYPE_KERNEL:
      g_assert (!priv->wrapper_source);
      break;
    }
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
meta_thread_dispatch_callbacks (MetaThread   *thread,
                                GMainContext *main_context)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  MetaThreadCallbackSource *callback_source;
  g_autoptr (GList) pending_callbacks = NULL;

  if (!main_context)
    main_context = g_main_context_default ();

  callback_source = g_hash_table_lookup (priv->callback_sources, main_context);

  g_assert (callback_source->main_context == main_context);

  g_mutex_lock (&priv->callbacks_mutex);
  pending_callbacks = g_steal_pointer (&callback_source->callbacks);
  g_mutex_unlock (&priv->callbacks_mutex);

  dispatch_callbacks (thread, pending_callbacks);
}

void
meta_thread_flush_callbacks (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);
  GSource *source;
  g_autoptr (GPtrArray) main_thread_sources = NULL;
  g_autoptr (GList) callback_sources = NULL;
  GList *l;

  g_assert (!g_main_context_get_thread_default ());
  main_thread_sources = g_ptr_array_new ();
  source = g_hash_table_lookup (priv->callback_sources,
                                priv->main_context);
  g_ptr_array_add (main_thread_sources, source);
  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_USER:
      source =
        g_hash_table_lookup (priv->callback_sources,
                             meta_thread_impl_get_main_context (priv->impl));
      g_ptr_array_add (main_thread_sources, source);
      break;
    case META_THREAD_TYPE_KERNEL:
      break;
    }

  while (TRUE)
    {
      g_autoptr (GList) pending_callbacks = NULL;
      gboolean needs_reflush = FALSE;
      int i;

      g_mutex_lock (&priv->callbacks_mutex);
      for (i = 0; i < main_thread_sources->len; i++)
        {
          MetaThreadCallbackSource *source =
            g_ptr_array_index (main_thread_sources, i);

          pending_callbacks =
            g_list_concat (pending_callbacks,
                           g_steal_pointer (&source->callbacks));
        }

      callback_sources = g_hash_table_get_values (priv->callback_sources);
      g_mutex_unlock (&priv->callbacks_mutex);

      if (dispatch_callbacks (thread, pending_callbacks) > 0)
        needs_reflush = TRUE;

      g_list_foreach (callback_sources, (GFunc) g_source_ref, NULL);
      for (l = callback_sources; l; l = l->next)
        {
          MetaThreadCallbackSource *callback_source = l->data;

          if (g_ptr_array_find (main_thread_sources, callback_source, NULL))
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
  g_autofree char *name = NULL;

  source = g_source_new (&callback_source_funcs,
                         sizeof (MetaThreadCallbackSource));
  name = g_strdup_printf ("[mutter] MetaThread '%s' callback source",
                          meta_thread_get_name (thread));
  g_source_set_name (source, name);
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

static void
no_op_callback (MetaThread *thread,
                gpointer    user_data)
{
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
    .callback = callback ? callback : no_op_callback,
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

/**
 * meta_thread_post_impl_task:
 * @thread: A #MetaThread
 * @func: The #MetaThreadTaskFunc to invoke in the impl context
 * @user_data: An opaque pointer passed to func
 * @user_data_destroy: Function called when user_data is no longer needed
 * @feedback_func: A #MetaThreadTaskFeedbackFunc to invoke with the result
 * @feedback_user_data: An opaque pointer passed to feedback_func
 *
 * Post tasks to be invoked inside the thread impl context.
 *
 * The user_data_notify function may be called in any thread, and must be
 * thread safe.
 *
 * The feedback_func will be called on the thread implied by
 * feedback_main_contxext. Passing a NULL feedback_main_context implies the
 * GLib main thread.
 */
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

void
meta_thread_inhibit_realtime_in_impl (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_KERNEL:
      priv->kernel.realtime_inhibit_count++;

      if (priv->kernel.realtime_inhibit_count == 1)
        sync_realtime_scheduling_in_impl (thread);
      break;
    case META_THREAD_TYPE_USER:
      break;
    }
}

void
meta_thread_uninhibit_realtime_in_impl (MetaThread *thread)
{
  MetaThreadPrivate *priv = meta_thread_get_instance_private (thread);

  switch (priv->thread_type)
    {
    case META_THREAD_TYPE_KERNEL:
      priv->kernel.realtime_inhibit_count--;

      if (priv->kernel.realtime_inhibit_count == 0)
        sync_realtime_scheduling_in_impl (thread);
      break;
    case META_THREAD_TYPE_USER:
      break;
    }
}
