#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "clutter/test-utils.h"
#include "tests/clutter-test-utils.h"

typedef struct _TestMultiLayerPipelineState
{
  ClutterActor    *group;
  CoglTexture     *alpha_tex;
  CoglTexture     *redhand_tex;
  gfloat          *tex_coords;

  ClutterTimeline *timeline;

  CoglPipeline *pipeline0;
  graphene_matrix_t tex_matrix0;
  graphene_matrix_t rot_matrix0;
  CoglTexture     *light_tex0;

  CoglPipeline *pipeline1;
  graphene_matrix_t tex_matrix1;
  graphene_matrix_t rot_matrix1;
  CoglTexture     *light_tex1;

} TestMultiLayerPipelineState;

int
test_cogl_multitexture_main (int argc, char *argv[]);

const char *
test_cogl_multitexture_describe (void);

static void
frame_cb (ClutterTimeline  *timeline,
          int               frame_no,
          gpointer          data)
{
  TestMultiLayerPipelineState *state = data;

  graphene_matrix_multiply (&state->rot_matrix0,
                            &state->tex_matrix0,
                            &state->tex_matrix0);
  cogl_pipeline_set_layer_matrix (state->pipeline0, 2, &state->tex_matrix0);

  graphene_matrix_multiply (&state->rot_matrix1,
                            &state->tex_matrix1,
                            &state->tex_matrix1);
  cogl_pipeline_set_layer_matrix (state->pipeline1, 2, &state->tex_matrix1);
}

static void
material_rectangle_paint (ClutterActor        *actor,
                          ClutterPaintContext *paint_context,
                          gpointer             data)
{
  TestMultiLayerPipelineState *state = data;
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);

  cogl_framebuffer_push_matrix (framebuffer);

  cogl_framebuffer_translate (framebuffer, 150, 15, 0);

  cogl_framebuffer_draw_multitextured_rectangle (framebuffer,
                                                 COGL_FRAMEBUFFER (state->pipeline0),
                                                 0, 0, 200, 213,
                                                 state->tex_coords,
                                                 12);
  cogl_framebuffer_translate (framebuffer, -300, -30, 0);
  cogl_framebuffer_draw_multitextured_rectangle (framebuffer,
                                                 COGL_FRAMEBUFFER (state->pipeline1),
                                                 0, 0, 200, 213,
                                                 state->tex_coords,
                                                 12);

  cogl_framebuffer_pop_matrix (framebuffer);
}

static void
animation_completed_cb (ClutterAnimation            *animation,
                        TestMultiLayerPipelineState *state)
{
  static gboolean go_back = FALSE;
  gdouble new_rotation_y;

  if (go_back)
    new_rotation_y = 30;
  else
    new_rotation_y = -30;
  go_back = !go_back;

  clutter_actor_animate_with_timeline (state->group,
                                       CLUTTER_LINEAR,
                                       state->timeline,
                                       "rotation-angle-y", new_rotation_y,
                                       "signal-after::completed",
                                       animation_completed_cb, state,
                                       NULL);


}

