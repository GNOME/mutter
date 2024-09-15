/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "tests/meta-context-test-private.h"

#include <glib.h>
#include <gio/gio.h>
#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>
#include <fcntl.h>

#include "compositor/compositor-private.h"
#include "compositor/meta-plugin-manager.h"
#include "core/display-private.h"
#include "core/meta-context-private.h"
#include "meta/meta-x11-display.h"
#include "tests/meta-backend-test.h"
#include "tests/meta-test-shell.h"
#include "tests/meta-test-utils-private.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-xwayland.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

enum
{
  BEFORE_TESTS,
  RUN_TESTS,
  AFTER_TESTS,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaContextTestPrivate
{
  MetaContextTestType type;
  MetaContextTestFlag flags;
  MetaSessionManager *session_manager;
  CoglColor *background_color;
  UMockdevTestbed *udev_testbed;
} MetaContextTestPrivate;

struct _MetaContextTestClass
{
  MetaContextClass parent_class;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaContextTest, meta_context_test,
                            META_TYPE_CONTEXT)

static void
ensure_gsettings_memory_backend (void)
{
  g_autoptr (GSettingsBackend) memory_backend = NULL;
  GSettingsBackend *default_backend;

  g_assert_cmpstr (getenv ("GSETTINGS_BACKEND"), ==, "memory");
  g_assert_cmpstr (getenv ("XDG_CURRENT_DESKTOP"), ==, "");

  memory_backend = g_memory_settings_backend_new ();
  default_backend = g_settings_backend_get_default ();
  g_assert_true (G_TYPE_FROM_INSTANCE (memory_backend) ==
                 G_TYPE_FROM_INSTANCE (default_backend));
}

static void
ensure_xcursor_path (void)
{
  g_autofree char *xcursor_path = NULL;

  xcursor_path = g_test_build_filename (G_TEST_DIST, "xcursors", NULL);
  g_setenv ("XCURSOR_PATH", xcursor_path, TRUE);
}

static void
meta_context_test_finalize (GObject *object)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (object);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  g_clear_pointer (&priv->background_color, cogl_color_free);
  g_clear_object (&priv->session_manager);
  g_clear_object (&priv->udev_testbed);

  G_OBJECT_CLASS (meta_context_test_parent_class)->finalize (object);
}

static gboolean
meta_context_test_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (context);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);
  MetaContextClass *context_class =
    META_CONTEXT_CLASS (meta_context_test_parent_class);

  g_test_init (argc, argv, NULL);

  if (!context_class->configure (context, argc, argv, error))
    return FALSE;

  g_test_bug_base ("https://gitlab.gnome.org/GNOME/mutter/issues/");

  if (priv->flags & META_CONTEXT_TEST_FLAG_TEST_CLIENT)
    meta_ensure_test_client_path (*argc, *argv);

  meta_wayland_override_display_name ("mutter-test-display");
#ifdef HAVE_XWAYLAND
  meta_xwayland_override_display_number (512);
#endif

  meta_context_set_plugin_gtype (context, META_TYPE_TEST_SHELL);

  ensure_gsettings_memory_backend ();
  ensure_xcursor_path ();

  return TRUE;
}

static MetaCompositorType
meta_context_test_get_compositor_type (MetaContext *context)
{
  return META_COMPOSITOR_TYPE_WAYLAND;
}

static MetaX11DisplayPolicy
meta_context_test_get_x11_display_policy (MetaContext *context)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (context);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  if (priv->flags & META_CONTEXT_TEST_FLAG_NO_X11)
    return META_X11_DISPLAY_POLICY_DISABLED;
  else
    return META_X11_DISPLAY_POLICY_ON_DEMAND;
}

static gboolean
meta_context_test_is_replacing (MetaContext *context)
{
  return FALSE;
}

