#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "clutter/test-utils.h"
#include "tests/clutter-test-utils.h"

/* Coglbox declaration
 *--------------------------------------------------*/

G_BEGIN_DECLS

#define TEST_TYPE_COGLBOX test_coglbox_get_type()

static
G_DECLARE_FINAL_TYPE (TestCoglbox, test_coglbox, TEST, COGLBOX, ClutterActor)

struct _TestCoglbox
{
  ClutterActor           parent;

  CoglTexture *texhand_id;
  CoglTexture *texture_id;
  CoglFramebuffer *framebuffer;
};

G_DEFINE_TYPE (TestCoglbox, test_coglbox, CLUTTER_TYPE_ACTOR);

int
test_cogl_offscreen_main (int argc, char *argv[]);

const char *
test_cogl_offscreen_describe (void);

G_END_DECLS

/* Coglbox implementation
 *--------------------------------------------------*/

static void
test_coglbox_paint (ClutterActor        *self,
                    ClutterPaintContext *paint_context)
{
  TestCoglbox *coglbox = TEST_COGLBOX (self);
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  gfloat texcoords[4] = { 0, 0, 1, 1 };
  CoglPipeline *pipeline;
  CoglColor color;

  pipeline = cogl_pipeline_new (ctx);
  cogl_color_init_from_4f (&color, 0.4, 0.4, 221.0 / 255.0, 1.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_framebuffer_draw_rectangle (framebuffer, pipeline, 0, 0, 400, 400);
  g_object_unref (pipeline);

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_texture (pipeline, 0, coglbox->texhand_id);
  cogl_framebuffer_draw_textured_rectangle (framebuffer, pipeline,
                                            0, 0,
                                            400, 400,
                                            0, 0,
                                            6, 6);
  g_object_unref (pipeline);

  pipeline = cogl_pipeline_new (ctx);

  cogl_color_init_from_4f (&color, 1.0, 0.0, 0.0, 1.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_framebuffer_draw_rectangle (coglbox->framebuffer, pipeline,
                                   20, 20, 20 + 100, 20 + 100);

  cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_framebuffer_draw_rectangle (coglbox->framebuffer, pipeline,
                                   80, 80, 80 + 100, 80 + 100);
  g_object_unref (pipeline);

  pipeline = cogl_pipeline_new (ctx);
  cogl_color_init_from_4f (&color,
                           136.0 / 255.0, 136.0 / 255.0,
                           136.0 / 255.0, 136.0 / 255.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_pipeline_set_layer_texture (pipeline, 0, coglbox->texture_id);
  cogl_framebuffer_draw_textured_rectangle (framebuffer, pipeline,
                                            100, 100,
                                            300, 300,
                                            texcoords[0],
                                            texcoords[1],
                                            texcoords[2],
                                            texcoords[3]);
  g_object_unref (pipeline);
}

static void
test_coglbox_dispose (GObject *object)
{
  TestCoglbox *coglbox = TEST_COGLBOX (object);

  g_object_unref (coglbox->texture_id);
  g_object_unref (coglbox->framebuffer);

  G_OBJECT_CLASS (test_coglbox_parent_class)->dispose (object);
}

/* A newly created Cogl framebuffer will be initialized with a
 * viewport covering the size of the viewport i.e. equavalent to:
 *
 * calling cogl_framebuffer_set_viewport (
 *                                fb,
 *                                0, 0,
 *                                cogl_framebuffer_get_viewport_width (fb),
 *                                cogl_framebuffer_get_viewport_width (fb));
 *
 * The projection matrix will be an identity matrix.
 *
 * The modelview matrix will be an identity matrix, and this will
 * create a coordinate system - like OpenGL - with the viewport
 * being mapped to a unit cube with the origin (0, 0, 0) in the
 * center, x, y and z ranging from -1 to 1 with (-1, -1) being top
 * left and (1, 1) bottom right.
 *
 * This sets up a Clutter like coordinate system for a Cogl
 * framebuffer
 */
static void
setup_viewport (CoglFramebuffer *framebuffer,
                unsigned int     width,
                unsigned int     height,
                float            fovy,
                float            aspect,
                float            z_near,
                float            z_far)
{
  float z_camera;
  graphene_matrix_t projection_matrix;
  graphene_matrix_t mv_matrix;

  cogl_framebuffer_set_viewport (framebuffer, 0, 0, width, height);

  /* For Ortho projection.
   * _cogl_matrix_stack_ortho (projection_stack, 0, width, 0,  height, -1, 1);
   */

  cogl_framebuffer_perspective (framebuffer, fovy, aspect, z_near, z_far);

  /*
   * In theory, we can compute the camera distance from screen as:
   *
   *   0.5 * tan (FOV)
   *
   * However, it's better to compute the z_camera from our projection
   * matrix so that we get a 1:1 mapping at the screen distance. Consider
   * the upper-left corner of the screen. It has object coordinates
   * (0,0,0), so by the transform below, ends up with eye coordinate
   *
   *   x_eye = x_object / width - 0.5 = - 0.5
   *   y_eye = (height - y_object) / width - 0.5 = 0.5
   *   z_eye = z_object / width - z_camera = - z_camera
   *
   * From cogl_perspective(), we know that the projection matrix has
   * the form:
   *
   *  (x, 0,  0, 0)
   *  (0, y,  0, 0)
   *  (0, 0,  c, d)
   *  (0, 0, -1, 0)
   *
   * Applied to the above, we get clip coordinates of
   *
   *  x_clip = x * (- 0.5)
   *  y_clip = y * 0.5
   *  w_clip = - 1 * (- z_camera) = z_camera
   *
   * Dividing through by w to get normalized device coordinates, we
   * have, x_nd = x * 0.5 / z_camera, y_nd = - y * 0.5 / z_camera.
   * The upper left corner of the screen has normalized device coordinates,
   * (-1, 1), so to have the correct 1:1 mapping, we have to have:
   *
   *   z_camera = 0.5 * x = 0.5 * y
   *
   * If x != y, then we have a non-uniform aspect ration, and a 1:1 mapping
   * doesn't make sense.
   */

  cogl_framebuffer_get_projection_matrix (framebuffer, &projection_matrix);
  z_camera = 0.5 * graphene_matrix_get_value (&projection_matrix, 0, 0);

  graphene_matrix_init_translate (&mv_matrix,
                                  &GRAPHENE_POINT3D_INIT (0.0f,
                                                          -1.0 * height,
                                                          0.0f));
  graphene_matrix_scale (&mv_matrix, 1.0f / width, -1.0f / height, 1.0f / width);
  graphene_matrix_translate (&mv_matrix,
                             &GRAPHENE_POINT3D_INIT (-0.5f, -0.5f, -z_camera));
  cogl_framebuffer_set_modelview_matrix (framebuffer, &mv_matrix);
}

static void
test_coglbox_map (ClutterActor *actor)
{
  TestCoglbox *coglbox = TEST_COGLBOX (actor);
  ClutterActor *stage;
  ClutterPerspective perspective;
  float stage_width;
  float stage_height;
  GError *error = NULL;

  CLUTTER_ACTOR_CLASS (test_coglbox_parent_class)->map (actor);

  printf ("Creating offscreen\n");
  coglbox->framebuffer =
    COGL_FRAMEBUFFER (cogl_offscreen_new_with_texture (coglbox->texture_id));
  if (!cogl_framebuffer_allocate (coglbox->framebuffer, &error))
    g_error ("Failed to allocate framebuffer: %s", error->message);

  stage = clutter_actor_get_stage (actor);
  clutter_stage_get_perspective (CLUTTER_STAGE (stage), &perspective);
  clutter_actor_get_size (stage, &stage_width, &stage_height);

  setup_viewport (coglbox->framebuffer,
                  stage_width, stage_height,
                  perspective.fovy,
                  perspective.aspect,
                  perspective.z_near,
                  perspective.z_far);

  if (!coglbox->framebuffer)
    printf ("Failed creating offscreen to texture!\n");
}

static void
test_coglbox_init (TestCoglbox *self)
{
  g_autoptr (GError) error = NULL;
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  gchar *file;

  printf ("Loading redhand.png\n");
  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  self->texhand_id = clutter_test_texture_2d_new_from_file (ctx, file, &error);
  if (error)
    g_warning ("Error loading redhand.png: %s", error->message);
  g_free (file);

  printf ("Creating texture with size\n");
  self->texture_id = cogl_texture_2d_new_with_size (ctx, 200, 200);

  if (self->texture_id == NULL)
    printf ("Failed creating texture with size!\n");
}

static void
test_coglbox_class_init (TestCoglboxClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class   = CLUTTER_ACTOR_CLASS (klass);

  gobject_class->dispose      = test_coglbox_dispose;

  actor_class->map            = test_coglbox_map;
  actor_class->paint          = test_coglbox_paint;
}

static ClutterActor*
test_coglbox_new (void)
{
  return g_object_new (TEST_TYPE_COGLBOX, NULL);
}

G_MODULE_EXPORT int
test_cogl_offscreen_main (int argc, char *argv[])
{
  ClutterActor *stage;
  ClutterActor *coglbox;

  clutter_test_init (&argc, &argv);

  /* Stage */
  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, 400, 400);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl Offscreen Buffers");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  /* Cogl Box */
  coglbox = test_coglbox_new ();
  clutter_actor_add_child (stage, coglbox);

  clutter_actor_show (stage);

  clutter_test_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_cogl_offscreen_describe (void)
{
  return "Offscreen buffer support in Cogl.";
}
