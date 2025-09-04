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

#include <adwaita.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "mdk-context.h"
#include "mdk-launchers-editor.h"
#include "mdk-main-window.h"
#include "mdk-monitor.h"

struct _MdkApplication
{
  AdwApplication parent;

  MdkContext *context;
};

#define MDK_TYPE_APPLICATION (mdk_application_get_type ())
G_DECLARE_FINAL_TYPE (MdkApplication, mdk_application,
                      MDK, APPLICATION, AdwApplication)
G_DEFINE_FINAL_TYPE (MdkApplication, mdk_application, ADW_TYPE_APPLICATION)

static void
activate_about (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GtkApplication *app = user_data;
  GtkWindow *parent_window;
  const char *authors[] = {
    _("The Mutter Team"),
    NULL
  };

  parent_window = GTK_WINDOW (gtk_application_get_active_window (app));

  adw_show_about_dialog (GTK_WIDGET (parent_window),
                         "application-name", _("Mutter Development Kit"),
                         "version", VERSION,
                         "copyright", "© 2001—2025 The Mutter Team",
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "website", "http://mutter.gnome.org",
                         "issue-url", "http://gitlab.gnome.org/GNOME/mutter/-/issues",
                         "comments", _("Mutter software development kit"),
                         "developers", authors,
                         "application-icon", "org.gnome.Mutter.Mdk",
                         "title", _("About Mutter Development Kit"),
                         NULL);
}

static void
activate_edit_launchers (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
  MdkApplication *app = MDK_APPLICATION (user_data);
  GtkWindow *parent_window;
  AdwDialog *dialog;

  parent_window = gtk_application_get_active_window (GTK_APPLICATION (app));

  dialog = g_object_new (MDK_TYPE_LAUNCHERS_EDITOR,
                         "context", app->context,
                         NULL);
  adw_dialog_present (dialog, GTK_WIDGET (parent_window));
}

static void
activate_launch (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  MdkApplication *app = MDK_APPLICATION (user_data);
  int id;
  GtkWindow *window;

  id = g_variant_get_int32 (parameter);
  mdk_context_activate_launcher (app->context, id);

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  gtk_window_set_focus (window, gtk_window_get_child (window));
}

static void
on_context_ready (MdkContext   *context,
                  GApplication *app)
{
  GList *windows;
  GtkWindow *window;
  MdkMonitor *monitor;

  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  g_warn_if_fail (g_list_length (windows) == 1);

  window = windows->data;

  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);

  monitor = mdk_monitor_new (context);
  gtk_window_set_child (window, GTK_WIDGET (monitor));
  gtk_window_set_focus (window, GTK_WIDGET (monitor));
}

static void
on_context_error (MdkContext   *context,
                  GError       *error,
                  GApplication *app)
{
  g_warning ("Context got an error: %s", error->message);
  exit (EXIT_FAILURE);
}

static void
startup (GApplication *app)
{
  GdkDisplay *display = gdk_display_get_default ();
  g_autoptr (GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/ui/mdk-devkit.css");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
activate (MdkApplication *app)
{
  GtkWidget *window;

  window = g_object_new (MDK_TYPE_MAIN_WINDOW,
                         "context", app->context,
                         NULL);
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));

  g_signal_connect (app->context, "ready", G_CALLBACK (on_context_ready), app);
  g_signal_connect (app->context, "error", G_CALLBACK (on_context_error), app);
  mdk_context_activate (app->context);
}

static gboolean
transform_action_state_to (GBinding     *binding,
                           const GValue *from_value,
                           GValue       *to_value,
                           gpointer      user_data)
{
  GVariant *state_variant;

  state_variant = g_value_get_variant (from_value);
  if (g_variant_is_of_type (state_variant, G_VARIANT_TYPE_BOOLEAN))
    {
      g_value_set_boolean (to_value, g_variant_get_boolean (state_variant));
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static void
bind_action_to_property (MdkApplication *app,
                         const char     *action_name,
                         gpointer        object,
                         const char     *property)
{
  GAction *action;
  GParamSpec *pspec;

  action = g_action_map_lookup_action (G_ACTION_MAP (app), action_name);
  g_return_if_fail (action);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), property);

  g_object_bind_property_full (action, "state", object, property,
                               G_BINDING_SYNC_CREATE,
                               transform_action_state_to,
                               NULL,
                               g_param_spec_ref (pspec),
                               (GDestroyNotify) g_param_spec_unref);
}

static void
on_context_closed (MdkContext     *context,
                   GtkApplication *app)
{
  g_application_quit (G_APPLICATION (app));
}

static void
mdk_application_dispose (GObject *object)
{
  MdkApplication *app = MDK_APPLICATION (object);

  g_clear_object (&app->context);

  G_OBJECT_CLASS (mdk_application_parent_class)->dispose (object);
}

static void
mdk_application_class_init (MdkApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = mdk_application_dispose;
}

static void
mdk_application_init (MdkApplication *app)
{
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MdkApplication) app = NULL;
  static GActionEntry app_entries[] = {
    { "about", activate_about, NULL, NULL, NULL },
    { "toggle_emulate_touch", .state = "false", },
    { "toggle_inhibit_system_shortcuts", .state = "false", },
    { "launch", activate_launch, .parameter_type = "i", },
    { "edit_launchers", activate_edit_launchers, },
  };

  app = g_object_new (MDK_TYPE_APPLICATION,
                      "application-id", "org.gnome.Mutter.Mdk",
                      "flags", G_APPLICATION_NON_UNIQUE,
                      NULL);
  app->context = mdk_context_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_entries, G_N_ELEMENTS (app_entries),
                                   app);

  g_application_set_version (G_APPLICATION (app), VERSION);

  bind_action_to_property (app, "toggle_emulate_touch",
                           app->context, "emulate-touch");
  bind_action_to_property (app, "toggle_inhibit_system_shortcuts",
                           app->context, "inhibit-system-shortcuts");

  g_signal_connect (app, "startup", G_CALLBACK (startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  g_signal_connect (app->context, "closed", G_CALLBACK (on_context_closed), app);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
