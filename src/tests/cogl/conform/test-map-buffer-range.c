#include <cogl/cogl.h>

#include <string.h>

#include "tests/cogl-test-utils.h"

static uint8_t
tex_data[2 * 2 * 4] =
  {
    0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
    0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff
  };

/* Vertex data for a quad with all of the texture coordinates set to
 * the top left (red) pixel */
static CoglVertexP2T2
vertex_data[4] =
  {
    { -1, -1, 0, 0 },
    { 1, -1, 0, 0 },
    { -1, 1, 0, 0 },
    { 1, 1, 0, 0 }
  };

static void
test_map_buffer_range (void)
{
  CoglTexture *tex;
  CoglPipeline *pipeline;
  int fb_width, fb_height;
  CoglAttributeBuffer *buffer;
  CoglVertexP2T2 *data;
  CoglAttribute *pos_attribute;
  CoglAttribute *tex_coord_attribute;
  CoglPrimitive *primitive;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE))
    {
      g_test_skip ("Missing map buffer for write capability");
      return;
    }

  tex = cogl_texture_2d_new_from_data (test_ctx,
                                       2, 2, /* width/height */
                                       COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                       2 * 4, /* rowstride */
                                       tex_data,
                                       NULL /* error */);

  pipeline = cogl_pipeline_new (test_ctx);

  cogl_pipeline_set_layer_texture (pipeline, 0, tex);
  cogl_pipeline_set_layer_filters (pipeline,
                                   0, /* layer */
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_pipeline_set_layer_wrap_mode (pipeline,
                                     0, /* layer */
                                     COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);

  fb_width = cogl_framebuffer_get_width (test_fb);
  fb_height = cogl_framebuffer_get_height (test_fb);

  buffer = cogl_attribute_buffer_new (test_ctx,
                                      sizeof (vertex_data),
                                      vertex_data);

  /* Replace the texture coordinates of the third vertex with the
   * coordinates for a green texel */
  data = cogl_buffer_map_range (COGL_BUFFER (buffer),
                                sizeof (vertex_data[0]) * 2,
                                sizeof (vertex_data[0]),
                                COGL_BUFFER_ACCESS_WRITE,
                                COGL_BUFFER_MAP_HINT_DISCARD_RANGE,
                                NULL); /* don't catch errors */
  g_assert (data != NULL);

  data->x = vertex_data[2].x;
  data->y = vertex_data[2].y;
  data->s = 1.0f;
  data->t = 0.0f;

  cogl_buffer_unmap (COGL_BUFFER (buffer));

  pos_attribute =
    cogl_attribute_new (buffer,
                        "cogl_position_in",
                        sizeof (vertex_data[0]),
                        offsetof (CoglVertexP2T2, x),
                        2, /* n_components */
                        COGL_ATTRIBUTE_TYPE_FLOAT);
  tex_coord_attribute =
    cogl_attribute_new (buffer,
                        "cogl_tex_coord_in",
                        sizeof (vertex_data[0]),
                        offsetof (CoglVertexP2T2, s),
                        2, /* n_components */
                        COGL_ATTRIBUTE_TYPE_FLOAT);

  cogl_framebuffer_clear4f (test_fb,
                            COGL_BUFFER_BIT_COLOR,
                            0, 0, 0, 1);

  primitive =
    cogl_primitive_new (COGL_VERTICES_MODE_TRIANGLE_STRIP,
                        4, /* n_vertices */
                        pos_attribute,
                        tex_coord_attribute,
                        NULL);
  cogl_primitive_draw (primitive, test_fb, pipeline);
  g_object_unref (primitive);

  /* Top left pixel should be the one that is replaced to be green */
  test_utils_check_pixel (test_fb, 1, 1, 0x00ff00ff);
  /* The other three corners should be left as red */
  test_utils_check_pixel (test_fb, fb_width - 2, 1, 0xff0000ff);
  test_utils_check_pixel (test_fb, 1, fb_height - 2, 0xff0000ff);
  test_utils_check_pixel (test_fb, fb_width - 2, fb_height - 2, 0xff0000ff);

  g_object_unref (buffer);
  g_object_unref (pos_attribute);
  g_object_unref (tex_coord_attribute);

  g_object_unref (pipeline);
  g_object_unref (tex);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/map-buffer-range", test_map_buffer_range);
)
