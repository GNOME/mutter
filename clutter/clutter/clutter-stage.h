/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-actor.h"
#include "clutter/clutter-grab.h"
#include "clutter/clutter-types.h"
#include "clutter/clutter-stage-view.h"
#include "mtk/mtk.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE              (clutter_stage_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStage,
                          clutter_stage,
                          CLUTTER,
                          STAGE,
                          ClutterActor)
/**
 * ClutterStageClass:
 * @activate: handler for the #ClutterStage::activate signal
 * @deactivate: handler for the #ClutterStage::deactivate signal
 *
 * The #ClutterStageClass structure contains only private data
 */

struct _ClutterStageClass
{
  /*< private >*/
  ClutterActorClass parent_class;

  /*< public >*/
  /* signals */
  void (* activate)     (ClutterStage *stage);
  void (* deactivate)   (ClutterStage *stage);

  void (* before_paint) (ClutterStage     *stage,
                         ClutterStageView *view,
                         ClutterFrame     *frame);

  void (* paint_view) (ClutterStage     *stage,
                       ClutterStageView *view,
                       const MtkRegion  *redraw_clip,
                       ClutterFrame     *frame);
};

/**
 * ClutterPerspective:
 * @fovy: the field of view angle, in degrees, in the y direction
 * @aspect: the aspect ratio that determines the field of view in the x
 *   direction. The aspect ratio is the ratio of x (width) to y (height)
 * @z_near: the distance from the viewer to the near clipping
 *   plane (always positive)
 * @z_far: the distance from the viewer to the far clipping
 *   plane (always positive)
 *
 * Stage perspective definition.
 */
struct _ClutterPerspective
{
  gfloat fovy;
  gfloat aspect;
  gfloat z_near;
  gfloat z_far;
};

typedef enum
{
  CLUTTER_FRAME_INFO_FLAG_NONE = 0,
  /* presentation_time timestamp was provided by the hardware */
  CLUTTER_FRAME_INFO_FLAG_HW_CLOCK = 1 << 0,
  /*
   * The presentation of this frame was done zero-copy. This means the buffer
   * from the client was given to display hardware as is, without copying it.
   * Compositing with OpenGL counts as copying, even if textured directly from
   * the client buffer. Possible zero-copy cases include direct scanout of a
   * fullscreen surface and a surface on a hardware overlay.
   */
  CLUTTER_FRAME_INFO_FLAG_ZERO_COPY = 1 << 1,
  /*
   * The presentation was synchronized to the "vertical retrace" by the display
   * hardware such that tearing does not happen. Relying on user space
   * scheduling is not acceptable for this flag. If presentation is done by a
   * copy to the active frontbuffer, then it must guarantee that tearing cannot
   * happen.
   */
  CLUTTER_FRAME_INFO_FLAG_VSYNC = 1 << 2,
} ClutterFrameInfoFlag;

/**
 * ClutterFrameInfo: (skip)
 */
struct _ClutterFrameInfo
{
  int64_t frame_counter;
  int64_t presentation_time; /* microseconds; CLOCK_MONOTONIC */
  float refresh_rate;

  ClutterFrameInfoFlag flags;

  unsigned int sequence;

  gboolean has_valid_gpu_rendering_duration;
  int64_t gpu_rendering_duration_ns;
  int64_t cpu_time_before_buffer_swap_us;
};

CLUTTER_EXPORT
GType clutter_perspective_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
void            clutter_stage_get_perspective                   (ClutterStage          *stage,
			                                         ClutterPerspective    *perspective);
CLUTTER_EXPORT
void            clutter_stage_set_title                         (ClutterStage          *stage,
                                                                 const gchar           *title);
CLUTTER_EXPORT
const gchar *   clutter_stage_get_title                         (ClutterStage          *stage);

CLUTTER_EXPORT
void            clutter_stage_set_minimum_size                  (ClutterStage          *stage,
                                                                 guint                  width,
                                                                 guint                  height);