static gboolean
meta_context_test_setup (MetaContext  *context,
                         GError      **error)
{
  MetaBackend *backend;
  MetaSettings *settings;

  if (!META_CONTEXT_CLASS (meta_context_test_parent_class)->setup (context,
                                                                   error))
    return FALSE;

  backend = meta_context_get_backend (context);
  settings = meta_backend_get_settings (backend);
  meta_settings_override_experimental_features (settings);
  meta_settings_enable_experimental_feature (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  return TRUE;
}

#ifdef HAVE_NATIVE_BACKEND
static MetaBackend *
create_headless_backend (MetaContext  *context,
                         GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_NATIVE,
                         NULL, error,
                         "context", context,
                         "mode", META_BACKEND_NATIVE_MODE_HEADLESS,
                         NULL);
}

static MetaBackend *
create_test_vkms_backend (MetaContext  *context,
                          GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_NATIVE,
                         NULL, error,
                         "context", context,
                         "mode", META_BACKEND_NATIVE_MODE_TEST_VKMS,
                         NULL);
}

static MetaBackend *
create_test_headless_backend (MetaContext  *context,
                              GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_TEST,
                         NULL, error,
                         "context", context,
                         "mode", META_BACKEND_NATIVE_MODE_TEST_HEADLESS,
                         NULL);
}
#endif /* HAVE_NATIVE_BACKEND */

static MetaBackend *
meta_context_test_create_backend (MetaContext  *context,
                                  GError      **error)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (context);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  switch (priv->type)
    {
#ifdef HAVE_NATIVE_BACKEND
    case META_CONTEXT_TEST_TYPE_HEADLESS:
      return create_headless_backend (context, error);
    case META_CONTEXT_TEST_TYPE_VKMS:
      return create_test_vkms_backend (context, error);
    case META_CONTEXT_TEST_TYPE_TEST:
      return create_test_headless_backend (context, error);
#endif /* HAVE_NATIVE_BACKEND */
    }

  g_assert_not_reached ();
}

static void
meta_context_test_notify_ready (MetaContext *context)
{
}

static MetaSessionManager *
meta_context_test_get_session_manager (MetaContext *context)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (context);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  if (!priv->session_manager)
    {
      g_autoptr (GError) error = NULL;
      g_autofree char *template = NULL;
      int fd;

      template = g_build_filename (g_get_tmp_dir (),
                                   "session.gvdb.XXXXXX",
                                   NULL);

      fd = g_mkstemp (template);
      unlink (template);
      priv->session_manager =
        meta_session_manager_new_for_fd (NULL, fd, &error);
      g_assert_no_error (error);
    }

  return priv->session_manager;
}

#ifdef HAVE_X11
static gboolean
meta_context_test_is_x11_sync (MetaContext *context)
{
  return !!g_getenv ("MUTTER_SYNC");
}
#endif

static gboolean
run_tests_idle (gpointer user_data)
{
  MetaContext *context = user_data;
  int ret;

  g_signal_emit (context, signals[BEFORE_TESTS], 0);
  if (g_signal_has_handler_pending (context, signals[RUN_TESTS], 0, TRUE))
    {
      g_signal_emit (context, signals[RUN_TESTS], 0, &ret);
      g_assert_true (ret == 1 || ret == 0);
    }
  else
    {
      ret = g_test_run ();
    }
  g_signal_emit (context, signals[AFTER_TESTS], 0);

  if (ret != 0)
    {
      GError *error;

      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                           "One or more tests failed");
      meta_context_terminate_with_error (context, error);
    }
  else
    {
      meta_context_terminate (context);
    }

  return G_SOURCE_REMOVE;
}

