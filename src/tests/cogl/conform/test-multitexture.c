#include <clutter/clutter.h>
#include <cogl/cogl.h>
#include <string.h>

#include "test-conform-common.h"

static const CoglColor stage_color = { 0x0, 0x0, 0x0, 0xff };

#define QUAD_WIDTH 20

#define RED   0
#define GREEN 1
#define BLUE  2
#define ALPHA 3

typedef struct _TestState
{
  unsigned int padding;
} TestState;

static void
assert_region_color (int x,
                     int y,
                     int width,
                     int height,
                     uint8_t red,
                     uint8_t green,
                     uint8_t blue,
                     uint8_t alpha)
{
  uint8_t *data = g_malloc0 (width * height * 4);
  cogl_read_pixels (x, y, width, height,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    data);
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        uint8_t *pixel = &data[y * width * 4 + x * 4];
#if 1
        g_assert_true (pixel[RED] == red &&
                       pixel[GREEN] == green &&
                       pixel[BLUE] == blue);
#endif
      }
  g_free (data);
}

/* Creates a texture divided into 4 quads with colors arranged as follows:
 * (The same value are used in all channels for each texel)
 *
 * |-----------|
 * |0x11 |0x00 |
 * |+ref |     |
 * |-----------|
 * |0x00 |0x33 |
 * |     |+ref |
 * |-----------|
 *
 *
 */
static CoglTexture*
make_texture (guchar ref)
{
  int x;
  int y;
  guchar *tex_data, *p;
  CoglTexture *tex;
  guchar val;

  tex_data = g_malloc (QUAD_WIDTH * QUAD_WIDTH * 16);

  for (y = 0; y < QUAD_WIDTH * 2; y++)
    for (x = 0; x < QUAD_WIDTH * 2; x++)
      {
        p = tex_data + (QUAD_WIDTH * 8 * y) + x * 4;
        if (x < QUAD_WIDTH && y < QUAD_WIDTH)
          val = 0x11 + ref;
        else if (x >= QUAD_WIDTH && y >= QUAD_WIDTH)
          val = 0x33 + ref;
        else
          val = 0x00;
        p[0] = p[1] = p[2] = p[3] = val;
      }

  /* Note: we don't use COGL_PIXEL_FORMAT_ANY for the internal format here
   * since we don't want to allow Cogl to premultiply our data. */
  tex = test_utils_texture_new_from_data (QUAD_WIDTH * 2,
                                    QUAD_WIDTH * 2,
                                    TEST_UTILS_TEXTURE_NONE,
                                    COGL_PIXEL_FORMAT_RGBA_8888,
                                    COGL_PIXEL_FORMAT_RGBA_8888,
                                    QUAD_WIDTH * 8,
                                    tex_data);

  g_free (tex_data);

  return tex;
}

static void
on_paint (ClutterActor        *actor,
          ClutterPaintContext *paint_context,
          TestState           *state)
{
  CoglTexture *tex0, *tex1;
  CoglPipeline *pipeline;
  CoglColor color;
  gboolean status;
  GError *error = NULL;
  float tex_coords[] = {
    0, 0, 0.5, 0.5, /* tex0 */
    0.5, 0.5, 1, 1 /* tex1 */
  };

  tex0 = make_texture (0x00);
  tex1 = make_texture (0x11);

  pipeline = cogl_pipeline_new ();

  /* An arbitrary color which should be replaced by the first texture layer */
  cogl_color_init_from_4f (&color,
                           128.0 / 255.0, 128.0 / 255.0,
                           128.0 / 255.0, 128.0 / 255.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_pipekine_set_blend (pipeline, "RGBA = ADD (SRC_COLOR, 0)", NULL);

  cogl_pipeline_set_layer_texture (pipeline, 0, tex0);
  cogl_pipeline_set_layer_combine (pipeline, 0,
                                   "RGBA = REPLACE (TEXTURE)", NULL);
  /* We'll use nearest filtering mode on the textures, otherwise the
     edge of the quad can pull in texels from the neighbouring
     quarters of the texture due to imprecision */
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  cogl_pipeline_set_layer (pipeline, 1, tex1);
  cogl_pipeline_set_layer_filters (pipeline, 1,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  status = cogl_pipeline_set_layer_combine (pipeline, 1,
                                            "RGBA = ADD (PREVIOUS, TEXTURE)",
                                            &error);
  if (!status)
    {
      /* It's not strictly a test failure; you need a more capable GPU or
       * driver to test this texture combine string. */
      g_debug ("Failed to setup texture combine string "
               "RGBA = ADD (PREVIOUS, TEXTURE): %s",
               error->message);
    }

  cogl_set_source (pipeline);
  cogl_rectangle_with_multitexture_coords (0, 0, QUAD_WIDTH, QUAD_WIDTH,
                                           tex_coords, 8);

  g_object_unref (pipeline);
  g_object_unref (tex0);
  g_object_unref (tex1);

  /* See what we got... */

  assert_region_color (0, 0, QUAD_WIDTH, QUAD_WIDTH,
                       0x55, 0x55, 0x55, 0x55);

  /* Comment this out if you want visual feedback for what this test paints */
#if 1
  clutter_test_quit ();
#endif
}

static gboolean
queue_redraw (void *stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

void
test_multitexture (TestUtilsGTestFixture *fixture,
                        void *data)
{
  TestState state;
  ClutterActor *stage;
  ClutterActor *group;
  unsigned int idle_source;

  stage = clutter_stage_get_default ();

  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), &stage_color);

  group = clutter_actor_new ();
  clutter_actor_add_child (stage, group);

  /* We force continuous redrawing in case someone comments out the
   * clutter_test_quit and wants visual feedback for the test since we
   * won't be doing anything else that will trigger redrawing. */
  idle_source = g_idle_add (queue_redraw, stage);

  g_signal_connect (group, "paint", G_CALLBACK (on_paint), &state);

  clutter_actor_show (stage);

  clutter_test_main ();

  g_clear_handle_id (&idle_source, g_source_remove);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