CLUTTER_EXPORT
void            clutter_stage_set_key_focus                     (ClutterStage          *stage,
                                                                 ClutterActor          *actor);
CLUTTER_EXPORT
ClutterActor *  clutter_stage_get_key_focus                     (ClutterStage          *stage);

CLUTTER_EXPORT
ClutterActor *  clutter_stage_get_actor_at_pos                  (ClutterStage          *stage,
                                                                 ClutterPickMode        pick_mode,
                                                                 float                  x,
                                                                 float                  y);
CLUTTER_EXPORT
guchar *        clutter_stage_read_pixels                       (ClutterStage          *stage,
                                                                 gint                   x,
                                                                 gint                   y,
                                                                 gint                   width,
                                                                 gint                   height);

CLUTTER_EXPORT
void            clutter_stage_ensure_viewport                   (ClutterStage          *stage);

CLUTTER_EXPORT
gboolean        clutter_stage_is_redraw_queued_on_view          (ClutterStage          *stage,
                                                                 ClutterStageView      *view);
CLUTTER_EXPORT
void clutter_stage_schedule_update (ClutterStage *stage);

CLUTTER_EXPORT
gboolean clutter_stage_get_capture_final_size (ClutterStage *stage,
                                               MtkRectangle *rect,
                                               int          *out_width,
                                               int          *out_height,
                                               float        *out_scale);

CLUTTER_EXPORT
void clutter_stage_paint_to_framebuffer (ClutterStage       *stage,
                                         CoglFramebuffer    *framebuffer,
                                         const MtkRectangle *rect,
                                         float               scale,
                                         ClutterPaintFlag    paint_flags);

CLUTTER_EXPORT
gboolean clutter_stage_paint_to_buffer (ClutterStage        *stage,
                                        const MtkRectangle  *rect,
                                        float                scale,
                                        uint8_t             *data,
                                        int                  stride,
                                        CoglPixelFormat      format,
                                        ClutterPaintFlag     paint_flags,
                                        GError             **error);

CLUTTER_EXPORT
ClutterContent * clutter_stage_paint_to_content (ClutterStage        *stage,
                                                 const MtkRectangle  *rect,
                                                 float                scale,
                                                 ClutterPaintFlag     paint_flags,
                                                 GError             **error);

CLUTTER_EXPORT
ClutterStageView * clutter_stage_get_view_at (ClutterStage *stage,
                                              float         x,
                                              float         y);

CLUTTER_EXPORT
ClutterActor * clutter_stage_get_device_actor (ClutterStage         *stage,
                                               ClutterInputDevice   *device,
                                               ClutterEventSequence *sequence);
CLUTTER_EXPORT
ClutterActor * clutter_stage_get_event_actor (ClutterStage       *stage,
                                              const ClutterEvent *event);

CLUTTER_EXPORT
ClutterGrab * clutter_stage_grab (ClutterStage *stage,
                                  ClutterActor *actor);

CLUTTER_EXPORT
ClutterActor * clutter_stage_get_grab_actor (ClutterStage *stage);

/**
 * ClutterStageInputForeachFunc:
 * @stage: the stage
 * @device: Active input device
 * @sequence: Active sequence in @device, or %NULL
 * @user_data: Data passed to clutter_stage_active_input_foreach()
 *
 * Iterator function for active input. Active input counts as any pointing
 * device currently known to have some form of activity on the stage: Pointers
 * leaning on a widget, tablet styli in proximity, active touchpoints...
 *
 * Returns: %TRUE to keep iterating. %FALSE to stop.
 */
typedef gboolean (*ClutterStageInputForeachFunc) (ClutterStage         *stage,
                                                  ClutterInputDevice   *device,
                                                  ClutterEventSequence *sequence,
                                                  gpointer              user_data);

CLUTTER_EXPORT
gboolean clutter_stage_pointing_input_foreach (ClutterStage                 *self,
                                               ClutterStageInputForeachFunc  func,
                                               gpointer                      user_data);

G_END_DECLS
