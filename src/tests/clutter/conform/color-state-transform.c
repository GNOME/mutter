#include <fcntl.h>
#include <glib/gstdio.h>

#include "clutter-mutter.h"
#include "tests/clutter-test-utils.h"

#define COLOR_TRANSFORM_EPSILON 0.004f

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
};

#define ACTOR_SIZE 10.0f

static GList *
create_actors (ClutterActor *stage)
{
  GList *actors = NULL;

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
      clutter_actor_add_child (stage, actor);

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

static void
validate_transform (ClutterActor      *stage,
                    ClutterColorState *src_color_state,
                    ClutterColorState *target_color_state)
{
  ClutterStageView *view = get_stage_view (stage);
  CoglFramebuffer *fb = clutter_stage_view_get_onscreen (view);
  float in_color[3];
  float cpu_color[4];
  float shader_color[4];
  int x, y;
  gboolean transform_passed;

  for (int i = 0; i < G_N_ELEMENTS (test_colors); i++)
    {
      in_color[0] = test_colors[i].r;
      in_color[1] = test_colors[i].g;
      in_color[2] = test_colors[i].b;

      clutter_color_state_do_transform (src_color_state,
                                        target_color_state,
                                        in_color,
                                        cpu_color,
                                        1);

      x = (int) (i * ACTOR_SIZE);
      y = 0;
      cogl_framebuffer_read_pixels (fb,
                                    x, y, 1, 1,
                                    COGL_PIXEL_FORMAT_RGBA_FP_32323232_PRE,
                                    (uint8_t *) shader_color);

      transform_passed = G_APPROX_VALUE (cpu_color[0],
                                         shader_color[0],
                                         COLOR_TRANSFORM_EPSILON) &&
                         G_APPROX_VALUE (cpu_color[1],
                                         shader_color[1],
                                         COLOR_TRANSFORM_EPSILON) &&
                         G_APPROX_VALUE (cpu_color[2],
                                         shader_color[2],
                                         COLOR_TRANSFORM_EPSILON);

      if (!transform_passed)
        {
          g_test_message ("Failed color transform:\n"
                          "input  (%.5f, %.5f, %.5f)\n"
                          "cpu    (%.5f, %.5f, %.5f)\n"
                          "shader (%.5f, %.5f, %.5f)\n"
                          "diff   (%.5f, %.5f, %.5f)\n",
                          in_color[0], in_color[1], in_color[2],
                          cpu_color[0], cpu_color[1], cpu_color[2],
                          shader_color[0], shader_color[1], shader_color[2],
                          ABS (cpu_color[0] - shader_color[0]),
                          ABS (cpu_color[1] - shader_color[1]),
                          ABS (cpu_color[2] - shader_color[2]));
        }

      g_assert_true (transform_passed);
    }
}

static void
color_state_transform_params_to_params (void)
{
  ClutterContext *context = clutter_test_get_context ();
  g_autoptr (ClutterColorState) src_color_state = NULL;
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

  target_color_state =
    clutter_color_state_params_new (context,
                                    CLUTTER_COLORSPACE_BT2020,
                                    CLUTTER_TRANSFER_FUNCTION_PQ);
  stage_view = get_stage_view (stage);
  stage_view_set_color_state (stage_view, target_color_state);

  wait_for_paint (stage);

  validate_transform (stage, src_color_state, target_color_state);

  g_list_free_full (actors, (GDestroyNotify) clutter_actor_destroy);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/color-state-transform/params-to-params", color_state_transform_params_to_params)
)
