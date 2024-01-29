#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

typedef CoglVertexP2C4 Vertex;

static void
setup_orthographic_modelview (void)
{
  graphene_matrix_t matrix;
  int fb_width = cogl_framebuffer_get_width (test_fb);
  int fb_height = cogl_framebuffer_get_height (test_fb);

  /* Set up a non-identity modelview matrix. When the journal is
   * flushed it will usually flush the identity matrix. Using the
   * non-default matrix ensures that we test that Cogl restores the
   * matrix we asked for. The matrix sets up an orthographic transform
   * in the modelview matrix */

  graphene_matrix_init_ortho (&matrix,
                              0.f, fb_width,
                              fb_height, 0.f,
                              -1.f, 1.f);
  cogl_framebuffer_set_modelview_matrix (test_fb, &matrix);
}

static void
create_primitives (CoglPrimitive *primitives[2])
{
  static const Vertex vertex_data[8] =
    {
      /* triangle strip 1 */
      {   0,   0, 255, 0, 0, 255 },
      {   0, 100, 255, 0, 0, 255 },
      { 100,   0, 255, 0, 0, 255 },
      { 100, 100, 255, 0, 0, 255 },
      /* triangle strip 2 */
      { 200,   0, 0, 0, 255, 255 },
      { 200, 100, 0, 0, 255, 255 },
      { 300,   0, 0, 0, 255, 255 },
      { 300, 100, 0, 0, 255, 255 },
    };

  primitives[0] = cogl_primitive_new_p2c4 (test_ctx,
                                           COGL_VERTICES_MODE_TRIANGLE_STRIP,
                                           G_N_ELEMENTS (vertex_data),
                                           vertex_data);
  cogl_primitive_set_n_vertices (primitives[0], 4);

  primitives[1] = cogl_primitive_copy (primitives[0]);
  cogl_primitive_set_first_vertex (primitives[1], 4);
  cogl_primitive_set_n_vertices (primitives[1], 4);
}

static CoglPipeline *
create_pipeline (void)
{
  CoglColor color;
  CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
  cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
  cogl_pipeline_set_color (pipeline, &color);

  return pipeline;
}

static void
test_primitive_and_journal (void)
{
  CoglPrimitive *primitives[2];
  CoglPipeline *pipeline;

  setup_orthographic_modelview ();
  create_primitives (primitives);
  pipeline = create_pipeline ();

  /* Set a clip to clip all three rectangles to just the bottom half.
   * The journal flushes its own clip state so this verifies that the
   * clip state is correctly restored for the second primitive. */
  cogl_framebuffer_push_rectangle_clip (test_fb,
                                        0, 50, 300, 100);

  cogl_primitive_draw (primitives[0], test_fb, pipeline);

  /* Draw a rectangle using the journal in-between the two primitives.
   * This should test that the journal gets flushed correctly and that
   * the modelview matrix is restored. Half of the rectangle should be
   * overridden by the second primitive */
  cogl_framebuffer_draw_rectangle (test_fb,
                                   pipeline,
                                   100, 0, /* x1/y1 */
                                   300, 100 /* x2/y2 */);

  cogl_primitive_draw (primitives[1], test_fb, pipeline);

  /* Check the three rectangles */
  test_utils_check_region (test_fb,
                           1, 51,
                           98, 48,
                           0xff0000ff);
  test_utils_check_region (test_fb,
                           101, 51,
                           98, 48,
                           0x00ff00ff);
  test_utils_check_region (test_fb,
                           201, 51,
                           98, 48,
                           0x0000ffff);

  /* Check that the top half of all of the rectangles was clipped */
  test_utils_check_region (test_fb,
                           1, 1,
                           298, 48,
                           0x000000ff);

  cogl_framebuffer_pop_clip (test_fb);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/primitive-and-journal", test_primitive_and_journal);
)
