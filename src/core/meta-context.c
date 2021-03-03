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

#include <glib-unix.h>
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
  GType plugin_gtype;

  GOptionContext *option_context;

  MetaBackend *backend;
  MetaDisplay *display;

  GMainLoop *main_loop;
  GError *termination_error;
} MetaContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaContext, meta_context, G_TYPE_OBJECT)

void
meta_context_add_option_entries (MetaContext        *context,
                                 const GOptionEntry *entries,
                                 const char         *translation_domain)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_option_context_add_main_entries (priv->option_context,
                                     entries,
                                     translation_domain);
}

void
meta_context_add_option_group (MetaContext  *context,
                               GOptionGroup *group)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->option_context);

  g_option_context_add_group (priv->option_context, group);
}

void
meta_context_set_plugin_gtype (MetaContext *context,
                               GType        plugin_gtype)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (!priv->plugin_name);

  priv->plugin_gtype = plugin_gtype;
}

void
meta_context_set_plugin_name (MetaContext *context,
                              const char  *plugin_name)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_return_if_fail (priv->plugin_gtype == G_TYPE_NONE);

  priv->plugin_name = g_strdup (plugin_name);
}

void
meta_context_notify_ready (MetaContext *context)
{
  META_CONTEXT_GET_CLASS (context)->notify_ready (context);
}

/**
 * meta_context_get_backend:
 * @context: The #MetaContext
 *
 * Returns: (transfer none): the #MetaBackend
 */
MetaBackend *
meta_context_get_backend (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->backend;
}

/**
 * meta_context_get_display:
 * @context: The #MetaContext
 *
 * Returns: (transfer none): the #MetaDisplay
 */
MetaDisplay *
meta_context_get_display (MetaContext *context)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  return priv->display;
}

MetaCompositorType
meta_context_get_compositor_type (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->get_compositor_type (context);
}

static gboolean
meta_context_real_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  g_autoptr (GOptionContext) option_context = NULL;

  if (!priv->option_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Tried to configure multiple times");
      return FALSE;
    }

  option_context = g_steal_pointer (&priv->option_context);
  return g_option_context_parse (option_context, argc, argv, error);
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

static gboolean
on_sigterm (gpointer user_data)
{
  MetaContext *context = META_CONTEXT (user_data);

  meta_context_terminate (context);

  return G_SOURCE_REMOVE;
}

static void
init_signal_handlers (MetaContext *context)
{
  struct sigaction act = { 0 };
  sigset_t empty_mask;

  sigemptyset (&empty_mask);
  act.sa_handler = SIG_IGN;
  act.sa_mask = empty_mask;
  act.sa_flags = 0;
  if (sigaction (SIGPIPE,  &act, NULL) < 0)
    g_warning ("Failed to register SIGPIPE handler: %s", g_strerror (errno));
#ifdef SIGXFSZ
  if (sigaction (SIGXFSZ,  &act, NULL) < 0)
    g_warning ("Failed to register SIGXFSZ handler: %s", g_strerror (errno));
#endif

  g_unix_signal_add (SIGTERM, on_sigterm, context);
}

static void
change_to_home_directory (void)
{
  const char *home_dir;

  home_dir = g_get_home_dir ();
  if (!home_dir)
    return;

  if (chdir (home_dir) < 0)
    g_warning ("Could not change to home directory %s", home_dir);
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

static void
init_introspection (MetaContext *context)
{
#ifdef HAVE_INTROSPECTION
  g_irepository_prepend_search_path (MUTTER_PKGLIBDIR);
#endif
}

static gboolean
meta_context_real_setup (MetaContext  *context,
                         GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaBackend *backend;

  backend = META_CONTEXT_GET_CLASS (context)->create_backend (context, error);
  if (!backend)
    return FALSE;

  priv->backend = backend;
  return TRUE;
}

gboolean
meta_context_setup (MetaContext  *context,
                    GError      **error)
{
  MetaContextPrivate *priv = meta_context_get_instance_private (context);
  MetaCompositorType compositor_type;

  if (!priv->plugin_name && priv->plugin_gtype == G_TYPE_NONE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No compositor plugin set");
      return FALSE;
    }

  meta_init_debug_utils ();
  init_signal_handlers (context);

  change_to_home_directory ();

  compositor_type = meta_context_get_compositor_type (context);
  g_message ("Running %s (using mutter %s) as a %s",
             priv->name, VERSION,
             compositor_type_to_description (compositor_type));

  if (priv->plugin_name)
    meta_plugin_manager_load (priv->plugin_name);
  else
    meta_plugin_manager_set_plugin_type (priv->plugin_gtype);

  init_introspection (context);

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

  priv->display = meta_display_new (error);
  if (!priv->display)
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
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *compositor;
  MetaCompositorType compositor_type;
#endif

  if (priv->backend)
    meta_backend_prepare_shutdown (priv->backend);

#ifdef HAVE_WAYLAND
  compositor = meta_wayland_compositor_get_default ();
  if (compositor)
    meta_wayland_compositor_prepare_shutdown (compositor);
#endif

  if (priv->display)
    meta_display_close (priv->display, META_CURRENT_TIME);
  g_clear_object (&priv->display);

#ifdef HAVE_WAYLAND
  compositor_type = meta_context_get_compositor_type (context);
  if (compositor_type == META_COMPOSITOR_TYPE_WAYLAND)
    meta_wayland_finalize ();
#endif

  g_clear_pointer (&priv->backend, meta_backend_destroy);

  g_clear_pointer (&priv->option_context, g_option_context_free);
  g_clear_pointer (&priv->main_loop, g_main_loop_unref);
  g_clear_pointer (&priv->plugin_name, g_free);
  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_context_parent_class)->finalize (object);
}

/*
 * NOTE!
 *
 * This global singletone is a temporary stop-gap solution
 * to allow migrating to MetaContext in smaller steps. It will
 * be removed later in this series of changes.
 */
static MetaContext *_context_temporary;

MetaContext *
meta_get_context_temporary (void);

MetaContext *
meta_get_context_temporary (void)
{
  return _context_temporary;
}

static void
meta_context_class_init (MetaContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_context_get_property;
  object_class->set_property = meta_context_set_property;
  object_class->finalize = meta_context_finalize;

  klass->configure = meta_context_real_configure;
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
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_assert (!_context_temporary);
  _context_temporary = context;

  priv->plugin_gtype = G_TYPE_NONE;

  if (!setlocale (LC_ALL, ""))
    g_warning ("Locale not understood by C library");
  bindtextdomain (GETTEXT_PACKAGE, MUTTER_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  priv->option_context = g_option_context_new (NULL);
  g_option_context_set_main_group (priv->option_context,
                                   g_option_group_new (NULL, NULL, NULL,
                                                       context, NULL));
}
