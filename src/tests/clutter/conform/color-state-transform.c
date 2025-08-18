#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/mman.h>

#include "clutter-mutter.h"
#include "tests/clutter-test-utils.h"

#define COLOR_TRANSFORM_EPSILON 0.05f

typedef struct _TestColor {
  float r, g, b, a;
} TestColor;

static TestColor test_colors[] = {
  { 0.0f,   0.0f,  0.0f,  1.0f },
  { 1.0f,   0.0f,  0.0f,  1.0f },
  { 0.0f,   1.0f,  0.0f,  1.0f },
  { 0.0f,   0.0f,  1.0f,  1.0f },
  { 1.0f,   1.0f,  1.0f,  1.0f },
  { 0.22f,  0.33f, 0.44f, 1.0f },
  { 0.88f,  0.66f, 0.5f,  1.0f },
  { 0.0f,   0.66f, 0.44f, 1.0f },
  { 0.166f, 0.0f,  0.93f, 1.0f },
  { 0.99f,  0.75f, 0.0f,  1.0f },
  { 1.0f,   0.5f,  0.25f, 0.5f },
  { 1.0f,   1.0f,  1.0f,  0.9f },
  { 1.0f,   1.0f,  1.0f,  0.5f },
  { 1.0f,   1.0f,  1.0f,  0.3f },
  { 1.0f,   1.0f,  1.0f,  0.1f },
};

#define ACTOR_SIZE 10.0f

static ClutterColorState *
create_icc_color_state (const char *icc_filename)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autofree char *icc_path = NULL;
  g_autofd int srgb_icc_fd = -1;
  struct stat stat = { 0 };
  uint8_t *icc_bytes;
  uint32_t icc_size;
  ClutterColorState *color_state;

  icc_path = g_build_filename (g_getenv ("TEST_DATADIR"),
                               "icc-profiles",
                               icc_filename,
                               NULL);

  g_assert_true (g_file_test (icc_path, G_FILE_TEST_EXISTS));

  srgb_icc_fd = open (icc_path, O_RDONLY);
  g_assert_cmpint (srgb_icc_fd, !=, -1);

  fstat (srgb_icc_fd, &stat);
  icc_size = stat.st_size;
  g_assert_cmpuint (icc_size, >, 0);

  icc_bytes = mmap (NULL, icc_size, PROT_READ, MAP_PRIVATE, srgb_icc_fd, 0);
  g_assert_true (icc_bytes != MAP_FAILED);

  color_state = clutter_color_state_icc_new (context,
                                             icc_bytes,
                                             icc_size,
                                             NULL);
  g_assert_nonnull (color_state);

  munmap (icc_bytes, icc_size);

  return color_state;
}

static GList *
create_actors (ClutterActor *stage)
{
  GList *actors = NULL;

  const CoglColor black = { 0, 0, 0, UINT8_MAX };
  ClutterActor *background = clutter_actor_new ();
  clutter_actor_set_background_color (background, &black);
  clutter_actor_set_size (background,
                          G_N_ELEMENTS (test_colors) * ACTOR_SIZE,
                          ACTOR_SIZE);
  clutter_actor_set_position (background, 0.0f, 0.0f);
  clutter_actor_add_child (stage, background);

  actors = g_list_prepend (actors, background);

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      const CoglColor color = {
        (uint8_t) (test_colors[i].r * UINT8_MAX),
        (uint8_t) (test_colors[i].g * UINT8_MAX),
        (uint8_t) (test_colors[i].b * UINT8_MAX),
        (uint8_t) (test_colors[i].a * UINT8_MAX),
      };

      ClutterActor *actor = clutter_actor_new ();
      clutter_actor_set_background_color (actor, &color);
      clutter_actor_set_size (actor, ACTOR_SIZE, ACTOR_SIZE);
      clutter_actor_set_position (actor, i * ACTOR_SIZE, 0.0f);
      clutter_actor_add_child (background, actor);

      actors = g_list_prepend (actors, actor);
    }

  return actors;
}

static void
actors_set_color_state (GList             *actors,
                        ClutterColorState *color_state)
{
  GList *l;

  for (l = actors; l; l = l->next)
    {
      ClutterActor *actor = l->data;
      clutter_actor_set_color_state (actor, color_state);
    }
}

static void
stage_view_set_color_state (ClutterStageView  *stage_view,
                            ClutterColorState *color_state)
{
  g_autoptr (ClutterColorState) view_color_state = NULL;

  view_color_state = clutter_color_state_get_blending (color_state, FALSE);

  clutter_stage_view_set_color_state (stage_view, view_color_state);
  clutter_stage_view_set_output_color_state (stage_view, color_state);
}

static ClutterStageView *
get_stage_view (ClutterActor *stage)
{
  GList *l;

  l = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  return l->data;
}

static void
view_painted_cb (ClutterStage     *stage,
                 ClutterStageView *view,
                 MtkRegion        *redraw_clip,
                 ClutterFrame     *frame,
                 gpointer          data)
{
  gboolean *was_painted = data;

  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  int handler_id;

  clutter_actor_show (stage);

  handler_id = g_signal_connect_after (stage, "paint-view",
                                       G_CALLBACK (view_painted_cb),
                                       &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, handler_id);
}

