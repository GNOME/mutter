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

#include "mdk-window.h"

#include "mdk-context.h"

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MdkWindowPrivate
{
  MdkContext *context;
} MdkWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MdkWindow, mdk_window, GTK_TYPE_APPLICATION_WINDOW)

static void
mdk_window_dispose (GObject *object)
{
  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_WINDOW);

  G_OBJECT_CLASS (mdk_window_parent_class)->dispose (object);
}

static void
mdk_window_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  MdkWindow *window = MDK_WINDOW (object);
  MdkWindowPrivate *priv = mdk_window_get_instance_private (window);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MdkWindow *window = MDK_WINDOW (object);
  MdkWindowPrivate *priv = mdk_window_get_instance_private (window);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, priv->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_window_class_init (MdkWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = mdk_window_dispose;
  object_class->set_property = mdk_window_set_property;
  object_class->get_property = mdk_window_get_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-window.ui");

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 MDK_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
mdk_window_init (MdkWindow *window)
{
  gtk_widget_init_template (GTK_WIDGET (window));
}
