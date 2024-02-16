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

  CoglTexture *sliced_tex;
  CoglTexture *not_sliced_tex;
  gint       frame;
  gboolean   use_sliced;
  gboolean   use_linear_filtering;
};

G_DEFINE_TYPE (TestCoglbox, test_coglbox, CLUTTER_TYPE_ACTOR);


int
test_cogl_tex_polygon_main (int argc, char *argv[]);

const char *
test_cogl_tex_polygon_describe (void);

G_END_DECLS

/* Coglbox implementation
 *--------------------------------------------------*/

static void
test_coglbox_fade_texture (CoglFramebuffer *framebuffer,
                           CoglPipeline    *pipeline,
                           float            x1,
                           float            y1,
                           float            x2,
                           float            y2,
                           float            tx1,
                           float            ty1,
                           float            tx2,
                           float            ty2)
{
  CoglVertexP3T2C4 vertices[4];
  CoglPrimitive *primitive;
  int i;

  vertices[0].x = x1;
  vertices[0].y = y1;
  vertices[0].z = 0;
  vertices[0].s = tx1;
  vertices[0].t = ty1;
  vertices[1].x = x1;
  vertices[1].y = y2;
  vertices[1].z = 0;
  vertices[1].s = tx1;
  vertices[1].t = ty2;
  vertices[2].x = x2;
  vertices[2].y = y2;
  vertices[2].z = 0;
  vertices[2].s = tx2;
  vertices[2].t = ty2;
  vertices[3].x = x2;
  vertices[3].y = y1;
  vertices[3].z = 0;
  vertices[3].s = tx2;
  vertices[3].t = ty1;

  for (i = 0; i < 4; i++)
    {
      CoglColor cogl_color;

      cogl_color_init_from_4f (&cogl_color,
                               1.0,
                               1.0,
                               1.0,
                               ((i ^ (i >> 1)) & 1) ? 0.0 : 128.0 / 255.0);
      cogl_color_premultiply (&cogl_color);
      vertices[i].r = cogl_color_get_red (&cogl_color) * 255.0;
      vertices[i].g = cogl_color_get_green (&cogl_color) * 255.0;
      vertices[i].b = cogl_color_get_blue (&cogl_color) * 255.0;
      vertices[i].a = cogl_color_get_alpha (&cogl_color) * 255.0;
    }

  primitive =
    cogl_primitive_new_p3t2c4 (cogl_framebuffer_get_context (framebuffer),
                               COGL_VERTICES_MODE_TRIANGLE_FAN,
                               4,
                               vertices);
  cogl_primitive_draw (primitive, framebuffer, pipeline);
  g_object_unref (primitive);
}

static void
test_coglbox_triangle_texture (CoglFramebuffer *framebuffer,
                               CoglPipeline    *pipeline,
                               int              tex_width,
                               int              tex_height,
                               float            x,
                               float            y,
                               float            tx1,
                               float            ty1,
                               float            tx2,
                               float            ty2,
                               float            tx3,
                               float            ty3)
{
  CoglVertexP3T2 vertices[3];
  CoglPrimitive *primitive;

  vertices[0].x = x + tx1 * tex_width;
  vertices[0].y = y + ty1 * tex_height;
  vertices[0].z = 0;
  vertices[0].s = tx1;
  vertices[0].t = ty1;

  vertices[1].x = x + tx2 * tex_width;
  vertices[1].y = y + ty2 * tex_height;
  vertices[1].z = 0;
  vertices[1].s = tx2;
  vertices[1].t = ty2;

  vertices[2].x = x + tx3 * tex_width;
  vertices[2].y = y + ty3 * tex_height;
  vertices[2].z = 0;
  vertices[2].s = tx3;
  vertices[2].t = ty3;

  primitive = cogl_primitive_new_p3t2 (cogl_framebuffer_get_context (framebuffer),
                                       COGL_VERTICES_MODE_TRIANGLE_FAN,
                                       3,
                                       vertices);
  cogl_primitive_draw (primitive, framebuffer, pipeline);
  g_object_unref (primitive);
}