static gboolean
validate_one_transform (CoglFramebuffer *fb,
                        int              x,
                        float           *cpu_color,
                        TestColor       *test_color,
                        const char      *name)
{
  float shader_color[4];

  cogl_framebuffer_read_pixels (fb,
                                x, 0, 1, 1,
                                COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                (uint8_t *) shader_color);

  if (!G_APPROX_VALUE (cpu_color[0],
                       shader_color[0],
                       COLOR_TRANSFORM_EPSILON) ||
      !G_APPROX_VALUE (cpu_color[1],
                       shader_color[1],
                       COLOR_TRANSFORM_EPSILON) ||
      !G_APPROX_VALUE (cpu_color[2],
                       shader_color[2],
                       COLOR_TRANSFORM_EPSILON))
    {
      g_test_message ("Failed %s color transform:\n"
                      "input  (%.5f, %.5f, %.5f, %.5f)\n"
                      "cpu    (%.5f, %.5f, %.5f)\n"
                      "shader (%.5f, %.5f, %.5f)\n"
                      "diff   (%.5f, %.5f, %.5f)\n",
                      name,
                      test_color->r, test_color->g, test_color->b,
                      test_color->a,
                      cpu_color[0], cpu_color[1], cpu_color[2],
                      shader_color[0], shader_color[1], shader_color[2],
                      ABS (cpu_color[0] - shader_color[0]),
                      ABS (cpu_color[1] - shader_color[1]),
                      ABS (cpu_color[2] - shader_color[2]));

      return FALSE;
    }

  return TRUE;
}

static void
validate_transform (ClutterActor      *stage,
                    ClutterColorState *src_color_state,
                    ClutterColorState *blend_color_state,
                    ClutterColorState *output_color_state)
{
  ClutterStageView *view = get_stage_view (stage);
  CoglFramebuffer *output_fb = clutter_stage_view_get_onscreen (view);
  CoglFramebuffer *blend_fb = clutter_stage_view_get_framebuffer (view);
  float cpu_color[3];
  gboolean transform_passed;

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      if (blend_color_state)
        {
          /* Start unpremultiplied */
          cpu_color[0] = test_colors[i].r;
          cpu_color[1] = test_colors[i].g;
          cpu_color[2] = test_colors[i].b;

          clutter_color_state_do_transform (src_color_state,
                                            blend_color_state,
                                            cpu_color,
                                            1);

          /* Premultiply */
          cpu_color[0] *= test_colors[i].a;
          cpu_color[1] *= test_colors[i].a;
          cpu_color[2] *= test_colors[i].a;

          transform_passed = validate_one_transform (blend_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "source -> blend");
          g_assert_true (transform_passed);

          clutter_color_state_do_transform (blend_color_state,
                                            output_color_state,
                                            cpu_color,
                                            1);

          transform_passed = validate_one_transform (output_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "blend -> output");
          g_assert_true (transform_passed);
        }

      if (test_colors[i].a == 1.0f)
        {
          cpu_color[0] = test_colors[i].r;
          cpu_color[1] = test_colors[i].g;
          cpu_color[2] = test_colors[i].b;

          clutter_color_state_do_transform (src_color_state,
                                            output_color_state,
                                            cpu_color,
                                            1);

          transform_passed = validate_one_transform (output_fb,
                                                     (int) (i * ACTOR_SIZE),
                                                     cpu_color,
                                                     test_colors + i,
                                                     "source -> output");
          g_assert_true (transform_passed);
        }
    }
}

static void
color_state_transform_icc_to_params (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state = create_icc_color_state ("sRGB.icc");
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_params_to_icc (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_SRGB,
                                    CLUTTER_TRANSFER_FUNCTION_SRGB);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state = create_icc_color_state ("sRGB.icc");
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_icc_to_icc (void)
{
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) target_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state = create_icc_color_state ("vx239-calibrated.icc");
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  target_color_state = create_icc_color_state ("sRGB.icc");
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_params_to_params (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  ClutterColorState *blend_color_state = NULL;
  g_autoptr (ClutterColorState) output_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_SRGB,
                                    CLUTTER_TRANSFER_FUNCTION_SRGB);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  output_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, output_color_state);
  blend_color_state =
    clutter_stage_view_get_color_state (stage_view);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, blend_color_state,
                      output_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

static void
color_state_transform_bt2020_to_bt2020 (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
  g_autoptr (ClutterColorState) output_color_state = NULL;
  ClutterStageView *stage_view;
  ClutterActor *stage;
  GList *actors;

  stage = clutter_test_get_stage ();

  src_color_state =
    clutter_color_state_params_new_full (context,
                                         CLUTTER_COLORSPACE_BT2020,
                                         CLUTTER_TRANSFER_FUNCTION_SRGB,
                                         NULL,
                                         -1.0f,
                                         0.005f,
                                         203.0f,
                                         203.0f,
                                         FALSE);
  actors = create_actors (stage);
  actors_set_color_state (actors, src_color_state);

  output_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, output_color_state);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, NULL, output_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/color-state-transform/icc-to-params", color_state_transform_icc_to_params)
  CLUTTER_TEST_UNIT ("/color-state-transform/params-to-icc", color_state_transform_params_to_icc)
  CLUTTER_TEST_UNIT ("/color-state-transform/icc-to-icc", color_state_transform_icc_to_icc)
  CLUTTER_TEST_UNIT ("/color-state-transform/params-to-params", color_state_transform_params_to_params)
  CLUTTER_TEST_UNIT ("/color-state-transform/bt2020-to-bt2020", color_state_transform_bt2020_to_bt2020)
)
