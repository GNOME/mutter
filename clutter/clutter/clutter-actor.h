/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006, 2007, 2008 OpenedHand Ltd
 * Copyright (C) 2009, 2010 Intel Corp
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

/* clutter-actor.h */

#include <atk/atk.h>
#include <gio/gio.h>

#include "cogl/cogl.h"

#include "clutter/clutter-types.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-paint-context.h"
#include "clutter/clutter-pick-context.h"
#include "mtk/mtk.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_ACTOR              (clutter_actor_get_type ())
#define CLUTTER_ACTOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_ACTOR, ClutterActor))
#define CLUTTER_ACTOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_ACTOR, ClutterActorClass))
#define CLUTTER_IS_ACTOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_ACTOR))
#define CLUTTER_IS_ACTOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_ACTOR))
#define CLUTTER_ACTOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_ACTOR, ClutterActorClass))

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterActor, g_object_unref)

typedef struct _ClutterActorClass    ClutterActorClass;
typedef struct _ClutterActorPrivate  ClutterActorPrivate;


struct _ClutterActor
{
  /*< private >*/
  GInitiallyUnowned parent_instance;

  /*< public >*/
  guint32 flags;
  AtkRole accessible_role;

  /*< private >*/
  guint32 private_flags;

  ClutterActorPrivate *priv;
};

/**
 * ClutterActorClass:
 * @show: signal class handler for [signal@Clutter.Actor::show]; it must chain
 *   up to the parent's implementation
 * @hide: signal class handler for [signal@Clutter.Actor::hide]; it must chain
 *   up to the parent's implementation
 * @hide_all: virtual function for containers and composite actors, to
 *   determine which children should be shown when calling
 *   clutter_actor_hide_all() on the actor. Defaults to calling
 *   clutter_actor_hide(). This virtual function is deprecated and it
 *   should not be overridden.
 * @realize: virtual function, used to allocate resources for the actor;
 *   it should chain up to the parent's implementation. This virtual
 *   function is deprecated and should not be overridden in newly
 *   written code.
 * @unrealize: virtual function, used to deallocate resources allocated
 *   in ::realize; it should chain up to the parent's implementation. This
 *   function is deprecated and should not be overridden in newly
 *   written code.
 * @map: virtual function for containers and composite actors, to
 *   map their children; it must chain up to the parent's implementation.
 *   Overriding this function is optional.
 * @unmap: virtual function for containers and composite actors, to
 *   unmap their children; it must chain up to the parent's implementation.
 *   Overriding this function is optional.
 * @paint: virtual function, used to paint the actor
 * @get_preferred_width: virtual function, used when querying the minimum
 *   and natural widths of an actor for a given height; it is used by
 *   clutter_actor_get_preferred_width()
 * @get_preferred_height: virtual function, used when querying the minimum
 *   and natural heights of an actor for a given width; it is used by
 *   clutter_actor_get_preferred_height()
 * @allocate: virtual function, used when setting the coordinates of an
 *   actor; it is used by clutter_actor_allocate(); when overriding this
 *   function without chaining up, clutter_actor_set_allocation() must be
 *   called and children must be allocated by the implementation, when
 *   chaining up though, those things will be done by the parent's
 *   implementation.
 * @apply_transform: virtual function, used when applying the transformations
 *   to an actor before painting it or when transforming coordinates or
 *   the allocation; if the transformation calculated by this function may
 *   have changed, the cached transformation must be invalidated by calling
 *   clutter_actor_invalidate_transform(); it must chain up to the parent's
 *   implementation
 * @parent_set: signal class handler for the [signal@Clutter.Actor::parent-set]
 * @destroy: signal class handler for [signal@Clutter.Actor::destroy]. It must
 *   chain up to the parent's implementation
 * @pick: virtual function, used to draw an outline of the actor with
 *   the given color
 * @event: class handler for [signal@Clutter.Actor::event]
 * @button_press_event: class handler for [signal@Clutter.Actor::button-press-event]
 * @button_release_event: class handler for
 *   [signal@Clutter.Actor::button-release-event]
 * @scroll_event: signal class closure for [signal@Clutter.Actor::scroll-event]
 * @key_press_event: signal class closure for [signal@Clutter.Actor::key-press-event]
 * @key_release_event: signal class closure for
 *   [signal@Clutter.Actor::key-release-event]
 * @motion_event: signal class closure for [signal@Clutter.Actor::motion-event]
 * @enter_event: signal class closure for [signal@Clutter.Actor::enter-event]
 * @leave_event: signal class closure for [signal@Clutter.Actor::leave-event]
 * @captured_event: signal class closure for [signal@Clutter.Actor::captured-event]
 * @key_focus_in: signal class closure for [signal@Clutter.Actor::key-focus-in]
 * @key_focus_out: signal class closure for [signal@Clutter.Actor::key-focus-out]
 * @queue_relayout: class handler for [signal@Clutter.Actor::queue-relayout]
 * @get_accessible: virtual function, returns the accessible object that
 *   describes the actor to an assistive technology.
 * @get_accessible_type: returns the type of the accessible object that
 *   describes the actor to an assistive technology.
 * @get_paint_volume: virtual function, for sub-classes to define their
 *   #ClutterPaintVolume
 * @has_overlaps: virtual function for
 *   sub-classes to advertise whether they need an offscreen redirect
 *   to get the correct opacity. See
 *   clutter_actor_set_offscreen_redirect() for details.
 * @paint_node: virtual function for creating paint nodes and attaching
 *   them to the render tree
 * @touch_event: signal class closure for [signal@Clutter.Actor::touch-event]
 *
 * Base class for actors.
 */
