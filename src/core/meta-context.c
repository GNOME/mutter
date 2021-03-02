/*
 * Copyright (C) 2019 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "core/meta-context-private.h"

#include <locale.h>

#include "backends/meta-backend-private.h"
#include "compositor/meta-plugin-manager.h"
#include "core/display-private.h"
#include "core/prefs-private.h"
#include "core/util-private.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland.h"
#endif

enum
{
  PROP_0,

  PROP_NAME,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaContextPrivate
{
  char *name;
  char *plugin_name;

  GMainLoop *main_loop;
  GError *termination_error;
} MetaContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaContext, meta_context, G_TYPE_OBJECT)

void
meta_context_set_plugin_name (MetaContext *context,
                              const char  *plugin_name)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  priv->plugin_name = g_strdup (plugin_name);
}

static MetaCompositorType
meta_context_get_compositor_type (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->get_compositor_type (context);
}

gboolean
meta_context_configure (MetaContext   *context,
                        int           *argc,
                        char        ***argv,
                        GError       **error)
{
  MetaCompositorType compositor_type;

  if (!META_CONTEXT_GET_CLASS (context)->configure (context, argc, argv, error))
    return FALSE;

  compositor_type = meta_context_get_compositor_type (context);
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_WAYLAND:
      meta_set_is_wayland_compositor (TRUE);
      break;
    case META_COMPOSITOR_TYPE_X11:
      meta_set_is_wayland_compositor (FALSE);
      break;
    }

  return TRUE;
}

static const char *
compositor_type_to_description (MetaCompositorType compositor_type)
{
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_WAYLAND:
      return "Wayland display server";
    case META_COMPOSITOR_TYPE_X11:
      return "X11 window and compositing manager";
    }

  g_assert_not_reached ();
}

static gboolean
meta_context_real_setup (MetaContext  *context,
                         GError      **error)
{
  return !!META_CONTEXT_GET_CLASS (context)->create_backend (context, error);
}

gboolean
meta_context_setup (MetaContext  *context,
                    GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaCompositorType compositor_type;

  if (!priv->plugin_name)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No compositor plugin set");
      return FALSE;
    }

  compositor_type = meta_context_get_compositor_type (context);
  g_message ("Running %s (using mutter %s) as a %s",
             priv->name, VERSION,
             compositor_type_to_description (compositor_type));

  meta_plugin_manager_load (priv->plugin_name);

  return META_CONTEXT_GET_CLASS (context)->setup (context, error);
}

gboolean
meta_context_start (MetaContext  *context,
                    GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  meta_prefs_init ();

#ifdef HAVE_WAYLAND
  if (meta_context_get_compositor_type (context) ==
      META_COMPOSITOR_TYPE_WAYLAND)
    meta_backend_init_wayland (meta_get_backend ());
#endif

  if (!meta_display_open (error))
    return FALSE;

  priv->main_loop = g_main_loop_new (NULL, FALSE);

  return TRUE;
}

gboolean
meta_context_run_main_loop (MetaContext  *context,
                            GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  if (!priv->main_loop)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to run main loop without having started");
      return FALSE;
    }

  g_main_loop_run (priv->main_loop);
  g_clear_pointer (&priv->main_loop, g_main_loop_unref);

  if (priv->termination_error)
    {
      g_propagate_error (error, g_steal_pointer (&priv->termination_error));
      return FALSE;
    }

  return TRUE;
}

void
meta_context_terminate (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (g_main_loop_is_running (priv->main_loop));

  g_main_loop_quit (priv->main_loop);
}

void
meta_context_terminate_with_error (MetaContext *context,
                                   GError      *error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  priv->termination_error = g_steal_pointer (&error);
  meta_context_terminate (context);
}

static void
meta_context_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_finalize (GObject *object)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaDisplay *display;
  MetaBackend *backend;
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *compositor;
  MetaCompositorType compositor_type;
#endif

  backend = meta_get_backend ();
  if (backend)
    meta_backend_prepare_shutdown (backend);

#ifdef HAVE_WAYLAND
  compositor = meta_wayland_compositor_get_default ();
  if (compositor)
    meta_wayland_compositor_prepare_shutdown (compositor);
#endif

  display = meta_get_display ();
  if (display)
    meta_display_close (display, META_CURRENT_TIME);

#ifdef HAVE_WAYLAND
  compositor_type = meta_context_get_compositor_type (context);
  if (compositor_type == META_COMPOSITOR_TYPE_WAYLAND)
    meta_wayland_finalize ();
#endif

  meta_release_backend ();

  g_clear_pointer (&priv->main_loop, g_main_loop_unref);
  g_clear_pointer (&priv->plugin_name, g_free);
  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_context_parent_class)->finalize (object);
}

static void
meta_context_class_init (MetaContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_context_get_property;
  object_class->set_property = meta_context_set_property;
  object_class->finalize = meta_context_finalize;

  klass->setup = meta_context_real_setup;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "name",
                         "Human readable name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_context_init (MetaContext *context)
{
  if (!setlocale (LC_ALL, ""))
    g_warning ("Locale not understood by C library");
  bindtextdomain (GETTEXT_PACKAGE, MUTTER_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
}
