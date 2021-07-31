/*
 * Copyright (C) 2021-2025 Red Hat Inc.
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
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "mdk-context.h"
#include "mdk-monitor.h"

static void
activate_about (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GtkApplication *app = user_data;
  const char *authors[] = {
    _("The Mutter Team"),
    NULL
  };

  gtk_show_about_dialog (GTK_WINDOW (gtk_application_get_active_window (app)),
                         "program-name", _("Mutter Development Kit"),
                         "version", VERSION,
                         "copyright", "© 2001—2025 The Mutter Team",
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "website", "http://gitlab.gnome.org/GNOME/mutter",
                         "comments", _("Mutter software development kit"),
                         "authors", authors,
                         "logo-icon-name", "org.gnome.Mutter.Mdk",
                         "title", _("About Mutter Development Kit"),
                         NULL);
}

static void
on_context_ready (MdkContext   *context,
                  GApplication *app)
{
  GList *windows;
  GtkWindow *window;

  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  g_warn_if_fail (g_list_length (windows) == 1);

  window = windows->data;

  gtk_window_set_child (window, GTK_WIDGET (mdk_monitor_new (context)));
  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
}

static void
on_context_error (MdkContext   *context,
                  GError       *error,
                  GApplication *app)
{
  g_warning ("Context got an error: %s", error->message);
}

static void
startup (GApplication *app)
{
  GdkDisplay *display = gdk_display_get_default ();
  g_autoptr (GtkBuilder) builder = NULL;
  g_autoptr (GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/ui/mdk-devkit.css");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
activate (GApplication *app,
          MdkContext   *context)
{
  g_autoptr (GtkBuilder) builder = NULL;
  GtkWidget *window;

  builder = gtk_builder_new_from_resource ("/ui/mdk-devkit.ui");

  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));

  g_signal_connect (context, "ready", G_CALLBACK (on_context_ready), app);
  g_signal_connect (context, "error", G_CALLBACK (on_context_error), app);
  mdk_context_activate (context);
}

static void
on_context_closed (MdkContext     *context,
                   GtkApplication *app)
{
  g_application_quit (G_APPLICATION (app));
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MdkContext) context = NULL;
  g_autoptr (GtkApplication) app = NULL;
  static GActionEntry app_entries[] = {
    { "about", activate_about, NULL, NULL, NULL },
  };

  context = mdk_context_new ();

  app = gtk_application_new ("org.gnome.Mutter.Mdk",
                             G_APPLICATION_NON_UNIQUE);

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_entries, G_N_ELEMENTS (app_entries),
                                   app);

  g_application_set_version (G_APPLICATION (app), VERSION);

  g_signal_connect (app, "startup", G_CALLBACK (startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (activate), context);
  g_signal_connect (context, "closed", G_CALLBACK (on_context_closed), app);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
