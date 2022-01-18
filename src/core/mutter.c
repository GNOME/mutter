/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2011 Red Hat, Inc.
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

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-unix.h>
#include <stdlib.h>

#include "compositor/meta-plugin-manager.h"
#include "meta/main.h"
#include "meta/meta-context.h"
#include "meta/util.h"

static gboolean
print_version (const gchar    *option_name,
               const gchar    *value,
               gpointer        data,
               GError        **error)
{
  g_print ("mutter %s\n", VERSION);
  exit (0);
}

static void
command_exited_cb (GPid     command_pid,
                   int      status,
                   gpointer user_data)
{
  MetaContext *context = user_data;

  g_spawn_close_pid (command_pid);

  if (status)
    {
      GError *error;

      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                           "The command exited with a nonzero status: %d\n",
                           status);

      meta_context_terminate_with_error (context, error);
    }
  else
    {
      meta_context_terminate (context);
    }
}

static const char *plugin = "libdefault";
static char **argv_ignored = NULL;

GOptionEntry mutter_options[] = {
  {
    "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
    print_version,
    N_("Print version"),
    NULL
  },
  {
    "mutter-plugin", 0, 0, G_OPTION_ARG_STRING,
    &plugin,
    N_("Mutter plugin to use"),
    "PLUGIN",
  },
  {
    G_OPTION_REMAINING,
    .arg = G_OPTION_ARG_STRING_ARRAY,
    &argv_ignored,
    .arg_description = "[[--] COMMAND [ARGUMENT…]]"
  },
  { NULL }
};


static gboolean
on_sigterm (gpointer user_data)
{
  MetaContext *context = META_CONTEXT (user_data);

  meta_context_terminate (context);

  return G_SOURCE_REMOVE;
}

static void
init_signal_handlers (MetaContext *context)
{
  struct sigaction act = { 0 };
  sigset_t empty_mask;

  sigemptyset (&empty_mask);
  act.sa_handler = SIG_IGN;
  act.sa_mask = empty_mask;
  act.sa_flags = 0;
  if (sigaction (SIGPIPE,  &act, NULL) < 0)
    g_warning ("Failed to register SIGPIPE handler: %s", g_strerror (errno));
#ifdef SIGXFSZ
  if (sigaction (SIGXFSZ,  &act, NULL) < 0)
    g_warning ("Failed to register SIGXFSZ handler: %s", g_strerror (errno));
#endif

  g_unix_signal_add (SIGTERM, on_sigterm, context);
}

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_context ("Mutter");

  meta_context_add_option_entries (context, mutter_options, GETTEXT_PACKAGE);
  if (!meta_context_configure (context, &argc, &argv, &error))
    {
      g_printerr ("Failed to configure: %s\n", error->message);
      return EXIT_FAILURE;
    }

  meta_context_set_plugin_name (context, plugin);

  init_signal_handlers (context);

  if (!meta_context_setup (context, &error))
    {
      g_printerr ("Failed to setup: %s\n", error->message);
      return EXIT_FAILURE;
    }

  if (!meta_context_start (context, &error))
    {
      g_printerr ("Failed to start: %s\n", error->message);
      return EXIT_FAILURE;
    }

  meta_context_notify_ready (context);
  if (argv_ignored)
    {
      GPid command_pid;
      g_auto (GStrv) command_argv = NULL;

      command_argv = g_steal_pointer (&argv_ignored);

      if (!g_spawn_async (NULL, command_argv, NULL,
                          G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                          NULL, NULL, &command_pid, &error))
        {
          g_printerr ("Failed to run the command: %s\n", error->message);
          return EXIT_FAILURE;
        }

      g_child_watch_add (command_pid, command_exited_cb, context);
    }

  if (meta_context_get_compositor_type (context) == META_COMPOSITOR_TYPE_WAYLAND)
    meta_context_raise_rlimit_nofile (context, NULL);

  if (!meta_context_run_main_loop (context, &error))
    {
      g_printerr ("Mutter terminated with a failure: %s\n", error->message);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
