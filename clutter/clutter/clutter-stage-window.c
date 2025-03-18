#include "config.h"

#include <glib-object.h>

#include "clutter/clutter-actor.h"
#include "clutter/clutter-frame.h"
#include "clutter/clutter-stage-window.h"
#include "clutter/clutter-private.h"

/**
 * ClutterStageWindow:
 *
 * Handles the implementation for [class@Stage]
 *
 * #ClutterStageWindow is a class that provides the implementation for the
 * [class@Stage] actor, abstracting away the specifics of the windowing system.
 */

G_DEFINE_TYPE (ClutterStageWindow, clutter_stage_window, G_TYPE_OBJECT);

static void
clutter_stage_window_class_init (ClutterStageWindowClass *klass)
{
}

static void
clutter_stage_window_init (ClutterStageWindow *window)
{
}

gboolean
_clutter_stage_window_realize (ClutterStageWindow *window)
{
  return CLUTTER_STAGE_WINDOW_GET_CLASS (window)->realize (window);
}

void
_clutter_stage_window_unrealize (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->unrealize (window);
}

void
_clutter_stage_window_show (ClutterStageWindow *window,
                            gboolean            do_raise)
{
  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->show (window, do_raise);
}

void
_clutter_stage_window_hide (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->hide (window);
}

void
_clutter_stage_window_resize (ClutterStageWindow *window,
                              gint                width,
                              gint                height)
{
  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->resize (window, width, height);
}

void
_clutter_stage_window_get_geometry (ClutterStageWindow *window,
                                    MtkRectangle       *geometry)
{
  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->get_geometry (window, geometry);
}

void
_clutter_stage_window_redraw_view (ClutterStageWindow *window,
                                   ClutterStageView   *view,
                                   ClutterFrame       *frame)
{
  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  CLUTTER_STAGE_WINDOW_GET_CLASS (window)->redraw_view (window, view, frame);
}

gboolean
_clutter_stage_window_can_clip_redraws (ClutterStageWindow *window)
{
  ClutterStageWindowClass *klass;

  g_return_val_if_fail (CLUTTER_IS_STAGE_WINDOW (window), FALSE);

  klass = CLUTTER_STAGE_WINDOW_GET_CLASS (window);
  if (klass->can_clip_redraws != NULL)
    return klass->can_clip_redraws (window);

  return FALSE;
}

GList *
_clutter_stage_window_get_views (ClutterStageWindow *window)
{
  ClutterStageWindowClass *klass = CLUTTER_STAGE_WINDOW_GET_CLASS (window);

  return klass->get_views (window);
}

void
_clutter_stage_window_prepare_frame (ClutterStageWindow *window,
                                     ClutterStageView   *view,
                                     ClutterFrame       *frame)
{
  ClutterStageWindowClass *klass = CLUTTER_STAGE_WINDOW_GET_CLASS (window);

  if (klass->prepare_frame)
    klass->prepare_frame (window, view, frame);
}

void
_clutter_stage_window_finish_frame (ClutterStageWindow *window,
                                    ClutterStageView   *view,
                                    ClutterFrame       *frame)
{
  ClutterStageWindowClass *klass = CLUTTER_STAGE_WINDOW_GET_CLASS (window);

  if (klass->finish_frame)
    {
      klass->finish_frame (window, view, frame);
      return;
    }

  if (!clutter_frame_has_result (frame))
    clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
}
