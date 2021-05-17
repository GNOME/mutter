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

#include "meta/meta-backend.h"
#include "meta-test/meta-context-test.h"

typedef struct
{
  int number_of_frames_left;
  GMainLoop *loop;
} KmsRenderingTest;

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 KmsRenderingTest *test)
{
  test->number_of_frames_left--;
  if (test->number_of_frames_left == 0)
    g_main_loop_quit (test->loop);
  else
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_test_kms_render_basic (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  KmsRenderingTest test;

  test = (KmsRenderingTest) {
    .number_of_frames_left = 10,
    .loop = g_main_loop_new (NULL, FALSE),
  };
  g_signal_connect (stage, "after-update", G_CALLBACK (on_after_update), &test);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  g_main_loop_run (test.loop);
  g_main_loop_unref (test.loop);

  g_assert_cmpint (test.number_of_frames_left, ==, 0);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/render/basic",
                   meta_test_kms_render_basic);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
