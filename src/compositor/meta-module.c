/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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

#include "compositor/meta-module.h"

#include <gmodule.h>

#include "meta/meta-plugin.h"

enum
{
  PROP_0,
  PROP_PATH,
};

struct _MetaModule
{
  GTypeModule parent_instance;

  GModule *lib;
  gchar *path;
  GType plugin_type;
};

G_DEFINE_FINAL_TYPE (MetaModule, meta_module, G_TYPE_TYPE_MODULE);

static gboolean
meta_module_load (GTypeModule *gmodule)
{
  MetaModule *module = META_MODULE (gmodule);
  GType (*register_type) (GTypeModule *) = NULL;

  if (module->lib && module->plugin_type)
    return TRUE;

  g_assert (module->path);

  if (!module->lib &&
      !(module->lib = g_module_open (module->path, 0)))
    {
      g_warning ("Could not load library [%s (%s)]",
                 module->path, g_module_error ());
      return FALSE;
    }

  if (g_module_symbol (module->lib, "meta_plugin_register_type",
                       (gpointer *)(void *)&register_type) &&
      register_type)
    {
      GType plugin_type;

      if (!(plugin_type = register_type (gmodule)))
        {
          g_warning ("Could not register type for plugin %s",
                     module->path);
          return FALSE;
        }
      else
        {
          module->plugin_type = plugin_type;
        }

      return TRUE;
    }
  else
    g_warning ("Broken plugin module [%s]", module->path);

  return FALSE;
}

static void
meta_module_unload (GTypeModule *gmodule)
{
  MetaModule *module = META_MODULE (gmodule);

  g_module_close (module->lib);

  module->lib = NULL;
  module->plugin_type = 0;
}

static void
meta_module_finalize (GObject *object)
{
  MetaModule *module = META_MODULE (object);

  g_free (module->path);
  module->path = NULL;

  G_OBJECT_CLASS (meta_module_parent_class)->finalize (object);
}

static void
meta_module_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaModule *module = META_MODULE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_free (module->path);
      module->path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_module_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaModule *module = META_MODULE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, module->path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_module_class_init (MetaModuleClass *klass)
{
  GObjectClass     *gobject_class = G_OBJECT_CLASS (klass);
  GTypeModuleClass *gmodule_class = G_TYPE_MODULE_CLASS (klass);

  gobject_class->finalize     = meta_module_finalize;
  gobject_class->set_property = meta_module_set_property;
  gobject_class->get_property = meta_module_get_property;

  gmodule_class->load         = meta_module_load;
  gmodule_class->unload       = meta_module_unload;

  g_object_class_install_property (gobject_class,
				   PROP_PATH,
				   g_param_spec_string ("path", NULL, NULL,
							NULL,
							G_PARAM_READWRITE |
						      G_PARAM_CONSTRUCT_ONLY));
}

static void
meta_module_init (MetaModule *self)
{
}

GType
meta_module_get_plugin_type (MetaModule *module)
{
  return module->plugin_type;
}

