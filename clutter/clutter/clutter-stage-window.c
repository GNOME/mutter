#include "clutter-build-config.h"

#include <glib-object.h>

#include "clutter-actor.h"
#include "clutter-frame.h"
#include "clutter-stage-window.h"
#include "clutter-private.h"

/**
 * SECTION:clutter-stage-window
 * @short_description: Handles the implementation for ClutterStage
 *
 * #ClutterStageWindow is an interface that provides the implementation for the
 * #ClutterStage actor, abstracting away the specifics of the windowing system.
 */

G_DEFINE_INTERFACE (ClutterStageWindow, clutter_stage_window, G_TYPE_OBJECT);

static void
clutter_stage_window_default_init (ClutterStageWindowInterface *iface)
{
  GParamSpec *pspec;

  pspec = g_param_spec_object ("backend",
                               "Backend",
                               "Back pointer to the Backend instance",
                               CLUTTER_TYPE_BACKEND,
                               G_PARAM_WRITABLE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);

  pspec = g_param_spec_object ("wrapper",
                               "Wrapper",
                               "Back pointer to the Stage actor",
                               CLUTTER_TYPE_STAGE,
                               G_PARAM_WRITABLE |
                               G_PARAM_CONSTRUCT_ONLY |
                               G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);
}

/**
 * _clutter_stage_window_get_wrapper:
 * @window: a #ClutterStageWindow object
 *
 * Returns the pointer to the #ClutterStage it's part of.
 */
ClutterActor *
_clutter_stage_window_get_wrapper (ClutterStageWindow *window)
{
  return CLUTTER_STAGE_WINDOW_GET_IFACE (window)->get_wrapper (window);
}

void
_clutter_stage_window_set_title (ClutterStageWindow *window,
                                 const gchar        *title)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->set_title)
    iface->set_title (window, title);
}

gboolean
_clutter_stage_window_realize (ClutterStageWindow *window)
{
  return CLUTTER_STAGE_WINDOW_GET_IFACE (window)->realize (window);
}

void
_clutter_stage_window_unrealize (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->unrealize (window);
}

void
_clutter_stage_window_show (ClutterStageWindow *window,
                            gboolean            do_raise)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->show (window, do_raise);
}

void
_clutter_stage_window_hide (ClutterStageWindow *window)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->hide (window);
}

void
_clutter_stage_window_resize (ClutterStageWindow *window,
                              gint                width,
                              gint                height)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->resize (window, width, height);
}

void
_clutter_stage_window_get_geometry (ClutterStageWindow    *window,
                                    cairo_rectangle_int_t *geometry)
{
  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->get_geometry (window, geometry);
}

void
_clutter_stage_window_redraw_view (ClutterStageWindow *window,
                                   ClutterStageView   *view,
                                   ClutterFrame       *frame)
{
  g_return_if_fail (CLUTTER_IS_STAGE_WINDOW (window));

  CLUTTER_STAGE_WINDOW_GET_IFACE (window)->redraw_view (window, view, frame);
}

gboolean
_clutter_stage_window_can_clip_redraws (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface;

  g_return_val_if_fail (CLUTTER_IS_STAGE_WINDOW (window), FALSE);

  iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);
  if (iface->can_clip_redraws != NULL)
    return iface->can_clip_redraws (window);

  return FALSE;
}

GList *
_clutter_stage_window_get_views (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  return iface->get_views (window);
}

void
_clutter_stage_window_prepare_frame (ClutterStageWindow *window,
                                     ClutterStageView   *view,
                                     ClutterFrame       *frame)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->prepare_frame)
    iface->prepare_frame (window, view, frame);
}

void
_clutter_stage_window_finish_frame (ClutterStageWindow *window,
                                    ClutterStageView   *view,
                                    ClutterFrame       *frame)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->finish_frame)
    {
      iface->finish_frame (window, view, frame);
      return;
    }

  if (!clutter_frame_has_result (frame))
    clutter_frame_set_result (frame, CLUTTER_FRAME_RESULT_IDLE);
}

int64_t
_clutter_stage_window_get_frame_counter (ClutterStageWindow *window)
{
  ClutterStageWindowInterface *iface = CLUTTER_STAGE_WINDOW_GET_IFACE (window);

  if (iface->get_frame_counter)
    return iface->get_frame_counter (window);
  else
    return 0;
}
