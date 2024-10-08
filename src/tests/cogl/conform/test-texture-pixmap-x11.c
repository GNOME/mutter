#include <clutter/clutter.h>

#include "test-conform-common.h"

static const CoglColor stage_color = { 0x00, 0x00, 0x00, 0xff };

#ifdef HAVE_X11

#include <clutter/x11/clutter-x11.h>
#include <cogl/cogl-texture-pixmap-x11.h>

#define PIXMAP_WIDTH 512
#define PIXMAP_HEIGHT 256
#define GRID_SQUARE_SIZE 16

/* Coordinates of a square that we'll update */
#define PIXMAP_CHANGE_X 1
#define PIXMAP_CHANGE_Y 1

typedef struct _TestState
{
  ClutterActor *stage;
  CoglTexture *tfp;
  Pixmap pixmap;
  unsigned int frame_count;
  Display *display;
} TestState;

static Pixmap
create_pixmap (TestState *state)
{
  Pixmap pixmap;
  XGCValues gc_values = { 0, };
  GC black_gc, white_gc;
  int screen = DefaultScreen (state->display);
  int x, y;

  pixmap = XCreatePixmap (state->display,
                          DefaultRootWindow (state->display),
                          PIXMAP_WIDTH, PIXMAP_HEIGHT,
                          DefaultDepth (state->display, screen));

  gc_values.foreground = BlackPixel (state->display, screen);
  black_gc = XCreateGC (state->display, pixmap, GCForeground, &gc_values);
  gc_values.foreground = WhitePixel (state->display, screen);
  white_gc = XCreateGC (state->display, pixmap, GCForeground, &gc_values);

  /* Draw a grid of alternative black and white rectangles to the
     pixmap */
  for (y = 0; y < PIXMAP_HEIGHT / GRID_SQUARE_SIZE; y++)
    for (x = 0; x < PIXMAP_WIDTH / GRID_SQUARE_SIZE; x++)
      XFillRectangle (state->display, pixmap,
                  ((x ^ y) & 1) ? black_gc : white_gc,
                  x * GRID_SQUARE_SIZE,
                  y * GRID_SQUARE_SIZE,
                  GRID_SQUARE_SIZE,
                  GRID_SQUARE_SIZE);

  XFreeGC (state->display, black_gc);
  XFreeGC (state->display, white_gc);

  return pixmap;
}

static void
update_pixmap (TestState *state)
{
  XGCValues gc_values = { 0, };
  GC black_gc;
  int screen = DefaultScreen (state->display);

  gc_values.foreground = BlackPixel (state->display, screen);
  black_gc = XCreateGC (state->display, state->pixmap,
                        GCForeground, &gc_values);

  /* Fill in one the rectangles with black */
  XFillRectangle (state->display, state->pixmap,
                  black_gc,
                  PIXMAP_CHANGE_X * GRID_SQUARE_SIZE,
                  PIXMAP_CHANGE_Y * GRID_SQUARE_SIZE,
                  GRID_SQUARE_SIZE, GRID_SQUARE_SIZE);

  XFreeGC (state->display, black_gc);
}

static gboolean
check_paint (TestState *state, int x, int y, int scale)
{
  uint8_t *data, *p, update_value = 0;

  p = data = g_malloc (PIXMAP_WIDTH * PIXMAP_HEIGHT * 4);

  cogl_read_pixels (x, y, PIXMAP_WIDTH / scale, PIXMAP_HEIGHT / scale,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    data);

  for (y = 0; y < PIXMAP_HEIGHT / scale; y++)
    for (x = 0; x < PIXMAP_WIDTH / scale; x++)
      {
        int grid_x = x * scale / GRID_SQUARE_SIZE;
        int grid_y = y * scale / GRID_SQUARE_SIZE;

        /* If this is the updatable square then we'll let it be either
           color but we'll return which one it was */
        if (grid_x == PIXMAP_CHANGE_X && grid_y == PIXMAP_CHANGE_Y)
          {
            if (x % (GRID_SQUARE_SIZE / scale) == 0 &&
                y % (GRID_SQUARE_SIZE / scale) == 0)
              update_value = *p;
            else
              g_assert_cmpint (p[0], ==, update_value);

            g_assert_true (p[1] == update_value);
            g_assert_true (p[2] == update_value);
            p += 4;
          }
        else
          {
            uint8_t value = ((grid_x ^ grid_y) & 1) ? 0x00 : 0xff;
            g_assert_cmpint (*(p++), ==, value);
            g_assert_cmpint (*(p++), ==, value);
            g_assert_cmpint (*(p++), ==, value);
            p++;
          }
      }

  g_free (data);

  return update_value == 0x00;
}

