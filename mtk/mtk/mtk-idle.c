/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2026 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "mtk-idle.h"

typedef struct _OnceClosure OnceClosure;

struct _OnceClosure
{
  GSourceOnceFunc func;
  gpointer data;
  GDestroyNotify notify;
};

static OnceClosure *
once_closure_new (GSourceOnceFunc func,
                  gpointer        data)
{
  OnceClosure *once;

  once = g_new0 (OnceClosure, 1);
  once->func = func;
  once->data = data;

  return once;
}

static void
once_closure_destroy (gpointer user_data)
{
  OnceClosure *once = user_data;

  g_free (once);
}

static gboolean
once_closure_call (gpointer user_data)
{
  OnceClosure *once = user_data;

  once->func (once->data);

  return G_SOURCE_REMOVE;
}

/**
 * mtk_idle_add_full: (rename-to mtk_idle_add)
 * @priority: the priority of the idle source; typically this will be in the
 *   range between [const@GLib.PRIORITY_DEFAULT_IDLE] and
 *   [const@GLib.PRIORITY_HIGH_IDLE]
 * @function: function to call
 * @data: data to pass to @function
 * @notify: (nullable): function to call when the idle is removed
 *
 * Adds a function to be called whenever there are no higher priority
 * events pending.
 *
 * This method is similar to [func@GLib.idle_add_full] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_idle_add_full (int            priority,
                   GSourceFunc    function,
                   gpointer       data,
                   GDestroyNotify notify)
{
  g_autoptr (GMainContext) context = NULL;
  g_autoptr (GSource) source = NULL;
  guint id;

  context = g_main_context_ref_thread_default ();

  source = g_idle_source_new ();
  g_source_set_priority (source, priority);
  g_source_set_callback (source, function, data, notify);
  id = g_source_attach (source, context);

  return id;
}

/**
 * mtk_idle_add:
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * Adds a function to be called whenever there are no higher priority
 * events pending.
 *
 * This method is similar to [func@GLib.idle_add] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_idle_add (GSourceFunc function,
              gpointer    data)
{
  return mtk_idle_add_full (G_PRIORITY_DEFAULT_IDLE, function, data, NULL);
}

/**
 * mtk_idle_add_once:
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * Adds a function to be called whenever there are no higher priority
 * events pending to the default main loop.
 *
 * The function will only be called once and then the source will be
 * automatically removed from the main context.
 *
 * This method is similar to [func@GLib.idle_add_once] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_idle_add_once (GSourceOnceFunc function,
                   gpointer        data)
{
  OnceClosure *once;

  once = once_closure_new (function, data);

  return mtk_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                            once_closure_call, once,
                            once_closure_destroy);
}

/**
 * mtk_timeout_add_full: (rename-to mtk_timeout_add)
 * @priority: the priority of the timeout source; typically this will be in
 *   the range between [const@GLib.PRIORITY_DEFAULT] and
 *   [const@GLib.PRIORITY_HIGH]
 * @interval: the time between calls to the function, in milliseconds
 * @function: function to call
 * @data: data to pass to @function
 * @notify: (nullable): function to call when the timeout is removed
 *
 * Sets a function to be called at regular intervals, with the given
 * priority.
 *
 * This method is similar to [func@GLib.timeout_add_full] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add_full (int            priority,
                      unsigned int   interval,
                      GSourceFunc    function,
                      gpointer       data,
                      GDestroyNotify notify)
{
  g_autoptr (GMainContext) context = NULL;
  g_autoptr (GSource) source = NULL;
  guint id;

  context = g_main_context_ref_thread_default ();

  source = g_timeout_source_new (interval);
  g_source_set_priority (source, priority);
  g_source_set_callback (source, function, data, notify);
  id = g_source_attach (source, context);

  return id;
}

/**
 * mtk_timeout_add:
 * @interval: the time between calls to the function, in milliseconds
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * Sets a function to be called at regular intervals, with the default
 * priority, [const@GLib.PRIORITY_DEFAULT].
 *
 * This method is similar to [func@GLib.timeout_add] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add (unsigned int interval,
                 GSourceFunc  function,
                 gpointer     data)
{
  return mtk_timeout_add_full (G_PRIORITY_DEFAULT, interval,
                               function, data, NULL);
}

/**
 * mtk_timeout_add_once:
 * @interval: the time after which the function will be called, in milliseconds
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * Sets a function to be called after @interval milliseconds have elapsed,
 * with the default priority, [const@GLib.PRIORITY_DEFAULT].
 *
 * This method is similar to [func@GLib.timeout_add_once] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add_once (unsigned int    interval,
                      GSourceOnceFunc function,
                      gpointer        data)
{
  OnceClosure *once;

  once = once_closure_new (function, data);

  return mtk_timeout_add_full (G_PRIORITY_DEFAULT,
                               interval,
                               once_closure_call, once,
                               once_closure_destroy);
}

/**
 * mtk_timeout_add_seconds_full: (rename-to mtk_timeout_add_seconds)
 * @priority: the priority of the timeout source; typically this will be in
 *   the range between [const@GLib.PRIORITY_DEFAULT] and
 *   [const@GLib.PRIORITY_HIGH]
 * @interval: the time between calls to the function, in seconds
 * @function: function to call
 * @data: data to pass to @function
 * @notify: (nullable): function to call when the timeout is removed
 *
 * Sets a function to be called at regular intervals, with @priority.
 *
 * This method is similar to [func@GLib.timeout_add_seconds_full] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add_seconds_full (int            priority,
                              unsigned int   interval,
                              GSourceFunc    function,
                              gpointer       data,
                              GDestroyNotify notify)
{
  g_autoptr (GMainContext) context = NULL;
  g_autoptr (GSource) source = NULL;
  guint id;

  context = g_main_context_ref_thread_default ();

  source = g_timeout_source_new_seconds (interval);
  g_source_set_priority (source, priority);
  g_source_set_callback (source, function, data, notify);
  id = g_source_attach (source, context);

  return id;
}

/**
 * mtk_timeout_add_seconds:
 * @interval: the time between calls to the function, in seconds
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * Sets a function to be called at regular intervals with the default
 * priority, [const@GLib.PRIORITY_DEFAULT].
 *
 * This method is similar to [func@GLib.timeout_add_seconds] except it will
 * attach the source to the thread-default main context.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add_seconds (unsigned int interval,
                         GSourceFunc  function,
                         gpointer     data)
{
  return mtk_timeout_add_seconds_full (G_PRIORITY_DEFAULT, interval,
                                       function, data, NULL);
}

/**
 * mtk_timeout_add_seconds_once:
 * @interval: the time after which the function will be called, in seconds
 * @function: (scope forever): function to call
 * @data: data to pass to @function
 *
 * This function behaves like [func@Mtk.timeout_add_once] but with a range in
 * seconds.
 *
 * Returns: the ID (greater than 0) of the event source
 **/
