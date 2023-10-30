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

#include "clutter/clutter-types.h"
#include "clutter/clutter-input-device.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_EVENT	        (clutter_event_get_type ())
#define CLUTTER_TYPE_EVENT_SEQUENCE	(clutter_event_sequence_get_type ())

/**
 * CLUTTER_PRIORITY_EVENTS:
 *
 * Priority for event handling.
 */
#define CLUTTER_PRIORITY_EVENTS         (G_PRIORITY_DEFAULT)

/**
 * CLUTTER_CURRENT_TIME:
 *
 * Default value for "now".
 */
#define CLUTTER_CURRENT_TIME            (0L)

/**
 * CLUTTER_EVENT_PROPAGATE:
 *
 * Continues the propagation of an event; this macro should be
 * used in event-related signals.
 */
#define CLUTTER_EVENT_PROPAGATE         (FALSE)

/**
 * CLUTTER_EVENT_STOP:
 *
 * Stops the propagation of an event; this macro should be used
 * in event-related signals.
 */
#define CLUTTER_EVENT_STOP              (TRUE)

/**
 * CLUTTER_BUTTON_PRIMARY:
 *
 * The primary button of a pointer device.
 *
 * This is typically the left mouse button in a right-handed
 * mouse configuration.
 */
#define CLUTTER_BUTTON_PRIMARY          (1)

/**
 * CLUTTER_BUTTON_MIDDLE:
 *
 * The middle button of a pointer device.
 */
#define CLUTTER_BUTTON_MIDDLE           (2)

/**
 * CLUTTER_BUTTON_SECONDARY:
 *
 * The secondary button of a pointer device.
 *
 * This is typically the right mouse button in a right-handed
 * mouse configuration.
 */
#define CLUTTER_BUTTON_SECONDARY        (3)

typedef struct _ClutterAnyEvent         ClutterAnyEvent;
typedef struct _ClutterButtonEvent      ClutterButtonEvent;
typedef struct _ClutterKeyEvent         ClutterKeyEvent;
typedef struct _ClutterMotionEvent      ClutterMotionEvent;
typedef struct _ClutterScrollEvent      ClutterScrollEvent;
typedef struct _ClutterCrossingEvent    ClutterCrossingEvent;
typedef struct _ClutterTouchEvent       ClutterTouchEvent;
typedef struct _ClutterTouchpadPinchEvent ClutterTouchpadPinchEvent;
typedef struct _ClutterTouchpadSwipeEvent ClutterTouchpadSwipeEvent;
typedef struct _ClutterTouchpadHoldEvent ClutterTouchpadHoldEvent;
typedef struct _ClutterProximityEvent   ClutterProximityEvent;
typedef struct _ClutterPadButtonEvent   ClutterPadButtonEvent;
typedef struct _ClutterPadStripEvent    ClutterPadStripEvent;
typedef struct _ClutterPadRingEvent     ClutterPadRingEvent;
typedef struct _ClutterDeviceEvent      ClutterDeviceEvent;
typedef struct _ClutterIMEvent          ClutterIMEvent;

/**
 * ClutterEventFilterFunc:
 * @event: the event that is going to be emitted
 * @event_actor: the current device actor of the events device
 * @user_data: the data pointer passed to [func@Clutter.Event.add_filter]
 *
 * A function pointer type used by event filters that are added with
 * [func@Clutter.Event.add_filter].
 *
 * Return value: %CLUTTER_EVENT_STOP to indicate that the event
 *   has been handled or %CLUTTER_EVENT_PROPAGATE otherwise.
 *   Returning %CLUTTER_EVENT_STOP skips any further filter
 *   functions and prevents the signal emission for the event.
 */
typedef gboolean (* ClutterEventFilterFunc) (const ClutterEvent *event,
                                             ClutterActor       *event_actor,
                                             gpointer            user_data);

CLUTTER_EXPORT
GType clutter_event_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
GType clutter_event_sequence_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
gboolean                clutter_events_pending                  (void);
CLUTTER_EXPORT
ClutterEvent *          clutter_event_get                       (void);
CLUTTER_EXPORT
void                    clutter_event_put                       (const ClutterEvent     *event);

CLUTTER_EXPORT
guint                   clutter_event_add_filter                (ClutterStage          *stage,
                                                                 ClutterEventFilterFunc func,
                                                                 GDestroyNotify         notify,
                                                                 gpointer               user_data);
CLUTTER_EXPORT
void                    clutter_event_remove_filter             (guint                  id);

