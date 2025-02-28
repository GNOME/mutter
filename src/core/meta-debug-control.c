/*
 * Copyright (C) 2023 Red Hat
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
 *
 */

#include "config.h"

#include "core/meta-debug-control-private.h"

#include "core/util-private.h"
#include "meta/meta-backend.h"
#include "meta/meta-context.h"

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_EXPORTED,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

#define META_DEBUG_CONTROL_DBUS_SERVICE "org.gnome.Mutter.DebugControl"
#define META_DEBUG_CONTROL_DBUS_PATH "/org/gnome/Mutter/DebugControl"

struct _MetaDebugControl
{
  MetaDBusDebugControlSkeleton parent;

  MetaContext *context;
  gboolean exported;

  guint dbus_name_id;
};

static void meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaDebugControl,
                         meta_debug_control,
                         META_DBUS_TYPE_DEBUG_CONTROL_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_DEBUG_CONTROL,
                                                meta_dbus_debug_control_iface_init))

static void
meta_dbus_debug_control_iface_init (MetaDBusDebugControlIface *iface)
{
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (user_data);
  g_autoptr (GError) error = NULL;

  meta_topic (META_DEBUG_BACKEND,
              "Acquired D-Bus name '%s', exporting service on '%s'",
              META_DEBUG_CONTROL_DBUS_SERVICE, META_DEBUG_CONTROL_DBUS_PATH);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (debug_control),
                                         connection,
                                         META_DEBUG_CONTROL_DBUS_PATH,
                                         &error))
    {
      g_warning ("Failed to export '%s' object on '%s': %s",
                 META_DEBUG_CONTROL_DBUS_SERVICE,
                 META_DEBUG_CONTROL_DBUS_PATH,
                 error->message);
    }
}

static void
meta_debug_control_dispose (GObject *object)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  g_clear_handle_id (&debug_control->dbus_name_id, g_bus_unown_name);

  G_OBJECT_CLASS (meta_debug_control_parent_class)->dispose (object);
}

static void
meta_debug_control_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      debug_control->context = g_value_get_object (value);
      break;
    case PROP_EXPORTED:
      meta_debug_control_set_exported (debug_control,
                                       g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaDebugControl *debug_control = META_DEBUG_CONTROL (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, debug_control->context);
      break;
    case PROP_EXPORTED:
      g_value_set_boolean (value, debug_control->exported);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_debug_control_class_init (MetaDebugControlClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_debug_control_dispose;
  object_class->set_property = meta_debug_control_set_property;
  object_class->get_property = meta_debug_control_get_property;

  obj_props[PROP_CONTEXT] = g_param_spec_object ("context", NULL, NULL,
                                                 META_TYPE_CONTEXT,
                                                 G_PARAM_CONSTRUCT_ONLY |
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_STATIC_STRINGS);
  obj_props[PROP_EXPORTED] = g_param_spec_boolean ("exported", NULL, NULL,
                                                   FALSE,
                                                   G_PARAM_READWRITE |
                                                   G_PARAM_EXPLICIT_NOTIFY |
                                                   G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_debug_control_init (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);
  gboolean force_hdr, force_linear_blending;
  gboolean session_management_protocol;
  gboolean inhibit_hw_cursor;
  gboolean a11y_manager_without_access_control;

  force_hdr = g_strcmp0 (getenv ("MUTTER_DEBUG_FORCE_HDR"), "1") == 0;
  meta_dbus_debug_control_set_force_hdr (dbus_debug_control, force_hdr);

  force_linear_blending =
    g_strcmp0 (getenv ("MUTTER_DEBUG_FORCE_LINEAR_BLENDING"), "1") == 0;
  meta_dbus_debug_control_set_force_linear_blending (dbus_debug_control,
                                                     force_linear_blending);

  session_management_protocol =
    g_strcmp0 (getenv ("MUTTER_DEBUG_SESSION_MANAGEMENT_PROTOCOL"), "1") == 0;
  meta_dbus_debug_control_set_session_management_protocol (dbus_debug_control,
                                                           session_management_protocol);

  inhibit_hw_cursor =
    g_strcmp0 (getenv ("MUTTER_DEBUG_INHIBIT_HW_CURSOR"), "1") == 0;
  meta_dbus_debug_control_set_inhibit_hw_cursor (dbus_debug_control,
                                                 inhibit_hw_cursor);

  a11y_manager_without_access_control =
    g_strcmp0 (getenv ("MUTTER_DEBUG_A11Y_MANAGER_WITHOUT_ACCESS_CONTROL"), "1") == 0;
  meta_dbus_debug_control_set_a11y_manager_without_access_control (dbus_debug_control,
                                                                   a11y_manager_without_access_control);
}

gboolean
meta_debug_control_is_linear_blending_forced (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_force_linear_blending (dbus_debug_control);
}

gboolean
meta_debug_control_is_hdr_forced (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_force_hdr (dbus_debug_control);
}

gboolean
meta_debug_control_is_session_management_protocol_enabled (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_session_management_protocol (dbus_debug_control);
}

void
meta_debug_control_set_exported (MetaDebugControl *debug_control,
                                 gboolean          exported)
{
  if (debug_control->exported == exported)
    return;

  if (exported)
    {
      debug_control->dbus_name_id =
        g_bus_own_name (G_BUS_TYPE_SESSION,
                        META_DEBUG_CONTROL_DBUS_SERVICE,
                        G_BUS_NAME_OWNER_FLAGS_NONE,
                        on_bus_acquired,
                        NULL,
                        NULL,
                        debug_control,
                        NULL);
    }
  else
    {
      g_clear_handle_id (&debug_control->dbus_name_id, g_bus_unown_name);
    }

  debug_control->exported = exported;
  g_object_notify_by_pspec (G_OBJECT (debug_control), obj_props[PROP_EXPORTED]);
}

gboolean
meta_debug_control_is_hw_cursor_inhibited (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_inhibit_hw_cursor (dbus_debug_control);
}

gboolean
meta_debug_control_is_a11y_manager_without_access_control (MetaDebugControl *debug_control)
{
  MetaDBusDebugControl *dbus_debug_control =
    META_DBUS_DEBUG_CONTROL (debug_control);

  return meta_dbus_debug_control_get_a11y_manager_without_access_control (dbus_debug_control);
}
