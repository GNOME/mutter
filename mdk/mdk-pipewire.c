/*
 * Copyright (C) 2015-2024 Red Hat Inc.
 * Copyright (C) 2020 Pascal Nowack
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

#include "mdk-pipewire.h"

#include <errno.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <pipewire/core.h>
#include <pipewire/loop.h>
#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>
#include <spa/utils/result.h>
#include <sys/ioctl.h>

enum
{
  ERROR,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MdkPipeWireSource
{
  GSource base;

  MdkPipewire *pipewire;
} MdkPipeWireSource;

struct _MdkPipewire
{
  GObject parent;

  GSource *source;
  struct pw_loop *pipewire_loop;
  struct pw_context *pipewire_context;
  struct pw_core *pipewire_core;
  struct spa_hook pipewire_core_listener;

  GList *main_contexts;
};

G_DEFINE_FINAL_TYPE (MdkPipewire, mdk_pipewire, G_TYPE_OBJECT)

static gboolean
pipewire_loop_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  MdkPipeWireSource *pipewire_source = (MdkPipeWireSource *) source;
  int result;

  result = pw_loop_iterate (pipewire_source->pipewire->pipewire_loop, 0);
  if (result < 0)
    g_warning ("pipewire_loop_iterate failed: %s", spa_strerror (result));

  return TRUE;
}

static GSourceFuncs pipewire_source_funcs =
{
  .dispatch = pipewire_loop_source_dispatch,
};

static MdkPipeWireSource *
create_pipewire_source (MdkPipewire *pipewire)
{
  MdkPipeWireSource *pipewire_source;

  pipewire_source =
    (MdkPipeWireSource *) g_source_new (&pipewire_source_funcs,
                                        sizeof (MdkPipeWireSource));
  pipewire_source->pipewire = pipewire;
  g_source_add_unix_fd (&pipewire_source->base,
                        pw_loop_get_fd (pipewire->pipewire_loop),
                        G_IO_IN | G_IO_ERR);

  return pipewire_source;
}

static void
on_core_error (void       *user_data,
               uint32_t    id,
               int         seq,
               int         res,
               const char *message)
{
  MdkPipewire *pipewire = MDK_PIPEWIRE (user_data);
  g_autoptr (GError) error = NULL;

  error = g_error_new (G_IO_ERROR, g_io_error_from_errno (-res),
                       "PipeWire core error: id:%u: %s",
                       id, message);
  g_warning ("PipeWire core error: id:%u %s", id, message);

  if (id == PW_ID_CORE)
    g_signal_emit (pipewire, signals[ERROR], 0, error);
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

static void
mdk_pipewire_finalize (GObject *object)
{
  MdkPipewire *pipewire = MDK_PIPEWIRE (object);

  g_clear_pointer (&pipewire->pipewire_core, pw_core_disconnect);
  g_clear_pointer (&pipewire->pipewire_context, pw_context_destroy);
  g_clear_pointer (&pipewire->source, g_source_destroy);

  if (pipewire->pipewire_loop)
    {
      pw_loop_leave (pipewire->pipewire_loop);
      pw_loop_destroy (pipewire->pipewire_loop);
    }

  G_OBJECT_CLASS (mdk_pipewire_parent_class)->finalize (object);
}

static void
mdk_pipewire_class_init (MdkPipewireClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_pipewire_finalize;

  signals[ERROR] = g_signal_new ("error",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_ERROR);
}

static void
mdk_pipewire_init (MdkPipewire *pipewire)
{
}

static void
ensure_initialized (void)
{
  static gboolean is_pipewire_initialized = FALSE;

  if (is_pipewire_initialized)
    return;

  pw_init (NULL, NULL);
  is_pipewire_initialized = TRUE;
}

static void
mdk_pipewire_create_source (MdkPipewire *pipewire)
{
  MdkPipeWireSource *pipewire_source;
  GMainContext *main_context;

  g_return_if_fail (!pipewire->source);

  pipewire_source = create_pipewire_source (pipewire);
  pipewire->source = (GSource *) pipewire_source;

  main_context = pipewire->main_contexts ?
    g_list_first (pipewire->main_contexts)->data : NULL;
  g_source_attach (pipewire->source, main_context);
  g_source_unref (pipewire->source);
}

static void
mdk_pipewire_destroy_source (MdkPipewire *pipewire)
{
  g_return_if_fail (pipewire->source);

  g_clear_pointer (&pipewire->source, g_source_destroy);
}

static void
mdk_pipewire_reset_source (MdkPipewire *pipewire)
{
  mdk_pipewire_destroy_source (pipewire);
  mdk_pipewire_create_source (pipewire);
}

MdkPipewire *
mdk_pipewire_new (MdkContext  *context,
                  GError     **error)
{
  g_autoptr (MdkPipewire) pipewire = NULL;

  ensure_initialized ();

  pipewire = g_object_new (MDK_TYPE_PIPEWIRE, NULL);

  pipewire->pipewire_loop = pw_loop_new (NULL);
  if (!pipewire->pipewire_loop)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create pipewire loop");
      return NULL;
    }

  pw_loop_enter (pipewire->pipewire_loop);

  mdk_pipewire_create_source (pipewire);

  pipewire->pipewire_context = pw_context_new (pipewire->pipewire_loop,
                                               NULL, 0);
  if (!pipewire->pipewire_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create pipewire context");
      return NULL;
    }

  pipewire->pipewire_core = pw_context_connect (pipewire->pipewire_context,
                                                NULL, 0);
  if (!pipewire->pipewire_core)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to connect pipewire context");
      return NULL;
    }

  pw_core_add_listener (pipewire->pipewire_core,
                        &pipewire->pipewire_core_listener,
                        &core_events,
                        pipewire);

  return g_steal_pointer (&pipewire);
}

struct pw_core *
mdk_pipewire_get_core (MdkPipewire *pipewire)
{
  return pipewire->pipewire_core;
}

struct pw_loop *
mdk_pipewire_get_loop (MdkPipewire *pipewire)
{
  return pipewire->pipewire_loop;
}

void
mdk_pipewire_push_main_context (MdkPipewire  *pipewire,
                                GMainContext *main_context)
{
  g_return_if_fail (!g_list_find (pipewire->main_contexts, main_context));

  pipewire->main_contexts = g_list_prepend (pipewire->main_contexts,
                                            main_context);
  mdk_pipewire_reset_source (pipewire);
}

void
mdk_pipewire_pop_main_context (MdkPipewire  *pipewire,
                               GMainContext *main_context)
{
  g_return_if_fail (pipewire->main_contexts);
  g_return_if_fail (g_list_find (pipewire->main_contexts, main_context) ==
                    g_list_first (pipewire->main_contexts));

  pipewire->main_contexts = g_list_remove (pipewire->main_contexts,
                                           main_context);
  mdk_pipewire_reset_source (pipewire);
}
