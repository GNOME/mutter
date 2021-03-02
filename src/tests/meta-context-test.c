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

struct _MetaContextTest
{
  GObject parent;
};

G_DEFINE_TYPE (MetaContextTest, meta_context_test, META_TYPE_CONTEXT)

static gboolean
meta_context_test_configure (MetaContext   *context,
                             int           *argc,
                             char        ***argv,
                             GError       **error)
{
  return TRUE;
}

static MetaCompositorType
meta_context_test_get_compositor_type (MetaContext *context)
{
  return META_COMPOSITOR_TYPE_WAYLAND;
}

MetaContext *
meta_create_test_context (void)
{
  MetaContextTest *context_test;

  context_test = g_object_new (META_TYPE_CONTEXT_TEST,
                               "name", "Mutter Test",
                               NULL);

  return META_CONTEXT (context_test);
}

static void
meta_context_test_class_init (MetaContextTestClass *klass)
{
  MetaContextClass *context_class = META_CONTEXT_CLASS (klass);

  context_class->configure = meta_context_test_configure;
  context_class->get_compositor_type = meta_context_test_get_compositor_type;
}

static void
meta_context_test_init (MetaContextTest *context_test)
{
}
