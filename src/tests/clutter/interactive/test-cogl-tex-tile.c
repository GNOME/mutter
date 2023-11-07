#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <math.h>
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

  CoglTexture *cogl_tex_id;
  gdouble    animation_progress;
};

G_DEFINE_TYPE (TestCoglbox, test_coglbox, CLUTTER_TYPE_ACTOR);

int
test_cogl_tex_tile_main (int argc, char *argv[]);

const char *
test_cogl_tex_tile_describe (void);

G_END_DECLS

/* Coglbox implementation
 *--------------------------------------------------*/

static void
test_coglbox_paint (ClutterActor        *self,
                    ClutterPaintContext *paint_context)
{
  TestCoglbox *coglbox = TEST_COGLBOX (self);
  CoglFramebuffer *framebuffer =
    clutter_paint_context_get_framebuffer (paint_context);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglPipeline *pipeline;
  gfloat texcoords[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
  gfloat angle;
  gfloat frac;
  gint t;

  angle = coglbox->animation_progress * 2 * G_PI;

  frac = ((coglbox->animation_progress <= 0.5f
           ? coglbox->animation_progress
           : 1.0f - coglbox->animation_progress) + 0.5f) * 2.0f;

  for (t=0; t<4; t+=2)
    {
      texcoords[t]   += cos (angle);
      texcoords[t+1] += sin (angle);

      texcoords[t]   *= frac;
      texcoords[t+1] *= frac;
    }

  cogl_framebuffer_push_matrix (framebuffer);

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_color4ub (pipeline, 0x66, 0x66, 0xdd, 0xff);
  cogl_framebuffer_draw_rectangle (framebuffer, pipeline, 0, 0, 400, 400);
  g_object_unref (pipeline);

  cogl_framebuffer_translate (framebuffer, 100, 100, 0);

  pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_texture (pipeline, 0, coglbox->cogl_tex_id);
  cogl_framebuffer_draw_textured_rectangle (framebuffer, pipeline,
                                            0, 0, 200, 213,
                                            texcoords[0], texcoords[1],
                                            texcoords[2], texcoords[3]);
  g_object_unref (pipeline);

  cogl_framebuffer_pop_matrix (framebuffer);
}

static void
test_coglbox_dispose (GObject *object)
{
  TestCoglbox *coglbox = TEST_COGLBOX (object);

  g_object_unref (coglbox->cogl_tex_id);

  G_OBJECT_CLASS (test_coglbox_parent_class)->dispose (object);
}

static void
test_coglbox_init (TestCoglbox *self)
{
  g_autoptr (GError) error = NULL;
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());
  gchar *file;

  file = g_build_filename (TESTS_DATADIR, "redhand.png", NULL);
  self->cogl_tex_id = clutter_test_texture_2d_new_from_file (ctx, file, &error);
  if (error)
    g_warning ("Error loading redhand.png: %s", error->message);
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
          int              msecs,
          gpointer         data)
{
  TestCoglbox *coglbox = TEST_COGLBOX (data);

  coglbox->animation_progress = clutter_timeline_get_progress (timeline);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

G_MODULE_EXPORT int
test_cogl_tex_tile_main (int argc, char *argv[])
{
  ClutterActor     *stage;
  ClutterActor     *coglbox;
  ClutterTimeline  *timeline;

  clutter_test_init (&argc, &argv);

  /* Stage */
  stage = clutter_test_get_stage ();
  clutter_actor_set_size (stage, 400, 400);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Cogl Texture Tiling");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);

  /* Cogl Box */
  coglbox = test_coglbox_new ();
  clutter_actor_add_child (stage, coglbox);

  /* Timeline for animation */
  timeline = clutter_timeline_new_for_actor (stage, 6000); /* 6 second duration */
  clutter_timeline_set_repeat_count (timeline, -1);
  g_signal_connect (timeline, "new-frame", G_CALLBACK (frame_cb), coglbox);
  clutter_timeline_start (timeline);

  clutter_actor_show (stage);

  clutter_test_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_cogl_tex_tile_describe (void)
{
  return "Texture tiling.";
}
