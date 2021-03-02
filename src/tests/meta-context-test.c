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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "tests/meta-context-test.h"

#include <glib.h>
#include <gio/gio.h>

#include "tests/meta-backend-test.h"
#include "tests/test-utils.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-xwayland.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

struct _MetaContextTest
{
  GObject parent;

  MetaContextTestType type;
};

G_DEFINE_TYPE (MetaContextTest, meta_context_test, META_TYPE_CONTEXT)

static gboolean
meta_context_test_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  g_test_init (argc, argv, NULL);
  g_test_bug_base ("https://gitlab.gnome.org/GNOME/mutter/issues/");

  meta_ensure_test_client_path (*argc, *argv);

  meta_wayland_override_display_name ("mutter-test-display");
  meta_xwayland_override_display_number (512);

  return TRUE;
}

static MetaCompositorType
meta_context_test_get_compositor_type (MetaContext *context)
{
  return META_COMPOSITOR_TYPE_WAYLAND;
}

static gboolean
meta_context_test_setup (MetaContext  *context,
                         GError      **error)
{
  if (!META_CONTEXT_CLASS (meta_context_test_parent_class)->setup (context,
                                                                   error))
    return FALSE;

  return TRUE;
}

static MetaBackend *
create_nested_backend (MetaContext  *context,
                       GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_TEST,
                         NULL, error,
                         NULL);
}

#ifdef HAVE_NATIVE_BACKEND
static MetaBackend *
create_headless_backend (MetaContext  *context,
                         GError      **error)
{
  return g_initable_new (META_TYPE_BACKEND_NATIVE,
                         NULL, error,
                         "headless", TRUE,
                         NULL);
}
#endif /* HAVE_NATIVE_BACKEND */

static MetaBackend *
meta_context_test_create_backend (MetaContext  *context,
                                  GError      **error)
{
  MetaContextTest *context_test = META_CONTEXT_TEST (context);

  switch (context_test->type)
    {
    case META_CONTEXT_TEST_TYPE_NESTED:
      return create_nested_backend (context, error);
#ifdef HAVE_NATIVE_BACKEND
    case META_CONTEXT_TEST_TYPE_HEADLESS:
      return create_headless_backend (context, error);
#endif /* HAVE_NATIVE_BACKEND */
    }

  g_assert_not_reached ();
}

MetaContext *
meta_create_test_context (MetaContextTestType type)
{
  MetaContextTest *context_test;

  context_test = g_object_new (META_TYPE_CONTEXT_TEST,
                               "name", "Mutter Test",
                               NULL);
  context_test->type = type;

  return META_CONTEXT (context_test);
}

static void
meta_context_test_class_init (MetaContextTestClass *klass)
{
  MetaContextClass *context_class = META_CONTEXT_CLASS (klass);

  context_class->configure = meta_context_test_configure;
  context_class->get_compositor_type = meta_context_test_get_compositor_type;
  context_class->setup = meta_context_test_setup;
  context_class->create_backend = meta_context_test_create_backend;
}

static void
meta_context_test_init (MetaContextTest *context_test)
{
}