static void
test_coglbox_paint (ClutterActor        *self,
                    ClutterPaintContext *paint_context)
{
  TestCoglbox *coglbox = TEST_COGLBOX (self);
  CoglTexture *tex_handle = coglbox->use_sliced ? coglbox->sliced_tex
                                                : coglbox->not_sliced_tex;
  int tex_width = cogl_texture_get_width (tex_handle);
  int tex_height = cogl_texture_get_height (tex_handle);
  CoglPipeline *pipeline;
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_texture (pipeline, 0, tex_handle);

  cogl_pipeline_set_layer_filters (pipeline, 0,
                                   coglbox->use_linear_filtering
                                   ? COGL_PIPELINE_FILTER_LINEAR :
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   coglbox->use_linear_filtering
                                   ? COGL_PIPELINE_FILTER_LINEAR :
                                   COGL_PIPELINE_FILTER_NEAREST);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_translate (framebuffer, tex_width / 2, 0, 0);
  cogl_framebuffer_rotate (framebuffer, coglbox->frame, 0, 1, 0);
  cogl_framebuffer_translate (framebuffer, -tex_width / 2, 0, 0);

  /* Draw a hand and reflect it */
  cogl_framebuffer_draw_textured_rectangle (framebuffer, pipeline,
                                            0, 0, tex_width, tex_height,
                                            0, 0, 1, 1);
  test_coglbox_fade_texture (framebuffer, pipeline,
                             0, tex_height,
                             tex_width, (tex_height * 3 / 2),
                             0.0, 1.0,
                             1.0, 0.5);

  cogl_framebuffer_pop_matrix (framebuffer);

  cogl_framebuffer_push_matrix (framebuffer);
  cogl_framebuffer_translate (framebuffer, tex_width * 3 / 2 + 60, 0, 0);
  cogl_framebuffer_rotate (framebuffer, coglbox->frame, 0, 1, 0);
  cogl_framebuffer_translate (framebuffer, -tex_width / 2 - 10, 0, 0);

  /* Draw the texture split into two triangles */
  test_coglbox_triangle_texture (framebuffer, pipeline,
                                 tex_width, tex_height,
                                 0, 0,
                                 0, 0,
                                 0, 1,
                                 1, 1);
  test_coglbox_triangle_texture (framebuffer, pipeline,
                                 tex_width, tex_height,
                                 20, 0,
                                 0, 0,
                                 1, 0,
                                 1, 1);

  cogl_framebuffer_pop_matrix (framebuffer);

  g_object_unref (pipeline);
}

static void
test_coglbox_dispose (GObject *object)
{
  TestCoglbox *coglbox = TEST_COGLBOX (object);

  g_object_unref (coglbox->not_sliced_tex);
  g_object_unref (coglbox->sliced_tex);

  G_OBJECT_CLASS (test_coglbox_parent_class)->dispose (object);
}

