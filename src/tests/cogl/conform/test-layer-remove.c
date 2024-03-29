#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

#define TEST_SQUARE_SIZE 10

static CoglPipeline *
create_two_layer_pipeline (void)
{
  CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
  CoglColor color;

  /* The pipeline is initially black */
  cogl_color_init_from_4f (&color, 0.0, 0.0, 0.0, 1.0);
  cogl_pipeline_set_color (pipeline, &color);

  /* The first layer adds a full red component */
  cogl_color_init_from_4f (&color, 1.0, 0.0, 0.0, 1.0);
  cogl_pipeline_set_layer_combine_constant (pipeline, 0, &color);
  cogl_pipeline_set_layer_combine (pipeline,
                                   0, /* layer_num */
                                   "RGBA=ADD(PREVIOUS,CONSTANT)",
                                   NULL);

  /* The second layer adds a full green component */
  cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
  cogl_pipeline_set_layer_combine_constant (pipeline, 1, &color);
  cogl_pipeline_set_layer_combine (pipeline,
                                   1, /* layer_num */
                                   "RGBA=ADD(PREVIOUS,CONSTANT)",
                                   NULL);

  return pipeline;
}

static void
test_color (CoglPipeline *pipeline,
            uint32_t color,
            int pos)
{
  cogl_framebuffer_draw_rectangle (test_fb,
                                   pipeline,
                                   pos * TEST_SQUARE_SIZE,
                                   0,
                                   pos * TEST_SQUARE_SIZE + TEST_SQUARE_SIZE,
                                   TEST_SQUARE_SIZE);
  test_utils_check_pixel (test_fb,
                          pos * TEST_SQUARE_SIZE + TEST_SQUARE_SIZE / 2,
                          TEST_SQUARE_SIZE / 2,
                          color);
}

static void
test_layer_remove (void)
{
  CoglPipeline *pipeline0, *pipeline1;
  CoglColor color;
  int pos = 0;

  cogl_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 cogl_framebuffer_get_width (test_fb),
                                 cogl_framebuffer_get_height (test_fb),
                                 -1,
                                 100);

  /** TEST 1 **/
  /* Basic sanity check that the pipeline combines the two colors
   * together properly */
  pipeline0 = create_two_layer_pipeline ();
  test_color (pipeline0, 0xffff00ff, pos++);
  g_object_unref (pipeline0);

  /** TEST 2 **/
  /* Check that we can remove the second layer */
  pipeline0 = create_two_layer_pipeline ();
  cogl_pipeline_remove_layer (pipeline0, 1);
  test_color (pipeline0, 0xff0000ff, pos++);
  g_object_unref (pipeline0);

  /** TEST 3 **/
  /* Check that we can remove the first layer */
  pipeline0 = create_two_layer_pipeline ();
  cogl_pipeline_remove_layer (pipeline0, 0);
  test_color (pipeline0, 0x00ff00ff, pos++);
  g_object_unref (pipeline0);

  /** TEST 4 **/
  /* Check that we can make a copy and remove a layer from the
   * original pipeline */
  pipeline0 = create_two_layer_pipeline ();
  pipeline1 = cogl_pipeline_copy (pipeline0);
  cogl_pipeline_remove_layer (pipeline0, 1);
  test_color (pipeline0, 0xff0000ff, pos++);
  test_color (pipeline1, 0xffff00ff, pos++);
  g_object_unref (pipeline0);
  g_object_unref (pipeline1);

  /** TEST 5 **/
  /* Check that we can make a copy and remove the second layer from the
   * new pipeline */
  pipeline0 = create_two_layer_pipeline ();
  pipeline1 = cogl_pipeline_copy (pipeline0);
  cogl_pipeline_remove_layer (pipeline1, 1);
  test_color (pipeline0, 0xffff00ff, pos++);
  test_color (pipeline1, 0xff0000ff, pos++);
  g_object_unref (pipeline0);
  g_object_unref (pipeline1);

  /** TEST 6 **/
  /* Check that we can make a copy and remove the first layer from the
   * new pipeline */
  pipeline0 = create_two_layer_pipeline ();
  pipeline1 = cogl_pipeline_copy (pipeline0);
  cogl_pipeline_remove_layer (pipeline1, 0);
  test_color (pipeline0, 0xffff00ff, pos++);
  test_color (pipeline1, 0x00ff00ff, pos++);
  g_object_unref (pipeline0);
  g_object_unref (pipeline1);

  /** TEST 7 **/
  /* Check that we can modify a layer in a child pipeline */
  pipeline0 = create_two_layer_pipeline ();
  pipeline1 = cogl_pipeline_copy (pipeline0);
  cogl_color_init_from_4f (&color, 0.0, 0.0, 1.0, 1.0);
  cogl_pipeline_set_layer_combine_constant (pipeline1, 0, &color);
  test_color (pipeline0, 0xffff00ff, pos++);
  test_color (pipeline1, 0x00ffffff, pos++);
  g_object_unref (pipeline0);
  g_object_unref (pipeline1);

  /** TEST 8 **/
  /* Check that we can modify a layer in a child pipeline but then remove it */
  pipeline0 = create_two_layer_pipeline ();
  pipeline1 = cogl_pipeline_copy (pipeline0);
  cogl_color_init_from_4f (&color, 0.0, 0.0, 1.0, 1.0);
  cogl_pipeline_set_layer_combine_constant (pipeline1, 0, &color);
  cogl_pipeline_remove_layer (pipeline1, 0);
  test_color (pipeline0, 0xffff00ff, pos++);
  test_color (pipeline1, 0x00ff00ff, pos++);
  g_object_unref (pipeline0);
  g_object_unref (pipeline1);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/layer/remove", test_layer_remove);
)
