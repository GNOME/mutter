/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "mdk-context.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <stdio.h>

#include "mdk-launcher.h"
#include "mdk-pipewire.h"
#include "mdk-seat.h"
#include "mdk-session.h"

#include "mdk-dbus-devkit.h"

enum
{
  PROP_0,

  PROP_EMULATE_TOUCH,
  PROP_INHIBIT_SYSTEM_SHORTCUTS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  READY,
  ERROR,
  CLOSED,
  LAUNCHERS_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MdkContext
{
  GObject parent;

  MdkPipewire *pipewire;
  MdkSession *session;

  guint name_watcher_id;

  MdkDBusDevkit *devkit_proxy;

  gboolean emulate_touch;
  gboolean inhibit_system_shortcuts;

  GSettings *settings;

  GStrv launch_env;
  GPtrArray *launchers;
};

G_DEFINE_FINAL_TYPE (MdkContext, mdk_context, G_TYPE_OBJECT)

static void
update_active_input_devices (MdkContext *context)
{
  MdkSession *session;
  MdkSeat *seat;

  session = context->session;
  if (!session)
    return;

  seat = mdk_session_get_default_seat (session);

  if (context->emulate_touch)
    {
      mdk_seat_bind_touch (seat);
      mdk_seat_unbind_pointer (seat);
      mdk_seat_unbind_keyboard (seat);
    }
  else
    {
      mdk_seat_unbind_touch (seat);
      mdk_seat_bind_pointer (seat);
      mdk_seat_bind_keyboard (seat);
    }
}

static void
mdk_context_set_emulate_touch (MdkContext *context,
                               gboolean    emulate_touch)
{
  if (context->emulate_touch == emulate_touch)
    return;

  context->emulate_touch = emulate_touch;

  update_active_input_devices (context);
}

static const char *
launcher_type_to_string (MdkLauncherType launcher_type)
{
  switch (launcher_type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      return "desktop";
    case MDK_LAUNCHER_TYPE_EXEC:
      return "exec";
    }
  g_assert_not_reached ();
}

void
mdk_context_add_launcher (MdkContext      *context,
                          MdkLauncherType  launcher_type,
                          const char      *value,
                          const char      *option)
{
  g_autoptr (GVariant) launchers_variant = NULL;
  GVariantIter iter;
  GVariantBuilder launchers_builder;
  const char *existing_type;
  const char *existing_value;
  const char *existing_option;

  launchers_variant = g_settings_get_value (context->settings, "launchers");
  g_variant_iter_init (&iter, launchers_variant);

  g_variant_builder_init (&launchers_builder, G_VARIANT_TYPE ("a(sss)"));

  while (g_variant_iter_next (&iter, "(&s&s&s)",
                              &existing_type,
                              &existing_value,
                              &existing_option))
    {
      g_variant_builder_add (&launchers_builder, "(sss)",
                             existing_type, existing_value, existing_option);
    }
  g_variant_builder_add (&launchers_builder, "(sss)",
                         launcher_type_to_string (launcher_type),
                         value,
                         option);

  g_settings_set_value (context->settings, "launchers",
                        g_variant_builder_end (&launchers_builder));
}

void
mdk_context_remove_launcher (MdkContext      *context,
                             MdkLauncherType  launcher_type,
                             const char      *value,
                             const char      *option)
{
  g_autoptr (GVariant) launchers_variant = NULL;
  GVariantIter iter;
  GVariantBuilder launchers_builder;
  const char *existing_type;
  const char *existing_value;
  const char *existing_option;

  launchers_variant = g_settings_get_value (context->settings, "launchers");
  g_variant_iter_init (&iter, launchers_variant);

  g_variant_builder_init (&launchers_builder, G_VARIANT_TYPE ("a(sss)"));

  while (g_variant_iter_next (&iter, "(&s&s&s)",
                              &existing_type,
                              &existing_value,
                              &existing_option))
    {
      if (g_strcmp0 (existing_type,
                     launcher_type_to_string (launcher_type)) == 0 &&
          g_strcmp0 (existing_value, value) == 0 &&
          g_strcmp0 (existing_option, option) == 0)
        continue;

      g_variant_builder_add (&launchers_builder, "(sss)",
                             existing_type, existing_value, existing_option);
    }

  g_settings_set_value (context->settings, "launchers",
                        g_variant_builder_end (&launchers_builder));
}

void
mdk_context_set_launcher_action (MdkContext *context,
                                 const char *app_id,
                                 const char *action_id)
{
  g_autoptr (GVariant) launchers_variant = NULL;
  GVariantIter iter;
  GVariantBuilder launchers_builder;
  const char *type;
  const char *value;
  const char *option;

  launchers_variant = g_settings_get_value (context->settings, "launchers");
  g_variant_iter_init (&iter, launchers_variant);

  g_variant_builder_init (&launchers_builder, G_VARIANT_TYPE ("a(sss)"));

  while (g_variant_iter_next (&iter, "(&s&s&s)", &type, &value, &option))
    {
      if (g_strcmp0 (type, "desktop") == 0 &&
          g_strcmp0 (value, app_id) == 0)
        {
          g_variant_builder_add (&launchers_builder, "(sss)",
                                 type, value, action_id);
        }
      else
        {
          g_variant_builder_add (&launchers_builder, "(sss)",
                                 type, value, option);
        }
    }

  g_settings_set_value (context->settings, "launchers",
                        g_variant_builder_end (&launchers_builder));
}

static void
update_launchers (MdkContext *context)
{
  g_autoptr (GVariant) launchers_variant = NULL;
  GVariantIter iter;
  const char *type;
  const char *value;
  const char *option;
  int id_counter = 0;

  launchers_variant = g_settings_get_value (context->settings, "launchers");
  g_variant_iter_init (&iter, launchers_variant);

  g_ptr_array_remove_range (context->launchers, 0, context->launchers->len);

  while (g_variant_iter_next (&iter, "(&s&s&s)", &type, &value, &option))
    {
      MdkLauncher *launcher = NULL;

      if (g_strcmp0 (type, "desktop") == 0)
        {
          g_autofree char *desktop_id = NULL;
          g_autoptr (GDesktopAppInfo) app_info = NULL;

          desktop_id = g_strconcat (value, ".desktop", NULL);

          app_info = g_desktop_app_info_new (desktop_id);
          if (!app_info)
            {
              g_warning ("Invalid application ID '%s'", value);
              continue;
            }

          launcher =
            mdk_launcher_new_desktop (context,
                                      id_counter++,
                                      g_steal_pointer (&app_info),
                                      option);
        }
      else if (g_strcmp0 (type, "exec") == 0)
        {
          g_autoptr (GError) error = NULL;
          g_auto (GStrv) argv = NULL;

          if (!g_shell_parse_argv (value, NULL, &argv, &error))
            {
              g_warning ("Invalid command line '%s': %s",
                         value, error->message);
              continue;
            }

          launcher = mdk_launcher_new_exec (context,
                                            id_counter++,
                                            value,
                                            g_steal_pointer (&argv));
        }

      if (launcher)
        g_ptr_array_add (context->launchers, launcher);
    }

  g_signal_emit (context, signals[LAUNCHERS_CHANGED], 0);
}

static void
on_settings_changed (GSettings  *settings,
                     const char *key,
                     MdkContext *context)
{
  if (g_strcmp0 (key, "launchers") == 0)
    update_launchers (context);
}

static void
mdk_context_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MdkContext *context = MDK_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_EMULATE_TOUCH:
      mdk_context_set_emulate_touch (context, g_value_get_boolean (value));
      break;
    case PROP_INHIBIT_SYSTEM_SHORTCUTS:
      context->inhibit_system_shortcuts = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_context_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MdkContext *context = MDK_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_EMULATE_TOUCH:
      g_value_set_boolean (value, context->emulate_touch);
      break;
    case PROP_INHIBIT_SYSTEM_SHORTCUTS:
      g_value_set_boolean (value, context->inhibit_system_shortcuts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_context_finalize (GObject *object)
{
  MdkContext *context = MDK_CONTEXT (object);

  g_clear_pointer (&context->launchers, g_ptr_array_unref);
  g_clear_pointer (&context->launch_env, g_strfreev);
  g_clear_object (&context->devkit_proxy);
  g_clear_object (&context->session);
  g_clear_object (&context->settings);
  g_clear_handle_id (&context->name_watcher_id, g_bus_unwatch_name);

  G_OBJECT_CLASS (mdk_context_parent_class)->finalize (object);
}

static void
mdk_context_class_init (MdkContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_context_set_property;
  object_class->get_property = mdk_context_get_property;
  object_class->finalize = mdk_context_finalize;

  obj_props[PROP_EMULATE_TOUCH] =
    g_param_spec_boolean ("emulate-touch", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);
  obj_props[PROP_INHIBIT_SYSTEM_SHORTCUTS] =
    g_param_spec_boolean ("inhibit-system-shortcuts", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[READY] = g_signal_new ("ready",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 0);
  signals[ERROR] = g_signal_new ("error",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 1,
                                 G_TYPE_ERROR);
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
  signals[LAUNCHERS_CHANGED] =
    g_signal_new ("launchers-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
set_launch_env (MdkContext *context,
                GVariant   *env)
{
  g_auto (GStrv) launch_env = NULL;
  GVariantIter iter;
  g_autoptr (GStrvBuilder) strv_builder = NULL;
  char *name;
  char *value;

  launch_env = g_strdupv (environ);

  g_variant_iter_init (&iter, env);
  while (g_variant_iter_next (&iter, "{&s&s}", &name, &value))
    launch_env = g_environ_setenv (launch_env, name, value, TRUE);

  context->launch_env = g_steal_pointer (&launch_env);
}

static gboolean
init_launch_environment (MdkContext  *context,
                         GError     **error)
{
  GVariant *env;

  context->devkit_proxy =
    mdk_dbus_devkit_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                            "org.gnome.Mutter.Devkit",
                                            "/org/gnome/Mutter/Devkit",
                                            NULL,
                                            error);

  if (!context->devkit_proxy)
    return FALSE;

  env = mdk_dbus_devkit_get_env (context->devkit_proxy);
  if (!env)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No launch environment available");
      return FALSE;
    }

  set_launch_env (context, env);

  return TRUE;
}

static void
mdk_context_init (MdkContext *context)
{
  g_autoptr (GError) error = NULL;

  if (!init_launch_environment (context, &error))
    g_warning ("Failed to initialize launch environment: %s", error->message);

  context->launchers =
    g_ptr_array_new_with_free_func ((GDestroyNotify) mdk_launcher_free);

  context->settings = g_settings_new ("org.gnome.mutter.devkit");
  g_signal_connect (context->settings, "changed",
                    G_CALLBACK (on_settings_changed), context);
  update_launchers (context);
}

static void
on_session_closed (MdkSession *session,
                   MdkContext *context)
{
  g_signal_emit (context, signals[CLOSED], 0);
}

static void
init_session (MdkContext *context)
{
  g_autoptr (GError) error = NULL;
  MdkSession *session;

  session = g_initable_new (MDK_TYPE_SESSION,
                            NULL, &error,
                            "context", context,
                            NULL);
  if (!session)
    {
      g_signal_emit (context, signals[ERROR], 0, error);
      return;
    }

  g_debug ("Session is ready");

  context->session = session;
  update_active_input_devices (context);

  g_signal_connect (context->session, "closed",
                    G_CALLBACK (on_session_closed), context);

  g_signal_emit (context, signals[READY], 0);
}

MdkContext *
mdk_context_new (void)
{
  MdkContext *context;

  context = g_object_new (MDK_TYPE_CONTEXT, NULL);

  return context;
}

static void
remote_desktop_name_appeared_cb (GDBusConnection *connection,
                                 const char      *name,
                                 const char      *name_owner,
                                 gpointer         user_data)
{
}

static void
remote_desktop_name_vanished_cb (GDBusConnection *connection,
                                 const char      *name,
                                 gpointer         user_data)
{
  MdkContext *context = MDK_CONTEXT (user_data);

  g_signal_emit (context, signals[CLOSED], 0);
}

void
mdk_context_activate (MdkContext *context)
{
  g_autoptr (GError) error = NULL;

  context->pipewire = mdk_pipewire_new (context, &error);
  if (!context->pipewire)
    {
      g_signal_emit (context, signals[ERROR], 0, error);
      return;
    }

  init_session (context);

  setsid ();

  context->name_watcher_id =
    g_bus_watch_name (G_BUS_TYPE_SESSION,
                      "org.gnome.Mutter.RemoteDesktop",
                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                      remote_desktop_name_appeared_cb,
                      remote_desktop_name_vanished_cb,
                      context, NULL);
}

MdkSession *
mdk_context_get_session (MdkContext *context)
{
  return context->session;
}

MdkPipewire *
mdk_context_get_pipewire (MdkContext *context)
{
  return context->pipewire;
}

gboolean
mdk_context_get_emulate_touch (MdkContext *context)
{
  return context->emulate_touch;
}

gboolean
mdk_context_get_inhibit_system_shortcuts (MdkContext *context)
{
  return context->inhibit_system_shortcuts;
}

GPtrArray *
mdk_context_get_launchers (MdkContext *context)
{
  return context->launchers;
}

void
mdk_context_activate_launcher (MdkContext *context,
                               int         id)
{
  MdkLauncher *launcher;

  g_return_if_fail (id >= 0);
  g_return_if_fail (id < context->launchers->len);

  launcher = g_ptr_array_index (context->launchers, id);
  mdk_launcher_activate (launcher);
}

GStrv
mdk_context_get_launch_env (MdkContext *context)
{
  return context->launch_env;
}
