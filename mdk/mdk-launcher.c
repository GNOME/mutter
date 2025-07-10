/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include "mdk-launcher.h"

#include "mdk-context.h"

struct _MdkLauncherAction
{
  char *id;
  char *name;
};

typedef struct _MdkLauncher
{
  MdkContext *context;

  MdkLauncherType type;
  int id;

  union {
    struct {
      GDesktopAppInfo *app_info;
      GPtrArray *actions;
      MdkLauncherAction *action;
    } desktop;
    struct {
      char *value;
      GStrv argv;
    } exec;
  };
} MdkLauncher;

static void
mdk_launcher_action_free (MdkLauncherAction *action)
{
  g_free (action->id);
  g_free (action->name);
  g_free (action);
}

const char *
mdk_launcher_action_get_name (MdkLauncherAction *action)
{
  return action->name;
}

const char *
mdk_launcher_action_get_id (MdkLauncherAction *action)
{
  return action->id;
}

static void
update_actions (MdkLauncher *launcher,
                const char  *configured_action)
{
  const char * const * action_ids;
  GPtrArray *actions;
  size_t i;

  actions = g_ptr_array_new_with_free_func ((GDestroyNotify) mdk_launcher_action_free);

  launcher->desktop.action = NULL;

  action_ids = g_desktop_app_info_list_actions (launcher->desktop.app_info);
  for (i = 0; action_ids[i]; i++)
    {
      const char *action_id = action_ids[i];
      MdkLauncherAction *action;

      action = g_new0 (MdkLauncherAction, 1);
      action->id = g_strdup (action_id);
      action->name =
        g_desktop_app_info_get_action_name (launcher->desktop.app_info,
                                            action_id);

      g_ptr_array_add (actions, action);

      if (g_strcmp0 (action->id, configured_action) == 0)
        launcher->desktop.action = action;
    }

  launcher->desktop.actions = actions;
}

MdkLauncher *
mdk_launcher_new_desktop (MdkContext      *context,
                          int              id,
                          GDesktopAppInfo *app_info,
                          const char      *action)
{
  MdkLauncher *launcher;

  launcher = g_new0 (MdkLauncher, 1);
  launcher->context = context;
  launcher->id = id;
  launcher->type = MDK_LAUNCHER_TYPE_DESKTOP;
  launcher->desktop.app_info = g_steal_pointer (&app_info);

  update_actions (launcher, action);

  return launcher;
}

MdkLauncher *
mdk_launcher_new_exec (MdkContext *context,
                       int         id,
                       const char *value,
                       GStrv       argv)
{
  MdkLauncher *launcher;

  launcher = g_new0 (MdkLauncher, 1);
  launcher->context = context;
  launcher->id = id;
  launcher->type = MDK_LAUNCHER_TYPE_EXEC;
  launcher->exec.value = g_strdup (value);
  launcher->exec.argv = g_steal_pointer (&argv);

  return launcher;
}

void
mdk_launcher_free (MdkLauncher *launcher)
{
  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      g_clear_pointer (&launcher->desktop.actions, g_ptr_array_unref);
      g_clear_object (&launcher->desktop.app_info);
      break;
    case MDK_LAUNCHER_TYPE_EXEC:
      g_clear_pointer (&launcher->exec.value, g_free);
      g_clear_pointer (&launcher->exec.argv, g_strfreev);
      break;
    }
  g_free (launcher);
}

MdkContext *
mdk_launcher_get_context (MdkLauncher *launcher)
{
  return launcher->context;
}

const char *
mdk_launcher_get_name (MdkLauncher *launcher)
{
  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      return g_app_info_get_display_name (G_APP_INFO (launcher->desktop.app_info));
    case MDK_LAUNCHER_TYPE_EXEC:
      return launcher->exec.argv[0];
    }
  g_assert_not_reached ();
}

char *
mdk_launcher_get_action (MdkLauncher *launcher)
{
  return g_strdup_printf ("app.launch(%d)", launcher->id);
}