CLUTTER_EXPORT
ClutterEvent *          clutter_event_copy                      (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_free                      (ClutterEvent           *event);

CLUTTER_EXPORT
ClutterEventType        clutter_event_type                      (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterEventFlags       clutter_event_get_flags                 (const ClutterEvent     *event);
CLUTTER_EXPORT
guint32                 clutter_event_get_time                  (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterModifierType     clutter_event_get_state                 (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterInputDevice *    clutter_event_get_device                (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterInputDevice *    clutter_event_get_source_device         (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterInputDeviceTool *clutter_event_get_device_tool           (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterInputDeviceType  clutter_event_get_device_type           (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_get_coords                (const ClutterEvent     *event,
                                                                 gfloat                 *x,
                                                                 gfloat                 *y);
CLUTTER_EXPORT
void                    clutter_event_get_position              (const ClutterEvent     *event,
                                                                 graphene_point_t       *position);
CLUTTER_EXPORT
float                   clutter_event_get_distance              (const ClutterEvent     *source,
                                                                 const ClutterEvent     *target);
CLUTTER_EXPORT
double                  clutter_event_get_angle                 (const ClutterEvent     *source,
                                                                 const ClutterEvent     *target);
CLUTTER_EXPORT
gdouble *               clutter_event_get_axes                  (const ClutterEvent     *event,
                                                                 guint                  *n_axes);
CLUTTER_EXPORT
gboolean                clutter_event_has_shift_modifier        (const ClutterEvent     *event);
CLUTTER_EXPORT
gboolean                clutter_event_has_control_modifier      (const ClutterEvent     *event);
CLUTTER_EXPORT
gboolean                clutter_event_is_pointer_emulated       (const ClutterEvent     *event);
CLUTTER_EXPORT
guint                   clutter_event_get_key_symbol            (const ClutterEvent     *event);
CLUTTER_EXPORT
guint16                 clutter_event_get_key_code              (const ClutterEvent     *event);
CLUTTER_EXPORT
gunichar                clutter_event_get_key_unicode           (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_get_key_state             (const ClutterEvent     *event,
                                                                 ClutterModifierType    *pressed,
                                                                 ClutterModifierType    *latched,
                                                                 ClutterModifierType    *locked);
CLUTTER_EXPORT
guint32                 clutter_event_get_button                (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterActor *          clutter_event_get_related               (const ClutterEvent     *event);
CLUTTER_EXPORT
ClutterScrollDirection  clutter_event_get_scroll_direction      (const ClutterEvent     *event);
CLUTTER_EXPORT
void                    clutter_event_get_scroll_delta          (const ClutterEvent     *event,
                                                                 gdouble                *dx,
                                                                 gdouble                *dy);

CLUTTER_EXPORT
ClutterEventSequence *  clutter_event_get_event_sequence        (const ClutterEvent     *event);

CLUTTER_EXPORT
guint32                 clutter_keysym_to_unicode               (guint                   keyval);
CLUTTER_EXPORT
guint                   clutter_unicode_to_keysym               (guint32                 wc);

CLUTTER_EXPORT
guint32                 clutter_get_current_event_time          (void);
CLUTTER_EXPORT
const ClutterEvent *    clutter_get_current_event               (void);

CLUTTER_EXPORT
guint                   clutter_event_get_touchpad_gesture_finger_count (const ClutterEvent  *event);

CLUTTER_EXPORT
gdouble                 clutter_event_get_gesture_pinch_angle_delta  (const ClutterEvent     *event);

CLUTTER_EXPORT
gdouble                 clutter_event_get_gesture_pinch_scale        (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterTouchpadGesturePhase clutter_event_get_gesture_phase          (const ClutterEvent     *event);

CLUTTER_EXPORT
void                    clutter_event_get_gesture_motion_delta       (const ClutterEvent     *event,
                                                                      gdouble                *dx,
                                                                      gdouble                *dy);

CLUTTER_EXPORT
void                    clutter_event_get_gesture_motion_delta_unaccelerated (const ClutterEvent     *event,
                                                                              gdouble                *dx,
                                                                              gdouble                *dy);

CLUTTER_EXPORT
ClutterScrollSource      clutter_event_get_scroll_source             (const ClutterEvent     *event);

CLUTTER_EXPORT
ClutterScrollFinishFlags clutter_event_get_scroll_finish_flags       (const ClutterEvent     *event);

CLUTTER_EXPORT
guint                    clutter_event_get_mode_group                (const ClutterEvent     *event);

CLUTTER_EXPORT
gboolean clutter_event_get_pad_details (const ClutterEvent          *event,
                                        guint                       *number,
                                        guint                       *mode,
                                        ClutterInputDevicePadSource *source,
                                        gdouble                     *value);
CLUTTER_EXPORT
uint32_t                 clutter_event_get_event_code                (const ClutterEvent     *event);

CLUTTER_EXPORT
int32_t                  clutter_event_sequence_get_slot (const ClutterEventSequence *sequence);

CLUTTER_EXPORT
int64_t                  clutter_event_get_time_us (const ClutterEvent *event);
CLUTTER_EXPORT
gboolean clutter_event_get_relative_motion (const ClutterEvent *event,
                                            double             *dx,
                                            double             *dy,
                                            double             *dx_unaccel,
                                            double             *dy_unaccel,
                                            double             *dx_constrained,
                                            double             *dy_constrained);

CLUTTER_EXPORT
const char * clutter_event_get_im_text (const ClutterEvent *event);
CLUTTER_EXPORT
gboolean clutter_event_get_im_location (const ClutterEvent  *event,
                                        int32_t             *offset,
                                        int32_t             *anchor);
CLUTTER_EXPORT
uint32_t clutter_event_get_im_delete_length (const ClutterEvent  *event);
CLUTTER_EXPORT
ClutterPreeditResetMode clutter_event_get_im_preedit_reset_mode (const ClutterEvent *event);

G_END_DECLS