/* We skip these frames first */
#define FRAME_COUNT_BASE 5
/* First paint the tfp with no mipmaps */
#define FRAME_COUNT_NORMAL 6
/* Then use mipmaps */
#define FRAME_COUNT_MIPMAP 7
/* After this frame will start waiting for the pixmap to change */
#define FRAME_COUNT_UPDATED 8

static void
on_after_paint (ClutterActor     *actor,
                ClutterStageView *view,
                ClutterFrame     *frame,
                TestState        *state)
{
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new ();
  cogl_pipeline_set_layer (pipeline, 0, state->tfp);
  if (state->frame_count == FRAME_COUNT_MIPMAP)
    {
      const CoglPipelineFilter min_filter =
        COGL_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST;
      cogl_pipeline_set_layer_filters (pipeline, 0,
                                       min_filter,
                                       COGL_PIPELINE_FILTER_NEAREST);
    }
  else
    cogl_pipeline_set_layer_filters (pipeline, 0,
                                     COGL_PIPELINE_FILTER_NEAREST,
                                     COGL_PIPELINE_FILTER_NEAREST);
  cogl_set_source (pipeline);

  cogl_rectangle (0, 0, PIXMAP_WIDTH, PIXMAP_HEIGHT);

  cogl_rectangle (0, PIXMAP_HEIGHT,
                  PIXMAP_WIDTH / 4, PIXMAP_HEIGHT * 5 / 4);

  if (state->frame_count >= 5)
    {
      gboolean big_updated, small_updated;

      big_updated = check_paint (state, 0, 0, 1);
      small_updated = check_paint (state, 0, PIXMAP_HEIGHT, 4);

      g_assert_true (big_updated == small_updated);

      if (state->frame_count < FRAME_COUNT_UPDATED)
        g_assert_true (big_updated == FALSE);
      else if (state->frame_count == FRAME_COUNT_UPDATED)
        /* Change the pixmap and keep drawing until it updates */
        update_pixmap (state);
      else if (big_updated)
        /* If we successfully got the update then the test is over */
        clutter_test_quit ();
    }

  state->frame_count++;
}

static gboolean
queue_redraw (void *stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

#endif /* HAVE_X11 */

void
test_texture_pixmap_x11 (TestUtilsGTestFixture *fixture,
                              void *data)
{
#ifdef HAVE_X11

  TestState state;
  unsigned int idle_handler;
  unsigned long paint_handler;

  state.frame_count = 0;
  state.stage = clutter_stage_get_default ();

  state.display = clutter_x11_get_default_display ();

  state.pixmap = create_pixmap (&state);
  state.tfp = cogl_texture_pixmap_x11_new (state.pixmap, TRUE);

  clutter_actor_set_background_color (CLUTTER_ACTOR (state.stage), &stage_color);

  paint_handler = g_signal_connect (CLUTTER_STAGE (state.stage), "after-paint",
                                    G_CALLBACK (on_after_paint), &state);

  idle_handler = g_idle_add (queue_redraw, state.stage);

  clutter_actor_show (state.stage);

  clutter_test_main ();

  g_clear_signal_handler (&paint_handler, state.stage);

  g_clear_handle_id (&idle_handler, g_source_remove);

  XFreePixmap (state.display, state.pixmap);

  if (cogl_test_verbose ())
    g_print ("OK\n");

#else /* HAVE_X11 */

  if (cogl_test_verbose ())
   g_print ("Skipping\n");

#endif /* HAVE_X11 */
}

