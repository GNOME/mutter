#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

/* This test assumes the GL driver supports point sizes up to 16
   pixels. Cogl should probably have some way of querying the size so
   we start from that instead */
#define MAX_POINT_SIZE 16
/* The size of the area that we'll paint each point in */
#define POINT_BOX_SIZE (MAX_POINT_SIZE * 2)

static int
calc_coord_offset (int pos, int pos_index, int point_size)
{
  switch (pos_index)
    {
    case 0: return pos - point_size / 2 - 2;
    case 1: return pos - point_size / 2 + 2;
    case 2: return pos + point_size / 2 - 2;
    case 3: return pos + point_size / 2 + 2;
    }

  g_assert_not_reached ();
  return 0;
}

static void
verify_point_size (CoglFramebuffer *framebuffer,
                   int x_pos,
                   int y_pos,
                   int point_size)
{
  int y, x;

  for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
      {
        gboolean in_point = x >= 1 && x <= 2 && y >= 1 && y <= 2;
        uint32_t expected_pixel = in_point ? 0x00ff00ff : 0xff0000ff;

        test_utils_check_pixel (framebuffer,
                                calc_coord_offset (x_pos, x, point_size),
                                calc_coord_offset (y_pos, y, point_size),
                                expected_pixel);
      }
}

static void
test_point_size (void)
{
  int fb_width = cogl_framebuffer_get_width (test_fb);
  int fb_height = cogl_framebuffer_get_height (test_fb);
  CoglColor color;
  int point_size;
  int x_pos;

  cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
  cogl_framebuffer_orthographic (test_fb,
                                 0, 0, /* x_1, y_1 */
                                 fb_width, /* x_2 */
                                 fb_height /* y_2 */,
                                 -1, 100 /* near/far */);

  cogl_framebuffer_clear4f (test_fb,
                            COGL_BUFFER_BIT_COLOR,
                            1.0f, 0.0f, 0.0f, 1.0f);

  /* Try a rendering a single point with a few different point
     sizes */
  for (x_pos = 0, point_size = MAX_POINT_SIZE;
       point_size >= 4;
       x_pos += POINT_BOX_SIZE, point_size /= 2)
    {
      CoglPipeline *pipeline = cogl_pipeline_new (test_ctx);
      CoglVertexP2 point = { x_pos + POINT_BOX_SIZE / 2,
                             POINT_BOX_SIZE / 2 };
      CoglPrimitive *prim =
        cogl_primitive_new_p2 (test_ctx,
                               COGL_VERTICES_MODE_POINTS,
                               1, /* n_vertices */
                               &point);

      cogl_pipeline_set_point_size (pipeline, point_size);
      cogl_pipeline_set_color (pipeline, &color);
      cogl_primitive_draw (prim, test_fb, pipeline);

      g_object_unref (prim);
      g_object_unref (pipeline);
    }

  /* Verify all of the points where drawn at the right size */
  for (x_pos = 0, point_size = MAX_POINT_SIZE;
       point_size >= 4;
       x_pos += POINT_BOX_SIZE, point_size /= 2)
    verify_point_size (test_fb,
                       x_pos + POINT_BOX_SIZE / 2,
                       POINT_BOX_SIZE / 2,
                       point_size);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/point-size", test_point_size);
)