struct _ClutterActorClass
{
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  void (* show)                 (ClutterActor          *self);
  void (* hide)                 (ClutterActor          *self);
  void (* hide_all)             (ClutterActor          *self);
  void (* realize)              (ClutterActor          *self);
  void (* unrealize)            (ClutterActor          *self);
  void (* map)                  (ClutterActor          *self);
  void (* unmap)                (ClutterActor          *self);
  void (* paint)                (ClutterActor          *self,
                                 ClutterPaintContext   *paint_context);
  void (* parent_set)           (ClutterActor          *actor,
                                 ClutterActor          *old_parent);

  void (* destroy)              (ClutterActor          *self);
  void (* pick)                 (ClutterActor          *actor,
                                 ClutterPickContext    *pick_context);

  /* size negotiation */
  void (* get_preferred_width)  (ClutterActor           *self,
                                 gfloat                  for_height,
                                 gfloat                 *min_width_p,
                                 gfloat                 *natural_width_p);
  void (* get_preferred_height) (ClutterActor           *self,
                                 gfloat                  for_width,
                                 gfloat                 *min_height_p,
                                 gfloat                 *natural_height_p);
  void (* allocate)             (ClutterActor           *self,
                                 const ClutterActorBox  *box);

  /* transformations */
  void (* apply_transform)      (ClutterActor           *actor,
                                 graphene_matrix_t      *matrix);

  /* event signals */
  gboolean (* event)                (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* button_press_event)   (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* button_release_event) (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* scroll_event)         (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* key_press_event)      (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* key_release_event)    (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* motion_event)         (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* enter_event)          (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* leave_event)          (ClutterActor         *actor,
                                     ClutterEvent         *event);
  gboolean (* captured_event)       (ClutterActor         *actor,
                                     ClutterEvent         *event);
  void     (* key_focus_in)         (ClutterActor         *actor);
  void     (* key_focus_out)        (ClutterActor         *actor);

  void     (* queue_relayout)       (ClutterActor         *self);

  /* accessibility support */
  AtkObject * (* get_accessible)    (ClutterActor         *self);
  GType    (* get_accessible_type) (void);

  gboolean (* get_paint_volume)     (ClutterActor         *actor,
                                     ClutterPaintVolume   *volume);

  gboolean (* has_overlaps)         (ClutterActor         *self);

  void     (* paint_node)           (ClutterActor         *self,
                                     ClutterPaintNode     *root,
                                     ClutterPaintContext  *paint_context);

  gboolean (* touch_event)          (ClutterActor         *self,
                                     ClutterEvent         *event);
  void     (* resource_scale_changed) (ClutterActor *self);
  float    (* calculate_resource_scale) (ClutterActor *self,
                                         int           phase);

  void     (* child_added)          (ClutterActor         *self,
                                     ClutterActor         *child);
  void     (* child_removed)        (ClutterActor         *self,
                                     ClutterActor         *child);

  /* private */
  GType layout_manager_type;
};