int
meta_context_test_run_tests (MetaContextTest  *context_test,
                             MetaTestRunFlags  flags)
{
  MetaContext *context = META_CONTEXT (context_test);
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);
  MetaDisplay *display;
  MetaCompositor *compositor;
  MetaPluginManager *plugin_manager;
  MetaPlugin *plugin;
  g_autoptr (GError) error = NULL;

  if (!meta_context_setup (context, &error))
    {
      if ((flags & META_TEST_RUN_FLAG_CAN_SKIP) &&
          ((g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
            strstr (error->message, "No GPUs found")) ||
           (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED) &&
            strstr (error->message, "Native backend mode needs to be session controller"))))
        {
          g_printerr ("Test skipped: %s\n", error->message);
          return 77;
        }
      else
        {
          g_printerr ("Test case failed to setup: %s\n", error->message);
          return EXIT_FAILURE;
        }
    }

  if (!meta_context_start (context, &error))
    {
      g_printerr ("Test case failed to start: %s\n", error->message);
      return EXIT_FAILURE;
    }

  display = meta_context_get_display (context);
  compositor = display->compositor;
  plugin_manager = meta_compositor_get_plugin_manager (compositor);
  plugin = meta_plugin_manager_get_plugin (plugin_manager);

  if (priv->background_color)
    {
      meta_test_shell_set_background_color (META_TEST_SHELL (plugin),
                                            *priv->background_color);
    }

  if (priv->flags & META_CONTEXT_TEST_FLAG_NO_ANIMATIONS)
    meta_test_shell_disable_animations (META_TEST_SHELL (plugin));

  g_idle_add (run_tests_idle, context_test);

  meta_context_notify_ready (context);

  if (!meta_context_run_main_loop (context, &error))
    {
      g_printerr ("Test case failed: %s\n", error->message);
      return EXIT_FAILURE;
    }
  else
    {
      return EXIT_SUCCESS;
    }
}

void
meta_context_test_wait_for_x11_display (MetaContextTest *context_test)
{
  MetaDisplay *display = meta_context_get_display (META_CONTEXT (context_test));

  while (!meta_display_get_x11_display (display))
    g_main_context_iteration (NULL, TRUE);

  g_assert_nonnull (meta_display_get_x11_display (display));
}

/**
 * meta_context_test_get_udev_testbed: (skip)
 */
UMockdevTestbed *
meta_context_test_get_udev_testbed (MetaContextTest *context_test)
{
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  return priv->udev_testbed;
}

/**
 * meta_create_test_context: (skip)
 */
MetaContext *
meta_create_test_context (MetaContextTestType type,
                          MetaContextTestFlag flags)
{
  MetaContextTest *context_test;
  MetaContextTestPrivate *priv;

  context_test = g_object_new (META_TYPE_CONTEXT_TEST,
                               "name", "Mutter Test",
                               NULL);
  priv = meta_context_test_get_instance_private (context_test);
  priv->type = type;
  priv->flags = flags;
  priv->udev_testbed = umockdev_testbed_new ();

  return META_CONTEXT (context_test);
}

static void
meta_context_test_class_init (MetaContextTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaContextClass *context_class = META_CONTEXT_CLASS (klass);

  object_class->finalize = meta_context_test_finalize;

  context_class->configure = meta_context_test_configure;
  context_class->get_compositor_type = meta_context_test_get_compositor_type;
  context_class->get_x11_display_policy =
    meta_context_test_get_x11_display_policy;
  context_class->is_replacing = meta_context_test_is_replacing;
  context_class->setup = meta_context_test_setup;
  context_class->create_backend = meta_context_test_create_backend;
  context_class->notify_ready = meta_context_test_notify_ready;
#ifdef HAVE_X11
  context_class->is_x11_sync = meta_context_test_is_x11_sync;
#endif
  context_class->get_session_manager = meta_context_test_get_session_manager;

  signals[BEFORE_TESTS] =
    g_signal_new ("before-tests",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
  signals[RUN_TESTS] =
    g_signal_new ("run-tests",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_INT,
                  0);
  signals[AFTER_TESTS] =
    g_signal_new ("after-tests",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}

static void
meta_context_test_init (MetaContextTest *context_test)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

  proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                   NULL,
                                   "org.freedesktop.ColorManager",
                                   "/org/freedesktop/ColorManager",
                                   "org.freedesktop.DBus.Mock",
                                   NULL, &error);
  if (!proxy)
    {
      g_warning ("Failed to find mocked color manager system service, %s",
                 error->message);
      return;
    }

  ret = g_dbus_proxy_call_sync (proxy,
                                "Reset",
                                NULL,
                                G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                                &error);
  if (ret == NULL)
    g_warning ("Failed to clear mocked color devices: %s", error->message);
}

void
meta_context_test_set_background_color (MetaContextTest *context_test,
                                        CoglColor        color)
{
  MetaContextTestPrivate *priv =
    meta_context_test_get_instance_private (context_test);

  g_clear_pointer (&priv->background_color, cogl_color_free);
  priv->background_color = cogl_color_copy (&color);
}
