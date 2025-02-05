/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2024 GNOME Foundation
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
 *
 */

#include "config.h"

#include "backends/meta-a11y-manager.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"
#include "meta/util.h"

#include "meta-dbus-a11y.h"

enum
{
  A11Y_MODIFIERS_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_0,
  PROP_BACKEND,
  N_PROPS,
};

static GParamSpec *props[N_PROPS];

typedef struct _MetaA11yManager
{
  GObject parent;
  MetaBackend *backend;
} MetaA11yManager;

G_DEFINE_TYPE (MetaA11yManager, meta_a11y_manager, G_TYPE_OBJECT)

static void
meta_a11y_manager_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaA11yManager *a11y_manager = META_A11Y_MANAGER (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      a11y_manager->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_a11y_manager_class_init (MetaA11yManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_a11y_manager_set_property;

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_a11y_manager_init (MetaA11yManager *a11y_manager)
{
}

MetaA11yManager *
meta_a11y_manager_new (MetaBackend *backend)
{
  return g_object_new (META_TYPE_A11Y_MANAGER,
                       "backend", backend,
                       NULL);
}
