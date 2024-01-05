/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2; -*- */

#pragma once

#include <glib-object.h>

#include "meta/display.h"

G_BEGIN_DECLS

/**
 * MetaBarrierDirection:
 * @META_BARRIER_DIRECTION_POSITIVE_X: Positive direction in the X axis
 * @META_BARRIER_DIRECTION_POSITIVE_Y: Positive direction in the Y axis
 * @META_BARRIER_DIRECTION_NEGATIVE_X: Negative direction in the X axis
 * @META_BARRIER_DIRECTION_NEGATIVE_Y: Negative direction in the Y axis
 */

/* Keep in sync with XFixes */
typedef enum
{
  META_BARRIER_DIRECTION_POSITIVE_X = 1 << 0,
  META_BARRIER_DIRECTION_POSITIVE_Y = 1 << 1,
  META_BARRIER_DIRECTION_NEGATIVE_X = 1 << 2,
  META_BARRIER_DIRECTION_NEGATIVE_Y = 1 << 3,
} MetaBarrierDirection;

typedef enum
{
  META_BARRIER_FLAG_NONE = 1 << 0,
  META_BARRIER_FLAG_STICKY = 1 << 1,
} MetaBarrierFlags;

#define META_TYPE_BARRIER (meta_barrier_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaBarrier, meta_barrier,
                          META, BARRIER, GObject)

struct _MetaBarrierClass
{
  GObjectClass parent_class;
};

typedef struct _MetaBarrierEvent MetaBarrierEvent;

META_EXPORT
MetaBarrier * meta_barrier_new (MetaBackend           *backend,
                                int                    x1,
                                int                    y1,
                                int                    x2,
                                int                    y2,
                                MetaBarrierDirection   directions,
                                MetaBarrierFlags       flags,
                                GError               **error);

META_EXPORT
gboolean meta_barrier_is_active (MetaBarrier *barrier);

META_EXPORT
void meta_barrier_destroy (MetaBarrier *barrier);

META_EXPORT
void meta_barrier_release (MetaBarrier      *barrier,
                           MetaBarrierEvent *event);

/**
 * MetaBarrierEvent:
 * @event_id: A unique integer ID identifying a
 * consecutive series of motions at or along the barrier
 * @time: Server time, in milliseconds
 * @dt: Server time, in milliseconds, since the last event
 * sent for this barrier
 * @x: The cursor X position in screen coordinates
 * @y: The cursor Y position in screen coordinates.
 * @dx: If the cursor hadn't been constrained, the delta
 * of X movement past the barrier, in screen coordinates
 * @dy: If the cursor hadn't been constrained, the delta
 * of X movement past the barrier, in screen coordinates
 * @released: A boolean flag, %TRUE if this event generated
 * by the pointer leaving the barrier as a result of a client
 * calling meta_barrier_release() (will be set only for
 * MetaBarrier::leave signals)
 * @grabbed: A boolean flag, %TRUE if the pointer was grabbed
 * at the time this event was sent
 */
struct _MetaBarrierEvent {
  /* < private > */
  volatile guint ref_count;

  /* < public > */
  int event_id;
  int dt;
  guint32 time;
  double x;
  double y;
  double dx;
  double dy;
  gboolean released;
  gboolean grabbed;
};

#define META_TYPE_BARRIER_EVENT (meta_barrier_event_get_type ())

META_EXPORT
GType meta_barrier_event_get_type (void) G_GNUC_CONST;

G_END_DECLS
