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

#include "mdk-launcher-action-item.h"

struct _MdkLauncherActionItem
{
  GObject parent;

  char *name;
  MdkLauncherAction *action;
};

G_DEFINE_FINAL_TYPE (MdkLauncherActionItem, mdk_launcher_action_item,
                     G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_NAME,
  PROP_ACTION,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

static void
mdk_launcher_action_item_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MdkLauncherActionItem *item = MDK_LAUNCHER_ACTION_ITEM (object);

  switch (prop_id)
    {
    case PROP_NAME:
      item->name = g_value_dup_string (value);
      break;
    case PROP_ACTION:
      item->action = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launcher_action_item_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MdkLauncherActionItem *item = MDK_LAUNCHER_ACTION_ITEM (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, item->name);
      break;
    case PROP_ACTION:
      g_value_set_pointer (value, item->action);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_launcher_action_item_dispose (GObject *object)
{
  MdkLauncherActionItem *item = MDK_LAUNCHER_ACTION_ITEM (object);

  g_free (item->name);

  G_OBJECT_CLASS (mdk_launcher_action_item_parent_class)->dispose (object);
}

static void
mdk_launcher_action_item_class_init (MdkLauncherActionItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_launcher_action_item_set_property;
  object_class->get_property = mdk_launcher_action_item_get_property;
  object_class->dispose = mdk_launcher_action_item_dispose;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);
  obj_props[PROP_ACTION] =
    g_param_spec_pointer ("action", NULL, NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
mdk_launcher_action_item_init (MdkLauncherActionItem *item)
{
}

MdkLauncherActionItem *
mdk_launcher_action_item_new (const char        *name,
                              MdkLauncherAction *action)
{
  return g_object_new (MDK_TYPE_LAUNCHER_ACTION_ITEM,
                       "name", name,
                       "action", action,
                       NULL);
}

const char *
mdk_launcher_action_item_get_name (MdkLauncherActionItem *item)
{
  return item->name;
}

MdkLauncherAction *
mdk_launcher_action_item_get_action (MdkLauncherActionItem *item)
{
  return item->action;
}