/**
 * ClutterActorIter:
 *
 * An iterator structure that allows to efficiently iterate over a
 * section of the scene graph.
 *
 * The contents of the #ClutterActorIter structure
 * are private and should only be accessed using the provided API.
 */
struct _ClutterActorIter
{
  /*< private >*/
  gpointer CLUTTER_PRIVATE_FIELD (dummy1);
  gpointer CLUTTER_PRIVATE_FIELD (dummy2);
  gint     CLUTTER_PRIVATE_FIELD (dummy3);
};

CLUTTER_EXPORT
GType clutter_actor_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterActor *                  clutter_actor_new                               (void);

CLUTTER_EXPORT
void                            clutter_actor_show                              (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_hide                              (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_realize                           (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_unrealize                         (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_map                               (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_unmap                             (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_paint                             (ClutterActor                *self,
                                                                                 ClutterPaintContext         *paint_context);
CLUTTER_EXPORT
void                            clutter_actor_continue_paint                    (ClutterActor                *self,
                                                                                 ClutterPaintContext         *paint_context);
CLUTTER_EXPORT
ClutterPaintNode *              clutter_actor_create_texture_paint_node         (ClutterActor                *self,
                                                                                 CoglTexture                 *texture);
CLUTTER_EXPORT
void                            clutter_actor_pick                              (ClutterActor                *actor,
                                                                                 ClutterPickContext          *pick_context);
CLUTTER_EXPORT
void                            clutter_actor_continue_pick                     (ClutterActor                *actor,
                                                                                 ClutterPickContext          *pick_context);
CLUTTER_EXPORT
void                            clutter_actor_queue_redraw                      (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_queue_redraw_with_clip            (ClutterActor       *self,
                                                                                 const MtkRectangle *clip);
CLUTTER_EXPORT
void                            clutter_actor_queue_relayout                    (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_destroy                           (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_name                          (ClutterActor                *self,
                                                                                 const gchar                 *name);
CLUTTER_EXPORT
const gchar *                   clutter_actor_get_name                          (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_accessible_role               (ClutterActor                *self,
                                                                                 AtkRole                      role);
CLUTTER_EXPORT
AtkRole                         clutter_actor_get_accessible_role               (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_accessible_name               (ClutterActor                *self,
                                                                                 const gchar                 *name);
CLUTTER_EXPORT
const gchar *                   clutter_actor_get_accessible_name               (ClutterActor                *self);
CLUTTER_EXPORT
AtkObject *                     clutter_actor_get_accessible                    (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_accessible                    (ClutterActor                *self,
                                                                                 AtkObject                   *accessible);
CLUTTER_EXPORT
void                            clutter_actor_add_accessible_state              (ClutterActor                *actor,
                                                                                 AtkStateType                 state);
CLUTTER_EXPORT
void                            clutter_actor_remove_accessible_state           (ClutterActor                *actor,
                                                                                 AtkStateType                 state);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_visible                        (ClutterActor                *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_mapped                         (ClutterActor                *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_realized                       (ClutterActor                *self);

/* Size negotiation */
CLUTTER_EXPORT
void                            clutter_actor_set_no_layout                     (ClutterActor               *actor,
                                                                                 gboolean                    no_layout);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_no_layout                      (ClutterActor               *actor);
CLUTTER_EXPORT
void                            clutter_actor_set_request_mode                  (ClutterActor                *self,
                                                                                 ClutterRequestMode           mode);
CLUTTER_EXPORT
ClutterRequestMode              clutter_actor_get_request_mode                  (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_get_preferred_width               (ClutterActor                *self,
                                                                                 gfloat                       for_height,
                                                                                 gfloat                      *min_width_p,
                                                                                 gfloat                      *natural_width_p);
CLUTTER_EXPORT
void                            clutter_actor_get_preferred_height              (ClutterActor                *self,
                                                                                 gfloat                       for_width,
                                                                                 gfloat                      *min_height_p,
                                                                                 gfloat                      *natural_height_p);
CLUTTER_EXPORT
void                            clutter_actor_get_preferred_size                (ClutterActor                *self,
                                                                                 gfloat                      *min_width_p,
                                                                                 gfloat                      *min_height_p,
                                                                                 gfloat                      *natural_width_p,
                                                                                 gfloat                      *natural_height_p);
CLUTTER_EXPORT
void                            clutter_actor_allocate                          (ClutterActor                *self,
                                                                                 const ClutterActorBox       *box);
CLUTTER_EXPORT
void                            clutter_actor_allocate_preferred_size           (ClutterActor                *self,
                                                                                 float                        x,
                                                                                 float                        y);
CLUTTER_EXPORT
void                            clutter_actor_allocate_available_size           (ClutterActor                *self,
                                                                                 gfloat                       x,
                                                                                 gfloat                       y,
                                                                                 gfloat                       available_width,
                                                                                 gfloat                       available_height);
CLUTTER_EXPORT
void                            clutter_actor_allocate_align_fill               (ClutterActor                *self,
                                                                                 const ClutterActorBox       *box,
                                                                                 gdouble                      x_align,
                                                                                 gdouble                      y_align,
                                                                                 gboolean                     x_fill,
                                                                                 gboolean                     y_fill);
CLUTTER_EXPORT
void                            clutter_actor_set_allocation                    (ClutterActor                *self,
                                                                                 const ClutterActorBox       *box);
CLUTTER_EXPORT
void                            clutter_actor_get_allocation_box                (ClutterActor                *self,
                                                                                 ClutterActorBox             *box);
CLUTTER_EXPORT
gboolean                        clutter_actor_has_allocation                    (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_size                          (ClutterActor                *self,
                                                                                 gfloat                       width,
                                                                                 gfloat                       height);
CLUTTER_EXPORT
void                            clutter_actor_get_size                          (ClutterActor                *self,
                                                                                 gfloat                      *width,
                                                                                 gfloat                      *height);
CLUTTER_EXPORT
void                            clutter_actor_set_position                      (ClutterActor                *self,
                                                                                 gfloat                       x,
                                                                                 gfloat                       y);
CLUTTER_EXPORT
gboolean clutter_actor_get_fixed_position (ClutterActor *self,
                                           float        *x,
                                           float        *y);
CLUTTER_EXPORT
void                            clutter_actor_get_position                      (ClutterActor                *self,
                                                                                 gfloat                      *x,
                                                                                 gfloat                      *y);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_fixed_position_set            (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_fixed_position_set            (ClutterActor                *self,
                                                                                 gboolean                     is_set);
CLUTTER_EXPORT
void                            clutter_actor_move_by                           (ClutterActor                *self,
                                                                                 gfloat                       dx,
                                                                                 gfloat                       dy);

/* Actor geometry */
CLUTTER_EXPORT
gfloat                          clutter_actor_get_width                         (ClutterActor                *self);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_height                        (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_width                         (ClutterActor                *self,
                                                                                 gfloat                       width);
CLUTTER_EXPORT
void                            clutter_actor_set_height                        (ClutterActor                *self,
                                                                                 gfloat                       height);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_x                             (ClutterActor                *self);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_y                             (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_x                             (ClutterActor                *self,
                                                                                 gfloat                       x);
CLUTTER_EXPORT
void                            clutter_actor_set_y                             (ClutterActor                *self,
                                                                                 gfloat                       y);
CLUTTER_EXPORT
void                            clutter_actor_set_z_position                    (ClutterActor                *self,
                                                                                 gfloat                       z_position);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_z_position                    (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_layout_manager                (ClutterActor                *self,
                                                                                 ClutterLayoutManager        *manager);
CLUTTER_EXPORT
ClutterLayoutManager *          clutter_actor_get_layout_manager                (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_x_align                       (ClutterActor                *self,
                                                                                 ClutterActorAlign            x_align);
CLUTTER_EXPORT
ClutterActorAlign               clutter_actor_get_x_align                       (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_y_align                       (ClutterActor                *self,
                                                                                 ClutterActorAlign            y_align);
CLUTTER_EXPORT
ClutterActorAlign               clutter_actor_get_y_align                       (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_margin_top                    (ClutterActor                *self,
                                                                                 gfloat                       margin);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_margin_top                    (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_margin_bottom                 (ClutterActor                *self,
                                                                                 gfloat                       margin);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_margin_bottom                 (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_margin_left                   (ClutterActor                *self,
                                                                                 gfloat                       margin);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_margin_left                   (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_margin_right                  (ClutterActor                *self,
                                                                                 gfloat                       margin);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_margin_right                  (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_margin                        (ClutterActor                *self,
                                                                                 const ClutterMargin         *margin);
CLUTTER_EXPORT
void                            clutter_actor_get_margin                        (ClutterActor                *self,
                                                                                 ClutterMargin               *margin);
CLUTTER_EXPORT
void                            clutter_actor_set_x_expand                      (ClutterActor                *self,
                                                                                 gboolean                     expand);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_x_expand                      (ClutterActor                *self);
CLUTTER_EXPORT
void                            clutter_actor_set_y_expand                      (ClutterActor                *self,
                                                                                 gboolean                     expand);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_y_expand                      (ClutterActor                *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_needs_expand                      (ClutterActor                *self,
                                                                                 ClutterOrientation           orientation);

/* Paint */
CLUTTER_EXPORT
void                            clutter_actor_set_clip                          (ClutterActor                *self,
                                                                                 gfloat                       xoff,
                                                                                 gfloat                       yoff,
                                                                                 gfloat                       width,
                                                                                 gfloat                       height);
CLUTTER_EXPORT
void                            clutter_actor_remove_clip                       (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_has_clip                          (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_get_clip                          (ClutterActor               *self,
                                                                                 gfloat                     *xoff,
                                                                                 gfloat                     *yoff,
                                                                                 gfloat                     *width,
                                                                                 gfloat                     *height);
CLUTTER_EXPORT
void                            clutter_actor_set_clip_to_allocation            (ClutterActor               *self,
                                                                                 gboolean                    clip_set);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_clip_to_allocation            (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_opacity                       (ClutterActor               *self,
                                                                                 guint8                      opacity);
CLUTTER_EXPORT
guint8                          clutter_actor_get_opacity                       (ClutterActor               *self);
CLUTTER_EXPORT
guint8                          clutter_actor_get_paint_opacity                 (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_paint_visibility              (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_offscreen_redirect            (ClutterActor               *self,
                                                                                 ClutterOffscreenRedirect    redirect);
CLUTTER_EXPORT
ClutterOffscreenRedirect        clutter_actor_get_offscreen_redirect            (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_should_pick                       (ClutterActor               *self,
                                                                                 ClutterPickContext         *pick_context);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_in_clone_paint                 (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_paint_box                     (ClutterActor               *self,
                                                                                 ClutterActorBox            *box);

CLUTTER_EXPORT
float                           clutter_actor_get_resource_scale                (ClutterActor *self);

CLUTTER_EXPORT
gboolean                        clutter_actor_has_overlaps                      (ClutterActor               *self);

/* Content */
CLUTTER_EXPORT
void                            clutter_actor_set_content                       (ClutterActor               *self,
                                                                                 ClutterContent             *content);
CLUTTER_EXPORT
ClutterContent *                clutter_actor_get_content                       (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_content_gravity               (ClutterActor               *self,
                                                                                 ClutterContentGravity       gravity);
CLUTTER_EXPORT
ClutterContentGravity           clutter_actor_get_content_gravity               (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_content_scaling_filters       (ClutterActor               *self,
                                                                                 ClutterScalingFilter        min_filter,
                                                                                 ClutterScalingFilter        mag_filter);
CLUTTER_EXPORT
void                            clutter_actor_get_content_scaling_filters       (ClutterActor               *self,
                                                                                 ClutterScalingFilter       *min_filter,
                                                                                 ClutterScalingFilter       *mag_filter);
CLUTTER_EXPORT
void                            clutter_actor_set_content_repeat                (ClutterActor               *self,
                                                                                 ClutterContentRepeat        repeat);
CLUTTER_EXPORT
ClutterContentRepeat            clutter_actor_get_content_repeat                (ClutterActor               *self);

CLUTTER_EXPORT
void                            clutter_actor_set_color_state                   (ClutterActor               *self,
                                                                                 ClutterColorState          *color_state);

CLUTTER_EXPORT
void                            clutter_actor_unset_color_state                 (ClutterActor               *self);

CLUTTER_EXPORT
ClutterColorState *             clutter_actor_get_color_state                   (ClutterActor               *self);

CLUTTER_EXPORT
void                            clutter_actor_get_content_box                   (ClutterActor               *self,
                                                                                 ClutterActorBox            *box);
CLUTTER_EXPORT
void                            clutter_actor_set_background_color              (ClutterActor               *self,
                                                                                 const CoglColor            *color);
CLUTTER_EXPORT
void                            clutter_actor_get_background_color              (ClutterActor               *self,
                                                                                 CoglColor                  *color);
CLUTTER_EXPORT
const ClutterPaintVolume *      clutter_actor_get_paint_volume                  (ClutterActor               *self);
CLUTTER_EXPORT
ClutterPaintVolume *            clutter_actor_get_transformed_paint_volume      (ClutterActor               *self,
                                                                                 ClutterActor               *relative_to_ancestor);

/* Events */
CLUTTER_EXPORT
void                            clutter_actor_set_reactive                      (ClutterActor               *actor,
                                                                                 gboolean                    reactive);
CLUTTER_EXPORT
gboolean                        clutter_actor_get_reactive                      (ClutterActor               *actor);
CLUTTER_EXPORT
gboolean                        clutter_actor_has_key_focus                     (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_grab_key_focus                    (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_event                             (ClutterActor               *actor,
                                                                                 const ClutterEvent         *event,
                                                                                 gboolean                    capture);
CLUTTER_EXPORT
gboolean                        clutter_actor_has_pointer                       (ClutterActor               *self);

CLUTTER_EXPORT
void                            clutter_actor_set_text_direction                (ClutterActor               *self,
                                                                                 ClutterTextDirection        text_dir);
CLUTTER_EXPORT
ClutterTextDirection            clutter_actor_get_text_direction                (ClutterActor               *self);

/* Actor hierarchy */
CLUTTER_EXPORT
void                            clutter_actor_add_child                         (ClutterActor               *self,
                                                                                 ClutterActor               *child);
CLUTTER_EXPORT
void                            clutter_actor_insert_child_at_index             (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 gint                        index_);
CLUTTER_EXPORT
void                            clutter_actor_insert_child_above                (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 ClutterActor               *sibling);
CLUTTER_EXPORT
void                            clutter_actor_insert_child_below                (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 ClutterActor               *sibling);
CLUTTER_EXPORT
void                            clutter_actor_replace_child                     (ClutterActor               *self,
                                                                                 ClutterActor               *old_child,
                                                                                 ClutterActor               *new_child);
CLUTTER_EXPORT
void                            clutter_actor_remove_child                      (ClutterActor               *self,
                                                                                 ClutterActor               *child);
CLUTTER_EXPORT
void                            clutter_actor_remove_all_children               (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_destroy_all_children              (ClutterActor               *self);
CLUTTER_EXPORT
GList *                         clutter_actor_get_children                      (ClutterActor               *self);
CLUTTER_EXPORT
gint                            clutter_actor_get_n_children                    (ClutterActor               *self);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_child_at_index                (ClutterActor               *self,
                                                                                 gint                        index_);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_previous_sibling              (ClutterActor               *self);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_next_sibling                  (ClutterActor               *self);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_first_child                   (ClutterActor               *self);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_last_child                    (ClutterActor               *self);
CLUTTER_EXPORT
ClutterActor *                  clutter_actor_get_parent                        (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_contains                          (ClutterActor               *self,
                                                                                 ClutterActor               *descendant);
CLUTTER_EXPORT
ClutterActor*                   clutter_actor_get_stage                         (ClutterActor               *actor);

CLUTTER_EXPORT
ClutterContext *                clutter_actor_get_context                       (ClutterActor *actor);

CLUTTER_EXPORT
void                            clutter_actor_set_child_below_sibling           (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 ClutterActor               *sibling);
CLUTTER_EXPORT
void                            clutter_actor_set_child_above_sibling           (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 ClutterActor               *sibling);
CLUTTER_EXPORT
void                            clutter_actor_set_child_at_index                (ClutterActor               *self,
                                                                                 ClutterActor               *child,
                                                                                 gint                        index_);
CLUTTER_EXPORT
void                            clutter_actor_iter_init                         (ClutterActorIter           *iter,
                                                                                 ClutterActor               *root);
CLUTTER_EXPORT
gboolean                        clutter_actor_iter_next                         (ClutterActorIter           *iter,
                                                                                 ClutterActor              **child);
CLUTTER_EXPORT
gboolean                        clutter_actor_iter_prev                         (ClutterActorIter           *iter,
                                                                                 ClutterActor              **child);
CLUTTER_EXPORT
void                            clutter_actor_iter_remove                       (ClutterActorIter           *iter);
CLUTTER_EXPORT
void                            clutter_actor_iter_destroy                      (ClutterActorIter           *iter);
CLUTTER_EXPORT
gboolean                        clutter_actor_iter_is_valid                     (const ClutterActorIter     *iter);

/* Transformations */
CLUTTER_EXPORT
gboolean                        clutter_actor_is_rotated                        (ClutterActor               *self);
CLUTTER_EXPORT
gboolean                        clutter_actor_is_scaled                         (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_pivot_point                   (ClutterActor               *self,
                                                                                 gfloat                      pivot_x,
                                                                                 gfloat                      pivot_y);
CLUTTER_EXPORT
void                            clutter_actor_get_pivot_point                   (ClutterActor               *self,
                                                                                 gfloat                     *pivot_x,
                                                                                 gfloat                     *pivot_y);
CLUTTER_EXPORT
void                            clutter_actor_set_pivot_point_z                 (ClutterActor               *self,
                                                                                 gfloat                      pivot_z);
CLUTTER_EXPORT
gfloat                          clutter_actor_get_pivot_point_z                 (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_rotation_angle                (ClutterActor               *self,
                                                                                 ClutterRotateAxis           axis,
                                                                                 gdouble                     angle);
CLUTTER_EXPORT
gdouble                         clutter_actor_get_rotation_angle                (ClutterActor               *self,
                                                                                 ClutterRotateAxis           axis);
CLUTTER_EXPORT
void                            clutter_actor_set_scale                         (ClutterActor               *self,
                                                                                 gdouble                     scale_x,
                                                                                 gdouble                     scale_y);
CLUTTER_EXPORT
void                            clutter_actor_get_scale                         (ClutterActor               *self,
                                                                                 gdouble                    *scale_x,
                                                                                 gdouble                    *scale_y);
CLUTTER_EXPORT
void                            clutter_actor_set_scale_z                       (ClutterActor               *self,
                                                                                 gdouble                     scale_z);
CLUTTER_EXPORT
gdouble                         clutter_actor_get_scale_z                       (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_translation                   (ClutterActor               *self,
                                                                                 gfloat                      translate_x,
                                                                                 gfloat                      translate_y,
                                                                                 gfloat                      translate_z);
CLUTTER_EXPORT
void                            clutter_actor_get_translation                   (ClutterActor               *self,
                                                                                 gfloat                     *translate_x,
                                                                                 gfloat                     *translate_y,
                                                                                 gfloat                     *translate_z);
CLUTTER_EXPORT
void                            clutter_actor_set_transform                     (ClutterActor               *self,
                                                                                 const graphene_matrix_t    *transform);
CLUTTER_EXPORT
void                            clutter_actor_get_transform                     (ClutterActor               *self,
                                                                                 graphene_matrix_t          *transform);
CLUTTER_EXPORT
void                            clutter_actor_set_child_transform               (ClutterActor               *self,
                                                                                 const graphene_matrix_t    *transform);
CLUTTER_EXPORT
void                            clutter_actor_get_child_transform               (ClutterActor               *self,
                                                                                 graphene_matrix_t          *transform);

CLUTTER_EXPORT
void                            clutter_actor_get_transformed_extents          (ClutterActor               *self,
                                                                                graphene_rect_t            *rect);

CLUTTER_EXPORT
void                            clutter_actor_get_transformed_position          (ClutterActor               *self,
                                                                                 gfloat                     *x,
                                                                                 gfloat                     *y);
CLUTTER_EXPORT
void                            clutter_actor_get_transformed_size              (ClutterActor               *self,
                                                                                 gfloat                     *width,
                                                                                 gfloat                     *height);
CLUTTER_EXPORT
gboolean                        clutter_actor_transform_stage_point             (ClutterActor               *self,
                                                                                 gfloat                      x,
                                                                                 gfloat                      y,
                                                                                 gfloat                     *x_out,
                                                                                 gfloat                     *y_out);
CLUTTER_EXPORT
void                            clutter_actor_get_abs_allocation_vertices       (ClutterActor               *self,
                                                                                 graphene_point3d_t         *verts);
CLUTTER_EXPORT
void                            clutter_actor_apply_transform_to_point          (ClutterActor               *self,
                                                                                 const graphene_point3d_t   *point,
                                                                                 graphene_point3d_t         *vertex);
CLUTTER_EXPORT
void                            clutter_actor_apply_relative_transform_to_point (ClutterActor               *self,
                                                                                 ClutterActor               *ancestor,
                                                                                 const graphene_point3d_t   *point,
                                                                                 graphene_point3d_t         *vertex);

/* Implicit animations */
CLUTTER_EXPORT
void                            clutter_actor_save_easing_state                 (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_restore_easing_state              (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_easing_mode                   (ClutterActor               *self,
                                                                                 ClutterAnimationMode        mode);
CLUTTER_EXPORT
ClutterAnimationMode            clutter_actor_get_easing_mode                   (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_easing_duration               (ClutterActor               *self,
                                                                                 guint                       msecs);
CLUTTER_EXPORT
guint                           clutter_actor_get_easing_duration               (ClutterActor               *self);
CLUTTER_EXPORT
void                            clutter_actor_set_easing_delay                  (ClutterActor               *self,
                                                                                 guint                       msecs);
CLUTTER_EXPORT
guint                           clutter_actor_get_easing_delay                  (ClutterActor               *self);
CLUTTER_EXPORT
ClutterTransition *             clutter_actor_get_transition                    (ClutterActor               *self,
                                                                                 const char                 *name);
CLUTTER_EXPORT
void                            clutter_actor_add_transition                    (ClutterActor               *self,
                                                                                 const char                 *name,
                                                                                 ClutterTransition          *transition);
CLUTTER_EXPORT
void                            clutter_actor_remove_transition                 (ClutterActor               *self,
                                                                                 const char                 *name);
CLUTTER_EXPORT
void                            clutter_actor_remove_all_transitions            (ClutterActor               *self);


CLUTTER_EXPORT
gboolean                        clutter_actor_has_mapped_clones                 (ClutterActor *self);
CLUTTER_EXPORT
void                            clutter_actor_set_opacity_override              (ClutterActor               *self,
                                                                                 gint                        opacity);
CLUTTER_EXPORT
gint                            clutter_actor_get_opacity_override              (ClutterActor               *self);

CLUTTER_EXPORT
void                            clutter_actor_inhibit_culling                   (ClutterActor               *actor);
CLUTTER_EXPORT
void                            clutter_actor_uninhibit_culling                 (ClutterActor               *actor);

/**
 * ClutterActorCreateChildFunc:
 * @item: (type GObject): the item in the model
 * @user_data: Data passed to clutter_actor_bind_model()
 *
 * Creates a #ClutterActor using the @item in the model.
 *
 * The usual way to implement this function is to create a #ClutterActor
 * instance and then bind the #GObject properties to the actor properties
 * of interest, using g_object_bind_property(). This way, when the @item
 * in the #GListModel changes, the #ClutterActor changes as well.
 *
 * Returns: (transfer full): The newly created child #ClutterActor4
 */
typedef ClutterActor * (* ClutterActorCreateChildFunc) (gpointer item,
                                                        gpointer user_data);

CLUTTER_EXPORT
void                            clutter_actor_bind_model                        (ClutterActor               *self,
                                                                                 GListModel                 *model,
                                                                                 ClutterActorCreateChildFunc create_child_func,
                                                                                 gpointer                    user_data,
                                                                                 GDestroyNotify              notify);
CLUTTER_EXPORT
void                            clutter_actor_bind_model_with_properties        (ClutterActor               *self,
                                                                                 GListModel                 *model,
                                                                                 GType                       child_type,
                                                                                 const char                 *first_model_property,
                                                                                 ...);

CLUTTER_EXPORT
void clutter_actor_pick_box (ClutterActor          *self,
                             ClutterPickContext    *pick_context,
                             const ClutterActorBox *box);

CLUTTER_EXPORT
GList * clutter_actor_peek_stage_views (ClutterActor *self);

CLUTTER_EXPORT
void clutter_actor_invalidate_transform (ClutterActor *self);

CLUTTER_EXPORT
void clutter_actor_invalidate_paint_volume (ClutterActor *self);

CLUTTER_EXPORT
void clutter_actor_class_set_layout_manager_type (ClutterActorClass *actor_class,
                                                  GType              type);
CLUTTER_EXPORT
GType clutter_actor_class_get_layout_manager_type (ClutterActorClass *actor_class);

G_END_DECLS