static void
test_coglbox_init (TestCoglbox *self)
{
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  GError *error = NULL;
  gchar *file;

  self->use_linear_filtering = FALSE;
  self->use_sliced = FALSE;

  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  self->sliced_tex =
    clutter_test_texture_2d_sliced_new_from_file (ctx, file,
                                                  &error);
  if (self->sliced_tex == NULL)
    {
      if (error)
        {
          g_warning ("Texture loading failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      else
        g_warning ("Texture loading failed: <unknown>");
    }

  self->not_sliced_tex = clutter_test_texture_2d_new_from_file (ctx, file, &error);
  if (self->not_sliced_tex == NULL)
    {
      if (error)
        {
          g_warning ("Texture loading failed: %s", error->message);
          g_error_free (error);
        }
      else
        g_warning ("Texture loading failed: <unknown>");
    }

  g_free (file);
}

static void
test_coglbox_class_init (TestCoglboxClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class   = CLUTTER_ACTOR_CLASS (klass);

  gobject_class->dispose      = test_coglbox_dispose;
  actor_class->paint          = test_coglbox_paint;
}

static ClutterActor*
test_coglbox_new (void)
{
  return g_object_new (TEST_TYPE_COGLBOX, NULL);
}

static void
frame_cb (ClutterTimeline *timeline,
          int              elapsed_msecs,
          gpointer         data)
{
  TestCoglbox *coglbox = TEST_COGLBOX (data);
  gdouble progress = clutter_timeline_get_progress (timeline);

  coglbox->frame = 360.0 * progress;
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

static void
update_toggle_text (ClutterText *button, gboolean val)
{
  clutter_text_set_text (button, val ? "Enabled" : "Disabled");
}

static gboolean
on_toggle_click (ClutterActor *button, ClutterEvent *event,
         gboolean *toggle_val)
{
  update_toggle_text (CLUTTER_TEXT (button), *toggle_val = !*toggle_val);

  return TRUE;
}

static ClutterActor *
make_toggle (const char *label_text, gboolean *toggle_val)
{
  ClutterActor *group = clutter_actor_new ();
  ClutterActor *label = clutter_text_new_with_text ("Sans 14", label_text);
  ClutterActor *button = clutter_text_new_with_text ("Sans 14", "");

  clutter_actor_set_reactive (button, TRUE);

  update_toggle_text (CLUTTER_TEXT (button), *toggle_val);

  clutter_actor_set_position (button, clutter_actor_get_width (label) + 10, 0);
  clutter_actor_add_child (group, label);
  clutter_actor_add_child (group, button);

  g_signal_connect (button, "button-press-event", G_CALLBACK (on_toggle_click),
                    toggle_val);

  return group;
}

G_MODULE_EXPORT int
test_cogl_tex_polygon_main (int argc, char *argv[])
{
  ClutterActor     *stage;
  TestCoglbox      *coglbox;
  ClutterActor     *filtering_toggle;
  ClutterActor     *slicing_toggle;
  ClutterActor     *note;
  ClutterTimeline  *timeline;
  ClutterColor      blue = { 0x30, 0x30, 0xff, 0xff };

  clutter_test_init (&argc, &argv);

  /* Stage */
  stage = clutter_test_get_stage ();
  clutter_actor_set_background_color (CLUTTER_ACTOR (stage), &blue);
  clutter_actor_set_size (stage, 640, 480);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl Texture Polygon");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  /* Cogl Box */
  coglbox = TEST_COGLBOX (test_coglbox_new ());
  clutter_actor_add_child (stage, CLUTTER_ACTOR (coglbox));

  /* Timeline for animation */
  timeline = clutter_timeline_new_for_actor (stage, 6000);
  clutter_timeline_set_repeat_count (timeline, -1);
  g_signal_connect (timeline, "new-frame", G_CALLBACK (frame_cb), coglbox);
  clutter_timeline_start (timeline);

  /* Labels for toggling settings */
  slicing_toggle = make_toggle ("Texture slicing: ", &coglbox->use_sliced);
  clutter_actor_set_position (slicing_toggle, 0,
                              clutter_actor_get_height (stage)
                              - clutter_actor_get_height (slicing_toggle));
  filtering_toggle = make_toggle ("Linear filtering: ",
                                  &coglbox->use_linear_filtering);
  clutter_actor_set_position (filtering_toggle, 0,
                              clutter_actor_get_y (slicing_toggle)
                              - clutter_actor_get_height (filtering_toggle));
  note = clutter_text_new_with_text ("Sans 10", "<- Click to change");
  clutter_actor_set_position (note,
                              clutter_actor_get_width (filtering_toggle) + 10,
                              (clutter_actor_get_height (stage)
                               + clutter_actor_get_y (filtering_toggle)) / 2
                              - clutter_actor_get_height (note) / 2);

  clutter_actor_add_child (stage, slicing_toggle);
  clutter_actor_add_child (stage, filtering_toggle);
  clutter_actor_add_child (stage, note);

  clutter_actor_show (stage);

  clutter_test_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_cogl_tex_polygon_describe (void)
{
  return "Texture polygon primitive.";
}
