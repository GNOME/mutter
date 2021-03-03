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

static const char *plugin = "libdefault";

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
  { NULL }
};

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_context ("Mutter");

  meta_context_add_option_entries (context, mutter_options, GETTEXT_PACKAGE);
  if (!meta_context_configure (context, &argc, &argv, &error))
    {
      g_printerr ("Failed to configure: %s", error->message);
      return EXIT_FAILURE;
    }

  meta_context_set_plugin_name (context, plugin);

  if (!meta_context_setup (context, &error))
    {
      g_printerr ("Failed to setup: %s", error->message);
      return EXIT_FAILURE;
    }

  if (!meta_context_start (context, &error))
    {
      g_printerr ("Failed to start: %s", error->message);
      return EXIT_FAILURE;
    }

  meta_context_notify_ready (context);

  if (!meta_context_run_main_loop (context, &error))
    {
      g_printerr ("Mutter terminated with a failure: %s", error->message);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
