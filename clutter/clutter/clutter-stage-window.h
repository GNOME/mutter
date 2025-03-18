#pragma once

#include "cogl/cogl.h"
#include "clutter/clutter-types.h"
#include "clutter/clutter-stage-view.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_WINDOW (clutter_stage_window_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStageWindow, clutter_stage_window,
                          CLUTTER, STAGE_WINDOW,
                          GObject)

/*
 * ClutterStageWindowClass: (skip)
 *
 * The parent class for for stage windows
 */
struct _ClutterStageWindowClass
{
  GObjectClass parent_class;

  gboolean          (* realize)                 (ClutterStageWindow *stage_window);
  void              (* unrealize)               (ClutterStageWindow *stage_window);

  void              (* show)                    (ClutterStageWindow *stage_window,
                                                 gboolean            do_raise);
  void              (* hide)                    (ClutterStageWindow *stage_window);

  void              (* resize)                  (ClutterStageWindow *stage_window,
                                                 gint                width,
                                                 gint                height);
  void              (* get_geometry)            (ClutterStageWindow *stage_window,
                                                 MtkRectangle       *geometry);

  void              (* redraw_view)             (ClutterStageWindow *stage_window,
                                                 ClutterStageView   *view,
                                                 ClutterFrame       *frame);

  gboolean          (* can_clip_redraws)        (ClutterStageWindow *stage_window);

  GList            *(* get_views)               (ClutterStageWindow *stage_window);
  void              (* prepare_frame)           (ClutterStageWindow *stage_window,
                                                 ClutterStageView   *view,
                                                 ClutterFrame       *frame);
  void              (* finish_frame)            (ClutterStageWindow *stage_window,
                                                 ClutterStageView   *view,
                                                 ClutterFrame       *frame);
};

gboolean          _clutter_stage_window_realize                 (ClutterStageWindow *window);
void              _clutter_stage_window_unrealize               (ClutterStageWindow *window);

void              _clutter_stage_window_show                    (ClutterStageWindow *window,
                                                                 gboolean            do_raise);
void              _clutter_stage_window_hide                    (ClutterStageWindow *window);

void              _clutter_stage_window_resize                  (ClutterStageWindow *window,
                                                                 gint                width,
                                                                 gint                height);
CLUTTER_EXPORT
void              _clutter_stage_window_get_geometry            (ClutterStageWindow *window,
                                                                 MtkRectangle       *geometry);

void               _clutter_stage_window_redraw_view            (ClutterStageWindow *window,
                                                                 ClutterStageView   *view,
                                                                 ClutterFrame       *frame);

CLUTTER_EXPORT
gboolean          _clutter_stage_window_can_clip_redraws        (ClutterStageWindow *window);

GList *           _clutter_stage_window_get_views               (ClutterStageWindow *window);

void              _clutter_stage_window_prepare_frame           (ClutterStageWindow *window,
                                                                 ClutterStageView   *view,
                                                                 ClutterFrame       *frame);

void              _clutter_stage_window_finish_frame            (ClutterStageWindow *window,
                                                                 ClutterStageView   *view,
                                                                 ClutterFrame       *frame);

G_END_DECLS
