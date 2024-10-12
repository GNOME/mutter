#include <stdlib.h>
#include <glib.h>
#include <clutter/clutter.h>
#include <clutter/clutter-mutter.h>

#include "tests/clutter-test-utils.h"

static GTimer *testtimer = NULL;
static gint testframes = 0;
static float testmaxtime = 1.0;

/* initialize environment to be suitable for fps testing */
static inline void
clutter_perf_fps_init (void)
{
  /* Force not syncing to vblank, we want free-running maximum FPS */
  g_setenv ("vblank_mode", "0", FALSE);
  g_setenv ("CLUTTER_VBLANK", "none", FALSE);

  /* also override internal default FPS */
  g_setenv ("CLUTTER_DEFAULT_FPS", "1000", FALSE);

  if (g_getenv ("CLUTTER_PERFORMANCE_TEST_DURATION"))
    testmaxtime = (float) atof (g_getenv("CLUTTER_PERFORMANCE_TEST_DURATION"));
  else
    testmaxtime = 10.0;

  g_random_set_seed (12345678);
}

static void perf_stage_after_paint_cb (ClutterStage     *stage,
                                       ClutterStageView *view,
                                       ClutterFrame     *frame,
                                       gpointer         *data);
static gboolean perf_fake_mouse_cb (gpointer stage);

static inline void
clutter_perf_fps_start (ClutterStage *stage)
{
  g_signal_connect (stage, "after-paint", G_CALLBACK (perf_stage_after_paint_cb), NULL);
}

static inline void
clutter_perf_fake_mouse (ClutterStage *stage)
{
  g_timeout_add (1000/60, perf_fake_mouse_cb, stage);
}

static inline void
clutter_perf_fps_report (const gchar *id)
{
  g_print ("\n@ %s: %.2f fps \n",
       id, testframes / g_timer_elapsed (testtimer, NULL));
}

static void
perf_stage_after_paint_cb (ClutterStage     *stage,
                           ClutterStageView *view,
                           ClutterFrame     *frame,
                           gpointer         *data)
{
  if (!testtimer)
    testtimer = g_timer_new ();
  testframes ++;
  if (g_timer_elapsed (testtimer, NULL) > testmaxtime)
    {
      clutter_test_quit ();
    }
}

static void wrap (gfloat *value, gfloat min, gfloat max)
{
  if (*value > max)
    *value = min;
  else if (*value < min)
    *value = max;
}

static gboolean perf_fake_mouse_cb (gpointer stage)
{
  static ClutterInputDevice *device = NULL;
  int i;
  static float x = 0.0;
  static float y = 0.0;
  static float xd = 0.0;
  static float yd = 0.0;
  static gboolean inited = FALSE;

  gfloat w, h;

  if (!inited) /* XXX:
                  force clutter to do handle our motion events,
                  by forcibly updating the input device's state
                  this should be possible to do in a better
                  manner in the future, a versioning check
                  will have to be added when this is possible
                  without a hack... and the means to do the
                  hack is deprecated
                */
    {
      ClutterEvent *event;
      ClutterBackend *backend = clutter_test_get_backend ();
      ClutterSeat *seat = clutter_backend_get_default_seat (backend);

      device = clutter_seat_get_pointer (seat);

      event = clutter_event_crossing_new (CLUTTER_ENTER,
                                          CLUTTER_EVENT_NONE,
                                          CLUTTER_CURRENT_TIME,
                                          device, NULL,
                                          GRAPHENE_POINT_INIT (10, 10),
                                          stage,
                                          NULL);
      clutter_event_put (event);
      clutter_event_free (event);
      inited = TRUE;
    }

  clutter_actor_get_size (stage, &w, &h);

  /* called about every 60fps, and do 10 picks per stage */
  for (i = 0; i < 10; i++)
    {
      ClutterEvent *event;

      event = clutter_event_motion_new (CLUTTER_EVENT_NONE,
                                        CLUTTER_CURRENT_TIME,
                                        device, NULL, 0,
                                        GRAPHENE_POINT_INIT (x, y),
                                        GRAPHENE_POINT_INIT (0, 0),
                                        GRAPHENE_POINT_INIT (0, 0),
                                        GRAPHENE_POINT_INIT (0, 0),
                                        NULL);
      clutter_event_put (event);
      clutter_event_free (event);

      x += xd;
      y += yd;
      xd += (float) g_random_double_range (-0.1, 0.1);
      yd += (float) g_random_double_range (-0.1, 0.1);

      wrap (&x, 0, w);
      wrap (&y, 0, h);

      xd = CLAMP(xd, -1.3f, 1.3f);
      yd = CLAMP(yd, -1.3f, 1.3f);
    }
  return G_SOURCE_CONTINUE;
}
