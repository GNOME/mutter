#pragma once

#include "clutter/clutter-event.h"

G_BEGIN_DECLS

typedef struct _ClutterModifierSet ClutterModifierSet;

struct _ClutterModifierSet
{
  ClutterModifierType pressed;
  ClutterModifierType latched;
  ClutterModifierType locked;
};

CLUTTER_EXPORT
ClutterEvent * clutter_event_key_new (ClutterEventType     type,
                                      ClutterEventFlags    flags,
                                      int64_t              timestamp_us,
                                      ClutterInputDevice  *source_device,
                                      ClutterModifierSet   raw_modifiers,
                                      ClutterModifierType  modifiers,
                                      uint32_t             keyval,
                                      uint32_t             evcode,
                                      uint32_t             keycode,
                                      gunichar             unicode_value);

CLUTTER_EXPORT
ClutterEvent * clutter_event_button_new (ClutterEventType        type,
                                         ClutterEventFlags       flags,
                                         int64_t                 timestamp_us,
                                         ClutterInputDevice     *source_device,
                                         ClutterInputDeviceTool *tool,
                                         ClutterModifierType     modifiers,
                                         graphene_point_t        coords,
                                         int                     button,
                                         uint32_t                evcode,
                                         double                 *axes);
CLUTTER_EXPORT
ClutterEvent * clutter_event_motion_new (ClutterEventFlags       flags,
                                         int64_t                 timestamp_us,
                                         ClutterInputDevice     *source_device,
                                         ClutterInputDeviceTool *tool,
                                         ClutterModifierType     modifiers,
                                         graphene_point_t        coords,
                                         graphene_point_t        delta,
                                         graphene_point_t        delta_unaccel,
                                         graphene_point_t        delta_constrained,
                                         double                 *axes);
CLUTTER_EXPORT
ClutterEvent * clutter_event_scroll_smooth_new (ClutterEventFlags         flags,
                                                int64_t                   timestamp_us,
                                                ClutterInputDevice       *source_device,
                                                ClutterInputDeviceTool   *tool,
                                                ClutterModifierType       modifiers,
                                                graphene_point_t          coords,
                                                graphene_point_t          delta,
                                                ClutterScrollSource       scroll_source,
                                                ClutterScrollFinishFlags  finish_flags);
CLUTTER_EXPORT
ClutterEvent * clutter_event_scroll_discrete_new (ClutterEventFlags       flags,
                                                  int64_t                 timestamp_us,
                                                  ClutterInputDevice     *source_device,
                                                  ClutterInputDeviceTool *tool,
                                                  ClutterModifierType     modifiers,
                                                  graphene_point_t        coords,
                                                  ClutterScrollSource     scroll_source,
                                                  ClutterScrollDirection  direction);
CLUTTER_EXPORT
ClutterEvent * clutter_event_crossing_new (ClutterEventType      type,
                                           ClutterEventFlags     flags,
                                           int64_t               timestamp_us,
                                           ClutterInputDevice   *device,
                                           ClutterEventSequence *sequence,
                                           graphene_point_t      coords,
                                           ClutterActor         *source,
                                           ClutterActor         *related);
CLUTTER_EXPORT
ClutterEvent * clutter_event_touch_new (ClutterEventType      type,
                                        ClutterEventFlags     flags,
                                        int64_t               timestamp_us,
                                        ClutterInputDevice   *source_device,
                                        ClutterEventSequence *sequence,
                                        ClutterModifierType   modifiers,
                                        graphene_point_t      coords);
CLUTTER_EXPORT
ClutterEvent * clutter_event_touch_cancel_new (ClutterEventFlags     flags,
                                               int64_t               timestamp_us,
                                               ClutterInputDevice   *source_device,
                                               ClutterEventSequence *sequence);
CLUTTER_EXPORT
ClutterEvent * clutter_event_proximity_new (ClutterEventType        type,
                                            ClutterEventFlags       flags,
                                            int64_t                 timestamp_us,
                                            ClutterInputDevice     *source_device,
                                            ClutterInputDeviceTool *tool);