G_MODULE_EXPORT int
test_cogl_multitexture_main (int argc, char *argv[])
{
  GError            *error = NULL;
  ClutterActor      *stage;
  ClutterColor       stage_color = { 0x61, 0x56, 0x56, 0xff };
  g_autofree TestMultiLayerPipelineState *state = g_new0 (TestMultiLayerPipelineState, 1);
  gfloat             stage_w, stage_h;
  gchar            **files;
  gfloat             tex_coords[] =
    {
    /* tx1  ty1  tx2  ty2 */
         0,   0,   1,   1,
         0,   0,   1,   1,
         0,   0,   1,   1
    };
  CoglContext *ctx;

  clutter_test_init (&argc, &argv);

  stage = clutter_test_get_stage ();
  clutter_actor_get_size (stage, &stage_w, &stage_h);

  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl: Multi-texturing");
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), &stage_color);

  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  /* We create a non-descript actor that we know doesn't have a
   * default paint handler, so that we can easily control
   * painting in a paint signal handler, without having to
   * sub-class anything etc. */
  state->group = clutter_actor_new ();
  clutter_actor_set_position (state->group, stage_w / 2, stage_h / 2);
  g_signal_connect (state->group, "paint",
                    G_CALLBACK (material_rectangle_paint), state);

  files = g_new (gchar*, 4);
  files[0] = g_build_filename (TESTS_DATADIR, "redhand_alpha.png", NULL);
  files[1] = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  files[2] = g_build_filename (TESTS_DATADIR, "light0.png", NULL);
  files[3] = NULL;

  ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  state->alpha_tex = clutter_test_texture_2d_new_from_file (ctx, files[0], &error);
  if (!state->alpha_tex)
    g_critical ("Failed to load redhand_alpha.png: %s", error->message);

  state->redhand_tex = clutter_test_texture_2d_new_from_file (ctx, files[1], &error);
  if (!state->redhand_tex)
    g_critical ("Failed to load redhand.png: %s", error->message);

  state->light_tex0 = clutter_test_texture_2d_new_from_file (ctx, files[2], &error);
  if (!state->light_tex0)
    g_critical ("Failed to load light0.png: %s", error->message);

  state->light_tex1 = clutter_test_texture_2d_new_from_file (ctx, files[2], &error);
  if (!state->light_tex1)
    g_critical ("Failed to load light0.png: %s", error->message);

  g_strfreev (files);

  state->pipeline0 = cogl_pipeline_new ();
  cogl_pipeline_set_layer (state->pipeline0, 0, state->alpha_tex);
  cogl_pipeline_set_layer (state->pipeline0, 1, state->redhand_tex);
  cogl_pipeline_set_layer (state->pipeline0, 2, state->light_tex0);

  state->pipeline1 = cogl_pipeline_new ();
  cogl_pipeline_set_layer (state->pipeline1, 0, state->alpha_tex);
  cogl_pipeline_set_layer (state->pipeline1, 1, state->redhand_tex);
  cogl_pipeline_set_layer (state->pipeline1, 2, state->light_tex1);

  state->tex_coords = tex_coords;

  graphene_matrix_init_identity (&state->tex_matrix0);
  graphene_matrix_init_identity (&state->tex_matrix1);
  graphene_matrix_init_identity (&state->rot_matrix0);
  graphene_matrix_init_identity (&state->rot_matrix1);

  graohene_matrix_translate (&state->rot_matrix0,
                             &GRAPHENE_POINT3D_INIT (-0.5, -0.5, 0));
  graohene_matrix_rotate (&state->rot_matrix0, 10.0, graphene_vec3_z_axis ());
  graphene_matrix_translate (&state->rot_matrix0,
                             &GRAPHENE_POINT3D_INIT (0.5, 0.5, 0));

  graphene_matrix_translate (&state->rot_matrix1,
                             &GRAPHENE_POINT3D_INIT (-0.5, -0.5, 0));
  graohene_matrix_rotate (&state->rot_matrix1, -10.0, graphene_vec3_z_axis ());
  graphene_matrix_translate (&state->rot_matrix1,
                             &GRAPHENE_POINT3D_INIT (0.5, 0.5, 0));

  clutter_actor_set_translation (data->parent_container, -86.f, -125.f, 0.f);
  clutter_actor_add_child (stage, state->group);

  state->timeline = clutter_timeline_new_for_actor (stage, 2812);

  g_signal_connect (state->timeline, "new-frame", G_CALLBACK (frame_cb), state);

  clutter_actor_animate_with_timeline (state->group,
                                       CLUTTER_LINEAR,
                                       state->timeline,
                                       "rotation-angle-y", 30.0,
                                       "signal-after::completed",
                                       animation_completed_cb, state,
                                       NULL);

  /* start the timeline and thus the animations */
  clutter_timeline_start (state->timeline);

  clutter_actor_show (stage);

  clutter_test_main ();

  g_object_unref (state->pipeline1);
  g_object_unref (state->pipeline0);
  g_object_unref (state->alpha_tex);
  g_object_unref (state->redhand_tex);
  g_object_unref (state->light_tex0);
  g_object_unref (state->light_tex1);
  g_free (state);

  return 0;
}

G_MODULE_EXPORT const char *
test_cogl_multitexture_describe (void)
{
  return "Multi-texturing support in Cogl.";
}
