
#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "test-conform-common.h"

/* This test verifies that interleved attributes work with the vertex buffer
 * API. We add (x,y) GLfloat vertices, interleved with RGBA GLubyte color
 * attributes to a buffer, submit and draw.
 *
 * If you want visual feedback of what this test paints for debugging purposes,
 * then remove the call to clutter_main_quit() in validate_result.
 */

typedef struct _TestState
{
  CoglHandle buffer;
  ClutterGeometry stage_geom;
} TestState;

typedef struct _InterlevedVertex
{
  GLfloat x;
  GLfloat y;

  GLubyte r;
  GLubyte g;
  GLubyte b;
  GLubyte a;
} InterlevedVertex;


static void
validate_result (TestState *state)
{
  GLubyte pixel[4];
  GLint y_off = 90;

  /* NB: We ignore the alpha, since we don't know if our render target is
   * RGB or RGBA */

#define RED 0
#define GREEN 1
#define BLUE 2

  /* Should see a blue pixel */
  cogl_read_pixels (10, y_off, 1, 1,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    pixel);
  if (cogl_test_verbose ())
    g_print ("pixel 0 = %x, %x, %x\n", pixel[RED], pixel[GREEN], pixel[BLUE]);
  g_assert (pixel[RED] == 0 && pixel[GREEN] == 0 && pixel[BLUE] != 0);

#undef RED
#undef GREEN
#undef BLUE

  /* Comment this out if you want visual feedback of what this test
   * paints.
   */
  clutter_main_quit ();
}

static void
on_paint (ClutterActor *actor, TestState *state)
{
  /* Draw a faded blue triangle */
  cogl_vertex_buffer_draw (state->buffer,
			   GL_TRIANGLE_STRIP, /* mode */
			   0, /* first */
			   3); /* count */

  validate_result (state);
}

static gboolean
queue_redraw (gpointer stage)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));

  return TRUE;
}

void
test_vertex_buffer_interleved (TestUtilsGTestFixture *fixture,
		                    void *data)
{
  TestState state;
  ClutterActor *stage;
  ClutterColor stage_clr = {0x0, 0x0, 0x0, 0xff};
  ClutterActor *group;
  unsigned int idle_source;

  stage = clutter_stage_get_default ();

  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_clr);
  clutter_actor_get_geometry (stage, &state.stage_geom);

  group = clutter_group_new ();
  clutter_actor_set_size (group,
			  state.stage_geom.width,
			  state.stage_geom.height);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), group);

  /* We force continuous redrawing incase someone comments out the
   * clutter_main_quit and wants visual feedback for the test since we
   * wont be doing anything else that will trigger redrawing. */
  idle_source = g_idle_add (queue_redraw, stage);

  g_signal_connect (group, "paint", G_CALLBACK (on_paint), &state);

  {
    InterlevedVertex verts[3] =
      {
	{ /* .x = */ 0.0, /* .y = */ 0.0,
	  /* blue */
	  /* .r = */ 0x00, /* .g = */ 0x00, /* .b = */ 0xff, /* .a = */ 0xff },

	{ /* .x = */ 100.0, /* .y = */ 100.0,
	  /* transparent blue */
	  /* .r = */ 0x00, /* .g = */ 0x00, /* .b = */ 0xff, /* .a = */ 0x00 },

	{ /* .x = */ 0.0, /* .y = */ 100.0,
	  /* transparent blue */
	  /* .r = */ 0x00, /* .g = */ 0x00, /* .b = */ 0xff, /* .a = */ 0x00 },
      };

    /* We assume the compiler is doing no funny struct padding for this test:
     */
    g_assert (sizeof (InterlevedVertex) == 12);

    state.buffer = cogl_vertex_buffer_new (3 /* n vertices */);
    cogl_vertex_buffer_add (state.buffer,
			    "gl_Vertex",
			    2, /* n components */
			    GL_FLOAT,
			    FALSE, /* normalized */
                            12, /* stride */
			    &verts[0].x);
    cogl_vertex_buffer_add (state.buffer,
                            "gl_Color",
			    4, /* n components */
			    GL_UNSIGNED_BYTE,
			    FALSE, /* normalized */
			    12, /* stride */
			    &verts[0].r);
    cogl_vertex_buffer_submit (state.buffer);
  }

  clutter_actor_show_all (stage);

  clutter_main ();

  cogl_handle_unref (state.buffer);

  g_source_remove (idle_source);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