guint
mtk_timeout_add_seconds_once (unsigned int    interval,
                              GSourceOnceFunc function,
                              gpointer        data)
{
  OnceClosure *once;

  once = once_closure_new (function, data);

  return mtk_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                       interval,
                                       once_closure_call, once,
                                       once_closure_destroy);
}

/**
 * mtk_source_remove:
 * @tag: the ID of the source to remove.
 *
 * Removes the source with the given ID from the default main context.
 *
 * This function behaves like g_source_remove but checking @tag
 * against the thread-default main context, as opposed to the default one.
 *
 * Returns: %TRUE if source was found and removed
 **/
gboolean
mtk_source_remove (guint tag)
{
  g_autoptr (GMainContext) context = NULL;
  GSource *source;

  g_return_val_if_fail (tag > 0, FALSE);

  context = g_main_context_ref_thread_default ();

  source = g_main_context_find_source_by_id (context, tag);
  if (source)
    g_source_destroy (source);
  else
    g_critical ("Source ID %u was not found when attempting to remove it", tag);

  return source != NULL;
}

/**
 * mtk_source_set_name_by_id:
 * @tag: a source ID
 * @name: debug name for the source
 *
 * Sets the name of a source using its ID.
 *
 * This function behaves like g_source_set_name_by_id(), except it
 * looks up @tag in the thread default main context, as opposed to the default
 * one.
 **/
void
mtk_source_set_name_by_id (guint       tag,
                           const char *name)
{
  g_autoptr (GMainContext) context = NULL;
  GSource *source;

  g_return_if_fail (tag > 0);

  context = g_main_context_ref_thread_default ();

  source = g_main_context_find_source_by_id (context, tag);
  if (source)
    g_source_set_name (source, name);
}
