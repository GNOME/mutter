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

#include "mdk-launcher-entry.h"

#include <glib/gi18n-lib.h>

#include "mdk-context.h"
#include "mdk-launcher-action-item.h"
#include "mdk-launcher.h"

enum
{
  PROP_0,

  PROP_LAUNCHER,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MdkLauncherEntry
{
  AdwExpanderRow parent;

  MdkLauncher *launcher;

  GtkWidget *icon;
  GtkWidget *delete_button;
  AdwComboRow *actions;
  GListStore *actions_list;
};

G_DEFINE_TYPE (MdkLauncherEntry, mdk_launcher_entry, ADW_TYPE_EXPANDER_ROW)

static void
on_action_selected (AdwComboRow      *actions,
                    GParamSpec       *pspec,
                    MdkLauncherEntry *launcher_entry)
{
  MdkLauncherActionItem *item =
    MDK_LAUNCHER_ACTION_ITEM (adw_combo_row_get_selected_item (actions));
  MdkLauncher *launcher = launcher_entry->launcher;
  MdkContext *context = mdk_launcher_get_context (launcher);
  MdkLauncherAction *action;
  g_autofree char *app_id = NULL;

  action = mdk_launcher_action_item_get_action (item);
  if (!action)
    return;

  app_id = mdk_launcher_get_desktop_app_id (launcher);
  mdk_context_set_launcher_action (context,
                                   app_id,
                                   mdk_launcher_action_get_id (action));
}

static void
on_delete_clicked (MdkLauncherEntry *launcher_entry)
{
  MdkLauncher *launcher = launcher_entry->launcher;
  MdkContext *context = mdk_launcher_get_context (launcher);
  MdkLauncherAction *action;
  g_autofree char *value = NULL;
  const char *option = "";

  switch (mdk_launcher_get_type (launcher_entry->launcher))
    {
    case MDK_LAUNCHER_TYPE_DESKTOP:
      action = mdk_launcher_get_configured_action (launcher);

      value = mdk_launcher_get_desktop_app_id (launcher);
      option = action ? mdk_launcher_action_get_id (action) : "";
      break;
    case MDK_LAUNCHER_TYPE_EXEC:
      value = g_strdup (mdk_launcher_get_command_line (launcher));
      break;
    }

  mdk_context_remove_launcher (context,
                               mdk_launcher_get_type (launcher),
                               value, option);
}

static void
mdk_launcher_entry_dispose (GObject *object)
{
  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_LAUNCHER_ENTRY);

  G_OBJECT_CLASS (mdk_launcher_entry_parent_class)->dispose (object);
}

static void
mdk_launcher_entry_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MdkLauncherEntry *launcher_entry = MDK_LAUNCHER_ENTRY (object);

  switch (prop_id)
    {
    case PROP_LAUNCHER:
      launcher_entry->launcher = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launcher_entry_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MdkLauncherEntry *launcher_entry = MDK_LAUNCHER_ENTRY (object);

  switch (prop_id)
    {
    case PROP_LAUNCHER:
      g_value_set_pointer (value, launcher_entry->launcher);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launcher_entry_constructed (GObject *object)
{
  MdkLauncherEntry *launcher_entry = MDK_LAUNCHER_ENTRY (object);
  MdkLauncher *launcher = launcher_entry->launcher;
  g_autoptr (GIcon) icon = NULL;
  GPtrArray *actions_list;
  g_autoptr (GtkExpression) expression = NULL;

  g_object_set (object,
                "title", mdk_launcher_get_name (launcher),
                NULL);

  g_set_object (&icon, mdk_launcher_get_icon (launcher));
  if (!icon)
    icon = g_themed_icon_new ("application-x-executable");
  gtk_image_set_from_gicon (GTK_IMAGE (launcher_entry->icon), icon);

  actions_list = mdk_launcher_get_actions (launcher);
  if (actions_list && actions_list->len > 0)
    {
      MdkLauncherAction *configured_action;
      size_t selected_idx = 0;
      size_t i;

      configured_action = mdk_launcher_get_configured_action (launcher);

      for (i = 0; i < actions_list->len; i++)
        {
          MdkLauncherAction *action = g_ptr_array_index (actions_list, i);
          const char *name = mdk_launcher_action_get_name (action);
          g_autoptr (MdkLauncherActionItem) item = NULL;

          item = mdk_launcher_action_item_new (name, action);
          g_list_store_append (launcher_entry->actions_list, item);

          if (configured_action == action)
            selected_idx = i;
        }

      adw_combo_row_set_selected (launcher_entry->actions, selected_idx);

      g_signal_connect (launcher_entry->actions, "notify::selected-item",
                        G_CALLBACK (on_action_selected),
                        launcher_entry);
    }
  else
    {
      g_autoptr (MdkLauncherActionItem) item = NULL;

      item = mdk_launcher_action_item_new (_("Run"), NULL);
      g_list_store_append (launcher_entry->actions_list, item);
    }

  expression =
    gtk_property_expression_new (MDK_TYPE_LAUNCHER_ACTION_ITEM,
                                 NULL,
                                 "name");
  adw_combo_row_set_expression (launcher_entry->actions, expression);

  g_signal_connect_swapped (launcher_entry->delete_button,
                            "clicked",
                            G_CALLBACK (on_delete_clicked),
                            launcher_entry);

  G_OBJECT_CLASS (mdk_launcher_entry_parent_class)->constructed (object);
}

static void
mdk_launcher_entry_class_init (MdkLauncherEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = mdk_launcher_entry_dispose;
  object_class->set_property = mdk_launcher_entry_set_property;
  object_class->get_property = mdk_launcher_entry_get_property;
  object_class->constructed = mdk_launcher_entry_constructed;

  obj_props[PROP_LAUNCHER] = g_param_spec_pointer ("launcher", NULL, NULL,
                                                   G_PARAM_CONSTRUCT_ONLY |
                                                   G_PARAM_READWRITE |
                                                   G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  g_type_ensure (MDK_TYPE_LAUNCHER_ACTION_ITEM);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-launcher-entry.ui");
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherEntry,
                                        icon);
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherEntry,
                                        delete_button);
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherEntry,
                                        actions);
  gtk_widget_class_bind_template_child (widget_class, MdkLauncherEntry,
                                        actions_list);
}

static void
mdk_launcher_entry_init (MdkLauncherEntry *launcher_entry)
{
  gtk_widget_init_template (GTK_WIDGET (launcher_entry));
}