static void
override_environ (GAppLaunchContext *launch_context,
                  GStrv              env)
{
  size_t i;

  for (i = 0; env[i]; i++)
    {
      char *equal_sign;
      g_autofree char *name = NULL;
      g_autofree char *value = NULL;

      equal_sign = strchr (env[i], '=');
      name = g_strndup (env[i], equal_sign - env[i]);
      value = g_strdup (equal_sign + 1);

      g_app_launch_context_setenv (launch_context, name, value);
    }
}

void
mdk_launcher_activate (MdkLauncher *launcher)
{
  g_autoptr (GError) error = NULL;
  GStrv launch_env = mdk_context_get_launch_env (launcher->context);

  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      {
        g_autoptr (GAppLaunchContext) launch_context = NULL;
        GAppInfo *app_info = G_APP_INFO (launcher->desktop.app_info);
        MdkLauncherAction *action;

        launch_context = g_app_launch_context_new ();
        override_environ (launch_context, launch_env);

        action = launcher->desktop.action;
        if (action)
          {
            g_desktop_app_info_launch_action (G_DESKTOP_APP_INFO (app_info),
                                              action->id,
                                              launch_context);
          }
        else
          {
            if (!g_app_info_launch (app_info, NULL, launch_context, &error))
              {
                g_warning ("Failed to launch %s: %s",
                           g_app_info_get_display_name (app_info),
                           error->message);
              }
          }
        break;
      }
    case MDK_LAUNCHER_TYPE_EXEC:
      if (!g_spawn_async (NULL,
                          (char **) launcher->exec.argv,
                          launch_env,
                          (G_SPAWN_SEARCH_PATH |
                           G_SPAWN_DO_NOT_REAP_CHILD),
                          NULL, NULL, NULL,
                          &error))
        {
          g_warning ("Failed to run %s: %s",
                     launcher->exec.argv[0],
                     error->message);
        }
      break;
    }
}

GPtrArray *
mdk_launcher_get_actions (MdkLauncher *launcher)
{
  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      return launcher->desktop.actions;
    case MDK_LAUNCHER_TYPE_EXEC:
      return NULL;
    }
  g_assert_not_reached ();
}

MdkLauncherAction *
mdk_launcher_get_configured_action (MdkLauncher *launcher)
{
  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      return launcher->desktop.action;
    case MDK_LAUNCHER_TYPE_EXEC:
      return NULL;
    }
  g_assert_not_reached ();
}

char *
mdk_get_app_id_from_app_info (GAppInfo *app_info)
{
  const char *desktop_file_name;
  size_t new_len;

  desktop_file_name = g_app_info_get_id (app_info);

  g_return_val_if_fail (g_str_has_suffix (desktop_file_name, ".desktop"), NULL);

  new_len = strlen (desktop_file_name) - strlen (".desktop");
  return g_strndup (desktop_file_name, new_len);
}

char *
mdk_launcher_get_desktop_app_id (MdkLauncher *launcher)
{
  g_return_val_if_fail (launcher->type == MDK_LAUNCHER_TYPE_DESKTOP, NULL);

  return mdk_get_app_id_from_app_info (G_APP_INFO (launcher->desktop.app_info));
}

MdkLauncherType
mdk_launcher_get_type (MdkLauncher *launcher)
{
  return launcher->type;
}

GIcon *
mdk_launcher_get_icon (MdkLauncher *launcher)
{
  switch (launcher->type)
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      return g_app_info_get_icon (G_APP_INFO (launcher->desktop.app_info));
    case MDK_LAUNCHER_TYPE_EXEC:
      return NULL;
    }
  g_assert_not_reached ();
}

GAppInfo *
mdk_launcher_get_app_info (MdkLauncher *launcher)
{
  g_return_val_if_fail (launcher->type == MDK_LAUNCHER_TYPE_DESKTOP, NULL);

  return G_APP_INFO (launcher->desktop.app_info);
}

GStrv
mdk_launcher_get_argv (MdkLauncher *launcher)
{
  return launcher->exec.argv;
}

const char *
mdk_launcher_get_command_line (MdkLauncher *launcher)
{
  return launcher->exec.value;
}
