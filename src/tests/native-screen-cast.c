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

#include <errno.h>
#include <gio/gio.h>
#include <unistd.h>

#include "meta/util.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

static void
meta_test_screen_cast_record_virtual (void)
{
  g_autoptr (GSubprocess) subprocess = NULL;

  meta_add_verbose_topic (META_DEBUG_SCREEN_CAST);
  subprocess = meta_launch_test_executable ("mutter-screen-cast-client",
                                            NULL);
  meta_wait_test_process (subprocess);
  meta_remove_verbose_topic (META_DEBUG_SCREEN_CAST);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/screen-cast/record-virtual",
                   meta_test_screen_cast_record_virtual);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
