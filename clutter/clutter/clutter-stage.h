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

#ifndef __CLUTTER_STAGE_H__
#define __CLUTTER_STAGE_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-actor.h>
#include <clutter/clutter-types.h>
#include <clutter/clutter-stage-view.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE              (clutter_stage_get_type())

#define CLUTTER_STAGE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_STAGE, ClutterStage))
#define CLUTTER_STAGE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_STAGE, ClutterStageClass))
#define CLUTTER_IS_STAGE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_STAGE))
#define CLUTTER_IS_STAGE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_STAGE))
#define CLUTTER_STAGE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_STAGE, ClutterStageClass))

typedef struct _ClutterStageClass   ClutterStageClass;
typedef struct _ClutterStagePrivate ClutterStagePrivate;

/**
 * ClutterStage:
 *
 * The #ClutterStage structure contains only private data
 * and should be accessed using the provided API
 *
 * Since: 0.2
 */
struct _ClutterStage
{
  /*< private >*/
  ClutterActor parent_instance;

  ClutterStagePrivate *priv;
};
/**
 * ClutterStageClass:
 * @activate: handler for the #ClutterStage::activate signal
 * @deactivate: handler for the #ClutterStage::deactivate signal
 *
 * The #ClutterStageClass structure contains only private data
 *
 * Since: 0.2
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
                         ClutterStageView *view);

  void (* paint_view) (ClutterStage         *stage,
                       ClutterStageView     *view,
                       const cairo_region_t *redraw_clip);

  /*< private >*/
  /* padding for future expansion */
  gpointer _padding_dummy[31];
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
 *
 * Since: 0.4
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
};

typedef struct _ClutterCapture
{
  cairo_surface_t *image;
  cairo_rectangle_int_t rect;
} ClutterCapture;

CLUTTER_EXPORT
GType clutter_perspective_get_type (void) G_GNUC_CONST;
CLUTTER_EXPORT
GType clutter_stage_get_type (void) G_GNUC_CONST;

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
void            clutter_stage_get_minimum_size                  (ClutterStage          *stage,
                                                                 guint                 *width,
                                                                 guint                 *height);
CLUTTER_EXPORT
void            clutter_stage_set_use_alpha                     (ClutterStage          *stage,
                                                                 gboolean               use_alpha);
CLUTTER_EXPORT
gboolean        clutter_stage_get_use_alpha                     (ClutterStage          *stage);

CLUTTER_EXPORT
void            clutter_stage_set_key_focus                     (ClutterStage          *stage,
                                                                 ClutterActor          *actor);
CLUTTER_EXPORT
ClutterActor *  clutter_stage_get_key_focus                     (ClutterStage          *stage);
CLUTTER_EXPORT
void            clutter_stage_set_throttle_motion_events        (ClutterStage          *stage,
                                                                 gboolean               throttle);
CLUTTER_EXPORT
gboolean        clutter_stage_get_throttle_motion_events        (ClutterStage          *stage);
CLUTTER_EXPORT
void            clutter_stage_set_motion_events_enabled         (ClutterStage          *stage,
                                                                 gboolean               enabled);
CLUTTER_EXPORT
gboolean        clutter_stage_get_motion_events_enabled         (ClutterStage          *stage);
CLUTTER_EXPORT
gboolean        clutter_stage_event                             (ClutterStage          *stage,
                                                                 ClutterEvent          *event);

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
gboolean clutter_stage_get_capture_final_size (ClutterStage          *stage,
                                               cairo_rectangle_int_t *rect,
                                               int                   *out_width,
                                               int                   *out_height,
                                               float                 *out_scale);

CLUTTER_EXPORT
void clutter_stage_paint_to_framebuffer (ClutterStage                *stage,
                                         CoglFramebuffer             *framebuffer,
                                         const cairo_rectangle_int_t *rect,
                                         float                        scale,
                                         ClutterPaintFlag             paint_flags);

CLUTTER_EXPORT
gboolean clutter_stage_paint_to_buffer (ClutterStage                 *stage,
                                        const cairo_rectangle_int_t  *rect,
                                        float                         scale,
                                        uint8_t                      *data,
                                        int                           stride,
                                        CoglPixelFormat               format,
                                        ClutterPaintFlag              paint_flags,
                                        GError                      **error);

CLUTTER_EXPORT
ClutterStageView * clutter_stage_get_view_at (ClutterStage *stage,
                                              float         x,
                                              float         y);

CLUTTER_EXPORT
ClutterActor * clutter_stage_get_device_actor (ClutterStage         *stage,
                                               ClutterInputDevice   *device,
                                               ClutterEventSequence *sequence);

G_END_DECLS

#endif /* __CLUTTER_STAGE_H__ */
