
#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "test-conform-common.h"

/* This test verifies that modifying a vertex buffer works, by updating
 * vertex positions, and deleting and re-adding different color attributes.
 *
 * If you want visual feedback of what this test paints for debugging purposes,
 * then remove the call to clutter_main_quit() in validate_result.
 */

typedef struct _TestState
{
  CoglHandle buffer;
  ClutterGeometry stage_geom;
} TestState;

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

  /* Should see a red pixel */
  cogl_read_pixels (110, y_off, 1, 1,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    pixel);
  if (cogl_test_verbose ())
    g_print ("pixel 0 = %x, %x, %x\n", pixel[RED], pixel[GREEN], pixel[BLUE]);
  g_assert (pixel[RED] != 0 && pixel[GREEN] == 0 && pixel[BLUE] == 0);

  /* Should see a green pixel */
  cogl_read_pixels (210, y_off, 1, 1,
                    COGL_READ_PIXELS_COLOR_BUFFER,
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                    pixel);
  if (cogl_test_verbose ())
    g_print ("pixel 1 = %x, %x, %x\n", pixel[RED], pixel[GREEN], pixel[BLUE]);
  g_assert (pixel[RED] == 0 && pixel[GREEN] != 0 && pixel[BLUE] == 0);

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
  GLfloat triangle_verts[3][2] =
    {
      {100.0, 0.0},
      {200.0, 100.0},
      {100.0, 100.0}
    };
  GLbyte triangle_colors[3][4] =
    {
      {0x00, 0xff, 0x00, 0xff}, /* blue */
      {0x00, 0xff, 0x00, 0x00}, /* transparent blue */
      {0x00, 0xff, 0x00, 0x00}  /* transparent blue */
    };

  /*
   * Draw a red triangle
   */

  cogl_set_source_color4ub (0xff, 0x00, 0x00, 0xff);

  cogl_vertex_buffer_add (state->buffer,
			  "gl_Vertex",
			  2, /* n components */
			  GL_FLOAT,
			  FALSE, /* normalized */
			  0, /* stride */
			  triangle_verts);
  cogl_vertex_buffer_delete (state->buffer, "gl_Color");
  cogl_vertex_buffer_submit (state->buffer);

  cogl_vertex_buffer_draw (state->buffer,
			   GL_TRIANGLE_STRIP, /* mode */
			   0, /* first */
			   3); /* count */

  /*
   * Draw a faded green triangle
   */

  cogl_vertex_buffer_add (state->buffer,
			  "gl_Color",
			  4, /* n components */
			  GL_UNSIGNED_BYTE,
			  FALSE, /* normalized */
			  0, /* stride */
			  triangle_colors);
  cogl_vertex_buffer_submit (state->buffer);

  cogl_translate (100, 0, 0);
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
test_vertex_buffer_mutability (TestUtilsGTestFixture *fixture,
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
    GLfloat triangle_verts[3][2] =
      {
	{0.0,	0.0},
	{100.0, 100.0},
	{0.0,	100.0}
      };
    GLbyte triangle_colors[3][4] =
      {
	{0x00, 0x00, 0xff, 0xff}, /* blue */
	{0x00, 0x00, 0xff, 0x00}, /* transparent blue */
	{0x00, 0x00, 0xff, 0x00}  /* transparent blue */
      };
    state.buffer = cogl_vertex_buffer_new (3 /* n vertices */);
    cogl_vertex_buffer_add (state.buffer,
			    "gl_Vertex",
			    2, /* n components */
			    GL_FLOAT,
			    FALSE, /* normalized */
			    0, /* stride */
			    triangle_verts);
    cogl_vertex_buffer_add (state.buffer,
			    "gl_Color",
			    4, /* n components */
			    GL_UNSIGNED_BYTE,
			    FALSE, /* normalized */
			    0, /* stride */
			    triangle_colors);
    cogl_vertex_buffer_submit (state.buffer);
  }

  clutter_actor_show_all (stage);

  clutter_main ();

  cogl_handle_unref (state.buffer);

  g_source_remove (idle_source);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

