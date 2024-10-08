#include <clutter/clutter.h>
#include <cogl/cogl.h>
#include <string.h>

#include "test-conform-common.h"

static const CoglColor stage_color = { 0x00, 0x00, 0x00, 0xff };

#define TEX_SIZE 64

typedef struct _TestState
{
  unsigned int padding;
} TestState;

/* Creates a texture where the pixels are evenly divided between
   selecting just one of the R,G and B components */
static CoglTexture*
make_texture (void)
{
  guchar *tex_data = g_malloc (TEX_SIZE * TEX_SIZE * 3), *p = tex_data;
  CoglTexture *tex;
  int x, y;

  for (y = 0; y < TEX_SIZE; y++)
    for (x = 0; x < TEX_SIZE; x++)
      {
        memset (p, 0, 3);
        /* Set one of the components to full. The components should be
           evenly represented so that each gets a third of the
           texture */
        p[(p - tex_data) / (TEX_SIZE * TEX_SIZE * 3 / 3)] = 255;
        p += 3;
      }

  tex = test_utils_texture_new_from_data (TEX_SIZE, TEX_SIZE, TEST_UTILS_TEXTURE_NONE,
                                    COGL_PIXEL_FORMAT_RGB_888,
                                    COGL_PIXEL_FORMAT_ANY,
                                    TEX_SIZE * 3,
                                    tex_data);

  g_free (tex_data);

  return tex;
}

static void
on_paint (ClutterActor        *actor,
          ClutterPaintContext *paint_context,
          TestState           *state)
{
  CoglTexture *tex;
  CoglPipeline *pipeline;
  uint8_t pixels[8];

  tex = make_texture ();
  pipeline = cogl_pipeline_new ();
  cogl_pipeline_set_layer (pipeline, 0, tex);
  g_object_unref (tex);

  /* Render a 1x1 pixel quad without mipmaps */
  cogl_set_source (pipeline);
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_rectangle (0, 0, 1, 1);
  /* Then with mipmaps */
  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   COGL_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);
  cogl_rectangle (1, 0, 2, 1);

  g_object_unref (pipeline);

  /* Read back the two pixels we rendered */
  cogl_read_pixels (0, 0, 2, 1,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    pixels);

  /* The first pixel should be just one of the colors from the
     texture. It doesn't matter which one */
  g_assert_true ((pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0) ||
                 (pixels[0] == 0 && pixels[1] == 255 && pixels[2] == 0) ||
                 (pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255));
  /* The second pixel should be more or less the average of all of the
     pixels in the texture. Each component gets a third of the image
     so each component should be approximately 255/3 */
  g_assert_true (ABS (pixels[4] - 255 / 3) <= 3 &&
                 ABS (pixels[5] - 255 / 3) <= 3 &&
                 ABS (pixels[6] - 255 / 3) <= 3);

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
test_texture_mipmaps (TestUtilsGTestFixture *fixture,
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

  /* We force continuous redrawing of the stage, since we need to skip
   * the first few frames, and we won't be doing anything else that
   * will trigger redrawing. */
  idle_source = g_idle_add (queue_redraw, stage);

  g_signal_connect (group, "paint", G_CALLBACK (on_paint), &state);

  clutter_actor_show (stage);

  clutter_test_main ();

  g_clear_handle_id (&idle_source, g_source_remove);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