CLUTTER_EXPORT
ClutterEvent * clutter_event_touchpad_pinch_new (ClutterEventFlags            flags,
                                                 int64_t                      timestamp_us,
                                                 ClutterInputDevice          *source_device,
                                                 ClutterTouchpadGesturePhase  phase,
                                                 uint32_t                     fingers,
                                                 graphene_point_t             coords,
                                                 graphene_point_t             delta,
                                                 graphene_point_t             delta_unaccel,
                                                 float                        angle,
                                                 float                        scale);
CLUTTER_EXPORT
ClutterEvent * clutter_event_touchpad_swipe_new (ClutterEventFlags            flags,
                                                 int64_t                      timestamp_us,
                                                 ClutterInputDevice          *source_device,
                                                 ClutterTouchpadGesturePhase  phase,
                                                 uint32_t                     fingers,
                                                 graphene_point_t             coords,
                                                 graphene_point_t             delta,
                                                 graphene_point_t             delta_unaccel);
CLUTTER_EXPORT
ClutterEvent * clutter_event_touchpad_hold_new (ClutterEventFlags            flags,
                                                int64_t                      timestamp_us,
                                                ClutterInputDevice          *source_device,
                                                ClutterTouchpadGesturePhase  phase,
                                                uint32_t                     fingers,
                                                graphene_point_t             coords);
CLUTTER_EXPORT
ClutterEvent * clutter_event_pad_button_new (ClutterEventType    type,
                                             ClutterEventFlags   flags,
                                             int64_t             timestamp_us,
                                             ClutterInputDevice *source_device,
                                             uint32_t            button,
                                             uint32_t            group,
                                             uint32_t            mode);
CLUTTER_EXPORT
ClutterEvent * clutter_event_pad_strip_new (ClutterEventFlags            flags,
                                            int64_t                      timestamp_us,
                                            ClutterInputDevice          *source_device,
                                            ClutterInputDevicePadSource  strip_source,
                                            uint32_t                     strip,
                                            uint32_t                     group,
                                            double                       value,
                                            uint32_t                     mode);
CLUTTER_EXPORT
ClutterEvent * clutter_event_pad_ring_new (ClutterEventFlags            flags,
                                           int64_t                      timestamp_us,
                                           ClutterInputDevice          *source_device,
                                           ClutterInputDevicePadSource  ring_source,
                                           uint32_t                     ring,
                                           uint32_t                     group,
                                           double                       angle,
                                           uint32_t                     mode);
CLUTTER_EXPORT
ClutterEvent * clutter_event_device_notify_new (ClutterEventType    type,
                                                ClutterEventFlags   flags,
                                                int64_t             timestamp_us,
                                                ClutterInputDevice *source_device);
CLUTTER_EXPORT
ClutterEvent * clutter_event_im_new (ClutterEventType         type,
                                     ClutterEventFlags        flags,
                                     int64_t                  timestamp_us,
                                     ClutterSeat             *seat,
                                     const char              *text,
                                     int32_t                  offset,
                                     int32_t                  anchor,
                                     uint32_t                 len,
                                     ClutterPreeditResetMode  mode);

/* Reinjecting queued events for processing */
CLUTTER_EXPORT
void            clutter_stage_process_event             (ClutterStage *stage,
                                                         ClutterEvent *event);

CLUTTER_EXPORT
gboolean        _clutter_event_process_filters          (ClutterEvent *event,
                                                         ClutterActor *event_actor);

/* clears the event queue inside the main context */
void            _clutter_clear_events_queue             (void);

CLUTTER_EXPORT
void            _clutter_event_push                     (const ClutterEvent *event,
                                                         gboolean            do_copy);

CLUTTER_EXPORT
const char * clutter_event_get_name (const ClutterEvent *event);

CLUTTER_EXPORT
char * clutter_event_describe (const ClutterEvent *event);

G_END_DECLS
