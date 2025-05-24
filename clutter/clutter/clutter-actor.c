/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006, 2007, 2008 OpenedHand Ltd
 * Copyright (C) 2009, 2010, 2011, 2012 Intel Corp
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

/**
 * ClutterActor:
 *
 * The basic element of the scene graph
 *
 * The ClutterActor class is the basic element of the scene graph in Clutter,
 * and it encapsulates the position, size, and transformations of a node in
 * the graph.
 *
 * ## Actor transformations
 *
 * Each actor can be transformed using methods like [method@Actor.set_scale]
 * or [method@Actor.set_rotation_angle]. The order in which the transformations are
 * applied is decided by Clutter and it is the following:
 *
 *  1. translation by the origin of the [property@Actor:allocation] property
 *  2. translation by the actor's [property@Actor:z-position] property
 *  3. translation by the actor's [property@Actor:pivot-point] property
 *  4. scaling by the [property@Actor:scale-x] and [property@Actor:scale-y] factors
 *  5. rotation around the [property@Actor:rotation-angle-x]
 *  6. rotation around the [property@Actor:rotation-angle-y]
 *  7. rotation around the [property@Actor:rotation-angle-z]
 *  8. negative translation by the actor's [property@Actor:pivot-point]
 *
 * ## Modifying an actor's geometry
 *
 * Each actor has a bounding box, called [property@Actor:allocation]
 * which is either set by its parent or explicitly through the
 * [method@Actor.set_position] and [method@Actor.set_size] methods.
 * Each actor also has an implicit preferred size.
 *
 * An actor’s preferred size can be defined by any subclass by
 * overriding the [vfunc@Actor.get_preferred_width] and the
 * [vfunc@Actor.get_preferred_height] virtual functions, or it can
 * be explicitly set by using [method@Actor.set_width] and
 * [method@Actor.set_height].
 *
 * An actor’s position can be set explicitly by using
 * [method@Actor.set_x] and [method@Actor.set_y]; the coordinates are
 * relative to the origin of the actor’s parent.
 *
 * ## Managing actor children
 *
 * Each actor can have multiple children, by calling
 * [method@Clutter.Actor.add_child] to add a new child actor, and
 * [method@Clutter.Actor.remove_child] to remove an existing child. #ClutterActor
 * will hold a reference on each child actor, which will be released when
 * the child is removed from its parent, or destroyed using
 * [method@Clutter.Actor.destroy].
 *
 * ```c
 *  ClutterActor *actor = clutter_actor_new ();
 *
 *  // set the bounding box of the actor
 *  clutter_actor_set_position (actor, 0, 0);
 *  clutter_actor_set_size (actor, 480, 640);
 *
 *  // set the background color of the actor
 *  clutter_actor_set_background_color (actor, &COGL_COLOR_INIT (0xf5, 0x79, 0x00, 0xff));
 *
 *  // set the bounding box of the child, relative to the parent
 *  ClutterActor *child = clutter_actor_new ();
 *  clutter_actor_set_position (child, 20, 20);
 *  clutter_actor_set_size (child, 80, 240);
 *
 *  // set the background color of the child
 *  clutter_actor_set_background_color (child, &COGL_COLOR_INIT (0x00, 0x00, 0xff, 0xff));
 *
 *  // add the child to the actor
 *  clutter_actor_add_child (actor, child);
 * ```
 *
 * Children can be inserted at a given index, or above and below
 * another child actor. The order of insertion determines the order of the
 * children when iterating over them. Iterating over children is performed
 * by using [method@Clutter.Actor.get_first_child], [method@Clutter.Actor.get_previous_sibling],
 * [method@Clutter.Actor.get_next_sibling], and [method@Clutter.Actor.get_last_child]. It is
 * also possible to retrieve a list of children by using
 * [method@Clutter.Actor.get_children], as well as retrieving a specific child at a
 * given index by using [method@Clutter.Actor.get_child_at_index].
 *
 * If you need to track additions of children to a [type@Clutter.Actor], use
 * the [signal@Clutter.Actor::child-added] signal; similarly, to track
 * removals of children from a ClutterActor, use the
 * [signal@Clutter.Actor::child-removed] signal.
 *
 * ## Painting an actor
 *
 * There are three ways to paint an actor:
 *
 *  - set a delegate #ClutterContent as the value for the [property@Clutter.Actor:content] property of the actor
 *  - subclass #ClutterActor and override the [vfunc@Clutter.Actor.paint_node] virtual function
 *  - subclass #ClutterActor and override the [vfunc@Clutter.Actor.paint] virtual function.
 *
 * A #ClutterContent is a delegate object that takes over the painting
 * operations of one, or more actors. The #ClutterContent painting will
 * be performed on top of the [property@Clutter.Actor:background-color] of the actor,
 * and before calling the actor's own implementation of the
 * [vfunc@Clutter.Actor.paint_node] virtual function.
 *
 * ```c
 * ClutterActor *actor = clutter_actor_new ();
 *
 * // set the bounding box
 * clutter_actor_set_position (actor, 50, 50);
 * clutter_actor_set_size (actor, 100, 100);
 *
 * // set the content; the image_content variable is set elsewhere
 * clutter_actor_set_content (actor, image_content);
 * ```
 *
 * The [vfunc@Clutter.Actor.paint_node] virtual function is invoked whenever
 * an actor needs to be painted. The implementation of the virtual function
 * must only paint the contents of the actor itself, and not the contents of
 * its children, if the actor has any.
 *
 * The #ClutterPaintNode passed to the virtual function is the local root of
 * the render tree; any node added to it will be rendered at the correct
 * position, as defined by the actor's [property@Clutter.Actor:allocation].
 *
 * ```c
 * static void
 * my_actor_paint_node (ClutterActor     *actor,
 *                      ClutterPaintNode *root)
 * {
 *   ClutterPaintNode *node;
 *   ClutterActorBox box;
 *
 *   // where the content of the actor should be painted
 *   clutter_actor_get_allocation_box (actor, &box);
 *
 *   // the cogl_texture variable is set elsewhere
 *   node = clutter_texture_node_new (cogl_texture, &COGL_COLOR_INIT (255, 255, 255, 255),
 *                                    CLUTTER_SCALING_FILTER_TRILINEAR,
 *                                    CLUTTER_SCALING_FILTER_LINEAR);
 *
 *   // paint the content of the node using the allocation
 *   clutter_paint_node_add_rectangle (node, &box);
 *
 *   // add the node, and transfer ownership
 *   clutter_paint_node_add_child (root, node);
 *   clutter_paint_node_unref (node);
 * }
 * ```
 *
 * The [vfunc@Clutter.Actor.paint] virtual function function gives total
 * control to the paint sequence of the actor itself, including the
 * children of the actor, if any. It is strongly discouraged to override
 * the [vfunc@Clutter.Actor.paint] virtual function and it will be removed
 * when the Clutter API changes.
 *
 * ## Handling events on an actor
 *
 * A #ClutterActor can receive and handle input device events, for
 * instance pointer events and key events, as long as its
 * [property@Clutter.Actor:reactive] property is set to %TRUE.
 *
 * Once an actor has been determined to be the source of an event,
 * Clutter will traverse the scene graph from the top-level actor towards the
 * event source, emitting the [signal@Clutter.Actor::captured-event] signal on each
 * ancestor until it reaches the source; this phase is also called
 * the "capture" phase. If the event propagation was not stopped, the graph
 * is walked backwards, from the source actor to the top-level, and the
 * [signal@Clutter.Actor::event signal] is emitted, alongside eventual event-specific
 * signals like [signal@Clutter.Actor::button-press-event] or [signal@Clutter.Actor::motion-event];
 * this phase is also called the "bubble" phase.
 *
 * At any point of the signal emission, signal handlers can stop the propagation
 * through the scene graph by returning %CLUTTER_EVENT_STOP; otherwise, they can
 * continue the propagation by returning %CLUTTER_EVENT_PROPAGATE.
 *
 * ## Animation
 *
 * Animation is a core concept of modern user interfaces; Clutter provides a
 * complete and powerful animation framework that automatically tweens the
 * actor's state without requiring direct, frame by frame manipulation from
 * your application code. You have two models at your disposal:
 *
 *  - an implicit animation model
 *  - an explicit animation model
 *
 * The implicit animation model of Clutter assumes that all the
 * changes in an actor state should be gradual and asynchronous; Clutter
 * will automatically transition an actor's property change between the
 * current state and the desired one without manual intervention, if the
 * property is defined to be animatable in its documentation.
 *
 * By default, in the 1.0 API series, the transition happens with a duration
 * of zero milliseconds, and the implicit animation is an opt in feature to
 * retain backwards compatibility.
 *
 * Implicit animations depend on the current easing state; in order to use
 * the default easing state for an actor you should call the
 * [method@Clutter.Actor.save_easing_state] function:
 *
 * ```c
 * // assume that the actor is currently positioned at (100, 100)
 *
 * // store the current easing state and reset the new easing state to
 * // its default values
 * clutter_actor_save_easing_state (actor);
 *
 * // change the actor's position
 * clutter_actor_set_position (actor, 500, 500);
 *
 * // restore the previously saved easing state
 * clutter_actor_restore_easing_state (actor);
 * ```
 *
 * The example above will trigger an implicit animation of the
 * actor between its current position to a new position.
 *
 * Implicit animations use a default duration of 250 milliseconds,
 * and a default easing mode of %CLUTTER_EASE_OUT_CUBIC, unless you call
 * [method@Clutter.Actor.set_easing_mode] and [method@Clutter.Actor.set_easing_duration]
 * after changing the easing state of the actor.
 *
 * It is possible to animate multiple properties of an actor
 * at the same time, and you can animate multiple actors at the same
 * time as well, for instance:
 *
 * ```c
 * clutter_actor_save_easing_state (actor);
 *
 * // animate the actor's opacity and depth
 * clutter_actor_set_opacity (actor, 0);
 * clutter_actor_set_z_position (actor, -100);
 *
 * clutter_actor_restore_easing_state (actor);
 *
 * clutter_actor_save_easing_state (another_actor);
 *
 * // animate another actor's opacity
 * clutter_actor_set_opacity (another_actor, 255);
 * clutter_actor_set_z_position (another_actor, 100);
 *
 * clutter_actor_restore_easing_state (another_actor);
 * ```
 *
 * Changing the easing state will affect all the following property
 * transitions, but will not affect existing transitions.
 *
 * It is important to note that if you modify the state on an
 * animatable property while a transition is in flight, the transition's
 * final value will be updated, as well as its duration and progress
 * mode by using the current easing state; for instance, in the following
 * example:
 *
 * ```c
 * clutter_actor_save_easing_state (actor);
 * clutter_actor_set_easing_duration (actor, 1000);
 * clutter_actor_set_x (actor, 200);
 * clutter_actor_restore_easing_state (actor);
 *
 * clutter_actor_save_easing_state (actor);
 * clutter_actor_set_easing_duration (actor, 500);
 * clutter_actor_set_x (actor, 100);
 * clutter_actor_restore_easing_state (actor);
 * ```
 *
 * the first call to [method@Clutter.Actor.set_x] will begin a transition
 * of the [property@Clutter.Actor:x] property from the current value to the value of
 * 200 over a duration of one second; the second call to [method@Clutter.Actor.set_x]
 * will change the transition's final value to 100 and the duration to 500
 * milliseconds.
 *
 * It is possible to receive a notification of the completion of an
 * implicit transition by using the [signal@Clutter.Actor::transition-stopped]
 * signal, decorated with the name of the property. In case you want to
 * know when all the currently in flight transitions are complete, use
 * the [signal@Clutter.Actor::transitions-completed] signal instead.
 *
 * It is possible to retrieve the [class@Clutter.Transition] used by the
 * animatable properties by using [method@Clutter.Actor.get_transition] and using
 * the property name as the transition name.
 *
 * The explicit animation model supported by Clutter requires that
 * you create a #ClutterTransition object, and optionally set the initial
 * and final values. The transition will not start unless you add it to the
 * #ClutterActor.
 *
 * ```c
 * ClutterTransition *transition;
 *
 * transition = clutter_property_transition_new_for_actor (actor, "opacity");
 * clutter_timeline_set_duration (CLUTTER_TIMELINE (transition), 3000);
 * clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);
 * clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
 * clutter_transition_set_from (transition, G_TYPE_UINT, 255);
 * clutter_transition_set_to (transition, G_TYPE_UINT, 0);
 *
 * clutter_actor_add_transition (actor, "animate-opacity", transition);
 * ```
 *
 * The example above will animate the [property@Clutter.Actor:opacity] property
 * of an actor between fully opaque and fully transparent, and back, over
 * a span of 3 seconds. The animation does not begin until it is added to
 * the actor.
 *
 * The explicit animation API applies to all #GObject properties,
 * as well as the custom properties defined through the [iface@Clutter.Animatable]
 * interface, regardless of whether they are defined as implicitly
 * animatable or not.
 *
 * The explicit animation API should also be used when using custom
 * animatable properties for [class@Clutter.Action], [class@Clutter.Constraint], and
 * [class@Clutter.Effect] instances associated to an actor; see the section on
 * custom animatable properties below for an example.
 *
 * Finally, explicit animations are useful for creating animations
 * that run continuously, for instance:
 *
 * ```c
 * // this animation will pulse the actor's opacity continuously
 * ClutterTransition *transition;
 * ClutterInterval *interval;
 *
 * transition = clutter_property_transition_new_for_actor (actor, "opacity");
 *
 * // we want to animate the opacity between 0 and 255
 * clutter_transition_set_from (transition, G_TYPE_UINT, 0);
 * clutter_transition_set_to (transition, G_TYPE_UINT, 255);
 *
 * // over a one second duration, running an infinite amount of times
 * clutter_timeline_set_duration (CLUTTER_TIMELINE (transition), 1000);
 * clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), -1);
 *
 * // we want to fade in and out, so we need to auto-reverse the transition
 * clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
 *
 * // and we want to use an easing function that eases both in and out
 * clutter_timeline_set_progress_mode (CLUTTER_TIMELINE (transition),
 *                                     CLUTTER_EASE_IN_OUT_CUBIC);
 *
 * // add the transition to the desired actor to start it
 * clutter_actor_add_transition (actor, "opacityAnimation", transition);
 * ```
 *
 * ## Implementing an actor
 *
 * Careful consideration should be given when deciding to implement
 * a #ClutterActor sub-class. It is generally recommended to implement a
 * sub-class of #ClutterActor only for actors that should be used as leaf
 * nodes of a scene graph.
 *
 * By overriding the [vfunc@Clutter.Actor.get_preferred_width] and
 * [vfunc@Clutter.Actor.get_preferred_height] virtual functions it is
 * possible to change or provide the preferred size of an actor; similarly,
 * by overriding the [vfunc@Clutter.Actor.allocate] virtual function it is
 * possible to control the layout of the children of an actor. Make sure to
 * always chain up to the parent implementation of the
 * [vfunc@Clutter.Actor.allocate] virtual function.
 *
 * In general, it is strongly encouraged to use delegation and composition
 * instead of direct subclassing.
 *
 * ## Custom animatable properties
 *
 * #ClutterActor allows accessing properties of [class@Clutter.Action],
 * [class@Clutter.Effect], and [class@Clutter.Constraint] instances associated to an actor
 * instance for animation purposes, as well as its [class@Clutter.LayoutManager].
 *
 * In order to access a specific [class@Clutter.Action] or a [class@Clutter.Constraint]
 * property it is necessary to set the [property@Clutter.ActorMeta:name] property on the
 * given action or constraint.
 *
 * The property can be accessed using the following syntax:
 *
 * ```
 *   @<section>.<meta-name>.<property-name>
 * ```
 *
 *  - the initial `@` is mandatory
 *  - the `section` fragment can be one between "actions", "constraints", "content",
 *    and "effects"
 *  - the `meta-name` fragment is the name of the action, effect, or constraint, as
 *    specified by the #ClutterActorMeta:name property of #ClutterActorMeta
 *  - the `property-name` fragment is the name of the action, effect, or constraint
 *    property to be animated.
 *
 * The example below animates a [class@Clutter.BindConstraint] applied to an actor
 * using an explicit transition. The `rect` actor has a binding constraint
 * on the `origin` actor, and in its initial state is overlapping the actor
 * to which is bound to.
 *
 * As the actor has only one [class@Clutter.LayoutManager], the syntax for accessing its
 * properties is simpler:
 *
 * ```
 *   @layout.<property-name>
 * ```
 *
 * ```c
 * constraint = clutter_bind_constraint_new (origin, CLUTTER_BIND_X, 0.0);
 * clutter_actor_meta_set_name (CLUTTER_ACTOR_META (constraint), "bind-x");
 * clutter_actor_add_constraint (rect, constraint);
 *
 * constraint = clutter_bind_constraint_new (origin, CLUTTER_BIND_Y, 0.0);
 * clutter_actor_meta_set_name (CLUTTER_ACTOR_META (constraint), "bind-y");
 * clutter_actor_add_constraint (rect, constraint);
 *
 * clutter_actor_set_reactive (origin, TRUE);
 *
 * g_signal_connect (origin, "button-press-event",
 *                   G_CALLBACK (on_button_press),
 *                   rect);
 * ```
 *
 * On button press, the rectangle "slides" from behind the actor to
 * which is bound to, using the #ClutterBindConstraint:offset property to
 * achieve the effect:
 *
 * ```c
 * gboolean
 * on_button_press (ClutterActor *origin,
 *                  ClutterEvent *event,
 *                  ClutterActor *rect)
 * {
 *   ClutterTransition *transition;
 *
 *   // the offset that we want to apply; this will make the actor
 *   // slide in from behind the origin and rest at the right of
 *   // the origin, plus a padding value
 *   float new_offset = clutter_actor_get_width (origin) + h_padding;
 *
 *   // the property we wish to animate; the "@constraints" section
 *   // tells Clutter to check inside the constraints associated
 *   // with the actor; the "bind-x" section is the name of the
 *   // constraint; and the "offset" is the name of the property
 *   // on the constraint
 *   const char *prop = "@constraints.bind-x.offset";
 *
 *   // create a new transition for the given property
 *   transition = clutter_property_transition_new_for_actor (rect, prop);
 *
 *   // set the easing mode and duration
 *   clutter_timeline_set_progress_mode (CLUTTER_TIMELINE (transition),
 *                                       CLUTTER_EASE_OUT_CUBIC);
 *   clutter_timeline_set_duration (CLUTTER_TIMELINE (transition), 500);
 *
 *   // create the interval with the initial and final values
 *   clutter_transition_set_from (transition, G_TYPE_FLOAT, 0.f);
 *   clutter_transition_set_to (transition, G_TYPE_FLOAT, new_offset);
 *
 *   // add the transition to the actor; this causes the animation
 *   // to start. the name "offsetAnimation" can be used to retrieve
 *   // the transition later
 *   clutter_actor_add_transition (rect, "offsetAnimation", transition);
 *
 *   // we handled the event
 *   return CLUTTER_EVENT_STOP;
 * }
 * ```
 */

#include "config.h"

#include <math.h>

#include <gobject/gvaluecollector.h>
#ifdef HAVE_FONTS
#include <pango/pangocairo.h>
#endif

#include "cogl/cogl.h"

#include "clutter/clutter-actor-private.h"

#ifdef HAVE_FONTS
#include "clutter/pango/clutter-actor-pango.h"
#include "clutter/pango/clutter-pango-private.h"
#endif
#include "clutter/clutter-action.h"
#include "clutter/clutter-action-private.h"
#include "clutter/clutter-actor-meta-private.h"
#include "clutter/clutter-animatable.h"
#include "clutter/clutter-color-state.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-constraint-private.h"
#include "clutter/clutter-content-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-easing.h"
#include "clutter/clutter-effect-private.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-fixed-layout.h"
#include "clutter/clutter-flatten-effect.h"
#include "clutter/clutter-interval.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-paint-context-private.h"
#include "clutter/clutter-paint-nodes.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-paint-volume-private.h"
#include "clutter/clutter-pick-context-private.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-property-transition.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-stage-view-private.h"
#include "clutter/clutter-timeline.h"
#include "clutter/clutter-transition.h"
#include "glib-object.h"


static const CoglColor transparent = { 0x00, 0x00, 0x00, 0x00 };

/* Internal enum used to control mapped state update.  This is a hint
 * which indicates when to do something other than just enforce
 * invariants.
 */
typedef enum
{
  MAP_STATE_CHECK,           /* just enforce invariants. */
  MAP_STATE_MAKE_UNREALIZED, /* force unrealize, ignoring invariants,
                              * used when about to unparent.
                              */
  MAP_STATE_MAKE_MAPPED,     /* set mapped, error if invariants not met;
                              * used to set mapped on toplevels.
                              */
  MAP_STATE_MAKE_UNMAPPED    /* set unmapped, even if parent is mapped,
                              * used just before unmapping parent.
                              */
} MapStateChange;

/* 3 entries should be a good compromise, few layout managers
 * will ask for 3 different preferred size in each allocation cycle */
#define N_CACHED_SIZE_REQUESTS 3

struct _ClutterActorPrivate
{
  ClutterContext *context;

  /* Accessibility */
  AtkObject *accessible;
  gchar *accessible_name;
  AtkStateSet *accessible_state;

  /* request mode */
  ClutterRequestMode request_mode;

  /* our cached size requests for different width / height */
  SizeRequest width_requests[N_CACHED_SIZE_REQUESTS];
  SizeRequest height_requests[N_CACHED_SIZE_REQUESTS];

  /* An age of 0 means the entry is not set */
  guint cached_height_age;
  guint cached_width_age;

  /* the bounding box of the actor, relative to the parent's
   * allocation
   */
  ClutterActorBox allocation;

  /* clip, in actor coordinates */
  graphene_rect_t clip;

  /* the cached transformation matrix; see apply_transform() */
  graphene_matrix_t transform;

  graphene_matrix_t stage_relative_modelview;

  float resource_scale;

  guint8 opacity;
  gint opacity_override;
  unsigned int inhibit_culling_counter;

  ClutterOffscreenRedirect offscreen_redirect;

  /* This is an internal effect used to implement the
     offscreen-redirect property */
  ClutterEffect *flatten_effect;

  /* scene graph */
  ClutterActor *parent;
  ClutterActor *prev_sibling;
  ClutterActor *next_sibling;
  ClutterActor *first_child;
  ClutterActor *last_child;

  gint n_children;

  /* tracks whenever the children of an actor are changed; the
   * age is incremented by 1 whenever an actor is added or
   * removed. the age is not incremented when the first or the
   * last child pointers are changed, or when grandchildren of
   * an actor are changed.
   */
  gint age;

  gchar *name; /* a non-unique name, used for debugging */

  /* a back-pointer to the Pango context that we can use
   * to create pre-configured PangoLayout
   */
#ifdef HAVE_FONTS
  PangoContext *pango_context;
#endif

  /* the text direction configured for this child - either by
   * application code, or by the actor's parent
   */
  ClutterTextDirection text_direction;

  /* meta classes */
  ClutterMetaGroup *actions;
  ClutterMetaGroup *constraints;
  ClutterMetaGroup *effects;

  /* delegate object used to allocate the children of this actor */
  ClutterLayoutManager *layout_manager;

  /* delegate object used to paint the contents of this actor */
  ClutterContent *content;

  ClutterActorBox content_box;
  ClutterContentGravity content_gravity;
  ClutterScalingFilter min_filter;
  ClutterScalingFilter mag_filter;
  ClutterContentRepeat content_repeat;

  /* used when painting, to update the paint volume */
  ClutterEffect *current_effect;

  /* color state contains properties like colorspace for
   * each clutter actor */
  ClutterColorState *color_state;

  /* This is used to store an effect which needs to be redrawn. A
     redraw can be queued to start from a particular effect. This is
     used by parametrised effects that can cache an image of the
     actor. If a parameter of the effect changes then it only needs to
     redraw the cached image, not the actual actor. The pointer is
     only valid if is_dirty == TRUE. If the pointer is NULL then the
     whole actor is dirty. */
  ClutterEffect *effect_to_redraw;

  /* This is used when painting effects to implement the
     clutter_actor_continue_paint() function. It points to the node in
     the list of effects that is next in the chain */
  const GList *next_effect_to_paint;

  ClutterPaintVolume paint_volume;

  /* The paint volume of the actor when it was last drawn to the screen,
   * stored in absolute coordinates.
   */
  ClutterPaintVolume visible_paint_volume;

  CoglColor bg_color;

  /* a string used for debugging messages */
  char *debug_name;

  /* a set of clones of the actor */
  GHashTable *clones;

  /* whether the actor is inside a cloned branch; this
   * value is propagated to all the actor's children
   */
  gulong in_cloned_branch;

  guint unmapped_paint_branch_counter;

  GListModel *child_model;
  ClutterActorCreateChildFunc create_child_func;
  gpointer create_child_data;
  GDestroyNotify create_child_notify;

  gulong resolution_changed_id;
  gulong font_changed_id;
  gulong layout_changed_id;

  GList *stage_views;
  GList *grabs;

  unsigned int n_pointers;
  unsigned int implicitly_grabbed_count;

  GArray *next_redraw_clips;

  /* bitfields: KEEP AT THE END */

  /* fixed position and sizes */
  guint position_set                : 1;
  guint min_width_set               : 1;
  guint min_height_set              : 1;
  guint natural_width_set           : 1;
  guint natural_height_set          : 1;
  /* cached request is invalid (implies allocation is too) */
  guint needs_width_request         : 1;
  /* cached request is invalid (implies allocation is too) */
  guint needs_height_request        : 1;
  /* cached allocation is invalid (request has changed, probably) */
  guint needs_allocation            : 1;
  guint show_on_set_parent          : 1;
  guint has_clip                    : 1;
  guint clip_to_allocation          : 1;
  guint enable_model_view_transform : 1;
  guint enable_paint_unmapped       : 1;
  guint has_key_focus               : 1;
  guint propagated_one_redraw       : 1;
  guint has_paint_volume            : 1;
  guint visible_paint_volume_valid  : 1;
  guint in_clone_paint              : 1;
  guint transform_valid             : 1;
  /* This is TRUE if anything has queued a redraw since we were last
     painted. In this case effect_to_redraw will point to an effect
     the redraw was queued from or it will be NULL if the redraw was
     queued without an effect. */
  guint is_dirty                    : 1;
  guint bg_color_set                : 1;
  guint content_box_valid           : 1;
  guint x_expand_set                : 1;
  guint y_expand_set                : 1;
  guint needs_compute_expand        : 1;
  guint needs_x_expand              : 1;
  guint needs_y_expand              : 1;
  guint needs_paint_volume_update   : 1;
  guint needs_visible_paint_volume_update : 1;
  guint had_effects_on_last_paint_volume_update : 1;
  guint needs_update_stage_views    : 1;
  guint clear_stage_views_needs_stage_views_changed : 1;
  guint needs_redraw : 1;
  guint needs_finish_layout : 1;
  guint stage_relative_modelview_valid : 1;
};

enum
{
  PROP_0,

  PROP_CONTEXT,

  PROP_NAME,

  /* X, Y, WIDTH, HEIGHT are "do what I mean" properties;
   * when set they force a size request, when gotten they
   * get the allocation if the allocation is valid, and the
   * request otherwise
   */
  PROP_X,
  PROP_Y,
  PROP_WIDTH,
  PROP_HEIGHT,

  PROP_POSITION,
  PROP_SIZE,

  /* Then the rest of these size-related properties are the "actual"
   * underlying properties set or gotten by X, Y, WIDTH, HEIGHT
   */
  PROP_FIXED_X,
  PROP_FIXED_Y,

  PROP_FIXED_POSITION_SET,

  PROP_MIN_WIDTH,
  PROP_MIN_WIDTH_SET,

  PROP_MIN_HEIGHT,
  PROP_MIN_HEIGHT_SET,

  PROP_NATURAL_WIDTH,
  PROP_NATURAL_WIDTH_SET,

  PROP_NATURAL_HEIGHT,
  PROP_NATURAL_HEIGHT_SET,

  PROP_REQUEST_MODE,

  /* Allocation properties are read-only */
  PROP_ALLOCATION,

  PROP_Z_POSITION,

  PROP_CLIP_RECT,
  PROP_HAS_CLIP,
  PROP_CLIP_TO_ALLOCATION,

  PROP_OPACITY,

  PROP_OFFSCREEN_REDIRECT,

  PROP_VISIBLE,
  PROP_MAPPED,
  PROP_REALIZED,
  PROP_REACTIVE,

  PROP_PIVOT_POINT,
  PROP_PIVOT_POINT_Z,

  PROP_SCALE_X,
  PROP_SCALE_Y,
  PROP_SCALE_Z,

  PROP_ROTATION_ANGLE_X, /* XXX:2.0 rename to rotation-x */
  PROP_ROTATION_ANGLE_Y, /* XXX:2.0 rename to rotation-y */
  PROP_ROTATION_ANGLE_Z, /* XXX:2.0 rename to rotation-z */

  PROP_TRANSLATION_X,
  PROP_TRANSLATION_Y,
  PROP_TRANSLATION_Z,

  PROP_TRANSFORM,
  PROP_TRANSFORM_SET,
  PROP_CHILD_TRANSFORM,
  PROP_CHILD_TRANSFORM_SET,

  PROP_SHOW_ON_SET_PARENT, /*XXX:2.0 remove */

  PROP_TEXT_DIRECTION,
  PROP_HAS_POINTER,

  PROP_ACTIONS,
  PROP_CONSTRAINTS,
  PROP_EFFECT,

  PROP_LAYOUT_MANAGER,

  PROP_X_EXPAND,
  PROP_Y_EXPAND,
  PROP_X_ALIGN,
  PROP_Y_ALIGN,
  PROP_MARGIN_TOP,
  PROP_MARGIN_BOTTOM,
  PROP_MARGIN_LEFT,
  PROP_MARGIN_RIGHT,

  PROP_BACKGROUND_COLOR,
  PROP_BACKGROUND_COLOR_SET,

  PROP_FIRST_CHILD,
  PROP_LAST_CHILD,

  PROP_CONTENT,
  PROP_CONTENT_GRAVITY,
  PROP_CONTENT_BOX,
  PROP_MINIFICATION_FILTER,
  PROP_MAGNIFICATION_FILTER,
  PROP_CONTENT_REPEAT,

  PROP_COLOR_STATE,

  /* Accessible */
  PROP_ACCESSIBLE_ROLE,
  PROP_ACCESSIBLE_NAME,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  SHOW,
  HIDE,
  DESTROY,
  PARENT_SET,
  KEY_FOCUS_IN,
  KEY_FOCUS_OUT,
  PAINT,
  PICK,
  REALIZE,
  UNREALIZE,
  QUEUE_RELAYOUT,
  EVENT,
  CAPTURED_EVENT,
  BUTTON_PRESS_EVENT,
  BUTTON_RELEASE_EVENT,
  SCROLL_EVENT,
  KEY_PRESS_EVENT,
  KEY_RELEASE_EVENT,
  MOTION_EVENT,
  ENTER_EVENT,
  LEAVE_EVENT,
  TRANSITIONS_COMPLETED,
  TOUCH_EVENT,
  TRANSITION_STOPPED,
  STAGE_VIEWS_CHANGED,
  RESOURCE_SCALE_CHANGED,
  CHILD_ADDED,
  CHILD_REMOVED,
  CLONED,
  DECLONED,

  LAST_SIGNAL
};

static guint actor_signals[LAST_SIGNAL] = { 0, };

typedef struct _TransitionClosure
{
  ClutterActor *actor;
  ClutterTransition *transition;
  gchar *name;
  gulong completed_id;
} TransitionClosure;

static void clutter_animatable_iface_init (ClutterAnimatableInterface *iface);
static void atk_implementor_iface_init    (AtkImplementorIface    *iface);

/* These setters are all static for now, maybe they should be in the
 * public API, but they are perhaps obscure enough to leave only as
 * properties
 */
static void clutter_actor_set_min_width          (ClutterActor *self,
                                                  gfloat        min_width);
static void clutter_actor_set_min_height         (ClutterActor *self,
                                                  gfloat        min_height);
static void clutter_actor_set_natural_width      (ClutterActor *self,
                                                  gfloat        natural_width);
static void clutter_actor_set_natural_height     (ClutterActor *self,
                                                  gfloat        natural_height);
static void clutter_actor_set_min_width_set      (ClutterActor *self,
                                                  gboolean      use_min_width);
static void clutter_actor_set_min_height_set     (ClutterActor *self,
                                                  gboolean      use_min_height);
static void clutter_actor_set_natural_width_set  (ClutterActor *self,
                                                  gboolean  use_natural_width);
static void clutter_actor_set_natural_height_set (ClutterActor *self,
                                                  gboolean  use_natural_height);
static void clutter_actor_update_map_state       (ClutterActor  *self,
                                                  MapStateChange change);
static void clutter_actor_unrealize_not_hiding   (ClutterActor *self);

static ClutterPaintVolume *_clutter_actor_get_paint_volume_mutable (ClutterActor *self);

static guint8   clutter_actor_get_paint_opacity_internal        (ClutterActor *self);

static inline void clutter_actor_set_background_color_internal (ClutterActor    *self,
                                                                const CoglColor *color);

static void on_layout_manager_changed (ClutterLayoutManager *manager,
                                       ClutterActor         *self);

static inline void clutter_actor_queue_compute_expand (ClutterActor *self);

static inline void clutter_actor_set_margin_internal (ClutterActor *self,
                                                      gfloat        margin,
                                                      GParamSpec   *pspec);

static void clutter_actor_set_transform_internal (ClutterActor            *self,
                                                  const graphene_matrix_t *transform);
static void clutter_actor_set_child_transform_internal (ClutterActor            *self,
                                                        const graphene_matrix_t *transform);

static void     clutter_actor_realize_internal          (ClutterActor *self);
static void     clutter_actor_unrealize_internal        (ClutterActor *self);

static void clutter_actor_push_in_cloned_branch (ClutterActor *self,
                                                 gulong        count);
static void clutter_actor_pop_in_cloned_branch (ClutterActor *self,
                                                gulong        count);
static void ensure_valid_actor_transform (ClutterActor *actor);

static void push_in_paint_unmapped_branch (ClutterActor *self,
                                           guint         count);
static void pop_in_paint_unmapped_branch (ClutterActor *self,
                                          guint         count);

static void clutter_actor_update_devices (ClutterActor *self);

static void clutter_actor_set_color_state_internal (ClutterActor      *self,
                                                    ClutterColorState *color_state);

static GQuark quark_actor_layout_info = 0;
static GQuark quark_actor_transform_info = 0;
static GQuark quark_actor_animation_info = 0;

static GQuark quark_key = 0;
static GQuark quark_motion = 0;
static GQuark quark_pointer_focus = 0;
static GQuark quark_button = 0;
static GQuark quark_scroll = 0;
static GQuark quark_stage = 0;
static GQuark quark_touch = 0;
static GQuark quark_touchpad = 0;
static GQuark quark_proximity = 0;
static GQuark quark_pad = 0;
static GQuark quark_im = 0;

G_DEFINE_TYPE_WITH_CODE (ClutterActor,
                         clutter_actor,
                         G_TYPE_INITIALLY_UNOWNED,
                         G_ADD_PRIVATE (ClutterActor)
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_ANIMATABLE,
                                                clutter_animatable_iface_init)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_IMPLEMENTOR,
                                                atk_implementor_iface_init));

/*< private >
 * clutter_actor_get_debug_name:
 * @actor: a #ClutterActor
 *
 * Retrieves a printable name of @actor for debugging messages
 *
 * Return value: a string with a printable name
 */
const char *
_clutter_actor_get_debug_name (ClutterActor *actor)
{
  ClutterActorPrivate *priv;
  const char *retval;

  if (!actor)
    return "<unnamed>[<ClutterActor>NULL]";

  priv = actor->priv;

  if (G_UNLIKELY (priv->debug_name == NULL))
    {
      priv->debug_name = g_strdup_printf ("%s [%s]",
                                          priv->name != NULL ? priv->name
                                                             : "unnamed",
                                          G_OBJECT_TYPE_NAME (actor));
    }

  retval = priv->debug_name;

  return retval;
}

#ifdef CLUTTER_ENABLE_DEBUG
/* XXX - this is for debugging only, remove once working (or leave
 * in only in some debug mode). Should leave it for a little while
 * until we're confident in the new map/realize/visible handling.
 */
static inline void
clutter_actor_verify_map_state (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  if (clutter_actor_is_realized (self))
    {
      if (priv->parent == NULL)
        {
          if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
            {
              g_warning ("Realized non-toplevel actor '%s' should "
                         "have a parent",
                         _clutter_actor_get_debug_name (self));
            }
        }
      else if (!clutter_actor_is_realized (priv->parent))
        {
          g_warning ("Realized actor %s has an unrealized parent %s",
                     _clutter_actor_get_debug_name (self),
                     _clutter_actor_get_debug_name (priv->parent));
        }
    }

  if (clutter_actor_is_mapped (self))
    {
      if (!clutter_actor_is_realized (self))
        g_warning ("Actor '%s' is mapped but not realized",
                   _clutter_actor_get_debug_name (self));

      if (priv->parent == NULL)
        {
          if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
            {
              if (!clutter_actor_is_visible (self) &&
                  !CLUTTER_ACTOR_IN_DESTRUCTION (self))
                {
                  g_warning ("Toplevel actor '%s' is mapped "
                             "but not visible",
                             _clutter_actor_get_debug_name (self));
                }
            }
          else
            {
              g_warning ("Mapped actor '%s' should have a parent",
                         _clutter_actor_get_debug_name (self));
            }
        }
      else
        {
          ClutterActor *iter = self;

          /* check for the enable_paint_unmapped flag on the actor
           * and parents; if the flag is enabled at any point of this
           * branch of the scene graph then all the later checks
           * become pointless
           */
          while (iter != NULL)
            {
              if (iter->priv->enable_paint_unmapped)
                return;

              iter = iter->priv->parent;
            }

          if (!clutter_actor_is_visible (priv->parent))
            {
              g_warning ("Actor '%s' should not be mapped if parent '%s'"
                         "is not visible",
                         _clutter_actor_get_debug_name (self),
                         _clutter_actor_get_debug_name (priv->parent));
            }

          if (!clutter_actor_is_realized (priv->parent))
            {
              g_warning ("Actor '%s' should not be mapped if parent '%s'"
                         "is not realized",
                         _clutter_actor_get_debug_name (self),
                         _clutter_actor_get_debug_name (priv->parent));
            }

          if (!CLUTTER_ACTOR_IS_TOPLEVEL (priv->parent))
            {
              if (!clutter_actor_is_mapped (priv->parent))
                g_warning ("Actor '%s' is mapped but its non-toplevel "
                           "parent '%s' is not mapped",
                           _clutter_actor_get_debug_name (self),
                           _clutter_actor_get_debug_name (priv->parent));
            }
        }
    }
}

#endif /* CLUTTER_ENABLE_DEBUG */

/**
 * clutter_actor_pick_box:
 * @self: The #ClutterActor being "pick" painted.
 * @pick_context: The #ClutterPickContext
 * @box: A rectangle in the actor's own local coordinates.
 *
 * Logs (does a virtual paint of) a rectangle for picking. Note that @box is
 * in the actor's own local coordinates, so is usually {0,0,width,height}
 * to include the whole actor. That is unless the actor has a shaped input
 * region in which case you may wish to log the (multiple) smaller rectangles
 * that make up the input region.
 */
void
clutter_actor_pick_box (ClutterActor          *self,
                        ClutterPickContext    *pick_context,
                        const ClutterActorBox *box)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (box != NULL);

  if (box->x1 >= box->x2 || box->y1 >= box->y2)
    return;

  clutter_pick_context_log_pick (pick_context, box, self);
}

static void
clutter_actor_set_mapped (ClutterActor *self,
                          gboolean      mapped)
{
  if (clutter_actor_is_mapped (self) == mapped)
    return;

  g_return_if_fail (!CLUTTER_ACTOR_IN_MAP_UNMAP (self));

  CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_IN_MAP_UNMAP);

  if (mapped)
    {
      CLUTTER_ACTOR_GET_CLASS (self)->map (self);
      g_assert (clutter_actor_is_mapped (self));
    }
  else
    {
      CLUTTER_ACTOR_GET_CLASS (self)->unmap (self);
      g_assert (!clutter_actor_is_mapped (self));
    }

  CLUTTER_UNSET_PRIVATE_FLAGS (self, CLUTTER_IN_MAP_UNMAP);
}

/* this function updates the mapped and realized states according to
 * invariants, in the appropriate order.
 */
static void
clutter_actor_update_map_state (ClutterActor  *self,
                                MapStateChange change)
{
  gboolean was_mapped;

  was_mapped = clutter_actor_is_mapped (self);

  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      /* the mapped flag on top-level actors must be set by the
       * per-backend implementation because it might be asynchronous.
       *
       * That is, the MAPPED flag on toplevels currently tracks the X
       * server mapped-ness of the window, while the expected behavior
       * (if used to GTK) may be to track WM_STATE!=WithdrawnState.
       * This creates some weird complexity by breaking the invariant
       * that if we're visible and all ancestors shown then we are
       * also mapped - instead, we are mapped if all ancestors
       * _possibly excepting_ the stage are mapped. The stage
       * will map/unmap for example when it is minimized or
       * moved to another workspace.
       *
       * So, the only invariant on the stage is that if visible it
       * should be realized, and that it has to be visible to be
       * mapped.
       */
      if (clutter_actor_is_visible (self))
        clutter_actor_realize (self);

      switch (change)
        {
        case MAP_STATE_CHECK:
          break;

        case MAP_STATE_MAKE_MAPPED:
          g_assert (!was_mapped);
          clutter_actor_set_mapped (self, TRUE);
          break;

        case MAP_STATE_MAKE_UNMAPPED:
          g_assert (was_mapped);
          clutter_actor_set_mapped (self, FALSE);
          break;

        case MAP_STATE_MAKE_UNREALIZED:
          /* we only use MAKE_UNREALIZED in unparent,
           * and unparenting a stage isn't possible.
           * If someone wants to just unrealize a stage
           * then clutter_actor_unrealize() doesn't
           * go through this codepath.
           */
          g_warning ("Trying to force unrealize stage is not allowed");
          break;
        }

      if (clutter_actor_is_mapped (self) &&
          !clutter_actor_is_visible (self) &&
          !CLUTTER_ACTOR_IN_DESTRUCTION (self))
        {
          g_warning ("Clutter toplevel of type '%s' is not visible, but "
                     "it is somehow still mapped",
                     _clutter_actor_get_debug_name (self));
        }
    }
  else
    {
      ClutterActorPrivate *priv = self->priv;
      ClutterActor *parent = priv->parent;
      gboolean should_be_mapped;
      gboolean may_be_realized;
      gboolean must_be_realized;

      should_be_mapped = FALSE;
      may_be_realized = TRUE;
      must_be_realized = FALSE;

      if (parent == NULL || change == MAP_STATE_MAKE_UNREALIZED)
        {
          may_be_realized = FALSE;
        }
      else
        {
          /* Maintain invariant that if parent is mapped, and we are
           * visible, then we are mapped ...  unless parent is a
           * stage, in which case we map regardless of parent's map
           * state but do require stage to be visible and realized.
           *
           * If parent is realized, that does not force us to be
           * realized; but if parent is unrealized, that does force
           * us to be unrealized.
           *
           * The reason we don't force children to realize with
           * parents is _clutter_actor_rerealize(); if we require that
           * a realized parent means children are realized, then to
           * unrealize an actor we would have to unrealize its
           * parents, which would end up meaning unrealizing and
           * hiding the entire stage. So we allow unrealizing a
           * child (as long as that child is not mapped) while that
           * child still has a realized parent.
           *
           * Also, if we unrealize from leaf nodes to root, and
           * realize from root to leaf, the invariants are never
           * violated if we allow children to be unrealized
           * while parents are realized.
           *
           * When unmapping, MAP_STATE_MAKE_UNMAPPED is specified
           * to force us to unmap, even though parent is still
           * mapped. This is because we're unmapping from leaf nodes
           * up to root nodes.
           */
          if (clutter_actor_is_visible (self) &&
              change != MAP_STATE_MAKE_UNMAPPED)
            {
              gboolean parent_is_visible_realized_toplevel;

              parent_is_visible_realized_toplevel =
                (CLUTTER_ACTOR_IS_TOPLEVEL (parent) &&
                 clutter_actor_is_visible (parent) &&
                 clutter_actor_is_realized (parent));

              if (clutter_actor_is_mapped (parent) ||
                  parent_is_visible_realized_toplevel)
                {
                  must_be_realized = TRUE;
                  should_be_mapped = TRUE;
                }
            }

          /* if the actor has been set to be painted even if unmapped
           * then we should map it and check for realization as well;
           * this is an override for the branch of the scene graph
           * which begins with this node
           */
          if (priv->enable_paint_unmapped)
            {
              should_be_mapped = TRUE;
              must_be_realized = TRUE;
            }

          if (!clutter_actor_is_realized (parent))
            may_be_realized = FALSE;
        }

      if (change == MAP_STATE_MAKE_MAPPED && !should_be_mapped)
        {
          if (parent == NULL)
            g_warning ("Attempting to map a child that does not "
                       "meet the necessary invariants: the actor '%s' "
                       "has no parent",
                       _clutter_actor_get_debug_name (self));
          else
            g_warning ("Attempting to map a child that does not "
                       "meet the necessary invariants: the actor '%s' "
                       "is parented to an unmapped actor '%s'",
                       _clutter_actor_get_debug_name (self),
                       _clutter_actor_get_debug_name (priv->parent));
        }

      /* We want to go in the order "realize, map" and "unmap, unrealize" */

      /* Unmap */
      if (!should_be_mapped)
        clutter_actor_set_mapped (self, FALSE);

      /* Realize */
      if (must_be_realized)
        clutter_actor_realize (self);

      /* if we must be realized then we may be, presumably */
      g_assert (!(must_be_realized && !may_be_realized));

      /* Unrealize */
      if (!may_be_realized)
        clutter_actor_unrealize_not_hiding (self);

      /* Map */
      if (should_be_mapped)
        {
          g_assert (should_be_mapped == must_be_realized);

          /* realization is allowed to fail (though I don't know what
           * an app is supposed to do about that - shouldn't it just
           * be a g_error? anyway, we have to avoid mapping if this
           * happens)
           */
          if (clutter_actor_is_realized (self))
            clutter_actor_set_mapped (self, TRUE);
        }
    }

#ifdef CLUTTER_ENABLE_DEBUG
  /* check all invariants were kept */
  clutter_actor_verify_map_state (self);
#endif
}

static void queue_update_paint_volume (ClutterActor *actor);

static void
queue_update_paint_volume_on_clones (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer key;

  if (priv->clones == NULL)
    return;

  g_hash_table_iter_init (&iter, priv->clones);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    queue_update_paint_volume (key);
}

void
queue_update_paint_volume (ClutterActor *actor)
{
  queue_update_paint_volume_on_clones (actor);

  while (actor)
    {
      actor->priv->needs_paint_volume_update = TRUE;
      actor->priv->needs_visible_paint_volume_update = TRUE;
      actor->priv->needs_finish_layout = TRUE;
      actor = actor->priv->parent;
    }
}

static void
clutter_actor_real_map (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *iter;

  g_assert (!clutter_actor_is_mapped (self));

  CLUTTER_NOTE (ACTOR, "Mapping actor '%s'",
                _clutter_actor_get_debug_name (self));

  self->flags |= CLUTTER_ACTOR_MAPPED;

  if (priv->unmapped_paint_branch_counter == 0)
    {
      /* Invariant that needs_finish_layout is set all the way up to the stage
       * needs to be met.
       */
      if (priv->needs_finish_layout)
        {
          iter = priv->parent;
          while (iter && !iter->priv->needs_finish_layout)
            {
              iter->priv->needs_finish_layout = TRUE;
              iter = iter->priv->parent;
            }
        }

      /* Avoid the early return in clutter_actor_queue_relayout() */
      priv->needs_width_request = FALSE;
      priv->needs_height_request = FALSE;
      priv->needs_allocation = FALSE;

      clutter_actor_queue_relayout (self);
    }

  /* notify on parent mapped before potentially mapping
   * children, so apps see a top-down notification.
   */
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MAPPED]);

  if (!clutter_actor_is_painting_unmapped (self))
    clutter_actor_add_accessible_state (self, ATK_STATE_SHOWING);

  for (iter = priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    {
      clutter_actor_map (iter);
    }
}

/**
 * clutter_actor_map:
 * @self: A #ClutterActor
 *
 * Sets the %CLUTTER_ACTOR_MAPPED flag on the actor and possibly maps
 * and realizes its children if they are visible. Does nothing if the
 * actor is not visible.
 *
 * Calling this function is strongly discouraged: the default
 * implementation of [vfunc@Clutter.Actor.map] will map all the children
 * of an actor when mapping its parent.
 *
 * When overriding map, it is mandatory to chain up to the parent
 * implementation.
 */
void
clutter_actor_map (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (clutter_actor_is_mapped (self))
    return;

  if (!clutter_actor_is_visible (self))
    return;

  clutter_actor_update_map_state (self, MAP_STATE_MAKE_MAPPED);
}

/**
 * clutter_actor_is_mapped:
 * @self: a #ClutterActor
 *
 * Checks whether a #ClutterActor has been set as mapped.
 *
 * See also [property@Clutter.Actor:mapped]
 *
 * Returns: %TRUE if the actor is mapped4
 */
gboolean
clutter_actor_is_mapped (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return (self->flags & CLUTTER_ACTOR_MAPPED) != FALSE;
}

static void
maybe_unset_key_focus (ClutterActor *self)
{
  ClutterActor *stage;

  stage = _clutter_actor_get_stage_internal (self);
  if (!stage)
    return;

  if (self != clutter_stage_get_key_focus (CLUTTER_STAGE (stage)))
    return;

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), NULL);
}

static void
clutter_actor_clear_grabs (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *stage;

  if (!priv->grabs && !priv->implicitly_grabbed_count)
    return;

  stage = _clutter_actor_get_stage_internal (self);
  g_assert (stage != NULL);

  if (priv->implicitly_grabbed_count > 0)
    clutter_stage_implicit_grab_actor_unmapped (CLUTTER_STAGE (stage), self);

  g_assert (priv->implicitly_grabbed_count == 0);

  /* Undo every grab that the actor may hold, priv->grabs
   * will be updated internally in clutter_stage_unlink_grab().
   */
  while (priv->grabs)
    clutter_stage_unlink_grab (CLUTTER_STAGE (stage), priv->grabs->data);
}

static void
clutter_actor_real_unmap (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *iter;

  g_assert (clutter_actor_is_mapped (self));

  CLUTTER_NOTE (ACTOR, "Unmapping actor '%s'",
                _clutter_actor_get_debug_name (self));

  for (iter = priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    {
      clutter_actor_unmap (iter);
    }

  self->flags &= ~CLUTTER_ACTOR_MAPPED;

  if (priv->unmapped_paint_branch_counter == 0)
    {
      if (priv->parent && !CLUTTER_ACTOR_IN_DESTRUCTION (priv->parent))
        {
          if (G_UNLIKELY (priv->parent->flags & CLUTTER_ACTOR_NO_LAYOUT))
            clutter_actor_queue_redraw (priv->parent);
          else
            clutter_actor_queue_relayout (priv->parent);
        }
    }

  /* notify on parent mapped after potentially unmapping
   * children, so apps see a bottom-up notification.
   */
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MAPPED]);

  if (!clutter_actor_is_painting_unmapped (self))
    clutter_actor_remove_accessible_state (self, ATK_STATE_SHOWING);

  if (priv->n_pointers > 0)
    {
      ClutterActor *stage = _clutter_actor_get_stage_internal (self);

      clutter_stage_invalidate_focus (CLUTTER_STAGE (stage), self);
    }

  /* relinquish keyboard focus if we were unmapped while owning it */
  if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
    maybe_unset_key_focus (self);

  clutter_actor_clear_grabs (self);
}

/**
 * clutter_actor_unmap:
 * @self: A #ClutterActor
 *
 * Unsets the %CLUTTER_ACTOR_MAPPED flag on the actor and possibly
 * unmaps its children if they were mapped.
 *
 * Calling this function is not encouraged: the default #ClutterActor
 * implementation of [vfunc@Clutter.Actor.unmap] will also unmap any
 * eventual children by default when their parent is unmapped.
 *
 * When overriding [vfunc@Clutter.Actor.unmap], it is mandatory to
 * chain up to the parent implementation.
 *
 * It is important to note that the implementation of the
 * [vfunc@Clutter.Actor.unmap] virtual function may be called after
 * the [vfunc@Clutter.Actor.destroy] or the [vfunc@GObject.Object.dispose]
 * implementation, but it is guaranteed to be called before the
 * [vfunc@GObject.Object.finalize] implementation.
 */
void
clutter_actor_unmap (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (!clutter_actor_is_mapped (self))
    return;

  clutter_actor_update_map_state (self, MAP_STATE_MAKE_UNMAPPED);
}

static void
clutter_actor_queue_shallow_relayout (ClutterActor *self)
{
  ClutterActor *stage = _clutter_actor_get_stage_internal (self);

  if (stage != NULL)
    clutter_stage_queue_actor_relayout (CLUTTER_STAGE (stage), self);
}

static void
clutter_actor_real_show (ClutterActor *self)
{
  if (clutter_actor_is_visible (self))
    return;

  self->flags |= CLUTTER_ACTOR_VISIBLE;

  /* we notify on the "visible" flag in the clutter_actor_show()
   * wrapper so the entire show signal emission completes first,
   * and the branch of the scene graph is in a stable state
   */
  clutter_actor_update_map_state (self, MAP_STATE_CHECK);

  if (clutter_actor_has_mapped_clones (self))
    {
      ClutterActorPrivate *priv = self->priv;

      /* Avoid the early return in clutter_actor_queue_relayout() */
      priv->needs_width_request = FALSE;
      priv->needs_height_request = FALSE;
      priv->needs_allocation = FALSE;

      clutter_actor_queue_relayout (self);
    }
}

static inline void
set_show_on_set_parent (ClutterActor *self,
                        gboolean      set_show)
{
  ClutterActorPrivate *priv = self->priv;

  set_show = !!set_show;

  if (priv->show_on_set_parent == set_show)
    return;

  if (priv->parent == NULL)
    {
      priv->show_on_set_parent = set_show;
      g_object_notify_by_pspec (G_OBJECT (self),
                                obj_props[PROP_SHOW_ON_SET_PARENT]);
    }
}

static void
clutter_actor_queue_redraw_on_parent (ClutterActor *self)
{
  g_autoptr (ClutterPaintVolume) pv = NULL;

  if (!self->priv->parent)
    return;

  /* A relayout/redraw is underway */
  if (self->priv->needs_allocation)
    return;

  pv = clutter_actor_get_transformed_paint_volume (self, self->priv->parent);
  _clutter_actor_queue_redraw_full (self->priv->parent, pv, NULL);
}

/**
 * clutter_actor_show:
 * @self: A #ClutterActor
 *
 * Flags an actor to be displayed. An actor that isn't shown will not
 * be rendered on the stage.
 *
 * Actors are visible by default.
 *
 * If this function is called on an actor without a parent, the
 * [property@Clutter.Actor:show-on-set-parent] will be set to %TRUE as a side
 * effect.
 */
void
clutter_actor_show (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* simple optimization */
  if (clutter_actor_is_visible (self))
    {
      /* we still need to set the :show-on-set-parent property, in
       * case show() is called on an unparented actor
       */
      set_show_on_set_parent (self, TRUE);
      return;
    }

#ifdef CLUTTER_ENABLE_DEBUG
  clutter_actor_verify_map_state (self);
#endif

  priv = self->priv;

  g_object_freeze_notify (G_OBJECT (self));

  set_show_on_set_parent (self, TRUE);

  /* if we're showing a child that needs to expand, or may
   * expand, then we need to recompute the expand flags for
   * its parent as well
   */
  if (priv->needs_compute_expand ||
      priv->needs_x_expand ||
      priv->needs_y_expand)
    {
      clutter_actor_queue_compute_expand (self);
    }

  g_signal_emit (self, actor_signals[SHOW], 0);
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_VISIBLE]);

  clutter_actor_add_accessible_state (self, ATK_STATE_VISIBLE);

  if (priv->parent != NULL)
    clutter_actor_queue_redraw (self);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_is_visible:
 * @self: a #ClutterActor
 *
 * Checks whether an actor is marked as visible.
 *
 * Returns: %TRUE if the actor visible4
 */
gboolean
clutter_actor_is_visible (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return (self->flags & CLUTTER_ACTOR_VISIBLE) != FALSE;
}

static void
clutter_actor_real_hide (ClutterActor *self)
{
  if (!clutter_actor_is_visible (self))
    return;

  self->flags &= ~CLUTTER_ACTOR_VISIBLE;

  /* we notify on the "visible" flag in the clutter_actor_hide()
   * wrapper so the entire hide signal emission completes first,
   * and the branch of the scene graph is in a stable state
   */
  clutter_actor_update_map_state (self, MAP_STATE_CHECK);
}

/**
 * clutter_actor_hide:
 * @self: A #ClutterActor
 *
 * Flags an actor to be hidden. A hidden actor will not be
 * rendered on the stage.
 *
 * Actors are visible by default.
 *
 * If this function is called on an actor without a parent, the
 * [property@Clutter.Actor:show-on-set-parent] property will be set to %FALSE
 * as a side-effect.
 */
void
clutter_actor_hide (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* simple optimization */
  if (!clutter_actor_is_visible (self))
    {
      /* we still need to set the :show-on-set-parent property, in
       * case hide() is called on an unparented actor
       */
      set_show_on_set_parent (self, FALSE);
      return;
    }

#ifdef CLUTTER_ENABLE_DEBUG
  clutter_actor_verify_map_state (self);
#endif

  priv = self->priv;

  g_object_freeze_notify (G_OBJECT (self));

  set_show_on_set_parent (self, FALSE);

  /* if we're hiding a child that needs to expand, or may
   * expand, then we need to recompute the expand flags for
   * its parent as well
   */
  if (priv->needs_compute_expand ||
      priv->needs_x_expand ||
      priv->needs_y_expand)
    {
      clutter_actor_queue_compute_expand (self);
    }

  g_signal_emit (self, actor_signals[HIDE], 0);
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_VISIBLE]);

  clutter_actor_remove_accessible_state (self, ATK_STATE_VISIBLE);

  if (priv->parent != NULL && priv->needs_allocation)
    clutter_actor_queue_redraw (priv->parent);
  else
    clutter_actor_queue_redraw_on_parent (self);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_realize:
 * @self: A #ClutterActor
 *
 * Realization informs the actor that it is attached to a stage. It
 * can use this to allocate resources if it wanted to delay allocation
 * until it would be rendered. However it is perfectly acceptable for
 * an actor to create resources before being realized because Clutter
 * only ever has a single rendering context so that actor is free to
 * be moved from one stage to another.
 *
 * This function does nothing if the actor is already realized.
 *
 * Because a realized actor must have realized parent actors, calling
 * clutter_actor_realize() will also realize all parents of the actor.
 *
 * This function does not realize child actors, except in the special
 * case that realizing the stage, when the stage is visible, will
 * suddenly map (and thus realize) the children of the stage.
 *
 * Deprecated: 1.16: Actors are automatically realized, and nothing
 *   requires explicit realization.
 */
void
clutter_actor_realize (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  clutter_actor_realize_internal (self);
}

/**
 * clutter_actor_is_realized:
 * @self: a #ClutterActor
 *
 * Checks whether a #ClutterActor is realized.
 *
 * Returns: %TRUE if the actor is realized4
 */
gboolean
clutter_actor_is_realized (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return (self->flags & CLUTTER_ACTOR_REALIZED) != FALSE;
}

static void
clutter_actor_realize_internal (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

#ifdef CLUTTER_ENABLE_DEBUG
  clutter_actor_verify_map_state (self);
#endif

  if (clutter_actor_is_realized (self))
    return;

  /* To be realized, our parent actors must be realized first.
   * This will only succeed if we're inside a toplevel.
   */
  if (priv->parent != NULL)
    clutter_actor_realize (priv->parent);

  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      /* toplevels can be realized at any time */
    }
  else
    {
      /* "Fail" the realization if parent is missing or unrealized;
       * this should really be a g_warning() not some kind of runtime
       * failure; how can an app possibly recover? Instead it's a bug
       * in the app and the app should get an explanatory warning so
       * someone can fix it. But for now it's too hard to fix this
       * because e.g. ClutterTexture needs reworking.
       */
      if (priv->parent == NULL ||
          !clutter_actor_is_realized (priv->parent))
        return;
    }

  CLUTTER_NOTE (ACTOR, "Realizing actor '%s'", _clutter_actor_get_debug_name (self));

  self->flags |= CLUTTER_ACTOR_REALIZED;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REALIZED]);

  g_signal_emit (self, actor_signals[REALIZE], 0);

  /* Stage actor is allowed to unset the realized flag again in its
   * default signal handler, though that is a pathological situation.
   */

  /* If realization "failed" we'll have to update child state. */
  clutter_actor_update_map_state (self, MAP_STATE_CHECK);
}

static void
clutter_actor_real_unrealize (ClutterActor *self)
{
  /* we must be unmapped (implying our children are also unmapped) */
  g_assert (!clutter_actor_is_mapped (self));
}

/**
 * clutter_actor_unrealize:
 * @self: A #ClutterActor
 *
 * Unrealization informs the actor that it may be being destroyed or
 * moved to another stage. The actor may want to destroy any
 * underlying graphics resources at this point. However it is
 * perfectly acceptable for it to retain the resources until the actor
 * is destroyed because Clutter only ever uses a single rendering
 * context and all of the graphics resources are valid on any stage.
 *
 * Because mapped actors must be realized, actors may not be
 * unrealized if they are mapped. This function hides the actor to be
 * sure it isn't mapped, an application-visible side effect that you
 * may not be expecting.
 *
 * This function should not be called by application code.
 *
 * This function should not really be in the public API, because
 * there isn't a good reason to call it. ClutterActor will already
 * unrealize things for you when it's important to do so.
 *
 * If you were using clutter_actor_unrealize() in a dispose
 * implementation, then don't, just chain up to ClutterActor's
 * dispose.
 *
 * If you were using clutter_actor_unrealize() to implement
 * unrealizing children of your container, then don't, ClutterActor
 * will already take care of that.
 *
 * Deprecated: 1.16: Actors are automatically unrealized, and nothing
 *   requires explicit realization.
 */
void
clutter_actor_unrealize (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (!clutter_actor_is_mapped (self));

  clutter_actor_unrealize_internal (self);
}

/* If you were using clutter_actor_unrealize() to re-realize to
 * create your resources in a different way, then use
 * _clutter_actor_rerealize() (inside Clutter) or just call your
 * code that recreates your resources directly (outside Clutter).
 */
static void
clutter_actor_unrealize_internal (ClutterActor *self)
{
#ifdef CLUTTER_ENABLE_DEBUG
  clutter_actor_verify_map_state (self);
#endif

  clutter_actor_hide (self);

  clutter_actor_unrealize_not_hiding (self);
}

static ClutterActorTraverseVisitFlags
unrealize_actor_before_children_cb (ClutterActor *self,
                                    int depth,
                                    void *user_data)
{
  ClutterActor *stage;

  /* If an actor is already unrealized we know its children have also
   * already been unrealized... */
  if (!clutter_actor_is_realized (self))
    return CLUTTER_ACTOR_TRAVERSE_VISIT_SKIP_CHILDREN;

  stage = _clutter_actor_get_stage_internal (self);
  if (stage != NULL)
    clutter_actor_clear_grabs (self);

  g_signal_emit (self, actor_signals[UNREALIZE], 0);

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

static ClutterActorTraverseVisitFlags
unrealize_actor_after_children_cb (ClutterActor *self,
                                   int           depth,
                                   void         *user_data)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *stage = user_data;

  /* We want to unset the realized flag only _after_
   * child actors are unrealized, to maintain invariants.
   */
  self->flags &= ~CLUTTER_ACTOR_REALIZED;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REALIZED]);

  if (stage != NULL &&
      priv->parent != NULL &&
      priv->parent->flags & CLUTTER_ACTOR_NO_LAYOUT)
    clutter_stage_dequeue_actor_relayout (CLUTTER_STAGE (stage), self);

  if (priv->unmapped_paint_branch_counter == 0)
    priv->allocation = (ClutterActorBox) CLUTTER_ACTOR_BOX_UNINITIALIZED;

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

/*
 * clutter_actor_unrealize_not_hiding:
 * @self: A #ClutterActor
 *
 * Unrealization informs the actor that it may be being destroyed or
 * moved to another stage. The actor may want to destroy any
 * underlying graphics resources at this point. However it is
 * perfectly acceptable for it to retain the resources until the actor
 * is destroyed because Clutter only ever uses a single rendering
 * context and all of the graphics resources are valid on any stage.
 *
 * Because mapped actors must be realized, actors may not be
 * unrealized if they are mapped. You must hide the actor or one of
 * its parents before attempting to unrealize.
 *
 * This function is separate from clutter_actor_unrealize() because it
 * does not automatically hide the actor.
 * Actors need not be hidden to be unrealized, they just need to
 * be unmapped. In fact we don't want to mess up the application's
 * setting of the "visible" flag, so hiding is very undesirable.
 *
 * clutter_actor_unrealize() does a clutter_actor_hide() just for
 * backward compatibility.
 */
static void
clutter_actor_unrealize_not_hiding (ClutterActor *self)
{
  ClutterActor *stage = _clutter_actor_get_stage_internal (self);

  _clutter_actor_traverse (self,
                           CLUTTER_ACTOR_TRAVERSE_DEPTH_FIRST,
                           unrealize_actor_before_children_cb,
                           unrealize_actor_after_children_cb,
                           stage);
}

static void
clutter_actor_real_pick (ClutterActor       *self,
                         ClutterPickContext *pick_context)
{
  ClutterActorPrivate *priv = self->priv;

  if (clutter_actor_should_pick (self, pick_context))
    {
      ClutterActorBox box = {
        .x1 = 0,
        .y1 = 0,
        .x2 = priv->allocation.x2 - priv->allocation.x1,
        .y2 = priv->allocation.y2 - priv->allocation.y1,
      };

      clutter_actor_pick_box (self, pick_context, &box);
    }

  /* XXX - this thoroughly sucks, but we need to maintain compatibility
   * with existing container classes that override the pick() virtual
   * and chain up to the default implementation - otherwise we'll end up
   * painting our children twice.
   *
   * this has to go away for 2.0; hopefully along the pick() itself.
   */
  if (CLUTTER_ACTOR_GET_CLASS (self)->pick == clutter_actor_real_pick)
    {
      ClutterActor *iter;

      for (iter = self->priv->first_child;
           iter != NULL;
           iter = iter->priv->next_sibling)
        clutter_actor_pick (iter, pick_context);
    }
}

/**
 * clutter_actor_should_pick:
 * @self: A #ClutterActor
 * @pick_context: a #ClutterPickContext
 *
 * Should be called inside the implementation of the
 * [vfunc@Clutter.Actor.pick] virtual function in order to check whether
 * the actor should be picked or not.
 *
 * This function should never be called directly by applications.
 *
 * Return value: %TRUE if the actor should be picked, %FALSE otherwise
 */
gboolean
clutter_actor_should_pick (ClutterActor       *self,
                           ClutterPickContext *pick_context)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (clutter_actor_is_mapped (self) &&
      clutter_actor_box_is_initialized (&self->priv->allocation) &&
      (clutter_pick_context_get_mode (pick_context) == CLUTTER_PICK_ALL ||
       clutter_actor_get_reactive (self)))
    return TRUE;

  return FALSE;
}

static void
clutter_actor_real_get_preferred_width (ClutterActor *self,
                                        gfloat        for_height,
                                        gfloat       *min_width_p,
                                        gfloat       *natural_width_p)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->layout_manager != NULL)
    {
      CLUTTER_NOTE (LAYOUT, "Querying the layout manager '%s'[%p] "
                    "for the preferred width",
                    G_OBJECT_TYPE_NAME (priv->layout_manager),
                    priv->layout_manager);

      clutter_layout_manager_get_preferred_width (priv->layout_manager,
                                                  self,
                                                  for_height,
                                                  min_width_p,
                                                  natural_width_p);

      return;
    }

  /* Default implementation is always 0x0, usually an actor
   * using this default is relying on someone to set the
   * request manually
   */
  CLUTTER_NOTE (LAYOUT, "Default preferred width: 0, 0");

  if (min_width_p)
    *min_width_p = 0;

  if (natural_width_p)
    *natural_width_p = 0;
}

static void
clutter_actor_real_get_preferred_height (ClutterActor *self,
                                         gfloat        for_width,
                                         gfloat       *min_height_p,
                                         gfloat       *natural_height_p)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->layout_manager != NULL)
    {
      CLUTTER_NOTE (LAYOUT, "Querying the layout manager '%s'[%p] "
                    "for the preferred height",
                    G_OBJECT_TYPE_NAME (priv->layout_manager),
                    priv->layout_manager);

      clutter_layout_manager_get_preferred_height (priv->layout_manager,
                                                   self,
                                                   for_width,
                                                   min_height_p,
                                                   natural_height_p);

      return;
    }
  /* Default implementation is always 0x0, usually an actor
   * using this default is relying on someone to set the
   * request manually
   */
  CLUTTER_NOTE (LAYOUT, "Default preferred height: 0, 0");

  if (min_height_p)
    *min_height_p = 0;

  if (natural_height_p)
    *natural_height_p = 0;
}

static void
clutter_actor_store_old_geometry (ClutterActor    *self,
                                  ClutterActorBox *box)
{
  *box = self->priv->allocation;
}

static inline void
clutter_actor_notify_if_geometry_changed (ClutterActor          *self,
                                          const ClutterActorBox *old)
{
  ClutterActorPrivate *priv = self->priv;
  GObject *obj = G_OBJECT (self);

  g_object_freeze_notify (obj);

  /* to avoid excessive requisition or allocation cycles we
   * use the cached values.
   *
   * - if we don't have an allocation we assume that we need
   *   to notify anyway
   * - if we don't have a width or a height request we notify
   *   width and height
   * - if we have a valid allocation then we check the old
   *   bounding box with the current allocation and we notify
   *   the changes
   */
  if (priv->needs_allocation)
    {
      g_object_notify_by_pspec (obj, obj_props[PROP_X]);
      g_object_notify_by_pspec (obj, obj_props[PROP_Y]);
      g_object_notify_by_pspec (obj, obj_props[PROP_POSITION]);
      g_object_notify_by_pspec (obj, obj_props[PROP_WIDTH]);
      g_object_notify_by_pspec (obj, obj_props[PROP_HEIGHT]);
      g_object_notify_by_pspec (obj, obj_props[PROP_SIZE]);
    }
  else if (priv->needs_width_request || priv->needs_height_request)
    {
      g_object_notify_by_pspec (obj, obj_props[PROP_WIDTH]);
      g_object_notify_by_pspec (obj, obj_props[PROP_HEIGHT]);
      g_object_notify_by_pspec (obj, obj_props[PROP_SIZE]);
    }
  else
    {
      gfloat x, y;
      gfloat width, height;

      x = priv->allocation.x1;
      y = priv->allocation.y1;
      width = priv->allocation.x2 - priv->allocation.x1;
      height = priv->allocation.y2 - priv->allocation.y1;

      if (x != old->x1)
        {
          g_object_notify_by_pspec (obj, obj_props[PROP_X]);
          g_object_notify_by_pspec (obj, obj_props[PROP_POSITION]);
        }

      if (y != old->y1)
        {
          g_object_notify_by_pspec (obj, obj_props[PROP_Y]);
          g_object_notify_by_pspec (obj, obj_props[PROP_POSITION]);
        }

      if (width != (old->x2 - old->x1))
        {
          g_object_notify_by_pspec (obj, obj_props[PROP_WIDTH]);
          g_object_notify_by_pspec (obj, obj_props[PROP_SIZE]);
        }

      if (height != (old->y2 - old->y1))
        {
          g_object_notify_by_pspec (obj, obj_props[PROP_HEIGHT]);
          g_object_notify_by_pspec (obj, obj_props[PROP_SIZE]);
        }
    }

  g_object_thaw_notify (obj);
}

static void
absolute_geometry_changed (ClutterActor *actor)
{
  actor->priv->needs_update_stage_views = TRUE;
  actor->priv->needs_visible_paint_volume_update = TRUE;
  actor->priv->stage_relative_modelview_valid = FALSE;

  actor->priv->needs_finish_layout = TRUE;
  /* needs_finish_layout is already TRUE on the whole parent tree thanks
   * to queue_update_paint_volume() that was called by transform_changed().
   */
}

static ClutterActorTraverseVisitFlags
absolute_geometry_changed_cb (ClutterActor *actor,
                              int           depth,
                              gpointer      user_data)
{
  absolute_geometry_changed (actor);

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

static void
transform_changed (ClutterActor *actor)
{
  actor->priv->transform_valid = FALSE;

  if (actor->priv->parent)
    queue_update_paint_volume (actor->priv->parent);

  _clutter_actor_traverse (actor,
                           CLUTTER_ACTOR_TRAVERSE_DEPTH_FIRST,
                           absolute_geometry_changed_cb,
                           NULL,
                           NULL);

  if (!clutter_actor_has_transitions (actor) &&
      !CLUTTER_ACTOR_IN_RELAYOUT (actor))
    clutter_actor_update_devices (actor);
}

/*< private >
 * clutter_actor_set_allocation_internal:
 * @self: a #ClutterActor
 * @box: a #ClutterActorBox
 * @flags: allocation flags
 *
 * Stores the allocation of @self.
 *
 * This function only performs basic storage and property notification.
 *
 * This function should be called by clutter_actor_set_allocation()
 * and by the default implementation of [vfunc@Clutter.Actor.destroyallocate
 *
 * Return value: %TRUE if the allocation of the #ClutterActor has been
 *   changed, and %FALSE otherwise
 */
static inline void
clutter_actor_set_allocation_internal (ClutterActor           *self,
                                       const ClutterActorBox  *box)
{
  ClutterActorPrivate *priv = self->priv;
  GObject *obj;
  gboolean origin_changed, size_changed;
  ClutterActorBox old_alloc = { 0, };

  g_return_if_fail (!isnan (box->x1) && !isnan (box->x2) &&
                    !isnan (box->y1) && !isnan (box->y2));

  obj = G_OBJECT (self);

  g_object_freeze_notify (obj);

  clutter_actor_store_old_geometry (self, &old_alloc);

  origin_changed =
    priv->allocation.x1 != box->x1 || priv->allocation.y1 != box->y1;
  size_changed =
    priv->allocation.x2 - priv->allocation.x1 != box->x2 - box->x1 ||
    priv->allocation.y2 - priv->allocation.y1 != box->y2 - box->y1;

  priv->allocation = *box;

  /* allocation is authoritative */
  priv->needs_width_request = FALSE;
  priv->needs_height_request = FALSE;
  priv->needs_allocation = FALSE;

  if (origin_changed || size_changed)
    {
      CLUTTER_NOTE (LAYOUT, "Allocation for '%s' changed",
                    _clutter_actor_get_debug_name (self));

      /* This will also call absolute_geometry_changed() on the subtree */
      transform_changed (self);

      if (size_changed)
        queue_update_paint_volume (self);

      g_object_notify_by_pspec (obj, obj_props[PROP_ALLOCATION]);

      /* if the allocation changes, so does the content box */
      if (priv->content != NULL)
        {
          priv->content_box_valid = FALSE;
          g_object_notify_by_pspec (obj, obj_props[PROP_CONTENT_BOX]);
        }
    }

  clutter_actor_notify_if_geometry_changed (self, &old_alloc);

  g_object_thaw_notify (obj);
}

static void
clutter_actor_real_allocate (ClutterActor           *self,
                             const ClutterActorBox  *box)
{
  ClutterActorPrivate *priv = self->priv;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_set_allocation_internal (self, box);

  /* we allocate our children before we notify changes in our geometry,
   * so that people connecting to properties will be able to get valid
   * data out of the sub-tree of the scene graph that has this actor at
   * the root.
   */
  if (priv->n_children != 0 &&
      priv->layout_manager != NULL)
    {
      ClutterActorBox children_box;

      /* normalize the box passed to the layout manager */
      children_box.x1 = children_box.y1 = 0.f;
      children_box.x2 = box->x2 - box->x1;
      children_box.y2 = box->y2 - box->y1;

      CLUTTER_NOTE (LAYOUT,
                    "Allocating %d children of %s "
                    "at { %.2f, %.2f - %.2f x %.2f } "
                    "using %s",
                    priv->n_children,
                    _clutter_actor_get_debug_name (self),
                    box->x1,
                    box->y1,
                    (box->x2 - box->x1),
                    (box->y2 - box->y1),
                    G_OBJECT_TYPE_NAME (priv->layout_manager));

      clutter_layout_manager_allocate (priv->layout_manager,
                                       self,
                                       &children_box);
    }

  g_object_thaw_notify (G_OBJECT (self));
}

static void
_clutter_actor_queue_redraw_on_clones (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer key;

  if (priv->clones == NULL)
    return;

  g_hash_table_iter_init (&iter, priv->clones);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    clutter_actor_queue_redraw (key);
}

static void
_clutter_actor_propagate_queue_redraw (ClutterActor *self)
{
  ClutterActor *origin = self;

  while (self)
    {
      /* no point in queuing a redraw on a destroyed actor */
      if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
        break;

      _clutter_actor_queue_redraw_on_clones (self);

      self->priv->is_dirty = TRUE;

      /* If the queue redraw is coming from a child then the actor has
         become dirty and any queued effect is no longer valid */
      if (self != origin)
        self->priv->effect_to_redraw = NULL;

      /* If the actor isn't visible, we still had to emit the signal
       * to allow for a ClutterClone, but the appearance of the parent
       * won't change so we don't have to propagate up the hierarchy.
       */
      if (!clutter_actor_is_visible (self))
        break;

      /* We guarantee that we will propagate a queue-redraw up the tree
       * at least once so that all clones can get notified.
       */
      if (self->priv->propagated_one_redraw)
        break;

      self->priv->propagated_one_redraw = TRUE;

      self = self->priv->parent;
    }
}

static void
clutter_actor_real_queue_relayout (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  /* no point in queueing a redraw on a destroyed actor */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return;

  priv->needs_width_request  = TRUE;
  priv->needs_height_request = TRUE;
  priv->needs_allocation     = TRUE;

  /* reset the cached size requests */
  memset (priv->width_requests, 0,
          N_CACHED_SIZE_REQUESTS * sizeof (SizeRequest));
  memset (priv->height_requests, 0,
          N_CACHED_SIZE_REQUESTS * sizeof (SizeRequest));

  /* We may need to go all the way up the hierarchy */
  if (priv->parent != NULL)
    {
      if (priv->parent->flags & CLUTTER_ACTOR_NO_LAYOUT)
        clutter_actor_queue_shallow_relayout (self);
      else
        _clutter_actor_queue_only_relayout (priv->parent);
    }
}

/**
 * clutter_actor_apply_relative_transform_to_point:
 * @self: A #ClutterActor
 * @ancestor: (nullable): A #ClutterActor ancestor, or %NULL to use the
 *   default #ClutterStage
 * @point: A point as #graphene_point3d_t
 * @vertex: (out caller-allocates): The translated #graphene_point3d_t
 *
 * Transforms @point in coordinates relative to the actor into
 * ancestor-relative coordinates using the relevant transform
 * stack (i.e. scale, rotation, etc).
 *
 * If @ancestor is %NULL the ancestor will be the #ClutterStage. In
 * this case, the coordinates returned will be the coordinates on
 * the stage before the projection is applied. This is different from
 * the behaviour of clutter_actor_apply_transform_to_point().
 */
void
clutter_actor_apply_relative_transform_to_point (ClutterActor             *self,
                                                 ClutterActor             *ancestor,
                                                 const graphene_point3d_t *point,
                                                 graphene_point3d_t       *vertex)
{
  gfloat w;
  graphene_matrix_t matrix;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (ancestor == NULL || CLUTTER_IS_ACTOR (ancestor));
  g_return_if_fail (point != NULL);
  g_return_if_fail (vertex != NULL);

  *vertex = *point;
  w = 1.0;

  if (ancestor == NULL)
    ancestor = _clutter_actor_get_stage_internal (self);

  if (ancestor == NULL)
    {
      *vertex = *point;
      return;
    }

  clutter_actor_get_relative_transformation_matrix (self, ancestor, &matrix);
  cogl_graphene_matrix_project_point (&matrix,
                                      &vertex->x,
                                      &vertex->y,
                                      &vertex->z,
                                      &w);
}

static gboolean
_clutter_actor_fully_transform_vertices (ClutterActor             *self,
                                         const graphene_point3d_t *vertices_in,
                                         graphene_point3d_t       *vertices_out,
                                         int                       n_vertices)
{
  ClutterActor *stage;
  graphene_matrix_t modelview;
  graphene_matrix_t projection;
  float viewport[4];

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  stage = _clutter_actor_get_stage_internal (self);

  /* We really can't do anything meaningful in this case so don't try
   * to do any transform */
  if (stage == NULL)
    return FALSE;

  /* Note: we pass NULL as the ancestor because we don't just want the modelview
   * that gets us to stage coordinates, we want to go all the way to eye
   * coordinates */
  clutter_actor_get_relative_transformation_matrix (self, NULL, &modelview);

  /* Fetch the projection and viewport */
  _clutter_stage_get_projection_matrix (CLUTTER_STAGE (stage), &projection);
  _clutter_stage_get_viewport (CLUTTER_STAGE (stage),
                               &viewport[0],
                               &viewport[1],
                               &viewport[2],
                               &viewport[3]);

  _clutter_util_fully_transform_vertices (&modelview,
                                          &projection,
                                          viewport,
                                          vertices_in,
                                          vertices_out,
                                          n_vertices);

  return TRUE;
}

/**
 * clutter_actor_apply_transform_to_point:
 * @self: A #ClutterActor
 * @point: A point as #graphene_point3d_t
 * @vertex: (out caller-allocates): The translated #graphene_point3d_t
 *
 * Transforms @point in coordinates relative to the actor
 * into screen-relative coordinates with the current actor
 * transformation (i.e. scale, rotation, etc)
 **/
void
clutter_actor_apply_transform_to_point (ClutterActor             *self,
                                        const graphene_point3d_t *point,
                                        graphene_point3d_t       *vertex)
{
  g_return_if_fail (point != NULL);
  g_return_if_fail (vertex != NULL);
  _clutter_actor_fully_transform_vertices (self, point, vertex, 1);
}

/**
 * clutter_actor_get_relative_transformation_matrix:
 * @self: The actor whose coordinate space you want to transform from.
 * @ancestor: (nullable): The ancestor actor whose coordinate space you want to transform to
 *            or %NULL if you want to transform all the way to eye coordinates.
 * @matrix: (out caller-allocates): A #graphene_matrix_t to store the transformation
 *
 * This gets a transformation @matrix that will transform coordinates from the
 * coordinate space of @self into the coordinate space of @ancestor.
 *
 * For example if you need a matrix that can transform the local actor
 * coordinates of @self into stage coordinates you would pass the actor's stage
 * pointer as the @ancestor.
 *
 * If you pass %NULL then the transformation will take you all the way through
 * to eye coordinates. This can be useful if you want to extract the entire
 * modelview transform that Clutter applies before applying the projection
 * transformation. If you want to explicitly set a modelview on a CoglFramebuffer
 * using cogl_set_modelview_matrix() for example then you would want a matrix
 * that transforms into eye coordinates.
 *
 * Note: This function explicitly initializes the given @matrix. If you just
 * want clutter to multiply a relative transformation with an existing matrix
 * you can use clutter_actor_apply_relative_transformation_matrix()
 * instead.
 *
 */
void
clutter_actor_get_relative_transformation_matrix (ClutterActor      *self,
                                                  ClutterActor      *ancestor,
                                                  graphene_matrix_t *matrix)
{
  graphene_matrix_init_identity (matrix);

  _clutter_actor_apply_relative_transformation_matrix (self, ancestor, matrix);
}

/* Project the given @box into stage window coordinates, writing the
 * transformed vertices to @verts[]. */
static gboolean
_clutter_actor_transform_and_project_box (ClutterActor          *self,
                                          const ClutterActorBox *box,
                                          graphene_point3d_t    *verts)
{
  graphene_point3d_t box_vertices[4];

  box_vertices[0].x = box->x1;
  box_vertices[0].y = box->y1;
  box_vertices[0].z = 0;
  box_vertices[1].x = box->x2;
  box_vertices[1].y = box->y1;
  box_vertices[1].z = 0;
  box_vertices[2].x = box->x1;
  box_vertices[2].y = box->y2;
  box_vertices[2].z = 0;
  box_vertices[3].x = box->x2;
  box_vertices[3].y = box->y2;
  box_vertices[3].z = 0;

  return
    _clutter_actor_fully_transform_vertices (self, box_vertices, verts, 4);
}

/**
 * clutter_actor_get_abs_allocation_vertices:
 * @self: A #ClutterActor
 * @verts: (out) (array fixed-size=4): Pointer to a location of an array
 *   of 4 #graphene_point3d_t where to store the result.
 *
 * Calculates the transformed screen coordinates of the four corners of
 * the actor; the returned vertices relate to the #ClutterActorBox
 * coordinates  as follows:
 *
 *  - v[0] contains (x1, y1)
 *  - v[1] contains (x2, y1)
 *  - v[2] contains (x1, y2)
 *  - v[3] contains (x2, y2)
 */
void
clutter_actor_get_abs_allocation_vertices (ClutterActor       *self,
                                           graphene_point3d_t *verts)
{
  ClutterActorPrivate *priv;
  ClutterActorBox actor_space_allocation;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  /* if the actor needs to be allocated we force a relayout, so that
   * the actor allocation box will be valid for
   * _clutter_actor_transform_and_project_box()
   */
  if (priv->needs_allocation)
    {
      ClutterActor *stage = _clutter_actor_get_stage_internal (self);
      /* There's nothing meaningful we can do now */
      if (!stage)
        return;

      clutter_stage_maybe_relayout (stage);
    }

  /* NB: _clutter_actor_transform_and_project_box expects a box in the actor's
   * own coordinate space... */
  actor_space_allocation.x1 = 0;
  actor_space_allocation.y1 = 0;
  actor_space_allocation.x2 = priv->allocation.x2 - priv->allocation.x1;
  actor_space_allocation.y2 = priv->allocation.y2 - priv->allocation.y1;
  _clutter_actor_transform_and_project_box (self,
					    &actor_space_allocation,
					    verts);
}

static void
clutter_actor_real_apply_transform (ClutterActor      *self,
                                    graphene_matrix_t *matrix)
{
  ClutterActorPrivate *priv = self->priv;
  const ClutterTransformInfo *info;
  graphene_point3d_t p;
  float pivot_x = 0.f, pivot_y = 0.f;

  info = _clutter_actor_get_transform_info_or_defaults (self);

  /* compute the pivot point given the allocated size */
  pivot_x = (priv->allocation.x2 - priv->allocation.x1)
          * info->pivot.x;
  pivot_y = (priv->allocation.y2 - priv->allocation.y1)
          * info->pivot.y;

  CLUTTER_NOTE (PAINT,
                "Allocation: (%.2f, %2.f), "
                "pivot: (%.2f, %.2f), "
                "translation: (%.2f, %.2f) -> "
                "new origin: (%.2f, %.2f)",
                priv->allocation.x1, priv->allocation.y1,
                info->pivot.x, info->pivot.y,
                info->translation.x, info->translation.y,
                priv->allocation.x1 + pivot_x + info->translation.x,
                priv->allocation.y1 + pivot_y + info->translation.y);

  /* roll back the pivot translation */
  if (pivot_x != 0.f || pivot_y != 0.f || info->pivot_z != 0.f)
    {
      graphene_point3d_init (&p, -pivot_x, -pivot_y, -info->pivot_z);
      graphene_matrix_translate (matrix, &p);
    }

  /* if we have an overriding transformation, we use that, and get out */
  if (info->transform_set)
    {
      graphene_matrix_multiply (matrix, &info->transform, matrix);

      /* we still need to apply the :allocation's origin and :pivot-point
       * translations, since :transform is relative to the actor's coordinate
       * space, and to the pivot point
       */
      graphene_point3d_init (&p,
                             priv->allocation.x1 + pivot_x,
                             priv->allocation.y1 + pivot_y,
                             info->pivot_z);
      graphene_matrix_translate (matrix, &p);
      goto roll_back;
    }

  if (info->rx_angle)
    {
      graphene_matrix_rotate (matrix,
                              (float) info->rx_angle,
                              graphene_vec3_x_axis ());
    }

  if (info->ry_angle)
    {
      graphene_matrix_rotate (matrix,
                              (float) info->ry_angle,
                              graphene_vec3_y_axis ());
    }

  if (info->rz_angle)
    {
      graphene_matrix_rotate (matrix,
                              (float) info->rz_angle,
                              graphene_vec3_z_axis ());
    }

  if (info->scale_x != 1.0 || info->scale_y != 1.0 || info->scale_z != 1.0)
    {
      graphene_matrix_scale (matrix,
                             (float) info->scale_x,
                             (float) info->scale_y,
                             (float) info->scale_z);
    }

  /* basic translation: :allocation's origin and :z-position; instead
   * of decomposing the pivot and translation info separate operations,
   * we just compose everything into a single translation
   */
  graphene_point3d_init (&p,
                         priv->allocation.x1 + pivot_x + info->translation.x,
                         priv->allocation.y1 + pivot_y + info->translation.y,
                         info->z_position + info->pivot_z + info->translation.z);
  graphene_matrix_translate (matrix, &p);

roll_back:
  /* we apply the :child-transform from the parent actor, if we have one */
  if (priv->parent != NULL)
    {
      const ClutterTransformInfo *parent_info;

      parent_info = _clutter_actor_get_transform_info_or_defaults (priv->parent);
      graphene_matrix_multiply (matrix, &parent_info->child_transform, matrix);
    }
}

/* Applies the transforms associated with this actor to the given
 * matrix. */

static void
ensure_valid_actor_transform (ClutterActor *actor)
{
  ClutterActorPrivate *priv = actor->priv;

  if (priv->transform_valid)
    return;

  graphene_matrix_init_identity (&priv->transform);

  CLUTTER_ACTOR_GET_CLASS (actor)->apply_transform (actor, &priv->transform);

  priv->transform_valid = TRUE;
}

void
_clutter_actor_apply_modelview_transform (ClutterActor      *self,
                                          graphene_matrix_t *matrix)
{
  ClutterActorPrivate *priv = self->priv;

  ensure_valid_actor_transform (self);
  graphene_matrix_multiply (&priv->transform, matrix, matrix);
}

/*
 * clutter_actor_apply_relative_transformation_matrix:
 * @self: The actor whose coordinate space you want to transform from.
 * @ancestor: The ancestor actor whose coordinate space you want to transform too
 *            or %NULL if you want to transform all the way to eye coordinates.
 * @matrix: A #graphene_matrix_t to apply the transformation too.
 *
 * This multiplies a transform with @matrix that will transform coordinates
 * from the coordinate space of @self into the coordinate space of @ancestor.
 *
 * For example if you need a matrix that can transform the local actor
 * coordinates of @self into stage coordinates you would pass the actor's stage
 * pointer as the @ancestor.
 *
 * If you pass %NULL then the transformation will take you all the way through
 * to eye coordinates. This can be useful if you want to extract the entire
 * modelview transform that Clutter applies before applying the projection
 * transformation. If you want to explicitly set a modelview on a CoglFramebuffer
 * using cogl_set_modelview_matrix() for example then you would want a matrix
 * that transforms into eye coordinates.
 *
 * This function doesn't initialize the given @matrix, it simply
 * multiplies the requested transformation matrix with the existing contents of
 * @matrix. You can use graphene_matrix_init_identity() to initialize the @matrix
 * before calling this function, or you can use
 * clutter_actor_get_relative_transformation_matrix() instead.
 */
void
_clutter_actor_apply_relative_transformation_matrix (ClutterActor      *self,
                                                     ClutterActor      *ancestor,
                                                     graphene_matrix_t *matrix)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *stage = _clutter_actor_get_stage_internal (self);
  graphene_matrix_t ancestor_modelview;
  graphene_matrix_t inverse_ancestor_modelview;

  /* Note we terminate before ever calling stage->apply_transform()
   * since that would conceptually be relative to the underlying
   * window OpenGL coordinates so we'd need a special @ancestor
   * value to represent the fake parent of the stage. */
  if (self == ancestor)
    return;

  if (!priv->stage_relative_modelview_valid)
    {
      graphene_matrix_init_identity (&priv->stage_relative_modelview);

      if (priv->parent != NULL)
        {
          _clutter_actor_apply_relative_transformation_matrix (priv->parent,
                                                               stage,
                                                               &priv->stage_relative_modelview);
        }

      _clutter_actor_apply_modelview_transform (self,
                                                &priv->stage_relative_modelview);

      priv->stage_relative_modelview_valid = TRUE;
    }

  if (ancestor == NULL)
    {
      _clutter_actor_apply_modelview_transform (stage, matrix);
      graphene_matrix_multiply (&priv->stage_relative_modelview, matrix, matrix);
      return;
    }

  if (ancestor == stage)
    {
      graphene_matrix_multiply (&priv->stage_relative_modelview, matrix, matrix);
      return;
    }

  if (ancestor == priv->parent)
    {
      _clutter_actor_apply_modelview_transform (self, matrix);
      return;
    }

  graphene_matrix_init_identity (&ancestor_modelview);
  _clutter_actor_apply_relative_transformation_matrix (ancestor,
                                                       stage,
                                                       &ancestor_modelview);

  if (graphene_matrix_near (&priv->stage_relative_modelview,
                            &ancestor_modelview,
                            FLT_EPSILON))
    return;

  if (graphene_matrix_is_identity (&ancestor_modelview))
    {
      graphene_matrix_multiply (&priv->stage_relative_modelview, matrix, matrix);
      return;
    }

  if (graphene_matrix_inverse (&ancestor_modelview,
                               &inverse_ancestor_modelview))
    {
      graphene_matrix_multiply (&inverse_ancestor_modelview, matrix, matrix);
      graphene_matrix_multiply (&priv->stage_relative_modelview, matrix, matrix);
      return;
    }

  if (priv->parent != NULL)
    _clutter_actor_apply_relative_transformation_matrix (priv->parent,
                                                         ancestor,
                                                         matrix);

  _clutter_actor_apply_modelview_transform (self, matrix);
}

static void
_clutter_actor_draw_paint_volume_full (ClutterActor       *self,
                                       ClutterPaintVolume *pv,
                                       const CoglColor   *color,
                                       ClutterPaintNode   *node)
{
  g_autoptr (ClutterPaintNode) pipeline_node = NULL;
  static CoglPipeline *outline = NULL;
  g_autoptr (CoglPrimitive) prim = NULL;
  graphene_point3d_t line_ends[12 * 2];
  int n_vertices;
  ClutterContext *context = clutter_actor_get_context (self);
  ClutterBackend *backend = clutter_context_get_backend (context);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);

  if (outline == NULL)
    outline = cogl_pipeline_new (cogl_context);

  _clutter_paint_volume_complete (pv);

  n_vertices = pv->is_2d ? 4 * 2 : 12 * 2;

  /* Front face */
  line_ends[0] = pv->vertices[0]; line_ends[1] = pv->vertices[1];
  line_ends[2] = pv->vertices[1]; line_ends[3] = pv->vertices[2];
  line_ends[4] = pv->vertices[2]; line_ends[5] = pv->vertices[3];
  line_ends[6] = pv->vertices[3]; line_ends[7] = pv->vertices[0];

  if (!pv->is_2d)
    {
      /* Back face */
      line_ends[8] = pv->vertices[4]; line_ends[9] = pv->vertices[5];
      line_ends[10] = pv->vertices[5]; line_ends[11] = pv->vertices[6];
      line_ends[12] = pv->vertices[6]; line_ends[13] = pv->vertices[7];
      line_ends[14] = pv->vertices[7]; line_ends[15] = pv->vertices[4];

      /* Lines connecting front face to back face */
      line_ends[16] = pv->vertices[0]; line_ends[17] = pv->vertices[4];
      line_ends[18] = pv->vertices[1]; line_ends[19] = pv->vertices[5];
      line_ends[20] = pv->vertices[2]; line_ends[21] = pv->vertices[6];
      line_ends[22] = pv->vertices[3]; line_ends[23] = pv->vertices[7];
    }

  prim = cogl_primitive_new_p3 (cogl_context, COGL_VERTICES_MODE_LINES,
                                n_vertices,
                                (CoglVertexP3 *)line_ends);

  cogl_pipeline_set_color (outline, color);

  pipeline_node = clutter_pipeline_node_new (outline);
  clutter_paint_node_set_static_name (pipeline_node,
                                      "ClutterActor (paint volume outline)");
  clutter_paint_node_add_primitive (pipeline_node, prim);
  clutter_paint_node_add_child (node, pipeline_node);
}

static void
_clutter_actor_draw_paint_volume (ClutterActor     *self,
                                  ClutterPaintNode *node)
{
  ClutterPaintVolume *pv;

  pv = _clutter_actor_get_paint_volume_mutable (self);
  if (!pv)
    {
      gfloat width, height;
      ClutterPaintVolume fake_pv;

      ClutterActor *stage = _clutter_actor_get_stage_internal (self);
      clutter_paint_volume_init_from_actor (&fake_pv, stage);

      clutter_actor_get_size (self, &width, &height);
      clutter_paint_volume_set_width (&fake_pv, width);
      clutter_paint_volume_set_height (&fake_pv, height);

      _clutter_actor_draw_paint_volume_full (self, &fake_pv,
                                             &COGL_COLOR_INIT (0, 0, 255, 255),
                                             node);
    }
  else
    {
      _clutter_actor_draw_paint_volume_full (self, pv,
                                             &COGL_COLOR_INIT (0, 255, 0, 255),
                                             node);
    }
}

static void
_clutter_actor_paint_cull_result (ClutterActor      *self,
                                  gboolean           success,
                                  ClutterCullResult  result,
                                  ClutterPaintNode  *node)
{
  ClutterPaintVolume *pv;
  CoglColor color;

  if (success)
    {
      switch (result)
        {
        case CLUTTER_CULL_RESULT_IN:
          color = COGL_COLOR_INIT (0, 255, 0, 255);
          break;
        case CLUTTER_CULL_RESULT_OUT:
          color = COGL_COLOR_INIT (0, 0, 255, 255);
          break;
        default:
          color = COGL_COLOR_INIT (0, 255, 255, 255);
          break;
        }
    }
  else
    color = COGL_COLOR_INIT (255, 255, 255, 255);

  if (success && (pv = _clutter_actor_get_paint_volume_mutable (self)))
    _clutter_actor_draw_paint_volume_full (self, pv,
                                           &color,
                                           node);
}

static int clone_paint_level = 0;

void
_clutter_actor_push_clone_paint (void)
{
  clone_paint_level++;
}

void
_clutter_actor_pop_clone_paint (void)
{
  clone_paint_level--;
}

static gboolean
in_clone_paint (void)
{
  return clone_paint_level > 0;
}

/* Returns TRUE if the actor can be ignored */
/* FIXME: we should return a ClutterCullResult, and
 * clutter_actor_paint should understand that a CLUTTER_CULL_RESULT_IN
 * means there's no point in trying to cull descendants of the current
 * node. */
static gboolean
cull_actor (ClutterActor        *self,
            ClutterPaintContext *paint_context,
            ClutterCullResult   *result_out)
{
  ClutterActorPrivate *priv = self->priv;
  const GArray *clip_frusta;
  ClutterCullResult result = CLUTTER_CULL_RESULT_IN;
  int i;

  if (!priv->visible_paint_volume_valid)
    {
      CLUTTER_NOTE (CLIPPING, "Bail from cull_actor without culling (%s): "
                    "->visible_paint_volume_valid == FALSE",
                    _clutter_actor_get_debug_name (self));
      return FALSE;
    }

  if (G_UNLIKELY (clutter_paint_debug_flags & CLUTTER_DEBUG_DISABLE_CULLING))
    return FALSE;

  if (clutter_paint_context_is_drawing_off_stage (paint_context))
    {
      CLUTTER_NOTE (CLIPPING, "Bail from cull_actor without culling (%s): "
                    "Drawing off stage",
                    _clutter_actor_get_debug_name (self));
      return FALSE;
    }

  clip_frusta = clutter_paint_context_get_clip_frusta (paint_context);
  if (!clip_frusta)
    {
      *result_out = result;
      return TRUE;
    }

  for (i = 0; i < clip_frusta->len; i++)
    {
      const graphene_frustum_t *clip_frustum =
        &g_array_index (clip_frusta, graphene_frustum_t, i);

      result = _clutter_paint_volume_cull (&priv->visible_paint_volume,
                                           clip_frustum);

      if (result != CLUTTER_CULL_RESULT_OUT)
        break;
    }

  *result_out = result;

  return TRUE;
}

/* Remove any transitions on properties with @prefix. */
static void
_clutter_actor_remove_transitions_for_prefix (ClutterActor *actor,
                                              const char   *prefix)
{
  const ClutterAnimationInfo *info;

  info = _clutter_actor_get_animation_info_or_defaults (actor);

  if (info->transitions != NULL)
    {
      GHashTableIter iter;
      gpointer key, value;
      g_autoptr (GPtrArray) to_remove = g_ptr_array_new_with_free_func (NULL);

      g_hash_table_iter_init (&iter, info->transitions);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          if (g_str_has_prefix (key, prefix))
            g_ptr_array_add (to_remove, key);
        }

      for (unsigned int i = 0; i < to_remove->len; i++)
        clutter_actor_remove_transition (actor, to_remove->pdata[i]);
    }
}

/* Remove any transitions on the properties of @meta.
 * @section should be "actions", "constraints" or "effects" */
static void
_clutter_actor_remove_transitions_for_meta_internal (ClutterActor     *actor,
                                                     const char       *section,
                                                     ClutterActorMeta *meta)
{
  g_autofree char *meta_prefix =
      g_strdup_printf ("@%s.%s.", section,
                       clutter_actor_meta_get_name (meta));
  _clutter_actor_remove_transitions_for_prefix (actor, meta_prefix);
}

/* Remove any transitions on the properties of any #ClutterActorMeta in @section.
 * @section should be "actions", "constraints" or "effects" */
static void
_clutter_actor_remove_transitions_for_meta_section_internal (ClutterActor *actor,
                                                             const char   *section)
{
  g_autofree char *meta_prefix = g_strdup_printf ("@%s.", section);
  _clutter_actor_remove_transitions_for_prefix (actor, meta_prefix);
}

/* This is the same as clutter_actor_add_effect except that it doesn't
   queue a redraw and it doesn't notify on the effect property */
static void
_clutter_actor_add_effect_internal (ClutterActor  *self,
                                    ClutterEffect *effect)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->effects == NULL)
    {
      priv->effects = g_object_new (CLUTTER_TYPE_META_GROUP, NULL);
      priv->effects->actor = self;
    }

  _clutter_meta_group_add_meta (priv->effects, CLUTTER_ACTOR_META (effect));
}

/* This is the same as clutter_actor_remove_effect except that it doesn't
   queue a redraw and it doesn't notify on the effect property */
static void
_clutter_actor_remove_effect_internal (ClutterActor  *self,
                                       ClutterEffect *effect)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->effects == NULL)
    return;

  /* Remove any transitions on the effect’s properties. */
  _clutter_actor_remove_transitions_for_meta_internal (self, "effects",
                                                       CLUTTER_ACTOR_META (effect));

  _clutter_meta_group_remove_meta (priv->effects, CLUTTER_ACTOR_META (effect));

  if (_clutter_meta_group_peek_metas (priv->effects) == NULL)
    g_clear_object (&priv->effects);
}

static gboolean
needs_flatten_effect (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  if (G_UNLIKELY (clutter_paint_debug_flags &
                  CLUTTER_DEBUG_DISABLE_OFFSCREEN_REDIRECT))
    return FALSE;

  /* We need to enable the effect immediately even in ON_IDLE because that can
   * only be implemented efficiently within the effect itself.
   * If it was implemented here using just priv->is_dirty then we would lose
   * the ability to animate opacity without repaints.
   */
  if ((priv->offscreen_redirect & CLUTTER_OFFSCREEN_REDIRECT_ALWAYS) ||
      (priv->offscreen_redirect & CLUTTER_OFFSCREEN_REDIRECT_ON_IDLE))
    return TRUE;
  else if (priv->offscreen_redirect & CLUTTER_OFFSCREEN_REDIRECT_AUTOMATIC_FOR_OPACITY)
    {
      if (clutter_actor_get_paint_opacity (self) < 255 &&
          clutter_actor_has_overlaps (self))
        return TRUE;
    }

  return FALSE;
}

static void
add_or_remove_flatten_effect (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  /* Add or remove the flatten effect depending on the
     offscreen-redirect property. */
  if (needs_flatten_effect (self))
    {
      if (priv->flatten_effect == NULL)
        {
          ClutterActorMeta *actor_meta;
          gint priority;

          priv->flatten_effect = _clutter_flatten_effect_new ();
          /* Keep a reference to the effect so that we can queue
             redraws from it */
          g_object_ref_sink (priv->flatten_effect);

          /* Set the priority of the effect to high so that it will
             always be applied to the actor first. It uses an internal
             priority so that it won't be visible to applications */
          actor_meta = CLUTTER_ACTOR_META (priv->flatten_effect);
          priority = CLUTTER_ACTOR_META_PRIORITY_INTERNAL_HIGH;
          _clutter_actor_meta_set_priority (actor_meta, priority);

          /* This will add the effect without queueing a redraw */
          _clutter_actor_add_effect_internal (self, priv->flatten_effect);
        }
    }
  else
    {
      if (priv->flatten_effect != NULL)
        {
          /* Destroy the effect so that it will lose its fbo cache of
             the actor */
          _clutter_actor_remove_effect_internal (self, priv->flatten_effect);
          g_clear_object (&priv->flatten_effect);
        }
    }
}

static void
clutter_actor_real_paint (ClutterActor        *actor,
                          ClutterPaintContext *paint_context)
{
  ClutterActorPrivate *priv = actor->priv;
  ClutterActor *iter;

  for (iter = priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    {
      CLUTTER_NOTE (PAINT, "Painting %s, child of %s, at { %.2f, %.2f - %.2f x %.2f }",
                    _clutter_actor_get_debug_name (iter),
                    _clutter_actor_get_debug_name (actor),
                    iter->priv->allocation.x1,
                    iter->priv->allocation.y1,
                    iter->priv->allocation.x2 - iter->priv->allocation.x1,
                    iter->priv->allocation.y2 - iter->priv->allocation.y1);

      clutter_actor_paint (iter, paint_context);
    }
}

static gboolean
clutter_actor_paint_node (ClutterActor        *actor,
                          ClutterPaintNode    *root,
                          ClutterPaintContext *paint_context)
{
  ClutterActorPrivate *priv = actor->priv;
  ClutterActorBox box;
  CoglColor bg_color;

  box.x1 = 0.f;
  box.y1 = 0.f;
  box.x2 = clutter_actor_box_get_width (&priv->allocation);
  box.y2 = clutter_actor_box_get_height (&priv->allocation);

  bg_color = priv->bg_color;

  if (!CLUTTER_ACTOR_IS_TOPLEVEL (actor) &&
      priv->bg_color_set &&
      !cogl_color_equal (&priv->bg_color, &transparent))
    {
      g_autoptr (ClutterPaintNode) node = NULL;

      bg_color.alpha = clutter_actor_get_paint_opacity_internal (actor)
                     * priv->bg_color.alpha
                     / 255;

      node = clutter_color_node_new (&bg_color);
      clutter_paint_node_set_static_name (node, "backgroundColor");
      clutter_paint_node_add_rectangle (node, &box);
      clutter_paint_node_add_child (root, node);
    }

  if (priv->content != NULL)
    _clutter_content_paint_content (priv->content, actor, root, paint_context);

  if (CLUTTER_ACTOR_GET_CLASS (actor)->paint_node != NULL)
    CLUTTER_ACTOR_GET_CLASS (actor)->paint_node (actor, root, paint_context);

  if (clutter_paint_node_get_n_children (root) == 0)
    return FALSE;

  clutter_paint_node_paint (root, paint_context);

  return TRUE;
}

/**
 * clutter_actor_paint:
 * @self: A #ClutterActor
 *
 * Renders the actor to display.
 *
 * This function should not be called directly by applications.
 * Call clutter_actor_queue_redraw() to queue paints, instead.
 *
 * This function is context-aware, and will either cause a
 * regular paint or a pick paint.
 *
 * This function will call the [vfunc@Clutter.Actor.paint] virtual
 * function.
 *
 * This function does not paint the actor if the actor is set to 0,
 * unless it is performing a pick paint.
 */
void
clutter_actor_paint (ClutterActor        *self,
                     ClutterPaintContext *paint_context)
{
  g_autoptr (ClutterPaintNode) actor_node = NULL;
  g_autoptr (ClutterPaintNode) root_node = NULL;
  ClutterActorPrivate *priv;
  ClutterActorBox clip;
  gboolean culling_inhibited;
  gboolean clip_set = FALSE;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return;

  priv = self->priv;
  priv->propagated_one_redraw = FALSE;

  /* It's an important optimization that we consider painting of
   * actors with 0 opacity to be a NOP... */
  if (/* ignore top-levels, since they might be transparent */
      !CLUTTER_ACTOR_IS_TOPLEVEL (self) &&
      /* Use the override opacity if its been set */
      ((priv->opacity_override >= 0) ?
       priv->opacity_override : priv->opacity) == 0)
    return;

  /* if we aren't paintable (not in a toplevel with all
   * parents paintable) then do nothing.
   */
  if (!clutter_actor_is_mapped (self))
    return;

#ifdef HAVE_PROFILER
  COGL_TRACE_SCOPED_ANCHOR (ClutterActorPaint);

  if (G_UNLIKELY (clutter_debug_flags & CLUTTER_DEBUG_DETAILED_TRACE))
    {
      COGL_TRACE_BEGIN_ANCHORED (ClutterActorPaint,
                                 "Clutter::Actor::paint()");
      COGL_TRACE_DESCRIBE (ClutterActorPaint,
                           _clutter_actor_get_debug_name (self));
    }
#endif

  actor_node = clutter_actor_node_new (self, -1);
  root_node = clutter_paint_node_ref (actor_node);

  if (priv->has_clip)
    {
      clip.x1 = priv->clip.origin.x;
      clip.y1 = priv->clip.origin.y;
      clip.x2 = priv->clip.origin.x + priv->clip.size.width;
      clip.y2 = priv->clip.origin.y + priv->clip.size.height;
      clip_set = TRUE;
    }
  else if (priv->clip_to_allocation)
    {
      clip.x1 = 0.f;
      clip.y1 = 0.f;
      clip.x2 = priv->allocation.x2 - priv->allocation.x1;
      clip.y2 = priv->allocation.y2 - priv->allocation.y1;
      clip_set = TRUE;
    }

  if (clip_set)
    {
      ClutterPaintNode *clip_node;

      clip_node = clutter_clip_node_new ();
      clutter_paint_node_add_rectangle (clip_node, &clip);
      clutter_paint_node_add_child (clip_node, root_node);
      clutter_paint_node_unref (root_node);

      root_node = g_steal_pointer (&clip_node);
    }

  if (priv->enable_model_view_transform)
    {
      ClutterPaintNode *transform_node;
      graphene_matrix_t transform;

      clutter_actor_get_transform (self, &transform);

      if (!graphene_matrix_is_identity (&transform))
        {
          transform_node = clutter_transform_node_new (&transform);
          clutter_paint_node_add_child (transform_node, root_node);
          clutter_paint_node_unref (root_node);

          root_node = g_steal_pointer (&transform_node);
        }

#ifdef CLUTTER_ENABLE_DEBUG
      /* Catch when out-of-band transforms have been made by actors not as part
       * of an apply_transform vfunc... */
      if (G_UNLIKELY (clutter_debug_flags & CLUTTER_DEBUG_OOB_TRANSFORMS))
        {
          graphene_matrix_t expected_matrix;

          clutter_actor_get_relative_transformation_matrix (self, NULL,
                                                            &expected_matrix);

          if (!graphene_matrix_equal_fast (&transform, &expected_matrix))
            {
              g_autoptr (GString) buf = g_string_sized_new (1024);
              ClutterActor *parent;

              parent = self;
              while (parent != NULL)
                {
                  g_string_append (buf, _clutter_actor_get_debug_name (parent));

                  if (parent->priv->parent != NULL)
                    g_string_append (buf, "->");

                  parent = parent->priv->parent;
                }

              g_warning ("Unexpected transform found when painting actor "
                         "\"%s\". This will be caused by one of the actor's "
                         "ancestors (%s) using the Cogl API directly to transform "
                         "children instead of using ::apply_transform().",
                         _clutter_actor_get_debug_name (self),
                         buf->str);
            }
        }
#endif /* CLUTTER_ENABLE_DEBUG */
    }

  /* We check whether we need to add the flatten effect before
   * each paint so that we can avoid having a mechanism for
   * applications to notify when the value of the
   * has_overlaps virtual changes.
   */
  add_or_remove_flatten_effect (self);

  culling_inhibited = priv->inhibit_culling_counter > 0;
  if (!culling_inhibited && !in_clone_paint ())
    {
      gboolean success;
      gboolean should_cull_out = (clutter_paint_debug_flags &
                                  (CLUTTER_DEBUG_DISABLE_CULLING |
                                   CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS)) !=
                                 (CLUTTER_DEBUG_DISABLE_CULLING |
                                  CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS);
      /* annoyingly gcc warns if uninitialized even though
       * the initialization is redundant :-( */
      ClutterCullResult result = CLUTTER_CULL_RESULT_IN;

      success = should_cull_out
        ? cull_actor (self, paint_context, &result)
        : FALSE;

      if (G_UNLIKELY (clutter_paint_debug_flags & CLUTTER_DEBUG_REDRAWS))
        _clutter_actor_paint_cull_result (self, success, result, actor_node);
      else if (result == CLUTTER_CULL_RESULT_OUT && success)
        return;
    }

  if (priv->effects == NULL)
    priv->next_effect_to_paint = NULL;
  else
    priv->next_effect_to_paint =
      _clutter_meta_group_peek_metas (priv->effects);

  if (G_UNLIKELY (clutter_paint_debug_flags & CLUTTER_DEBUG_PAINT_VOLUMES))
    _clutter_actor_draw_paint_volume (self, actor_node);

  clutter_paint_node_paint (root_node, paint_context);

  /* If we make it here then the actor has run through a complete
   * paint run including all the effects so it's no longer dirty,
   * unless a new redraw was queued up.
   */
  priv->is_dirty = priv->propagated_one_redraw;
}

/**
 * clutter_actor_continue_paint:
 * @self: A #ClutterActor
 *
 * Run the next stage of the paint sequence. This function should only
 * be called within the implementation of the ‘run’ virtual of a
 * #ClutterEffect. It will cause the run method of the next effect to
 * be applied, or it will paint the actual actor if the current effect
 * is the last effect in the chain.
 */
void
clutter_actor_continue_paint (ClutterActor        *self,
                              ClutterPaintContext *paint_context)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  /* This should only be called from with in the ‘run’ implementation
     of a ClutterEffect */
  g_return_if_fail (CLUTTER_ACTOR_IN_PAINT (self));

  priv = self->priv;

  /* Skip any effects that are disabled */
  while (priv->next_effect_to_paint &&
         !clutter_actor_meta_get_enabled (priv->next_effect_to_paint->data))
    priv->next_effect_to_paint = priv->next_effect_to_paint->next;

  /* If this has come from the last effect then we'll just paint the
     actual actor */
  if (priv->next_effect_to_paint == NULL)
    {
      CoglFramebuffer *framebuffer;
      g_autoptr (ClutterPaintNode) dummy = NULL;

      /* XXX - this will go away in 2.0, when we can get rid of this
       * stuff and switch to a pure retained render tree of PaintNodes
       * for the entire frame, starting from the Stage; the paint()
       * virtual function can then be called directly.
       */
      framebuffer = clutter_paint_context_get_base_framebuffer (paint_context);
      dummy = _clutter_dummy_node_new (self, framebuffer);
      clutter_paint_node_set_static_name (dummy, "Root");

      /* XXX - for 1.12, we use the return value of paint_node() to
       * decide whether we should call the paint() vfunc.
       */
      clutter_actor_paint_node (self, dummy, paint_context);

      CLUTTER_ACTOR_GET_CLASS (self)->paint (self, paint_context);
    }
  else
    {
      g_autoptr (ClutterPaintNode) effect_node = NULL;
      ClutterEffect *old_current_effect;
      ClutterEffectPaintFlags run_flags = 0;

      /* Cache the current effect so that we can put it back before
         returning */
      old_current_effect = priv->current_effect;

      priv->current_effect = priv->next_effect_to_paint->data;
      priv->next_effect_to_paint = priv->next_effect_to_paint->next;

      if (priv->is_dirty)
        {
          /* If there's an effect queued with this redraw then all
           * effects up to that one will be considered dirty. It
           * is expected the queued effect will paint the cached
           * image and not call clutter_actor_continue_paint again
           * (although it should work ok if it does)
           */
          if (priv->effect_to_redraw == NULL ||
              priv->current_effect != priv->effect_to_redraw)
            run_flags |= CLUTTER_EFFECT_PAINT_ACTOR_DIRTY;
        }

      if (priv->current_effect == priv->flatten_effect &&
          priv->offscreen_redirect & CLUTTER_OFFSCREEN_REDIRECT_ON_IDLE &&
          run_flags & CLUTTER_EFFECT_PAINT_ACTOR_DIRTY)
        run_flags |= CLUTTER_EFFECT_PAINT_BYPASS_EFFECT;

      effect_node = clutter_effect_node_new (priv->current_effect);

      _clutter_effect_paint (priv->current_effect,
                             effect_node,
                             paint_context,
                             run_flags);

      clutter_paint_node_paint (effect_node, paint_context);

      priv->current_effect = old_current_effect;
    }
}

/**
 * clutter_actor_pick:
 * @actor: A #ClutterActor
 *
 * Asks @actor to perform a pick.
 */
void
clutter_actor_pick (ClutterActor       *actor,
                    ClutterPickContext *pick_context)
{
  ClutterActorPrivate *priv;
  ClutterActorBox clip;
  gboolean transform_pushed = FALSE;
  gboolean clip_set = FALSE;
  gboolean should_cull = (clutter_paint_debug_flags &
                          (CLUTTER_DEBUG_DISABLE_CULLING |
                           CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS)) !=
                         (CLUTTER_DEBUG_DISABLE_CULLING |
                          CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS);

  if (CLUTTER_ACTOR_IN_DESTRUCTION (actor))
    return;

  priv = actor->priv;

  /* if we aren't paintable (not in a toplevel with all
   * parents paintable) then do nothing.
   */
  if (!clutter_actor_is_mapped (actor))
    return;

  /* mark that we are in the paint process */
  CLUTTER_SET_PRIVATE_FLAGS (actor, CLUTTER_IN_PICK);

  if (should_cull && priv->has_paint_volume && priv->visible_paint_volume_valid)
    {
      graphene_box_t box;

      clutter_paint_volume_to_box (&priv->visible_paint_volume, &box);
      if (!clutter_pick_context_intersects_box (pick_context, &box))
        {
          clutter_pick_context_log_overlap (pick_context, actor);
          goto out;
        }
    }

  if (priv->enable_model_view_transform)
    {
      graphene_matrix_t matrix;

      graphene_matrix_init_identity (&matrix);
      _clutter_actor_apply_modelview_transform (actor, &matrix);
      if (!graphene_matrix_is_identity (&matrix))
        {
          clutter_pick_context_push_transform (pick_context, &matrix);
          transform_pushed = TRUE;
        }
    }

  if (priv->has_clip)
    {
      clip.x1 = priv->clip.origin.x;
      clip.y1 = priv->clip.origin.y;
      clip.x2 = priv->clip.origin.x + priv->clip.size.width;
      clip.y2 = priv->clip.origin.y + priv->clip.size.height;
      clip_set = TRUE;
    }
  else if (priv->clip_to_allocation)
    {
      clip.x1 = 0.f;
      clip.y1 = 0.f;
      clip.x2 = priv->allocation.x2 - priv->allocation.x1;
      clip.y2 = priv->allocation.y2 - priv->allocation.y1;
      clip_set = TRUE;
    }

  if (clip_set)
    clutter_pick_context_push_clip (pick_context, &clip);

  priv->next_effect_to_paint = NULL;
  if (priv->effects)
    {
      priv->next_effect_to_paint =
        _clutter_meta_group_peek_metas (priv->effects);
    }

  clutter_actor_continue_pick (actor, pick_context);

  if (clip_set)
    clutter_pick_context_pop_clip (pick_context);

  if (transform_pushed)
    clutter_pick_context_pop_transform (pick_context);

out:
  /* paint sequence complete */
  CLUTTER_UNSET_PRIVATE_FLAGS (actor, CLUTTER_IN_PICK);
}

/**
 * clutter_actor_continue_pick:
 * @actor: A #ClutterActor
 *
 * Run the next stage of the pick sequence. This function should only
 * be called within the implementation of the ‘pick’ virtual of a
 * #ClutterEffect. It will cause the run method of the next effect to
 * be applied, or it will pick the actual actor if the current effect
 * is the last effect in the chain.
 */
void
clutter_actor_continue_pick (ClutterActor       *actor,
                             ClutterPickContext *pick_context)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  g_return_if_fail (CLUTTER_ACTOR_IN_PICK (actor));

  priv = actor->priv;

  /* Skip any effects that are disabled */
  while (priv->next_effect_to_paint &&
         !clutter_actor_meta_get_enabled (priv->next_effect_to_paint->data))
    priv->next_effect_to_paint = priv->next_effect_to_paint->next;

  /* If this has come from the last effect then we'll just pick the
   * actual actor.
   */
  if (priv->next_effect_to_paint == NULL)
    {
      /* The actor will log a silhouette of itself to the stage pick log.
       *
       * XXX:2.0 - Call the pick() virtual directly
       */
      if (g_signal_has_handler_pending (actor, actor_signals[PICK],
                                        0, TRUE))
        g_signal_emit (actor, actor_signals[PICK], 0, pick_context);
      else
        CLUTTER_ACTOR_GET_CLASS (actor)->pick (actor, pick_context);
    }
  else
    {
      ClutterEffect *old_current_effect;

      /* Cache the current effect so that we can put it back before
       * returning.
       */
      old_current_effect = priv->current_effect;

      priv->current_effect = priv->next_effect_to_paint->data;
      priv->next_effect_to_paint = priv->next_effect_to_paint->next;

      _clutter_effect_pick (priv->current_effect, pick_context);

      priv->current_effect = old_current_effect;
    }
}

static void
_clutter_actor_stop_transitions (ClutterActor *self)
{
  const ClutterAnimationInfo *info;
  GHashTableIter iter;
  gpointer value;

  info = _clutter_actor_get_animation_info_or_defaults (self);
  if (info->transitions == NULL)
    return;

  g_hash_table_iter_init (&iter, info->transitions);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      TransitionClosure *closure = value;

      if (clutter_transition_get_remove_on_complete (closure->transition))
        {
          g_hash_table_iter_remove (&iter);
        }
      else
        {
          /* otherwise we stop it, and the transition will be removed
           * later, either by the actor's destruction or by explicit
           * removal
           */
          clutter_timeline_stop (CLUTTER_TIMELINE (closure->transition));
        }
    }
}

static inline void
remove_child (ClutterActor *self,
              ClutterActor *child)
{
  ClutterActor *prev_sibling, *next_sibling;

  prev_sibling = child->priv->prev_sibling;
  next_sibling = child->priv->next_sibling;

  if (prev_sibling != NULL)
    prev_sibling->priv->next_sibling = next_sibling;

  if (next_sibling != NULL)
    next_sibling->priv->prev_sibling = prev_sibling;

  if (self->priv->first_child == child)
    self->priv->first_child = next_sibling;

  if (self->priv->last_child == child)
    self->priv->last_child = prev_sibling;

  child->priv->parent = NULL;
  child->priv->prev_sibling = NULL;
  child->priv->next_sibling = NULL;
}

typedef enum
{
  REMOVE_CHILD_EMIT_PARENT_SET    = 1 << 1,
  REMOVE_CHILD_EMIT_CHILD_REMOVED = 1 << 2,
  REMOVE_CHILD_CHECK_STATE        = 1 << 3,
  REMOVE_CHILD_NOTIFY_FIRST_LAST  = 1 << 4,
  REMOVE_CHILD_STOP_TRANSITIONS   = 1 << 5,
  REMOVE_CHILD_CLEAR_STAGE_VIEWS  = 1 << 6,

  /* default flags for public API */
  REMOVE_CHILD_DEFAULT_FLAGS      = REMOVE_CHILD_STOP_TRANSITIONS |
                                    REMOVE_CHILD_EMIT_PARENT_SET |
                                    REMOVE_CHILD_EMIT_CHILD_REMOVED |
                                    REMOVE_CHILD_CHECK_STATE |
                                    REMOVE_CHILD_NOTIFY_FIRST_LAST |
                                    REMOVE_CHILD_CLEAR_STAGE_VIEWS,
} ClutterActorRemoveChildFlags;

/*< private >
 * clutter_actor_remove_child_internal:
 * @self: a #ClutterActor
 * @child: the child of @self that has to be removed
 * @flags: control the removal operations
 *
 * Removes @child from the list of children of @self.
 */
static void
clutter_actor_remove_child_internal (ClutterActor                 *self,
                                     ClutterActor                 *child,
                                     ClutterActorRemoveChildFlags  flags)
{
  ClutterActor *old_first, *old_last;
  gboolean emit_parent_set, emit_child_removed, check_state;
  gboolean notify_first_last;
  gboolean stop_transitions;
  gboolean clear_stage_views;
  GObject *obj;

  if (self == child)
    {
      g_warning ("Cannot remove actor '%s' from itself.",
                 _clutter_actor_get_debug_name (self));
      return;
    }

  emit_parent_set = (flags & REMOVE_CHILD_EMIT_PARENT_SET) != 0;
  emit_child_removed = (flags & REMOVE_CHILD_EMIT_CHILD_REMOVED) != 0;
  check_state = (flags & REMOVE_CHILD_CHECK_STATE) != 0;
  notify_first_last = (flags & REMOVE_CHILD_NOTIFY_FIRST_LAST) != 0;
  stop_transitions = (flags & REMOVE_CHILD_STOP_TRANSITIONS) != 0;
  clear_stage_views = (flags & REMOVE_CHILD_CLEAR_STAGE_VIEWS) != 0;

  obj = G_OBJECT (self);
  g_object_freeze_notify (obj);

  if (stop_transitions)
    _clutter_actor_stop_transitions (child);

  if (check_state)
    {
      /* we need to unrealize *before* we set parent_actor to NULL,
       * because in an unrealize method actors are dissociating from the
       * stage, which means they need to be able to
       * clutter_actor_get_stage().
       *
       * yhis should unmap and unrealize, unless we're reparenting.
       */
      clutter_actor_update_map_state (child, MAP_STATE_MAKE_UNREALIZED);
    }

  old_first = self->priv->first_child;
  old_last = self->priv->last_child;

  remove_child (self, child);

  self->priv->n_children -= 1;

  self->priv->age += 1;

  if (self->priv->in_cloned_branch)
    clutter_actor_pop_in_cloned_branch (child, self->priv->in_cloned_branch);

  if (self->priv->unmapped_paint_branch_counter)
    pop_in_paint_unmapped_branch (child, self->priv->unmapped_paint_branch_counter);

  /* if the child that got removed was visible and set to
   * expand then we want to reset the parent's state in
   * case the child was the only thing that was making it
   * expand.
   */
  if (clutter_actor_is_visible (child) &&
      (child->priv->needs_compute_expand ||
       child->priv->needs_x_expand ||
       child->priv->needs_y_expand))
    {
      clutter_actor_queue_compute_expand (self);
    }

  /* Only actors which are attached to a stage get notified about changes
   * to the stage views, so make sure all the stage-views lists are
   * cleared as the child and its children leave the actor tree.
   */
  if (clear_stage_views && !CLUTTER_ACTOR_IN_DESTRUCTION (child))
    clutter_actor_clear_stage_views_recursive (child, stop_transitions);

  if (emit_parent_set && !CLUTTER_ACTOR_IN_DESTRUCTION (child))
    g_signal_emit (child, actor_signals[PARENT_SET], 0, self);

  /* we need to emit the signal before dropping the reference */
  if (emit_child_removed)
    g_signal_emit (self, actor_signals[CHILD_REMOVED], 0, child);

  if (notify_first_last)
    {
      if (old_first != self->priv->first_child)
        g_object_notify_by_pspec (obj, obj_props[PROP_FIRST_CHILD]);

      if (old_last != self->priv->last_child)
        g_object_notify_by_pspec (obj, obj_props[PROP_LAST_CHILD]);
    }

  g_object_thaw_notify (obj);

  /* remove the reference we acquired in clutter_actor_add_child() */
  g_object_unref (child);
}

static ClutterTransformInfo default_transform_info = {
  0.0,                          /* rotation-x */
  0.0,                          /* rotation-y */
  0.0,                          /* rotation-z */

  1.0, 1.0, 1.0,                /* scale */

  GRAPHENE_POINT3D_INIT_ZERO,   /* translation */

  0.f,                          /* z-position */

  GRAPHENE_POINT_INIT_ZERO,     /* pivot */
  0.f,                          /* pivot-z */

  { },
  FALSE,                        /* transform */
  { },
  FALSE,                        /* child-transform */
};

static inline const ClutterTransformInfo *
get_default_transform_info (void)
{
  static gsize initialized = FALSE;

  if (G_UNLIKELY (g_once_init_enter (&initialized)))
    {
      graphene_matrix_init_identity (&default_transform_info.transform);
      graphene_matrix_init_identity (&default_transform_info.child_transform);
      g_once_init_leave (&initialized, TRUE);
    }

  return &default_transform_info;
}

/*< private >
 * _clutter_actor_get_transform_info_or_defaults:
 * @self: a #ClutterActor
 *
 * Retrieves the ClutterTransformInfo structure associated to an actor.
 *
 * If the actor does not have a ClutterTransformInfo structure associated
 * to it, then the default structure will be returned.
 *
 * This function should only be used for getters.
 *
 * Return value: a const pointer to the ClutterTransformInfo structure
 */
const ClutterTransformInfo *
_clutter_actor_get_transform_info_or_defaults (ClutterActor *self)
{
  ClutterTransformInfo *info;

  info = g_object_get_qdata (G_OBJECT (self), quark_actor_transform_info);
  if (info != NULL)
    return info;

  return get_default_transform_info ();
}

static void
clutter_transform_info_free (gpointer data)
{
  g_free (data);
}

/*< private >
 * _clutter_actor_get_transform_info:
 * @self: a #ClutterActor
 *
 * Retrieves a pointer to the ClutterTransformInfo structure.
 *
 * If the actor does not have a ClutterTransformInfo associated to it, one
 * will be created and initialized to the default values.
 *
 * This function should be used for setters.
 *
 * For getters, you should use _clutter_actor_get_transform_info_or_defaults()
 * instead.
 *
 * Return value: (transfer none): a pointer to the ClutterTransformInfo
 *   structure
 */
ClutterTransformInfo *
_clutter_actor_get_transform_info (ClutterActor *self)
{
  ClutterTransformInfo *info;

  info = g_object_get_qdata (G_OBJECT (self), quark_actor_transform_info);
  if (info == NULL)
    {
      info = g_new0 (ClutterTransformInfo, 1);

      *info = *get_default_transform_info ();

      g_object_set_qdata_full (G_OBJECT (self), quark_actor_transform_info,
                               info,
                               clutter_transform_info_free);
    }

  return info;
}

static inline void
clutter_actor_set_pivot_point_internal (ClutterActor           *self,
                                        const graphene_point_t *pivot)
{
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);
  info->pivot = *pivot;

  transform_changed (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PIVOT_POINT]);

  clutter_actor_queue_redraw (self);
}

static inline void
clutter_actor_set_pivot_point_z_internal (ClutterActor *self,
                                          float         pivot_z)
{
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);
  info->pivot_z = pivot_z;

  transform_changed (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PIVOT_POINT_Z]);

  clutter_actor_queue_redraw (self);
}

/*< private >
 * clutter_actor_set_translation_internal:
 * @self: a #ClutterActor
 * @axis: the axis of the translation to change
 * @angle: the translation as a value along @axis
 *
 * Sets the translation on the given @axis
 */
static void
clutter_actor_set_translation_internal (ClutterActor *self,
                                        gfloat        value,
                                        GParamSpec   *pspec)
{
  GObject *obj = G_OBJECT (self);
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);

  if (pspec == obj_props[PROP_TRANSLATION_X])
    info->translation.x = value;
  else if (pspec == obj_props[PROP_TRANSLATION_Y])
    info->translation.y = value;
  else if (pspec == obj_props[PROP_TRANSLATION_Z])
    info->translation.z = value;
  else
    g_assert_not_reached ();

  transform_changed (self);

  clutter_actor_queue_redraw (self);
  g_object_notify_by_pspec (obj, pspec);
}

static inline void
clutter_actor_set_translation_factor (ClutterActor      *self,
                                      ClutterRotateAxis  axis,
                                      gdouble            value)
{
  const ClutterTransformInfo *info;
  const float *translate_p = NULL;
  GParamSpec *pspec = NULL;

  info = _clutter_actor_get_transform_info_or_defaults (self);

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      pspec = obj_props[PROP_TRANSLATION_X];
      translate_p = &info->translation.x;
      break;

    case CLUTTER_Y_AXIS:
      pspec = obj_props[PROP_TRANSLATION_Y];
      translate_p = &info->translation.y;
      break;

    case CLUTTER_Z_AXIS:
      pspec = obj_props[PROP_TRANSLATION_Z];
      translate_p = &info->translation.z;
      break;
    }

  g_assert (pspec != NULL);
  g_assert (translate_p != NULL);

  _clutter_actor_create_transition (self, pspec, *translate_p, value);
}

/**
 * clutter_actor_set_translation:
 * @self: a #ClutterActor
 * @translate_x: the translation along the X axis
 * @translate_y: the translation along the Y axis
 * @translate_z: the translation along the Z axis
 *
 * Sets an additional translation transformation on a #ClutterActor,
 * relative to the [property@Clutter.Actor:pivot-point].
 */
void
clutter_actor_set_translation (ClutterActor *self,
                               gfloat        translate_x,
                               gfloat        translate_y,
                               gfloat        translate_z)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_set_translation_factor (self, CLUTTER_X_AXIS, translate_x);
  clutter_actor_set_translation_factor (self, CLUTTER_Y_AXIS, translate_y);
  clutter_actor_set_translation_factor (self, CLUTTER_Z_AXIS, translate_z);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_get_translation:
 * @self: a #ClutterActor
 * @translate_x: (out) (optional): return location for the X component
 *   of the translation, or %NULL
 * @translate_y: (out) (optional): return location for the Y component
 *   of the translation, or %NULL
 * @translate_z: (out) (optional): return location for the Z component
 *   of the translation, or %NULL
 *
 * Retrieves the translation set using clutter_actor_set_translation().
 */
void
clutter_actor_get_translation (ClutterActor *self,
                               gfloat       *translate_x,
                               gfloat       *translate_y,
                               gfloat       *translate_z)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (translate_x != NULL)
    *translate_x = info->translation.x;

  if (translate_y != NULL)
    *translate_y = info->translation.y;

  if (translate_z != NULL)
    *translate_z = info->translation.z;
}

/*< private >
 * clutter_actor_set_rotation_angle_internal:
 * @self: a #ClutterActor
 * @angle: the angle of rotation
 * @pspec: the #GParamSpec of the property
 *
 * Sets the rotation angle on the given axis without affecting the
 * rotation center point.
 */
static inline void
clutter_actor_set_rotation_angle_internal (ClutterActor *self,
                                           gdouble       angle,
                                           GParamSpec   *pspec)
{
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);

  if (pspec == obj_props[PROP_ROTATION_ANGLE_X])
    info->rx_angle = angle;
  else if (pspec == obj_props[PROP_ROTATION_ANGLE_Y])
    info->ry_angle = angle;
  else if (pspec == obj_props[PROP_ROTATION_ANGLE_Z])
    info->rz_angle = angle;
  else
    g_assert_not_reached ();

  transform_changed (self);

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), pspec);
}

/**
 * clutter_actor_set_rotation_angle:
 * @self: a #ClutterActor
 * @axis: the axis to set the angle one
 * @angle: the angle of rotation, in degrees
 *
 * Sets the @angle of rotation of a #ClutterActor on the given @axis.
 *
 * This function is a convenience for setting the rotation properties
 * [property@Clutter.Actor:rotation-angle-x], [property@Clutter.Actor:rotation-angle-y],
 * and [property@Clutter.Actor:rotation-angle-z].
 *
 * The center of rotation is established by the [property@Clutter.Actor:pivot-point]
 * property.
 */
void
clutter_actor_set_rotation_angle (ClutterActor      *self,
                                  ClutterRotateAxis  axis,
                                  gdouble            angle)
{
  const ClutterTransformInfo *info;
  const double *cur_angle_p = NULL;
  GParamSpec *pspec = NULL;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      cur_angle_p = &info->rx_angle;
      pspec = obj_props[PROP_ROTATION_ANGLE_X];
      break;

    case CLUTTER_Y_AXIS:
      cur_angle_p = &info->ry_angle;
      pspec = obj_props[PROP_ROTATION_ANGLE_Y];
      break;

    case CLUTTER_Z_AXIS:
      cur_angle_p = &info->rz_angle;
      pspec = obj_props[PROP_ROTATION_ANGLE_Z];
      break;
    }

  g_assert (pspec != NULL);
  g_assert (cur_angle_p != NULL);

  _clutter_actor_create_transition (self, pspec, *cur_angle_p, angle);
}

/**
 * clutter_actor_get_rotation_angle:
 * @self: a #ClutterActor
 * @axis: the axis of the rotation
 *
 * Retrieves the angle of rotation set by clutter_actor_set_rotation_angle().
 *
 * Return value: the angle of rotation, in degrees
 */
gdouble
clutter_actor_get_rotation_angle (ClutterActor      *self,
                                  ClutterRotateAxis  axis)
{
  const ClutterTransformInfo *info;
  gdouble retval;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.0);

  info = _clutter_actor_get_transform_info_or_defaults (self);

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      retval = info->rx_angle;
      break;

    case CLUTTER_Y_AXIS:
      retval = info->ry_angle;
      break;

    case CLUTTER_Z_AXIS:
      retval = info->rz_angle;
      break;

    default:
      g_warn_if_reached ();
      retval = 0.0;
      break;
    }

  return retval;
}

static void
clutter_actor_set_scale_factor_internal (ClutterActor *self,
                                         double factor,
                                         GParamSpec *pspec)
{
  GObject *obj = G_OBJECT (self);
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);

  if (pspec == obj_props[PROP_SCALE_X])
    info->scale_x = factor;
  else if (pspec == obj_props[PROP_SCALE_Y])
    info->scale_y = factor;
  else if (pspec == obj_props[PROP_SCALE_Z])
    info->scale_z = factor;
  else
    g_assert_not_reached ();

  transform_changed (self);

  clutter_actor_queue_redraw (self);
  g_object_notify_by_pspec (obj, pspec);
}

static inline void
clutter_actor_set_scale_factor (ClutterActor      *self,
                                ClutterRotateAxis  axis,
                                gdouble            factor)
{
  const ClutterTransformInfo *info;
  const double *scale_p = NULL;
  GParamSpec *pspec = NULL;

  info = _clutter_actor_get_transform_info_or_defaults (self);

  switch (axis)
    {
    case CLUTTER_X_AXIS:
      pspec = obj_props[PROP_SCALE_X];
      scale_p = &info->scale_x;
      break;

    case CLUTTER_Y_AXIS:
      pspec = obj_props[PROP_SCALE_Y];
      scale_p = &info->scale_y;
      break;

    case CLUTTER_Z_AXIS:
      pspec = obj_props[PROP_SCALE_Z];
      scale_p = &info->scale_z;
      break;
    }

  g_assert (pspec != NULL);
  g_assert (scale_p != NULL);

  if (*scale_p != factor)
    _clutter_actor_create_transition (self, pspec, *scale_p, factor);
}

static void
clutter_actor_set_clip_rect (ClutterActor          *self,
                             const graphene_rect_t *clip)
{
  ClutterActorPrivate *priv = self->priv;
  GObject *obj = G_OBJECT (self);

  if (clip != NULL)
    {
      priv->clip = *clip;
      priv->has_clip = TRUE;
    }
  else
    priv->has_clip = FALSE;

  queue_update_paint_volume (self);
  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (obj, obj_props[PROP_CLIP_RECT]);
  g_object_notify_by_pspec (obj, obj_props[PROP_HAS_CLIP]);
}

static void
clutter_actor_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  ClutterActor *actor = CLUTTER_ACTOR (object);
  ClutterActorPrivate *priv = actor->priv;

  switch (prop_id)
    {
    case PROP_CONTEXT:
      priv->context = g_value_get_object (value);
      break;

    case PROP_X:
      clutter_actor_set_x (actor, g_value_get_float (value));
      break;

    case PROP_Y:
      clutter_actor_set_y (actor, g_value_get_float (value));
      break;

    case PROP_POSITION:
      {
        const graphene_point_t *pos = g_value_get_boxed (value);

        if (pos != NULL)
          clutter_actor_set_position (actor, pos->x, pos->y);
        else
          clutter_actor_set_fixed_position_set (actor, FALSE);
      }
      break;

    case PROP_WIDTH:
      clutter_actor_set_width (actor, g_value_get_float (value));
      break;

    case PROP_HEIGHT:
      clutter_actor_set_height (actor, g_value_get_float (value));
      break;

    case PROP_SIZE:
      {
        const graphene_size_t *size = g_value_get_boxed (value);

        if (size != NULL)
          clutter_actor_set_size (actor, size->width, size->height);
        else
          clutter_actor_set_size (actor, -1, -1);
      }
      break;

    case PROP_FIXED_X:
      clutter_actor_set_x (actor, g_value_get_float (value));
      break;

    case PROP_FIXED_Y:
      clutter_actor_set_y (actor, g_value_get_float (value));
      break;

    case PROP_FIXED_POSITION_SET:
      clutter_actor_set_fixed_position_set (actor, g_value_get_boolean (value));
      break;

    case PROP_MIN_WIDTH:
      clutter_actor_set_min_width (actor, g_value_get_float (value));
      break;

    case PROP_MIN_HEIGHT:
      clutter_actor_set_min_height (actor, g_value_get_float (value));
      break;

    case PROP_NATURAL_WIDTH:
      clutter_actor_set_natural_width (actor, g_value_get_float (value));
      break;

    case PROP_NATURAL_HEIGHT:
      clutter_actor_set_natural_height (actor, g_value_get_float (value));
      break;

    case PROP_MIN_WIDTH_SET:
      clutter_actor_set_min_width_set (actor, g_value_get_boolean (value));
      break;

    case PROP_MIN_HEIGHT_SET:
      clutter_actor_set_min_height_set (actor, g_value_get_boolean (value));
      break;

    case PROP_NATURAL_WIDTH_SET:
      clutter_actor_set_natural_width_set (actor, g_value_get_boolean (value));
      break;

    case PROP_NATURAL_HEIGHT_SET:
      clutter_actor_set_natural_height_set (actor, g_value_get_boolean (value));
      break;

    case PROP_REQUEST_MODE:
      clutter_actor_set_request_mode (actor, g_value_get_enum (value));
      break;

    case PROP_Z_POSITION:
      clutter_actor_set_z_position (actor, g_value_get_float (value));
      break;

    case PROP_OPACITY:
      clutter_actor_set_opacity (actor, g_value_get_uint (value));
      break;

    case PROP_OFFSCREEN_REDIRECT:
      clutter_actor_set_offscreen_redirect (actor, g_value_get_flags (value));
      break;

    case PROP_NAME:
      clutter_actor_set_name (actor, g_value_get_string (value));
      break;

    case PROP_VISIBLE:
      if (g_value_get_boolean (value) == TRUE)
	clutter_actor_show (actor);
      else
	clutter_actor_hide (actor);
      break;

    case PROP_PIVOT_POINT:
      {
        const graphene_point_t *pivot = g_value_get_boxed (value);

        if (pivot == NULL)
          pivot = graphene_point_zero ();

        clutter_actor_set_pivot_point (actor, pivot->x, pivot->y);
      }
      break;

    case PROP_PIVOT_POINT_Z:
      clutter_actor_set_pivot_point_z (actor, g_value_get_float (value));
      break;

    case PROP_TRANSLATION_X:
      clutter_actor_set_translation_factor (actor, CLUTTER_X_AXIS,
                                            g_value_get_float (value));
      break;

    case PROP_TRANSLATION_Y:
      clutter_actor_set_translation_factor (actor, CLUTTER_Y_AXIS,
                                            g_value_get_float (value));
      break;

    case PROP_TRANSLATION_Z:
      clutter_actor_set_translation_factor (actor, CLUTTER_Z_AXIS,
                                            g_value_get_float (value));
      break;

    case PROP_SCALE_X:
      clutter_actor_set_scale_factor (actor, CLUTTER_X_AXIS,
                                      g_value_get_double (value));
      break;

    case PROP_SCALE_Y:
      clutter_actor_set_scale_factor (actor, CLUTTER_Y_AXIS,
                                      g_value_get_double (value));
      break;

    case PROP_SCALE_Z:
      clutter_actor_set_scale_factor (actor, CLUTTER_Z_AXIS,
                                      g_value_get_double (value));
      break;

    case PROP_CLIP_RECT:
      clutter_actor_set_clip_rect (actor, g_value_get_boxed (value));
      break;

    case PROP_CLIP_TO_ALLOCATION:
      clutter_actor_set_clip_to_allocation (actor, g_value_get_boolean (value));
      break;

    case PROP_REACTIVE:
      clutter_actor_set_reactive (actor, g_value_get_boolean (value));
      break;

    case PROP_ROTATION_ANGLE_X:
      clutter_actor_set_rotation_angle (actor,
                                        CLUTTER_X_AXIS,
                                        g_value_get_double (value));
      break;

    case PROP_ROTATION_ANGLE_Y:
      clutter_actor_set_rotation_angle (actor,
                                        CLUTTER_Y_AXIS,
                                        g_value_get_double (value));
      break;

    case PROP_ROTATION_ANGLE_Z:
      clutter_actor_set_rotation_angle (actor,
                                        CLUTTER_Z_AXIS,
                                        g_value_get_double (value));
      break;

    case PROP_TRANSFORM:
      clutter_actor_set_transform (actor, g_value_get_boxed (value));
      break;

    case PROP_CHILD_TRANSFORM:
      clutter_actor_set_child_transform (actor, g_value_get_boxed (value));
      break;

    case PROP_SHOW_ON_SET_PARENT: /* XXX:2.0 - remove */
      priv->show_on_set_parent = g_value_get_boolean (value);
      break;

    case PROP_TEXT_DIRECTION:
      clutter_actor_set_text_direction (actor, g_value_get_enum (value));
      break;

    case PROP_ACTIONS:
      clutter_actor_add_action (actor, g_value_get_object (value));
      break;

    case PROP_CONSTRAINTS:
      clutter_actor_add_constraint (actor, g_value_get_object (value));
      break;

    case PROP_EFFECT:
      clutter_actor_add_effect (actor, g_value_get_object (value));
      break;

    case PROP_LAYOUT_MANAGER:
      clutter_actor_set_layout_manager (actor, g_value_get_object (value));
      break;

    case PROP_X_EXPAND:
      clutter_actor_set_x_expand (actor, g_value_get_boolean (value));
      break;

    case PROP_Y_EXPAND:
      clutter_actor_set_y_expand (actor, g_value_get_boolean (value));
      break;

    case PROP_X_ALIGN:
      clutter_actor_set_x_align (actor, g_value_get_enum (value));
      break;

    case PROP_Y_ALIGN:
      clutter_actor_set_y_align (actor, g_value_get_enum (value));
      break;

    case PROP_MARGIN_TOP:
      clutter_actor_set_margin_top (actor, g_value_get_float (value));
      break;

    case PROP_MARGIN_BOTTOM:
      clutter_actor_set_margin_bottom (actor, g_value_get_float (value));
      break;

    case PROP_MARGIN_LEFT:
      clutter_actor_set_margin_left (actor, g_value_get_float (value));
      break;

    case PROP_MARGIN_RIGHT:
      clutter_actor_set_margin_right (actor, g_value_get_float (value));
      break;

    case PROP_BACKGROUND_COLOR:
      clutter_actor_set_background_color (actor, g_value_get_boxed (value));
      break;

    case PROP_CONTENT:
      clutter_actor_set_content (actor, g_value_get_object (value));
      break;

    case PROP_CONTENT_GRAVITY:
      clutter_actor_set_content_gravity (actor, g_value_get_enum (value));
      break;

    case PROP_MINIFICATION_FILTER:
      clutter_actor_set_content_scaling_filters (actor,
                                                 g_value_get_enum (value),
                                                 actor->priv->mag_filter);
      break;

    case PROP_MAGNIFICATION_FILTER:
      clutter_actor_set_content_scaling_filters (actor,
                                                 actor->priv->min_filter,
                                                 g_value_get_enum (value));
      break;

    case PROP_CONTENT_REPEAT:
      clutter_actor_set_content_repeat (actor, g_value_get_flags (value));
      break;

    case PROP_COLOR_STATE:
      clutter_actor_set_color_state_internal (actor, g_value_get_object (value));
      break;

    case PROP_ACCESSIBLE_ROLE:
      clutter_actor_set_accessible_role (actor, g_value_get_enum (value));
      break;

    case PROP_ACCESSIBLE_NAME:
      clutter_actor_set_accessible_name (actor, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_actor_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  ClutterActor *actor = CLUTTER_ACTOR (object);
  ClutterActorPrivate *priv = actor->priv;

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, priv->context);
      break;

    case PROP_X:
      g_value_set_float (value, clutter_actor_get_x (actor));
      break;

    case PROP_Y:
      g_value_set_float (value, clutter_actor_get_y (actor));
      break;

    case PROP_POSITION:
      {
        graphene_point_t position;

        graphene_point_init (&position,
                             clutter_actor_get_x (actor),
                             clutter_actor_get_y (actor));
        g_value_set_boxed (value, &position);
      }
      break;

    case PROP_WIDTH:
      g_value_set_float (value, clutter_actor_get_width (actor));
      break;

    case PROP_HEIGHT:
      g_value_set_float (value, clutter_actor_get_height (actor));
      break;

    case PROP_SIZE:
      {
        graphene_size_t size;

        graphene_size_init (&size,
                            clutter_actor_get_width (actor),
                            clutter_actor_get_height (actor));
        g_value_set_boxed (value, &size);
      }
      break;

    case PROP_FIXED_X:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->fixed_pos.x);
      }
      break;

    case PROP_FIXED_Y:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->fixed_pos.y);
      }
      break;

    case PROP_FIXED_POSITION_SET:
      g_value_set_boolean (value, priv->position_set);
      break;

    case PROP_MIN_WIDTH:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->minimum.width);
      }
      break;

    case PROP_MIN_HEIGHT:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->minimum.height);
      }
      break;

    case PROP_NATURAL_WIDTH:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->natural.width);
      }
      break;

    case PROP_NATURAL_HEIGHT:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->natural.height);
      }
      break;

    case PROP_MIN_WIDTH_SET:
      g_value_set_boolean (value, priv->min_width_set);
      break;

    case PROP_MIN_HEIGHT_SET:
      g_value_set_boolean (value, priv->min_height_set);
      break;

    case PROP_NATURAL_WIDTH_SET:
      g_value_set_boolean (value, priv->natural_width_set);
      break;

    case PROP_NATURAL_HEIGHT_SET:
      g_value_set_boolean (value, priv->natural_height_set);
      break;

    case PROP_REQUEST_MODE:
      g_value_set_enum (value, priv->request_mode);
      break;

    case PROP_ALLOCATION:
      g_value_set_boxed (value, &priv->allocation);
      break;

    case PROP_Z_POSITION:
      g_value_set_float (value, clutter_actor_get_z_position (actor));
      break;

    case PROP_OPACITY:
      g_value_set_uint (value, priv->opacity);
      break;

    case PROP_OFFSCREEN_REDIRECT:
      g_value_set_flags (value, priv->offscreen_redirect);
      break;

    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;

    case PROP_VISIBLE:
      g_value_set_boolean (value, clutter_actor_is_visible (actor));
      break;

    case PROP_MAPPED:
      g_value_set_boolean (value, clutter_actor_is_mapped (actor));
      break;

    case PROP_REALIZED:
      g_value_set_boolean (value, clutter_actor_is_realized (actor));
      break;

    case PROP_HAS_CLIP:
      g_value_set_boolean (value, priv->has_clip);
      break;

    case PROP_CLIP_RECT:
      g_value_set_boxed (value, &priv->clip);
      break;

    case PROP_CLIP_TO_ALLOCATION:
      g_value_set_boolean (value, priv->clip_to_allocation);
      break;

    case PROP_PIVOT_POINT:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_boxed (value, &info->pivot);
      }
      break;

    case PROP_PIVOT_POINT_Z:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_float (value, info->pivot_z);
      }
      break;

    case PROP_TRANSLATION_X:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_float (value, info->translation.x);
      }
      break;

    case PROP_TRANSLATION_Y:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_float (value, info->translation.y);
      }
      break;

    case PROP_TRANSLATION_Z:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_float (value, info->translation.z);
      }
      break;

    case PROP_SCALE_X:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->scale_x);
      }
      break;

    case PROP_SCALE_Y:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->scale_y);
      }
      break;

    case PROP_SCALE_Z:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->scale_z);
      }
      break;

    case PROP_REACTIVE:
      g_value_set_boolean (value, clutter_actor_get_reactive (actor));
      break;

    case PROP_ROTATION_ANGLE_X:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->rx_angle);
      }
      break;

    case PROP_ROTATION_ANGLE_Y:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->ry_angle);
      }
      break;

    case PROP_ROTATION_ANGLE_Z:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_double (value, info->rz_angle);
      }
      break;

    case PROP_TRANSFORM:
      {
        graphene_matrix_t m;

        clutter_actor_get_transform (actor, &m);
        g_value_set_boxed (value, &m);
      }
      break;

    case PROP_TRANSFORM_SET:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_boolean (value, info->transform_set);
      }
      break;

    case PROP_CHILD_TRANSFORM:
      {
        graphene_matrix_t m;

        clutter_actor_get_child_transform (actor, &m);
        g_value_set_boxed (value, &m);
      }
      break;

    case PROP_CHILD_TRANSFORM_SET:
      {
        const ClutterTransformInfo *info;

        info = _clutter_actor_get_transform_info_or_defaults (actor);
        g_value_set_boolean (value, info->child_transform_set);
      }
      break;

    case PROP_SHOW_ON_SET_PARENT: /* XXX:2.0 - remove */
      g_value_set_boolean (value, priv->show_on_set_parent);
      break;

    case PROP_TEXT_DIRECTION:
      g_value_set_enum (value, priv->text_direction);
      break;

    case PROP_HAS_POINTER:
      g_value_set_boolean (value, priv->n_pointers > 0);
      break;

    case PROP_LAYOUT_MANAGER:
      g_value_set_object (value, priv->layout_manager);
      break;

    case PROP_X_EXPAND:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_boolean (value, info->x_expand);
      }
      break;

    case PROP_Y_EXPAND:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_boolean (value, info->y_expand);
      }
      break;

    case PROP_X_ALIGN:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_enum (value, info->x_align);
      }
      break;

    case PROP_Y_ALIGN:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_enum (value, info->y_align);
      }
      break;

    case PROP_MARGIN_TOP:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->margin.top);
      }
      break;

    case PROP_MARGIN_BOTTOM:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->margin.bottom);
      }
      break;

    case PROP_MARGIN_LEFT:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->margin.left);
      }
      break;

    case PROP_MARGIN_RIGHT:
      {
        const ClutterLayoutInfo *info;

        info = _clutter_actor_get_layout_info_or_defaults (actor);
        g_value_set_float (value, info->margin.right);
      }
      break;

    case PROP_BACKGROUND_COLOR_SET:
      g_value_set_boolean (value, priv->bg_color_set);
      break;

    case PROP_BACKGROUND_COLOR:
      g_value_set_boxed (value, &priv->bg_color);
      break;

    case PROP_FIRST_CHILD:
      g_value_set_object (value, priv->first_child);
      break;

    case PROP_LAST_CHILD:
      g_value_set_object (value, priv->last_child);
      break;

    case PROP_CONTENT:
      g_value_set_object (value, priv->content);
      break;

    case PROP_CONTENT_GRAVITY:
      g_value_set_enum (value, priv->content_gravity);
      break;

    case PROP_CONTENT_BOX:
      {
        ClutterActorBox box = { 0, };

        clutter_actor_get_content_box (actor, &box);
        g_value_set_boxed (value, &box);
      }
      break;

    case PROP_MINIFICATION_FILTER:
      g_value_set_enum (value, priv->min_filter);
      break;

    case PROP_MAGNIFICATION_FILTER:
      g_value_set_enum (value, priv->mag_filter);
      break;

    case PROP_CONTENT_REPEAT:
      g_value_set_flags (value, priv->content_repeat);
      break;

    case PROP_COLOR_STATE:
      g_value_set_object (value, priv->color_state);
      break;

    case PROP_ACCESSIBLE_ROLE:
      g_value_set_enum (value, clutter_actor_get_accessible_role (actor));
      break;

    case PROP_ACCESSIBLE_NAME:
      g_value_set_string (value, priv->accessible_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_actor_dispose (GObject *object)
{
  ClutterActor *self = CLUTTER_ACTOR (object);
  ClutterActorPrivate *priv = self->priv;
  ClutterContext *context = clutter_actor_get_context (self);
  ClutterBackend *backend = clutter_context_get_backend (context);

  CLUTTER_NOTE (MISC, "Dispose actor (name='%s', ref_count:%d) of type '%s'",
                _clutter_actor_get_debug_name (self),
                object->ref_count,
                g_type_name (G_OBJECT_TYPE (self)));

  maybe_unset_key_focus (self);

  /* Stop the emission of any property change */
  g_object_freeze_notify (object);

  g_signal_emit (self, actor_signals[DESTROY], 0);

  /* avoid recursing when called from clutter_actor_destroy() */
  if (priv->parent != NULL)
    {
      ClutterActor *parent = priv->parent;
      clutter_actor_remove_child (parent, self);
    }

  /* parent must be gone at this point */
  g_assert (priv->parent == NULL);

  if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      /* can't be mapped or realized with no parent */
      g_assert (!clutter_actor_is_mapped (self));
      g_assert (!clutter_actor_is_realized (self));
    }

  g_clear_signal_handler (&priv->resolution_changed_id, backend);
  g_clear_signal_handler (&priv->font_changed_id, backend);

  g_clear_pointer (&priv->accessible_name, g_free);

#ifdef HAVE_FONTS
  g_clear_object (&priv->pango_context);
#endif
  g_clear_object (&priv->actions);
  g_clear_object (&priv->color_state);
  g_clear_object (&priv->constraints);
  g_clear_object (&priv->effects);
  g_clear_object (&priv->flatten_effect);

  if (priv->child_model != NULL)
    {
      if (priv->create_child_notify != NULL)
        priv->create_child_notify (priv->create_child_data);

      priv->create_child_func = NULL;
      priv->create_child_data = NULL;
      priv->create_child_notify = NULL;

      g_clear_object (&priv->child_model);
    }

  if (priv->layout_manager != NULL)
    {
      g_clear_signal_handler (&priv->layout_changed_id, priv->layout_manager);
      clutter_layout_manager_set_container (priv->layout_manager, NULL);
      g_clear_object (&priv->layout_manager);
    }

  if (priv->content != NULL)
    {
      _clutter_content_detached (priv->content, self);
      g_clear_object (&priv->content);
    }

  g_clear_pointer (&priv->clones, g_hash_table_unref);
  g_clear_pointer (&priv->stage_views, g_list_free);
  g_clear_pointer (&priv->next_redraw_clips, g_array_unref);

  G_OBJECT_CLASS (clutter_actor_parent_class)->dispose (object);
}

static void
clutter_actor_finalize (GObject *object)
{
  ClutterActorPrivate *priv = CLUTTER_ACTOR (object)->priv;

  CLUTTER_NOTE (MISC, "Finalize actor (name='%s') of type '%s'",
                _clutter_actor_get_debug_name ((ClutterActor *) object),
                g_type_name (G_OBJECT_TYPE (object)));

  /* No new grabs should have happened after unrealizing */
  g_assert (priv->grabs == NULL);
  g_free (priv->name);

  g_free (priv->debug_name);
  g_clear_object (&priv->accessible_state);

  G_OBJECT_CLASS (clutter_actor_parent_class)->finalize (object);
}


/**
 * clutter_actor_get_accessible:
 * @self: a #ClutterActor
 *
 * Returns the accessible object that describes the actor to an
 * assistive technology.
 *
 * If no class-specific #AtkObject implementation is available for the
 * actor instance in question, it will inherit an #AtkObject
 * implementation from the first ancestor class for which such an
 * implementation is defined.
 *
 * The documentation of the [https://gnome.pages.gitlab.gnome.org/at-spi2-core/atk/](ATK)
 * library contains more information about accessible objects and
 * their uses.
 *
 * Returns: (transfer none): the #AtkObject associated with @actor
 */
AtkObject *
clutter_actor_get_accessible (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return CLUTTER_ACTOR_GET_CLASS (self)->get_accessible (self);
}

static AtkObject *
clutter_actor_real_get_accessible (ClutterActor *actor)
{
  ClutterActorPrivate *priv = actor->priv;

  if (priv->accessible == NULL)
    {
        if (!clutter_get_accessibility_enabled ())
          return NULL;

        priv->accessible =
          g_object_new (CLUTTER_ACTOR_GET_CLASS (actor)->get_accessible_type (),
                        NULL);

        atk_object_initialize (priv->accessible, actor);
        /* AtkGObjectAccessible, which ClutterActorAccessible derives from, clears
         * the back reference to the object in a weak notify for the object;
         * weak-ref notification, which occurs during g_object_real_dispose(),
         * is then the optimal time to clear the forward reference. We
         * can't clear the reference in dispose() before chaining up, since
         * clutter_actor_dispose() causes notifications to be sent out, which
         * will result in a new accessible object being created.
         */
        g_object_add_weak_pointer (G_OBJECT (actor),
                                   (gpointer *)&priv->accessible);

    }

  return priv->accessible;
}

static AtkObject *
_clutter_actor_ref_accessible (AtkImplementor *implementor)
{
  AtkObject *accessible;

  accessible = clutter_actor_get_accessible (CLUTTER_ACTOR (implementor));
  if (accessible != NULL)
    g_object_ref (accessible);

  return accessible;
}

static void
atk_implementor_iface_init (AtkImplementorIface *iface)
{
  iface->ref_accessible = _clutter_actor_ref_accessible;
}

static gboolean
clutter_actor_real_get_paint_volume (ClutterActor       *self,
                                     ClutterPaintVolume *volume)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *child;

  /* this should be checked before we call this function, but it's a
   * good idea to be explicit when it costs us nothing
   */
  if (priv->needs_allocation)
    return FALSE;

  if (priv->has_clip)
    {
      graphene_point3d_t origin;

      origin.x = priv->clip.origin.x;
      origin.y = priv->clip.origin.y;
      origin.z = 0;

      clutter_paint_volume_set_origin (volume, &origin);
      clutter_paint_volume_set_width (volume, priv->clip.size.width);
      clutter_paint_volume_set_height (volume, priv->clip.size.height);

      return TRUE;
    }

  /* we start from the allocation */
  clutter_paint_volume_set_width (volume,
                                  priv->allocation.x2 - priv->allocation.x1);
  clutter_paint_volume_set_height (volume,
                                   priv->allocation.y2 - priv->allocation.y1);

  /* if the actor has a clip set then we have a pretty definite
   * size for the paint volume: the actor cannot possibly paint
   * outside the clip region.
   */
  if (priv->clip_to_allocation)
    return TRUE;

  /* if we don't have children we just bail out here... */
  if (priv->n_children == 0)
    return TRUE;

  /* ...but if we have children then we ask for their paint volume in
   * our coordinates. if any of our children replies that it doesn't
   * have a paint volume, we bail out
   */
  for (child = priv->first_child;
       child != NULL;
       child = child->priv->next_sibling)
    {
      g_autoptr (ClutterPaintVolume) child_volume = NULL;

      /* we ignore unmapped children, since they won't be painted.
       *
       * XXX: we also have to ignore mapped children without a valid
       * allocation, because apparently some code above Clutter allows
       * them.
       */
      if ((!clutter_actor_is_mapped (child) &&
           !clutter_actor_has_mapped_clones (child)) ||
          !clutter_actor_has_allocation (child))
        continue;

      child_volume = clutter_actor_get_transformed_paint_volume (child, self);
      if (child_volume == NULL)
        return FALSE;

      clutter_paint_volume_union (volume, child_volume);
    }

  return TRUE;
}

static gboolean
clutter_actor_real_has_overlaps (ClutterActor *self)
{
  /* By default we'll assume that all actors need an offscreen redirect to get
   * the correct opacity. Actors such as ClutterTexture that would never need
   * an offscreen redirect can override this to return FALSE. */
  return TRUE;
}

static float
clutter_actor_real_calculate_resource_scale (ClutterActor *self,
                                             int           phase)
{
  GList *l;
  float new_resource_scale = -1.f;

  for (l = clutter_actor_peek_stage_views (self); l; l = l->next)
    {
      ClutterStageView *view = l->data;

      new_resource_scale = MAX (clutter_stage_view_get_scale (view),
                                new_resource_scale);
    }

  return new_resource_scale;
}

static void
clutter_actor_real_destroy (ClutterActor *actor)
{
  clutter_actor_destroy_all_children (actor);
}

static GObject *
clutter_actor_constructor (GType gtype,
                           guint n_props,
                           GObjectConstructParam *props)
{
  GObjectClass *gobject_class;
  ClutterActor *self;
  GObject *retval;

  gobject_class = G_OBJECT_CLASS (clutter_actor_parent_class);
  retval = gobject_class->constructor (gtype, n_props, props);
  self = CLUTTER_ACTOR (retval);

  if (self->priv->layout_manager == NULL)
    {
      ClutterActorClass *actor_class;
      ClutterLayoutManager *default_layout;
      GType layout_manager_type;

      actor_class = CLUTTER_ACTOR_GET_CLASS (self);

      layout_manager_type = clutter_actor_class_get_layout_manager_type (actor_class);
      if (layout_manager_type == G_TYPE_INVALID)
        layout_manager_type = CLUTTER_TYPE_FIXED_LAYOUT;

      CLUTTER_NOTE (LAYOUT, "Creating default layout manager");

      default_layout = g_object_new (layout_manager_type, NULL);
      clutter_actor_set_layout_manager (self, default_layout);
    }

  if (!self->priv->context)
    self->priv->context = _clutter_context_get_default ();

  if (!self->priv->color_state)
    clutter_actor_unset_color_state (self);

  return retval;
}

static void
clutter_actor_class_init (ClutterActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  quark_actor_layout_info = g_quark_from_static_string ("-clutter-actor-layout-info");
  quark_actor_transform_info = g_quark_from_static_string ("-clutter-actor-transform-info");
  quark_actor_animation_info = g_quark_from_static_string ("-clutter-actor-animation-info");

  quark_key = g_quark_from_static_string ("key");
  quark_motion = g_quark_from_static_string ("motion");
  quark_pointer_focus = g_quark_from_static_string ("pointer-focus");
  quark_button = g_quark_from_static_string ("button");
  quark_scroll = g_quark_from_static_string ("scroll");
  quark_stage = g_quark_from_static_string ("stage");
  quark_touch = g_quark_from_static_string ("touch");
  quark_touchpad = g_quark_from_static_string ("touchpad");
  quark_proximity = g_quark_from_static_string ("proximity");
  quark_pad = g_quark_from_static_string ("pad");
  quark_im = g_quark_from_static_string ("im");

  object_class->constructor = clutter_actor_constructor;
  object_class->set_property = clutter_actor_set_property;
  object_class->get_property = clutter_actor_get_property;
  object_class->dispose = clutter_actor_dispose;
  object_class->finalize = clutter_actor_finalize;

  klass->show = clutter_actor_real_show;
  klass->hide = clutter_actor_real_hide;
  klass->hide_all = clutter_actor_hide;
  klass->map = clutter_actor_real_map;
  klass->unmap = clutter_actor_real_unmap;
  klass->unrealize = clutter_actor_real_unrealize;
  klass->pick = clutter_actor_real_pick;
  klass->get_preferred_width = clutter_actor_real_get_preferred_width;
  klass->get_preferred_height = clutter_actor_real_get_preferred_height;
  klass->allocate = clutter_actor_real_allocate;
  klass->queue_relayout = clutter_actor_real_queue_relayout;
  klass->apply_transform = clutter_actor_real_apply_transform;
  klass->get_accessible = clutter_actor_real_get_accessible;
  klass->get_accessible_type = clutter_actor_accessible_get_type;
  klass->get_paint_volume = clutter_actor_real_get_paint_volume;
  klass->has_overlaps = clutter_actor_real_has_overlaps;
  klass->calculate_resource_scale = clutter_actor_real_calculate_resource_scale;
  klass->paint = clutter_actor_real_paint;
  klass->destroy = clutter_actor_real_destroy;

  klass->layout_manager_type = G_TYPE_INVALID;

  /**
   * ClutterActor:context:
   *
   * The %ClutterContext of the actor
   */
  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         CLUTTER_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:x:
   *
   * X coordinate of the actor in pixels. If written, forces a fixed
   * position for the actor. If read, returns the fixed position if any,
   * otherwise the allocation if available, otherwise 0.
   *
   * The [property@Clutter.Actor:x] property is animatable.
   */
  obj_props[PROP_X] =
    g_param_spec_float ("x", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:y:
   *
   * Y coordinate of the actor in pixels. If written, forces a fixed
   * position for the actor.  If read, returns the fixed position if
   * any, otherwise the allocation if available, otherwise 0.
   *
   * The [property@Clutter.Actor:y] property is animatable.
   */
  obj_props[PROP_Y] =
    g_param_spec_float ("y", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:position:
   *
   * The position of the origin of the actor.
   *
   * This property is a shorthand for setting and getting the
   * [property@Clutter.Actor:x] and [property@Clutter.Actor:y] properties at the same
   * time.
   *
   * The [property@Clutter.Actor:position] property is animatable.
   */
  obj_props[PROP_POSITION] =
    g_param_spec_boxed ("position", NULL, NULL,
                        GRAPHENE_TYPE_POINT,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:width:
   *
   * Width of the actor (in pixels). If written, forces the minimum and
   * natural size request of the actor to the given width. If read, returns
   * the allocated width if available, otherwise the width request.
   *
   * The [property@Clutter.Actor:width] property is animatable.
   */
  obj_props[PROP_WIDTH] =
    g_param_spec_float ("width", NULL, NULL,
                        -1.0f, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:height:
   *
   * Height of the actor (in pixels).  If written, forces the minimum and
   * natural size request of the actor to the given height. If read, returns
   * the allocated height if available, otherwise the height request.
   *
   * The [property@Clutter.Actor:height] property is animatable.
   */
  obj_props[PROP_HEIGHT] =
    g_param_spec_float ("height", NULL, NULL,
                        -1.0f, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:size:
   *
   * The size of the actor.
   *
   * This property is a shorthand for setting and getting the
   * [property@Clutter.Actor:width] and [property@Clutter.Actor:height]
   * at the same time.
   *
   * The [property@Clutter.Actor:size] property is animatable.
   */
  obj_props[PROP_SIZE] =
    g_param_spec_boxed ("size", NULL, NULL,
                        GRAPHENE_TYPE_SIZE,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:fixed-x:
   *
   * The fixed X position of the actor in pixels.
   *
   * Writing this property sets [property@Clutter.Actor:fixed-position-set]
   * property as well, as a side effect
   */
  obj_props[PROP_FIXED_X] =
    g_param_spec_float ("fixed-x", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:fixed-y:
   *
   * The fixed Y position of the actor in pixels.
   *
   * Writing this property sets the [property@Clutter.Actor:fixed-position-set]
   * property as well, as a side effect
   */
  obj_props[PROP_FIXED_Y] =
    g_param_spec_float ("fixed-y", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:fixed-position-set:
   *
   * This flag controls whether the [property@Clutter.Actor:fixed-x] and
   * [property@Clutter.Actor:fixed-y] properties are used
   */
  obj_props[PROP_FIXED_POSITION_SET] =
    g_param_spec_boolean ("fixed-position-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:min-width:
   *
   * A forced minimum width request for the actor, in pixels
   *
   * Writing this property sets the [property@Clutter.Actor:min-width-set] property
   * as well, as a side effect.
   *
   *This property overrides the usual width request of the actor.
   */
  obj_props[PROP_MIN_WIDTH] =
    g_param_spec_float ("min-width", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:min-height:
   *
   * A forced minimum height request for the actor, in pixels
   *
   * Writing this property sets the [property@Clutter.Actor:min-height-set] property
   * as well, as a side effect. This property overrides the usual height
   * request of the actor.
   */
  obj_props[PROP_MIN_HEIGHT] =
    g_param_spec_float ("min-height", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:natural-width:
   *
   * A forced natural width request for the actor, in pixels
   *
   * Writing this property sets the [property@Clutter.Actor:natural-width-set]
   * property as well, as a side effect. This property overrides the
   * usual width request of the actor
   */
  obj_props[PROP_NATURAL_WIDTH] =
    g_param_spec_float ("natural-width", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:natural-height:
   *
   * A forced natural height request for the actor, in pixels
   *
   * Writing this property sets the [property@Clutter.Actor:natural-height-set]
   * property as well, as a side effect. This property overrides the
   * usual height request of the actor
   */
  obj_props[PROP_NATURAL_HEIGHT] =
    g_param_spec_float ("natural-height", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:min-width-set:
   *
   * This flag controls whether the [property@Clutter.Actor:min-width] property
   * is used
   */
  obj_props[PROP_MIN_WIDTH_SET] =
    g_param_spec_boolean ("min-width-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:min-height-set:
   *
   * This flag controls whether the [property@Clutter.Actor:min-height] property
   * is used
   */
  obj_props[PROP_MIN_HEIGHT_SET] =
    g_param_spec_boolean ("min-height-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:natural-width-set:
   *
   * This flag controls whether the [property@Clutter.Actor:natural-width] property
   * is used
   */
  obj_props[PROP_NATURAL_WIDTH_SET] =
    g_param_spec_boolean ("natural-width-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:natural-height-set:
   *
   * This flag controls whether the [property@Clutter.Actor:natural-height] property
   * is used
   */
  obj_props[PROP_NATURAL_HEIGHT_SET] =
    g_param_spec_boolean ("natural-height-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:allocation:
   *
   * The allocation for the actor, in pixels
   *
   * This is property is read-only, but you might monitor it to know when an
   * actor moves or resizes
   */
  obj_props[PROP_ALLOCATION] =
    g_param_spec_boxed ("allocation", NULL, NULL,
                        CLUTTER_TYPE_ACTOR_BOX,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:request-mode:
   *
   * Request mode for the #ClutterActor. The request mode determines the
   * type of geometry management used by the actor, either height for width
   * (the default) or width for height.
   *
   * For actors implementing height for width, the parent container should get
   * the preferred width first, and then the preferred height for that width.
   *
   * For actors implementing width for height, the parent container should get
   * the preferred height first, and then the preferred width for that height.
   *
   * For instance:
   *
   * ```c
   *   ClutterRequestMode mode;
   *   gfloat natural_width, min_width;
   *   gfloat natural_height, min_height;
   *
   *   mode = clutter_actor_get_request_mode (child);
   *   if (mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
   *     {
   *       clutter_actor_get_preferred_width (child, -1,
   *                                          &min_width,
   *                                          &natural_width);
   *       clutter_actor_get_preferred_height (child, natural_width,
   *                                           &min_height,
   *                                           &natural_height);
   *     }
   *   else if (mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
   *     {
   *       clutter_actor_get_preferred_height (child, -1,
   *                                           &min_height,
   *                                           &natural_height);
   *       clutter_actor_get_preferred_width (child, natural_height,
   *                                          &min_width,
   *                                          &natural_width);
   *     }
   *   else if (mode == CLUTTER_REQUEST_CONTENT_SIZE)
   *     {
   *       ClutterContent *content = clutter_actor_get_content (child);
   *
   *       min_width, min_height = 0;
   *       natural_width = natural_height = 0;
   *
   *       if (content != NULL)
   *         clutter_content_get_preferred_size (content, &natural_width, &natural_height);
   *     }
   * ```
   *
   * will retrieve the minimum and natural width and height depending on the
   * preferred request mode of the #ClutterActor "child".
   *
   * The [method@Clutter.Actor.get_preferred_size] function will implement this
   * check for you.
   */
  obj_props[PROP_REQUEST_MODE] =
    g_param_spec_enum ("request-mode", NULL, NULL,
                       CLUTTER_TYPE_REQUEST_MODE,
                       CLUTTER_REQUEST_HEIGHT_FOR_WIDTH,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:z-position:
   *
   * The actor's position on the Z axis, relative to the parent's
   * transformations.
   *
   * Positive values will bring the actor's position nearer to the user,
   * whereas negative values will bring the actor's position farther from
   * the user.
   *
   * The [property@Clutter.Actor:z-position] does not affect the paint or allocation
   * order.
   *
   * The [property@Clutter.Actor:z-position] property is animatable.
   */
  obj_props[PROP_Z_POSITION] =
    g_param_spec_float ("z-position", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.0f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:opacity:
   *
   * Opacity of an actor, between 0 (fully transparent) and
   * 255 (fully opaque)
   *
   * The [property@Clutter.Actor:opacity] property is animatable.
   */
  obj_props[PROP_OPACITY] =
    g_param_spec_uint ("opacity", NULL, NULL,
                       0, 255,
                       255,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY |
                       CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:offscreen-redirect:
   *
   * Determines the conditions in which the actor will be redirected
   * to an offscreen framebuffer while being painted. For example this
   * can be used to cache an actor in a framebuffer or for improved
   * handling of transparent actors. See
   * clutter_actor_set_offscreen_redirect() for details.
   */
  obj_props[PROP_OFFSCREEN_REDIRECT] =
    g_param_spec_flags ("offscreen-redirect", NULL, NULL,
                        CLUTTER_TYPE_OFFSCREEN_REDIRECT,
                        0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  /**
   * ClutterActor:visible:
   *
   * Whether the actor is set to be visible or not
   *
   * See also [property@Clutter.Actor:mapped]
   */
  obj_props[PROP_VISIBLE] =
    g_param_spec_boolean ("visible", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:mapped:
   *
   * Whether the actor is mapped (will be painted when the stage
   * to which it belongs is mapped)
   */
  obj_props[PROP_MAPPED] =
    g_param_spec_boolean ("mapped", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:realized:
   *
   * Whether the actor has been realized
   */
  obj_props[PROP_REALIZED] =
    g_param_spec_boolean ("realized", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:reactive:
   *
   * Whether the actor is reactive to events or not
   *
   * Only reactive actors will emit event-related signals
   */
  obj_props[PROP_REACTIVE] =
    g_param_spec_boolean ("reactive", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:has-clip:
   *
   * Whether the actor has the [property@Clutter.Actor:clip-rect] property set or not
   */
  obj_props[PROP_HAS_CLIP] =
    g_param_spec_boolean ("has-clip", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:clip-rect:
   *
   * The visible region of the actor, in actor-relative coordinates,
   * expressed as a #graphene_rect_t.
   *
   * Setting this property to %NULL will unset the existing clip.
   *
   * Setting this property will change the [property@Clutter.Actor:has-clip]
   * property as a side effect.
   */
  obj_props[PROP_CLIP_RECT] =
    g_param_spec_boxed ("clip-rect", NULL, NULL,
                        GRAPHENE_TYPE_RECT,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:name:
   *
   * The name of the actor
   */
  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:pivot-point:
   *
   * The point around which the scaling and rotation transformations occur.
   *
   * The pivot point is expressed in normalized coordinates space, with (0, 0)
   * being the top left corner of the actor and (1, 1) the bottom right corner
   * of the actor.
   *
   * The default pivot point is located at (0, 0).
   *
   * The [property@Clutter.Actor:pivot-point] property is animatable.
   */
  obj_props[PROP_PIVOT_POINT] =
    g_param_spec_boxed ("pivot-point", NULL, NULL,
                        GRAPHENE_TYPE_POINT,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:pivot-point-z:
   *
   * The Z component of the [property@Clutter.Actor:pivot-point], expressed as a value
   * along the Z axis.
   *
   * The [property@Clutter.Actor:pivot-point-z] property is animatable.
   */
  obj_props[PROP_PIVOT_POINT_Z] =
    g_param_spec_float ("pivot-point-z", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:scale-x:
   *
   * The horizontal scale of the actor.
   *
   * The [property@Clutter.Actor:scale-x] property is animatable.
   */
  obj_props[PROP_SCALE_X] =
    g_param_spec_double ("scale-x", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         1.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:scale-y:
   *
   * The vertical scale of the actor.
   *
   * The [property@Clutter.Actor:scale-y] property is animatable.
   */
  obj_props[PROP_SCALE_Y] =
    g_param_spec_double ("scale-y", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         1.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:scale-z:
   *
   * The scale factor of the actor along the Z axis.
   *
   * The [property@Clutter.Actor:scale-y] property is animatable.
   */
  obj_props[PROP_SCALE_Z] =
    g_param_spec_double ("scale-z", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         1.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:rotation-angle-x:
   *
   * The rotation angle on the X axis.
   *
   * The [property@Clutter.Actor:rotation-angle-x] property is animatable.
   */
  obj_props[PROP_ROTATION_ANGLE_X] =
    g_param_spec_double ("rotation-angle-x", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         0.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:rotation-angle-y:
   *
   * The rotation angle on the Y axis
   *
   * The [property@Clutter.Actor:rotation-angle-y] property is animatable.
   */
  obj_props[PROP_ROTATION_ANGLE_Y] =
    g_param_spec_double ("rotation-angle-y", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         0.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:rotation-angle-z:
   *
   * The rotation angle on the Z axis
   *
   * The [property@Clutter.Actor:rotation-angle-z] property is animatable.
   */
  obj_props[PROP_ROTATION_ANGLE_Z] =
    g_param_spec_double ("rotation-angle-z", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE,
                         0.0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:translation-x:
   *
   * An additional translation applied along the X axis, relative
   * to the actor's [property@Clutter.Actor:pivot-point].
   *
   * The [property@Clutter.Actor:translation-x] property is animatable.
   */
  obj_props[PROP_TRANSLATION_X] =
    g_param_spec_float ("translation-x", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:translation-y:
   *
   * An additional translation applied along the Y axis, relative
   * to the actor's [property@Clutter.Actor:pivot-point].
   *
   * The [property@Clutter.Actor:translation-y] property is animatable.
   */
  obj_props[PROP_TRANSLATION_Y] =
    g_param_spec_float ("translation-y", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:translation-z:
   *
   * An additional translation applied along the Z axis, relative
   * to the actor's [property@Clutter.Actor:pivot-point].
   *
   * The [property@Clutter.Actor:translation-z] property is animatable.
   */
  obj_props[PROP_TRANSLATION_Z] =
    g_param_spec_float ("translation-z", NULL, NULL,
                        -G_MAXFLOAT, G_MAXFLOAT,
                        0.f,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:transform:
   *
   * Overrides the transformations of a #ClutterActor with a custom
   * matrix.
   *
   * The matrix specified by the [property@Clutter.Actor:transform] property is
   * applied to the actor and its children relative to the actor's
   * [property@Clutter.Actor:allocation] and
   * [property@Clutter.Actor:pivot-point].
   *
   * Application code should rarely need to use this function directly.
   *
   * Setting this property with a #graphene_matrix_t will set the
   * [property@Clutter.Actor:transform-set] property to %TRUE as a side effect;
   * setting this property with %NULL will set the
   * [property@Clutter.Actor:transform-set] property to %FALSE.
   *
   * The [property@Clutter.Actor:transform] property is animatable.
   */
  obj_props[PROP_TRANSFORM] =
    g_param_spec_boxed ("transform", NULL, NULL,
                        GRAPHENE_TYPE_MATRIX,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:transform-set:
   *
   * Whether the [property@Clutter.Actor:transform] property is set.
   */
  obj_props[PROP_TRANSFORM_SET] =
    g_param_spec_boolean ("transform-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:child-transform:
   *
   * Applies a transformation matrix on each child of an actor.
   *
   * Setting this property with a #graphene_matrix_t will set the
   * [property@Clutter.Actor:child-transform-set] property to %TRUE as a side effect;
   * setting this property with %NULL will set the
   * [property@Clutter.Actor:child-transform-set] property to %FALSE.
   *
   * The [property@Clutter.Actor:child-transform] property is animatable.
   */
  obj_props[PROP_CHILD_TRANSFORM] =
    g_param_spec_boxed ("child-transform", NULL, NULL,
                        GRAPHENE_TYPE_MATRIX,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:child-transform-set:
   *
   * Whether the [property@Clutter.Actor:child-transform] property is set.
   */
  obj_props[PROP_CHILD_TRANSFORM_SET] =
    g_param_spec_boolean ("child-transform-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:show-on-set-parent:
   *
   * If %TRUE, the actor is automatically shown when parented.
   *
   * Calling clutter_actor_hide() on an actor which has not been
   * parented will set this property to %FALSE as a side effect.
   */
  obj_props[PROP_SHOW_ON_SET_PARENT] = /* XXX:2.0 - remove */
    g_param_spec_boolean ("show-on-set-parent", NULL, NULL,
                          TRUE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * ClutterActor:clip-to-allocation:
   *
   * Whether the clip region should track the allocated area
   * of the actor.
   *
   * This property is ignored if a clip area has been explicitly
   * set using clutter_actor_set_clip().
   */
  obj_props[PROP_CLIP_TO_ALLOCATION] =
    g_param_spec_boolean ("clip-to-allocation", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:text-direction:
   *
   * The direction of the text inside a #ClutterActor.
   */
  obj_props[PROP_TEXT_DIRECTION] =
    g_param_spec_enum ("text-direction", NULL, NULL,
                       CLUTTER_TYPE_TEXT_DIRECTION,
                       CLUTTER_TEXT_DIRECTION_LTR,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:has-pointer:
   *
   * Whether the actor contains the pointer of a #ClutterInputDevice
   * or not.
   */
  obj_props[PROP_HAS_POINTER] =
    g_param_spec_boolean ("has-pointer", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:actions:
   *
   * Adds a #ClutterAction to the actor
   */
  obj_props[PROP_ACTIONS] =
    g_param_spec_object ("actions", NULL, NULL,
                         CLUTTER_TYPE_ACTION,
                         G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:constraints:
   *
   * Adds a #ClutterConstraint to the actor
   */
  obj_props[PROP_CONSTRAINTS] =
    g_param_spec_object ("constraints", NULL, NULL,
                         CLUTTER_TYPE_CONSTRAINT,
                         G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:effect:
   *
   * Adds #ClutterEffect to the list of effects be applied on a #ClutterActor
   */
  obj_props[PROP_EFFECT] =
    g_param_spec_object ("effect", NULL, NULL,
                         CLUTTER_TYPE_EFFECT,
                         G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:layout-manager:
   *
   * A delegate object for controlling the layout of the children of
   * an actor.
   */
  obj_props[PROP_LAYOUT_MANAGER] =
    g_param_spec_object ("layout-manager", NULL, NULL,
                         CLUTTER_TYPE_LAYOUT_MANAGER,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:x-expand:
   *
   * Whether a layout manager should assign more space to the actor on
   * the X axis.
   */
  obj_props[PROP_X_EXPAND] =
    g_param_spec_boolean ("x-expand", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:y-expand:
   *
   * Whether a layout manager should assign more space to the actor on
   * the Y axis.
   */
  obj_props[PROP_Y_EXPAND] =
    g_param_spec_boolean ("y-expand", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:x-align:
   *
   * The alignment of an actor on the X axis, if the actor has been given
   * extra space for its allocation. See also the [property@Clutter.Actor:x-expand]
   * property.
   */
  obj_props[PROP_X_ALIGN] =
    g_param_spec_enum ("x-align", NULL, NULL,
                       CLUTTER_TYPE_ACTOR_ALIGN,
                       CLUTTER_ACTOR_ALIGN_FILL,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:y-align:
   *
   * The alignment of an actor on the Y axis, if the actor has been given
   * extra space for its allocation.
   */
  obj_props[PROP_Y_ALIGN] =
    g_param_spec_enum ("y-align", NULL, NULL,
                       CLUTTER_TYPE_ACTOR_ALIGN,
                       CLUTTER_ACTOR_ALIGN_FILL,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:margin-top:
   *
   * The margin (in pixels) from the top of the actor.
   *
   * This property adds a margin to the actor's preferred size; the margin
   * will be automatically taken into account when allocating the actor.
   *
   * The [property@Clutter.Actor:margin-top] property is animatable.
   */
  obj_props[PROP_MARGIN_TOP] =
    g_param_spec_float ("margin-top", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:margin-bottom:
   *
   * The margin (in pixels) from the bottom of the actor.
   *
   * This property adds a margin to the actor's preferred size; the margin
   * will be automatically taken into account when allocating the actor.
   *
   * The [property@Clutter.Actor:margin-bottom] property is animatable.
   */
  obj_props[PROP_MARGIN_BOTTOM] =
    g_param_spec_float ("margin-bottom", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:margin-left:
   *
   * The margin (in pixels) from the left of the actor.
   *
   * This property adds a margin to the actor's preferred size; the margin
   * will be automatically taken into account when allocating the actor.
   *
   * The [property@Clutter.Actor:margin-left] property is animatable.
   */
  obj_props[PROP_MARGIN_LEFT] =
    g_param_spec_float ("margin-left", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:margin-right:
   *
   * The margin (in pixels) from the right of the actor.
   *
   * This property adds a margin to the actor's preferred size; the margin
   * will be automatically taken into account when allocating the actor.
   *
   * The [property@Clutter.Actor:margin-right] property is animatable.
   */
  obj_props[PROP_MARGIN_RIGHT] =
    g_param_spec_float ("margin-right", NULL, NULL,
                        0.0, G_MAXFLOAT,
                        0.0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:background-color-set:
   *
   * Whether the [property@Clutter.Actor:background-color] property has been set.
   */
  obj_props[PROP_BACKGROUND_COLOR_SET] =
    g_param_spec_boolean ("background-color-set", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:background-color:
   *
   * Paints a solid fill of the actor's allocation using the specified
   * color.
   *
   * The [property@Clutter.Actor:background-color] property is animatable.
   */
  obj_props[PROP_BACKGROUND_COLOR] =
    cogl_param_spec_color ("background-color", NULL, NULL,
                           &transparent,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY |
                           CLUTTER_PARAM_ANIMATABLE);

  /**
   * ClutterActor:first-child:
   *
   * The actor's first child.
   */
  obj_props[PROP_FIRST_CHILD] =
    g_param_spec_object ("first-child", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:last-child:
   *
   * The actor's last child.
   */
  obj_props[PROP_LAST_CHILD] =
    g_param_spec_object ("last-child", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:content:
   *
   * The #ClutterContent implementation that controls the content
   * of the actor.
   */
  obj_props[PROP_CONTENT] =
    g_param_spec_object ("content", NULL, NULL,
                         CLUTTER_TYPE_CONTENT,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:content-gravity:
   *
   * The alignment that should be honoured by the #ClutterContent
   * set with the [property@Clutter.Actor:content] property.
   *
   * Changing the value of this property will change the bounding box of
   * the content; you can use the [property@Clutter.Actor:content-box] property to
   * get the position and size of the content within the actor's
   * allocation.
   *
   * This property is meaningful only for #ClutterContent implementations
   * that have a preferred size, and if the preferred size is smaller than
   * the actor's allocation.
   *
   * The [property@Clutter.Actor:content-gravity] property is animatable.
   */
  obj_props[PROP_CONTENT_GRAVITY] =
    g_param_spec_enum ("content-gravity", NULL, NULL,
                       CLUTTER_TYPE_CONTENT_GRAVITY,
                       CLUTTER_CONTENT_GRAVITY_RESIZE_FILL,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:content-box:
   *
   * The bounding box for the #ClutterContent used by the actor.
   *
   * The value of this property is controlled by the [property@Clutter.Actor:allocation]
   * and [property@Clutter.Actor:content-gravity] properties of #ClutterActor.
   *
   * The bounding box for the content is guaranteed to never exceed the
   * allocation's of the actor.
   */
  obj_props[PROP_CONTENT_BOX] =
    g_param_spec_boxed ("content-box", NULL, NULL,
                        CLUTTER_TYPE_ACTOR_BOX,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY |
                        CLUTTER_PARAM_ANIMATABLE);

  obj_props[PROP_MINIFICATION_FILTER] =
    g_param_spec_enum ("minification-filter", NULL, NULL,
                       CLUTTER_TYPE_SCALING_FILTER,
                       CLUTTER_SCALING_FILTER_LINEAR,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  obj_props[PROP_MAGNIFICATION_FILTER] =
    g_param_spec_enum ("magnification-filter", NULL, NULL,
                       CLUTTER_TYPE_SCALING_FILTER,
                       CLUTTER_SCALING_FILTER_LINEAR,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:content-repeat:
   *
   * The repeat policy for the actor's [property@Clutter.Actor:content].
   */
  obj_props[PROP_CONTENT_REPEAT] =
    g_param_spec_flags ("content-repeat", NULL, NULL,
                        CLUTTER_TYPE_CONTENT_REPEAT,
                        CLUTTER_REPEAT_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:color-state:
   *
   * The #ClutterColorState contains the properties like colorspace for each
   * actors.
   */
  obj_props[PROP_COLOR_STATE] =
    g_param_spec_object ("color-state", NULL, NULL,
                         CLUTTER_TYPE_COLOR_STATE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:accessible-role:
   *
   * The accessible role of this object
   */
  obj_props[PROP_ACCESSIBLE_ROLE] =
    g_param_spec_enum ("accessible-role", NULL, NULL,
                       ATK_TYPE_ROLE,
                       ATK_ROLE_INVALID,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * ClutterActor:accessible-name:
   *
   * Object instance's name for assistive technology access.
   */
  obj_props[PROP_ACCESSIBLE_NAME] =
    g_param_spec_string ("accessible-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  /**
   * ClutterActor::destroy:
   * @actor: the #ClutterActor which emitted the signal
   *
   * The signal notifies that all references held on the
   * actor which emitted it should be released.
   *
   * The signal should be used by all holders of a reference
   * on @actor.
   *
   * This signal might result in the finalization of the #ClutterActor
   * if all references are released.
   *
   * Composite actors should override the default implementation of the
   * class handler of this signal and call clutter_actor_destroy() on
   * their children. When overriding the default class handler, it is
   * required to chain up to the parent's implementation.
   */
  actor_signals[DESTROY] =
    g_signal_new (I_("destroy"),
		  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_CLEANUP | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
		  G_STRUCT_OFFSET (ClutterActorClass, destroy),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterActor::show:
   * @actor: the object which received the signal
   *
   * The signal is emitted when an actor is visible and
   * rendered on the stage.
   */
  actor_signals[SHOW] =
    g_signal_new (I_("show"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ClutterActorClass, show),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterActor::hide:
   * @actor: the object which received the signal
   *
   * The signal is emitted when an actor is no longer rendered
   * on the stage.
   */
  actor_signals[HIDE] =
    g_signal_new (I_("hide"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ClutterActorClass, hide),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterActor::parent-set:
   * @actor: the object which received the signal
   * @old_parent: (nullable): the previous parent of the actor, or %NULL
   *
   * This signal is emitted when the parent of the actor changes.
   */
  actor_signals[PARENT_SET] =
    g_signal_new (I_("parent-set"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterActorClass, parent_set),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  /**
   * ClutterActor::queue-relayout:
   * @actor: the actor being queued for relayout
   *
   * The signal is emitted when clutter_actor_queue_relayout()
   * is called on an actor.
   *
   * The default implementation for #ClutterActor chains up to the
   * parent actor and queues a relayout on the parent, thus "bubbling"
   * the relayout queue up through the actor graph.
   *
   * The main purpose of this signal is to allow relayout to be propagated
   * properly in the procense of #ClutterClone actors. Applications will
   * not normally need to connect to this signal.
   */
  actor_signals[QUEUE_RELAYOUT] =
    g_signal_new (I_("queue-relayout"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST |
                  G_SIGNAL_NO_HOOKS,
		  G_STRUCT_OFFSET (ClutterActorClass, queue_relayout),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  /**
   * ClutterActor::event:
   * @actor: the actor which received the event
   * @event: a #ClutterEvent
   *
   * The signal is emitted each time an event is received
   * by the @actor. This signal will be emitted on every actor,
   * following the hierarchy chain, until it reaches the top-level
   * container (the #ClutterStage).
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[EVENT] =
    g_signal_new (I_("event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  G_STRUCT_OFFSET (ClutterActorClass, event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::button-press-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a button [struct@Event]
   *
   * The signal is emitted each time a mouse button
   * is pressed on @actor.
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[BUTTON_PRESS_EVENT] =
    g_signal_new (I_("button-press-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, button_press_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[BUTTON_PRESS_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::button-release-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a button [struct@Event]
   *
   * The signal is emitted each time a mouse button
   * is released on @actor.
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[BUTTON_RELEASE_EVENT] =
    g_signal_new (I_("button-release-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, button_release_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[BUTTON_RELEASE_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::scroll-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a scroll [struct@Event]
   *
   * The signal is emitted each time the mouse is
   * scrolled on @actor
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[SCROLL_EVENT] =
    g_signal_new (I_("scroll-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, scroll_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[SCROLL_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::key-press-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a key [struct@Event]
   *
   * The signal is emitted each time a keyboard button
   * is pressed while @actor has key focus (see clutter_stage_set_key_focus()).
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[KEY_PRESS_EVENT] =
    g_signal_new (I_("key-press-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, key_press_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[KEY_PRESS_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::key-release-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a key [struct@Event]
   *
   * The signal is emitted each time a keyboard button
   * is released while @actor has key focus (see
   * clutter_stage_set_key_focus()).
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[KEY_RELEASE_EVENT] =
    g_signal_new (I_("key-release-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, key_release_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[KEY_RELEASE_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);
  /**
   * ClutterActor::motion-event:
   * @actor: the actor which received the event
   * @event: (type ClutterEvent): a motion [struct@Event]
   *
   * The signal is emitted each time the mouse pointer is
   * moved over @actor.
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[MOTION_EVENT] =
    g_signal_new (I_("motion-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, motion_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[MOTION_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterActor::key-focus-in:
   * @actor: the actor which now has key focus
   *
   * The signal is emitted when @actor receives key focus.
   */
  actor_signals[KEY_FOCUS_IN] =
    g_signal_new (I_("key-focus-in"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, key_focus_in),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  /**
   * ClutterActor::key-focus-out:
   * @actor: the actor which now has key focus
   *
   * The signal is emitted when @actor loses key focus.
   */
  actor_signals[KEY_FOCUS_OUT] =
    g_signal_new (I_("key-focus-out"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, key_focus_out),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  /**
   * ClutterActor::enter-event:
   * @actor: the actor which the pointer has entered.
   * @event: (type ClutterEvent): a crossing [struct@Event]
   *
   * The signal is emitted when the pointer enters the @actor
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[ENTER_EVENT] =
    g_signal_new (I_("enter-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, enter_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[ENTER_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterActor::leave-event:
   * @actor: the actor which the pointer has left
   * @event: (type ClutterEvent): a crossing [struct@Event]
   *
   * The signal is emitted when the pointer leaves the @actor.
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[LEAVE_EVENT] =
    g_signal_new (I_("leave-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, leave_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[LEAVE_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterActor::captured-event:
   * @actor: the actor which received the signal
   * @event: a #ClutterEvent
   *
   * The signal is emitted when an event is captured
   * by Clutter. This signal will be emitted starting from the top-level
   * container (the [class@Clutter.Stage]) to the actor which received the event
   * going down the hierarchy. This signal can be used to intercept every
   * event before the specialized events (like
   * [signal@Clutter.Actor::button-press-event] or
   * [signal@Clutter.Actor::button-release-event]) are
   * emitted.
   *
   * Return value: %TRUE if the event has been handled by the actor,
   *   or %FALSE to continue the emission.
   */
  actor_signals[CAPTURED_EVENT] =
    g_signal_new (I_("captured-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		  G_STRUCT_OFFSET (ClutterActorClass, captured_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[CAPTURED_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterActor::realize:
   * @actor: the #ClutterActor that received the signal
   *
   * The signal is emitted each time an actor is being
   * realized.
   *
   * Deprecated: 1.16: The signal should not be used in newly
   *   written code
   */
  actor_signals[REALIZE] =
    g_signal_new (I_("realize"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                  G_STRUCT_OFFSET (ClutterActorClass, realize),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  /**
   * ClutterActor::unrealize:
   * @actor: the #ClutterActor that received the signal
   *
   * The signal is emitted each time an actor is being
   * unrealized.
   *
   * Deprecated: 1.16: The signal should not be used in newly
   *   written code
   */
  actor_signals[UNREALIZE] =
    g_signal_new (I_("unrealize"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                  G_STRUCT_OFFSET (ClutterActorClass, unrealize),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterActor::pick:
   * @actor: the #ClutterActor that received the signal
   * @pick_context: a #ClutterPickContext
   *
   * The signal is emitted each time an actor is being painted
   * in "pick mode". The pick mode is used to identify the actor during
   * the event handling phase, or by [method@Clutter.Stage.get_actor_at_pos].
   *
   * Subclasses of #ClutterActor should override the class signal handler
   * and paint themselves in that function.
   *
   * It is possible to connect a handler to the signal in order
   * to set up some custom aspect of a paint in pick mode.
   * Deprecated: 1.12: Override the [vfunc@Clutter.Actor.pick] virtual function
   *   instead.
   */
  actor_signals[PICK] =
    g_signal_new (I_("pick"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                  G_STRUCT_OFFSET (ClutterActorClass, pick),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_PICK_CONTEXT);

  /**
   * ClutterActor::transitions-completed:
   * @actor: a #ClutterActor
   *
   * The signal is emitted once all transitions
   * involving @actor are complete.
   */
  actor_signals[TRANSITIONS_COMPLETED] =
    g_signal_new (I_("transitions-completed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterActor::transition-stopped:
   * @actor: a #ClutterActor
   * @name: the name of the transition
   * @is_finished: whether the transition was finished, or stopped
   *
   * The signal is emitted once a transition
   * is stopped; a transition is stopped once it reached its total
   * duration (including eventual repeats), it has been stopped
   * using [method@Clutter.Timeline.stop], or it has been removed from the
   * transitions applied on @actor, using [method@Clutter.Actor.remove_transition].
   */
  actor_signals[TRANSITION_STOPPED] =
    g_signal_new (I_("transition-stopped"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE |
                  G_SIGNAL_NO_HOOKS | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _clutter_marshal_VOID__STRING_BOOLEAN,
                  G_TYPE_NONE, 2,
                  G_TYPE_STRING,
                  G_TYPE_BOOLEAN);
  g_signal_set_va_marshaller (actor_signals[TRANSITION_STOPPED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__STRING_BOOLEANv);

  /**
   * ClutterActor::touch-event:
   * @actor: a #ClutterActor
   * @event: a #ClutterEvent
   *
   * The signal is emitted each time a touch
   * begin/end/update/cancel event.
   *
   * Return value: %CLUTTER_EVENT_STOP if the event has been handled by
   *   the actor, or %CLUTTER_EVENT_PROPAGATE to continue the emission.
   */
  actor_signals[TOUCH_EVENT] =
    g_signal_new (I_("touch-event"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterActorClass, touch_event),
		  _clutter_boolean_handled_accumulator, NULL,
		  _clutter_marshal_BOOLEAN__BOXED,
		  G_TYPE_BOOLEAN, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  g_signal_set_va_marshaller (actor_signals[TOUCH_EVENT],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_BOOLEAN__BOXEDv);

  /**
   * ClutterActor::stage-views-changed:
   * @actor: a #ClutterActor
   *
   * The signal is emitted when the position or
   * size an actor is being painted at have changed so that it's visible
   * on different stage views.
   *
   * This signal is also emitted when the actor gets detached from the stage
   * or when the views of the stage have been invalidated and will be
   * replaced; it's not emitted when the actor gets hidden.
   */
  actor_signals[STAGE_VIEWS_CHANGED] =
    g_signal_new (I_("stage-views-changed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterActor::resource-scale-changed:
   * @actor: a #ClutterActor
   *
   * The signal is emitted when the resource scale
   * value returned by [method@Clutter.Actor.get_resource_scale] changes.
   *
   * This signal can be used to get notified about the correct resource scale
   * when the scale had to be queried outside of the paint cycle.
   */
  actor_signals[RESOURCE_SCALE_CHANGED] =
    g_signal_new (I_("resource-scale-changed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterActorClass, resource_scale_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * ClutterActor::child-added:
   * @actor: the actor which received the signal
   * @child: the new child that has been added to @actor
   *
   * The signal is emitted each time an actor
   * has been added to @actor.
   */
  actor_signals[CHILD_ADDED] =
    g_signal_new (I_("child-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterActorClass, child_added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);
  /**
   * ClutterActor::child-removed:
   * @actor: the actor which received the signal
   * @child: the child that has been removed from @actor
   *
   * The signal is emitted each time an actor
   * is removed from @actor.
   */
  actor_signals[CHILD_REMOVED] =
    g_signal_new (I_("child-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (ClutterActorClass, child_removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_ACTOR);

  /*< private > */
  actor_signals[CLONED] =
    g_signal_new ("cloned",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_CLONE);

  /*< private > */
  actor_signals[DECLONED] =
    g_signal_new ("decloned",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_CLONE);
}

static void
clutter_actor_init (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  self->priv = priv = clutter_actor_get_instance_private (self);

  priv->allocation = (ClutterActorBox) CLUTTER_ACTOR_BOX_UNINITIALIZED;

  priv->opacity = 0xff;
  priv->show_on_set_parent = TRUE;
  priv->resource_scale = -1.0f;

  priv->needs_width_request = TRUE;
  priv->needs_height_request = TRUE;
  priv->needs_allocation = TRUE;
  priv->needs_paint_volume_update = TRUE;
  priv->needs_visible_paint_volume_update = TRUE;
  priv->needs_update_stage_views = TRUE;
  priv->needs_finish_layout = TRUE;

  priv->cached_width_age = 1;
  priv->cached_height_age = 1;

  priv->opacity_override = -1;
  priv->enable_model_view_transform = TRUE;

  priv->transform_valid = FALSE;
  priv->stage_relative_modelview_valid = FALSE;

  /* the default is to stretch the content, to match the
   * current behaviour of basically all actors. also, it's
   * the easiest thing to compute.
   */
  priv->content_gravity = CLUTTER_CONTENT_GRAVITY_RESIZE_FILL;
  priv->min_filter = CLUTTER_SCALING_FILTER_LINEAR;
  priv->mag_filter = CLUTTER_SCALING_FILTER_LINEAR;

  /* this flag will be set to TRUE if the actor gets a child
   * or if the [xy]-expand flags are explicitly set; until
   * then, the actor does not need to expand.
   *
   * this also allows us to avoid computing the expand flag
   * when building up a scene.
   */
  priv->needs_compute_expand = FALSE;

  priv->next_redraw_clips =
    g_array_sized_new (FALSE, TRUE, sizeof (ClutterPaintVolume), 3);

  /* we start with an easing state with duration forcibly set
   * to 0, for backward compatibility.
   */
  clutter_actor_save_easing_state (self);
  clutter_actor_set_easing_duration (self, 0);
}

/**
 * clutter_actor_new:
 *
 * Creates a new #ClutterActor.
 *
 * A newly created actor has a floating reference, which will be sunk
 * when it is added to another actor.
 *
 * Return value: the newly created #ClutterActor
 */
ClutterActor *
clutter_actor_new (void)
{
  return g_object_new (CLUTTER_TYPE_ACTOR, NULL);
}

/**
 * clutter_actor_destroy:
 * @self: a #ClutterActor
 *
 * Destroys an actor.  When an actor is destroyed, it will break any
 * references it holds to other objects.  If the actor is inside a
 * container, the actor will be removed.
 *
 * When you destroy a container, its children will be destroyed as well.
 */
void
clutter_actor_destroy (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  g_object_ref (self);

  /* avoid recursion while destroying */
  if (!CLUTTER_ACTOR_IN_DESTRUCTION (self))
    {
      CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_IN_DESTRUCTION);

      g_object_run_dispose (G_OBJECT (self));

      CLUTTER_UNSET_PRIVATE_FLAGS (self, CLUTTER_IN_DESTRUCTION);
    }

  g_object_unref (self);
}

void
_clutter_actor_queue_redraw_full (ClutterActor             *self,
                                  const ClutterPaintVolume *volume,
                                  ClutterEffect            *effect)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *stage;

  /* ignore queueing a redraw for actors being destroyed */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return;

  /* we can ignore unmapped actors, unless they are inside a cloned branch
   * of the scene graph, as unmapped actors will simply be left unpainted.
   *
   * this allows us to ignore redraws queued on leaf nodes when one
   * of their parents has been hidden
   */
  if (!clutter_actor_is_mapped (self) &&
      !clutter_actor_has_mapped_clones (self))
    {
      CLUTTER_NOTE (PAINT,
                    "Skipping queue_redraw('%s'): mapped=%s, "
                    "has_mapped_clones=%s",
                    _clutter_actor_get_debug_name (self),
                    clutter_actor_is_mapped (self) ? "yes" : "no",
                    clutter_actor_has_mapped_clones (self) ? "yes" : "no");
      return;
    }

  /* given the check above we could end up queueing a redraw on an
   * unmapped actor with mapped clones, so we cannot assume that
   * get_stage() will return a Stage
   */
  stage = _clutter_actor_get_stage_internal (self);
  if (stage == NULL)
    return;

  /* ignore queueing a redraw on stages that are being destroyed */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (stage))
    return;

  if (priv->needs_redraw && priv->next_redraw_clips->len == 0)
    {
      /* priv->needs_redraw is TRUE while priv->next_redraw_clips->len is 0, this
       * means an unclipped redraw is already queued, no need to do anything.
       */
    }
  else
    {
      if (!priv->needs_redraw)
        {
          ClutterActor *iter = self;

          priv->needs_redraw = TRUE;

          clutter_stage_schedule_update (CLUTTER_STAGE (stage));

          while (iter && !iter->priv->needs_finish_layout)
            {
              iter->priv->needs_finish_layout = TRUE;
              iter = iter->priv->parent;
            }
        }

      if (volume)
        g_array_append_val (priv->next_redraw_clips, *volume);
      else
        priv->next_redraw_clips->len = 0;
    }

  /* If this is the first redraw queued then we can directly use the
     effect parameter */
  if (!priv->is_dirty)
    priv->effect_to_redraw = effect;
  /* Otherwise we need to merge it with the existing effect parameter */
  else if (effect != NULL)
    {
      /* If there's already an effect then we need to use whichever is
         later in the chain of actors. Otherwise a full redraw has
         already been queued on the actor so we need to ignore the
         effect parameter */
      if (priv->effect_to_redraw != NULL)
        {
          if (priv->effects == NULL)
            g_warning ("Redraw queued with an effect that is "
                       "not applied to the actor");
          else
            {
              const GList *l;

              for (l = _clutter_meta_group_peek_metas (priv->effects);
                   l != NULL;
                   l = l->next)
                {
                  if (l->data == priv->effect_to_redraw ||
                      l->data == effect)
                    priv->effect_to_redraw = l->data;
                }
            }
        }
    }
  else
    {
      /* If no effect is specified then we need to redraw the whole
         actor */
      priv->effect_to_redraw = NULL;
    }

  if (!priv->propagated_one_redraw)
    _clutter_actor_propagate_queue_redraw (self);
}

/**
 * clutter_actor_queue_redraw:
 * @self: A #ClutterActor
 *
 * Queues up a redraw of an actor and any children. The redraw occurs
 * once the main loop becomes idle (after the current batch of events
 * has been processed, roughly).
 *
 * Applications rarely need to call this, as redraws are handled
 * automatically by modification functions.
 *
 * This function will not do anything if @self is not visible, or
 * if the actor is inside an invisible part of the scenegraph.
 *
 * Also be aware that painting is a NOP for actors with an opacity of
 * 0
 *
 * When you are implementing a custom actor you must queue a redraw
 * whenever some private state changes that will affect painting or
 * picking of your actor.
 */
void
clutter_actor_queue_redraw (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  _clutter_actor_queue_redraw_full (self,
                                    NULL, /* clip volume */
                                    NULL /* effect */);
}

static void
_clutter_actor_queue_relayout_on_clones (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  GHashTableIter iter;
  gpointer key;

  if (priv->clones == NULL)
    return;

  g_hash_table_iter_init (&iter, priv->clones);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    clutter_actor_queue_relayout (key);
}

void
_clutter_actor_queue_only_relayout (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return;

  if (priv->needs_width_request &&
      priv->needs_height_request &&
      priv->needs_allocation)
    return; /* save some cpu cycles */

#ifdef CLUTTER_ENABLE_DEBUG
  if (!CLUTTER_ACTOR_IS_TOPLEVEL (self) && CLUTTER_ACTOR_IN_RELAYOUT (self))
    {
      g_warning ("The actor '%s' is currently inside an allocation "
                 "cycle; calling clutter_actor_queue_relayout() is "
                 "not recommended",
                 _clutter_actor_get_debug_name (self));
    }
#endif /* CLUTTER_ENABLE_DEBUG */

  _clutter_actor_queue_relayout_on_clones (self);

  g_signal_emit (self, actor_signals[QUEUE_RELAYOUT], 0);
}

/**
 * clutter_actor_queue_redraw_with_clip:
 * @self: a #ClutterActor
 * @clip: (nullable): a rectangular clip region, or %NULL
 *
 * Queues a redraw on @self limited to a specific, actor-relative
 * rectangular area.
 *
 * If @clip is %NULL this function is equivalent to
 * clutter_actor_queue_redraw().
 */
void
clutter_actor_queue_redraw_with_clip (ClutterActor       *self,
                                      const MtkRectangle *clip)
{
  ClutterPaintVolume volume;
  graphene_point3d_t origin;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (clip == NULL)
    {
      clutter_actor_queue_redraw (self);
      return;
    }

  clutter_paint_volume_init_from_actor (&volume, self);

  origin.x = clip->x;
  origin.y = clip->y;
  origin.z = 0.0f;

  clutter_paint_volume_set_origin (&volume, &origin);
  clutter_paint_volume_set_width (&volume, clip->width);
  clutter_paint_volume_set_height (&volume, clip->height);

  _clutter_actor_queue_redraw_full (self, &volume, NULL);
}

/**
 * clutter_actor_queue_relayout:
 * @self: A #ClutterActor
 *
 * Indicates that the actor's size request or other layout-affecting
 * properties may have changed. This function is used inside #ClutterActor
 * subclass implementations, not by applications directly.
 *
 * Queueing a new layout automatically queues a redraw as well.
 */
void
clutter_actor_queue_relayout (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  _clutter_actor_queue_only_relayout (self);
  clutter_actor_queue_redraw (self);
}

/**
 * clutter_actor_get_preferred_size:
 * @self: a #ClutterActor
 * @min_width_p: (out) (optional): return location for the minimum
 *   width, or %NULL
 * @min_height_p: (out) (optional): return location for the minimum
 *   height, or %NULL
 * @natural_width_p: (out) (optional): return location for the natural
 *   width, or %NULL
 * @natural_height_p: (out) (optional): return location for the natural
 *   height, or %NULL
 *
 * Computes the preferred minimum and natural size of an actor, taking into
 * account the actor's geometry management (either height-for-width
 * or width-for-height).
 *
 * The width and height used to compute the preferred height and preferred
 * width are the actor's natural ones.
 *
 * If you need to control the height for the preferred width, or the width for
 * the preferred height, you should [method@Clutter.Actor.get_preferred_width]
 * and [method@Clutter.Actor.get_preferred_height], and check the actor's preferred
 * geometry management using the [property@Clutter.Actor:request-mode] property.
 */
void
clutter_actor_get_preferred_size (ClutterActor *self,
                                  gfloat       *min_width_p,
                                  gfloat       *min_height_p,
                                  gfloat       *natural_width_p,
                                  gfloat       *natural_height_p)
{
  ClutterActorPrivate *priv;
  gfloat min_width, min_height;
  gfloat natural_width, natural_height;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  min_width = min_height = 0;
  natural_width = natural_height = 0;

  if (priv->request_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
    {
      CLUTTER_NOTE (LAYOUT, "Preferred size (height-for-width)");
      clutter_actor_get_preferred_width (self, -1,
                                         &min_width,
                                         &natural_width);
      clutter_actor_get_preferred_height (self, natural_width,
                                          &min_height,
                                          &natural_height);
    }
  else if (priv->request_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
    {
      CLUTTER_NOTE (LAYOUT, "Preferred size (width-for-height)");
      clutter_actor_get_preferred_height (self, -1,
                                          &min_height,
                                          &natural_height);
      clutter_actor_get_preferred_width (self, natural_height,
                                         &min_width,
                                         &natural_width);
    }
  else if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE)
    {
      CLUTTER_NOTE (LAYOUT, "Preferred size (content-size)");

      if (priv->content != NULL)
        clutter_content_get_preferred_size (priv->content, &natural_width, &natural_height);
    }
  else
    {
      CLUTTER_NOTE (LAYOUT, "Unknown request mode");
    }

  if (min_width_p)
    *min_width_p = min_width;

  if (min_height_p)
    *min_height_p = min_height;

  if (natural_width_p)
    *natural_width_p = natural_width;

  if (natural_height_p)
    *natural_height_p = natural_height;
}

/*< private >
 * effective_align:
 * @align: a #ClutterActorAlign
 * @direction: a #ClutterTextDirection
 *
 * Retrieves the correct alignment depending on the text direction
 *
 * Return value: the effective alignment
 */
static ClutterActorAlign
effective_align (ClutterActorAlign    align,
                 ClutterTextDirection direction)
{
  ClutterActorAlign res;

  switch (align)
    {
    case CLUTTER_ACTOR_ALIGN_START:
      res = (direction == CLUTTER_TEXT_DIRECTION_RTL)
          ? CLUTTER_ACTOR_ALIGN_END
          : CLUTTER_ACTOR_ALIGN_START;
      break;

    case CLUTTER_ACTOR_ALIGN_END:
      res = (direction == CLUTTER_TEXT_DIRECTION_RTL)
          ? CLUTTER_ACTOR_ALIGN_START
          : CLUTTER_ACTOR_ALIGN_END;
      break;

    default:
      res = align;
      break;
    }

  return res;
}

/*< private >
 * _clutter_actor_get_effective_x_align:
 * @self: a #ClutterActor
 *
 * Retrieves the effective horizontal alignment, taking into
 * consideration the text direction of @self.
 *
 * Return value: the effective horizontal alignment
 */
ClutterActorAlign
_clutter_actor_get_effective_x_align (ClutterActor *self)
{
  return effective_align (clutter_actor_get_x_align (self),
                          clutter_actor_get_text_direction (self));
}

static inline void
adjust_for_margin (float  margin_start,
                   float  margin_end,
                   float *minimum_size,
                   float *natural_size,
                   float *allocated_start,
                   float *allocated_end)
{
  float min_size = *minimum_size;
  float nat_size = *natural_size;
  float start = *allocated_start;
  float end = *allocated_end;

  min_size = MAX (min_size - (margin_start + margin_end), 0);
  nat_size = MAX (nat_size - (margin_start + margin_end), 0);

  *minimum_size = min_size;
  *natural_size = nat_size;

  start += margin_start;
  end -= margin_end;

  if (end - start >= 0)
    {
      *allocated_start = start;
      *allocated_end = end;
    }
}

static inline void
adjust_for_alignment (ClutterActorAlign  alignment,
                      float              natural_size,
                      float             *allocated_start,
                      float             *allocated_end)
{
  float allocated_size = *allocated_end - *allocated_start;

  if (allocated_size <= 0.f)
    return;

  switch (alignment)
    {
    case CLUTTER_ACTOR_ALIGN_FILL:
      /* do nothing */
      break;

    case CLUTTER_ACTOR_ALIGN_START:
      /* keep start */
      *allocated_end = *allocated_start + MIN (natural_size, allocated_size);
      break;

    case CLUTTER_ACTOR_ALIGN_END:
      if (allocated_size > natural_size)
        {
          *allocated_start += (allocated_size - natural_size);
          *allocated_end = *allocated_start + natural_size;
        }
      break;

    case CLUTTER_ACTOR_ALIGN_CENTER:
      if (allocated_size > natural_size)
        {
          *allocated_start += floorf ((allocated_size - natural_size) / 2);
          *allocated_end = *allocated_start + MIN (allocated_size, natural_size);
        }
      break;
    }
}

/*< private >
 * clutter_actor_adjust_width:
 * @self: a #ClutterActor
 * @minimum_width: (inout): the actor's preferred minimum width, which
 *   will be adjusted depending on the margin
 * @natural_width: (inout): the actor's preferred natural width, which
 *   will be adjusted depending on the margin
 * @adjusted_x1: (out): the adjusted x1 for the actor's bounding box
 * @adjusted_x2: (out): the adjusted x2 for the actor's bounding box
 *
 * Adjusts the preferred and allocated position and size of an actor,
 * depending on the margin and alignment properties.
 */
static void
clutter_actor_adjust_width (ClutterActor *self,
                            gfloat       *minimum_width,
                            gfloat       *natural_width,
                            gfloat       *adjusted_x1,
                            gfloat       *adjusted_x2)
{
  ClutterTextDirection text_dir;
  const ClutterLayoutInfo *info;

  info = _clutter_actor_get_layout_info_or_defaults (self);
  text_dir = clutter_actor_get_text_direction (self);

  CLUTTER_NOTE (LAYOUT, "Adjusting allocated X and width");

  /* this will tweak natural_width to remove the margin, so that
   * adjust_for_alignment() will use the correct size
   */
  adjust_for_margin (info->margin.left, info->margin.right,
                     minimum_width, natural_width,
                     adjusted_x1, adjusted_x2);

  adjust_for_alignment (effective_align (info->x_align, text_dir),
                        *natural_width,
                        adjusted_x1, adjusted_x2);
}

/*< private >
 * clutter_actor_adjust_height:
 * @self: a #ClutterActor
 * @minimum_height: (inout): the actor's preferred minimum height, which
 *   will be adjusted depending on the margin
 * @natural_height: (inout): the actor's preferred natural height, which
 *   will be adjusted depending on the margin
 * @adjusted_y1: (out): the adjusted y1 for the actor's bounding box
 * @adjusted_y2: (out): the adjusted y2 for the actor's bounding box
 *
 * Adjusts the preferred and allocated position and size of an actor,
 * depending on the margin and alignment properties.
 */
static void
clutter_actor_adjust_height (ClutterActor *self,
                             gfloat       *minimum_height,
                             gfloat       *natural_height,
                             gfloat       *adjusted_y1,
                             gfloat       *adjusted_y2)
{
  const ClutterLayoutInfo *info;

  info = _clutter_actor_get_layout_info_or_defaults (self);

  CLUTTER_NOTE (LAYOUT, "Adjusting allocated Y and height");

  /* this will tweak natural_height to remove the margin, so that
   * adjust_for_alignment() will use the correct size
   */
  adjust_for_margin (info->margin.top, info->margin.bottom,
                     minimum_height, natural_height,
                     adjusted_y1,
                     adjusted_y2);

  /* we don't use effective_align() here, because text direction
   * only affects the horizontal axis
   */
  adjust_for_alignment (info->y_align,
                        *natural_height,
                        adjusted_y1,
                        adjusted_y2);

}

/* looks for a cached size request for this for_size. If not
 * found, returns the oldest entry so it can be overwritten */
static gboolean
_clutter_actor_get_cached_size_request (gfloat         for_size,
                                        SizeRequest   *cached_size_requests,
                                        SizeRequest  **result)
{
  guint i;

  *result = &cached_size_requests[0];

  for (i = 0; i < N_CACHED_SIZE_REQUESTS; i++)
    {
      SizeRequest *sr;

      sr = &cached_size_requests[i];

      if (sr->age > 0 &&
          sr->for_size == for_size)
        {
          CLUTTER_NOTE (LAYOUT, "Size cache hit for size: %.2f", for_size);
          *result = sr;
          return TRUE;
        }
      else if (sr->age < (*result)->age)
        {
          *result = sr;
        }
    }

  CLUTTER_NOTE (LAYOUT, "Size cache miss for size: %.2f", for_size);

  return FALSE;
}

static void
clutter_actor_update_preferred_size_for_constraints (ClutterActor *self,
                                                     ClutterOrientation direction,
                                                     float for_size,
                                                     float *minimum_size,
                                                     float *natural_size)
{
  ClutterActorPrivate *priv = self->priv;
  const GList *constraints, *l;

  if (priv->constraints == NULL)
    return;

  constraints = _clutter_meta_group_peek_metas (priv->constraints);
  for (l = constraints; l != NULL; l = l->next)
    {
      ClutterConstraint *constraint = l->data;
      ClutterActorMeta *meta = l->data;

      if (!clutter_actor_meta_get_enabled (meta))
        continue;

      clutter_constraint_update_preferred_size (constraint, self,
                                                direction,
                                                for_size,
                                                minimum_size,
                                                natural_size);

      CLUTTER_NOTE (LAYOUT,
                    "Preferred %s of '%s' after constraint '%s': "
                    "{ min:%.2f, nat:%.2f }",
                    direction == CLUTTER_ORIENTATION_HORIZONTAL
                      ? "width"
                      : "height",
                    _clutter_actor_get_debug_name (self),
                    _clutter_actor_meta_get_debug_name (meta),
                    *minimum_size, *natural_size);
    }
}

/**
 * clutter_actor_get_preferred_width:
 * @self: A #ClutterActor
 * @for_height: available height when computing the preferred width,
 *   or a negative value to indicate that no height is defined
 * @min_width_p: (out) (optional): return location for minimum width,
 *   or %NULL
 * @natural_width_p: (out) (optional): return location for the natural
 *   width, or %NULL
 *
 * Computes the requested minimum and natural widths for an actor,
 * optionally depending on the specified height, or if they are
 * already computed, returns the cached values.
 *
 * An actor may not get its request - depending on the layout
 * manager that's in effect.
 *
 * A request should not incorporate the actor's scaleor translation;
 * those transformations do not affect layout, only rendering.
 */
void
clutter_actor_get_preferred_width (ClutterActor *self,
                                   gfloat        for_height,
                                   gfloat       *min_width_p,
                                   gfloat       *natural_width_p)
{
  float request_min_width, request_natural_width;
  SizeRequest *cached_size_request;
  const ClutterLayoutInfo *info;
  ClutterActorPrivate *priv;
  gboolean found_in_cache;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  info = _clutter_actor_get_layout_info_or_defaults (self);

  /* we shortcircuit the case of a fixed size set using set_width() */
  if (priv->min_width_set && priv->natural_width_set)
    {
      if (min_width_p != NULL)
        *min_width_p = info->minimum.width + (info->margin.left + info->margin.right);

      if (natural_width_p != NULL)
        *natural_width_p = info->natural.width + (info->margin.left + info->margin.right);

      return;
    }

  /* if the request mode is CONTENT_SIZE we simply return the content width */
  if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE)
    {
      float content_width = 0.f;

      if (priv->content != NULL)
        clutter_content_get_preferred_size (priv->content, &content_width, NULL);

      if (min_width_p != NULL)
        *min_width_p = content_width;

      if (natural_width_p != NULL)
        *natural_width_p = content_width;

      return;
    }

  CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_IN_PREF_WIDTH);

  /* the remaining cases are:
   *
   *   - either min_width or natural_width have been set
   *   - neither min_width or natural_width have been set
   *
   * in both cases, we go through the cache (and through the actor in case
   * of cache misses) and determine the authoritative value depending on
   * the *_set flags.
   */

  if (!priv->needs_width_request)
    {
      found_in_cache =
        _clutter_actor_get_cached_size_request (for_height,
                                                priv->width_requests,
                                                &cached_size_request);
    }
  else
    {
      /* if the actor needs a width request we use the first slot */
      found_in_cache = FALSE;
      cached_size_request = &priv->width_requests[0];
    }

  if (!found_in_cache)
    {
      gfloat minimum_width, natural_width;
      ClutterActorClass *klass;

      minimum_width = natural_width = 0;

      /* adjust for the margin */
      if (for_height >= 0)
        {
          for_height -= (info->margin.top + info->margin.bottom);
          if (for_height < 0)
            for_height = 0;
        }

      CLUTTER_NOTE (LAYOUT, "Width request for %.2f px", for_height);

      klass = CLUTTER_ACTOR_GET_CLASS (self);
      klass->get_preferred_width (self, for_height,
                                  &minimum_width,
                                  &natural_width);

      /* adjust for constraints */
      clutter_actor_update_preferred_size_for_constraints (self,
                                                           CLUTTER_ORIENTATION_HORIZONTAL,
                                                           for_height,
                                                           &minimum_width,
                                                           &natural_width);

      /* adjust for the margin */
      minimum_width += (info->margin.left + info->margin.right);
      natural_width += (info->margin.left + info->margin.right);

      /* Due to accumulated float errors, it's better not to warn
       * on this, but just fix it.
       */
      if (natural_width < minimum_width)
	natural_width = minimum_width;

      cached_size_request->min_size = minimum_width;
      cached_size_request->natural_size = natural_width;
      cached_size_request->for_size = for_height;
      cached_size_request->age = priv->cached_width_age;

      priv->cached_width_age += 1;
      priv->needs_width_request = FALSE;
    }

  if (!priv->min_width_set)
    request_min_width = cached_size_request->min_size;
  else
    request_min_width = info->margin.left
                      + info->minimum.width
                      + info->margin.right;

  if (!priv->natural_width_set)
    request_natural_width = cached_size_request->natural_size;
  else
    request_natural_width = info->margin.left
                          + info->natural.width
                          + info->margin.right;

  if (min_width_p)
    *min_width_p = request_min_width;

  if (natural_width_p)
    *natural_width_p = request_natural_width;

  CLUTTER_UNSET_PRIVATE_FLAGS (self, CLUTTER_IN_PREF_WIDTH);
}

/**
 * clutter_actor_get_preferred_height:
 * @self: A #ClutterActor
 * @for_width: available width to assume in computing desired height,
 *   or a negative value to indicate that no width is defined
 * @min_height_p: (out) (optional): return location for minimum height,
 *   or %NULL
 * @natural_height_p: (out) (optional): return location for natural
 *   height, or %NULL
 *
 * Computes the requested minimum and natural heights for an actor,
 * or if they are already computed, returns the cached values.
 *
 * An actor may not get its request - depending on the layout
 * manager that's in effect.
 *
 * A request should not incorporate the actor's scale or translation;
 * those transformations do not affect layout, only rendering.
 */
void
clutter_actor_get_preferred_height (ClutterActor *self,
                                    gfloat        for_width,
                                    gfloat       *min_height_p,
                                    gfloat       *natural_height_p)
{
  float request_min_height, request_natural_height;
  SizeRequest *cached_size_request;
  const ClutterLayoutInfo *info;
  ClutterActorPrivate *priv;
  gboolean found_in_cache;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  info = _clutter_actor_get_layout_info_or_defaults (self);

  /* we shortcircuit the case of a fixed size set using set_height() */
  if (priv->min_height_set && priv->natural_height_set)
    {
      if (min_height_p != NULL)
        *min_height_p = info->minimum.height + (info->margin.top + info->margin.bottom);

      if (natural_height_p != NULL)
        *natural_height_p = info->natural.height + (info->margin.top + info->margin.bottom);

      return;
    }

  /* if the request mode is CONTENT_SIZE we simply return the content height */
  if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE)
    {
      float content_height = 0.f;

      if (priv->content != NULL)
        clutter_content_get_preferred_size (priv->content, NULL, &content_height);

      if (min_height_p != NULL)
        *min_height_p = content_height;

      if (natural_height_p != NULL)
        *natural_height_p = content_height;

      return;
    }

  CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_IN_PREF_HEIGHT);

  /* the remaining cases are:
   *
   *   - either min_height or natural_height have been set
   *   - neither min_height or natural_height have been set
   *
   * in both cases, we go through the cache (and through the actor in case
   * of cache misses) and determine the authoritative value depending on
   * the *_set flags.
   */

  if (!priv->needs_height_request)
    {
      found_in_cache =
        _clutter_actor_get_cached_size_request (for_width,
                                                priv->height_requests,
                                                &cached_size_request);
    }
  else
    {
      found_in_cache = FALSE;
      cached_size_request = &priv->height_requests[0];
    }

  if (!found_in_cache)
    {
      gfloat minimum_height, natural_height;
      ClutterActorClass *klass;

      minimum_height = natural_height = 0;

      CLUTTER_NOTE (LAYOUT, "Height request for %.2f px", for_width);

      /* adjust for margin */
      if (for_width >= 0)
        {
          for_width -= (info->margin.left + info->margin.right);
          if (for_width < 0)
            for_width = 0;
        }

      klass = CLUTTER_ACTOR_GET_CLASS (self);
      klass->get_preferred_height (self, for_width,
                                   &minimum_height,
                                   &natural_height);

      /* adjust for constraints */
      clutter_actor_update_preferred_size_for_constraints (self,
                                                           CLUTTER_ORIENTATION_VERTICAL,
                                                           for_width,
                                                           &minimum_height,
                                                           &natural_height);

      /* adjust for margin */
      minimum_height += (info->margin.top + info->margin.bottom);
      natural_height += (info->margin.top + info->margin.bottom);

      /* Due to accumulated float errors, it's better not to warn
       * on this, but just fix it.
       */
      if (natural_height < minimum_height)
	natural_height = minimum_height;

      cached_size_request->min_size = minimum_height;
      cached_size_request->natural_size = natural_height;
      cached_size_request->for_size = for_width;
      cached_size_request->age = priv->cached_height_age;

      priv->cached_height_age += 1;
      priv->needs_height_request = FALSE;
    }

  if (!priv->min_height_set)
    request_min_height = cached_size_request->min_size;
  else
    request_min_height = info->margin.top
                       + info->minimum.height
                       + info->margin.bottom;

  if (!priv->natural_height_set)
    request_natural_height = cached_size_request->natural_size;
  else
    request_natural_height = info->margin.top
                           + info->natural.height
                           + info->margin.bottom;

  if (min_height_p)
    *min_height_p = request_min_height;

  if (natural_height_p)
    *natural_height_p = request_natural_height;

  CLUTTER_UNSET_PRIVATE_FLAGS (self, CLUTTER_IN_PREF_HEIGHT);
}

/**
 * clutter_actor_get_allocation_box:
 * @self: A #ClutterActor
 * @box: (out): the function fills this in with the actor's allocation
 *
 * Gets the layout box an actor has been assigned. The allocation can
 * only be assumed valid inside a paint() method; anywhere else, it
 * may be out-of-date.
 *
 * An allocation does not incorporate the actor's scale or translation;
 * those transformations do not affect layout, only rendering.
 *
 * Do not call any of the clutter_actor_get_allocation_*() family
 * of functions inside the implementation of the get_preferred_width()
 * or get_preferred_height() virtual functions.
 */
void
clutter_actor_get_allocation_box (ClutterActor    *self,
                                  ClutterActorBox *box)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* XXX - if needs_allocation=TRUE, we can either 1) g_return_if_fail,
   * which limits calling get_allocation to inside paint() basically; or
   * we can 2) force a layout, which could be expensive if someone calls
   * get_allocation somewhere silly; or we can 3) just return the latest
   * value, allowing it to be out-of-date, and assume people know what
   * they are doing.
   *
   * The least-surprises approach that keeps existing code working is
   * likely to be 2). People can end up doing some inefficient things,
   * though, and in general code that requires 2) is probably broken.
   */

  /* this implements 2) */
  if (G_UNLIKELY (self->priv->needs_allocation))
    {
      ClutterActor *stage = _clutter_actor_get_stage_internal (self);

      /* do not queue a relayout on an unparented actor */
      if (stage)
        clutter_stage_maybe_relayout (stage);
    }

  /* commenting out the code above and just keeping this assignment
   * implements 3)
   */
  *box = self->priv->allocation;
}

static void
clutter_actor_update_constraints (ClutterActor    *self,
                                  ClutterActorBox *allocation)
{
  ClutterActorPrivate *priv = self->priv;
  const GList *constraints, *l;

  if (priv->constraints == NULL)
    return;

  constraints = _clutter_meta_group_peek_metas (priv->constraints);
  for (l = constraints; l != NULL; l = l->next)
    {
      ClutterConstraint *constraint = l->data;
      ClutterActorMeta *meta = l->data;
      gboolean changed = FALSE;

      if (clutter_actor_meta_get_enabled (meta))
        {
          changed |=
            clutter_constraint_update_allocation (constraint,
                                                  self,
                                                  allocation);

          CLUTTER_NOTE (LAYOUT,
                        "Allocation of '%s' after constraint '%s': "
                        "{ %.2f, %.2f, %.2f, %.2f } (changed:%s)",
                        _clutter_actor_get_debug_name (self),
                        _clutter_actor_meta_get_debug_name (meta),
                        allocation->x1,
                        allocation->y1,
                        allocation->x2,
                        allocation->y2,
                        changed ? "yes" : "no");
        }
    }
}

/*< private >
 * clutter_actor_adjust_allocation:
 * @self: a #ClutterActor
 * @allocation: (inout): the allocation to adjust
 *
 * Adjusts the passed allocation box taking into account the actor's
 * layout information, like alignment, expansion, and margin.
 */
static void
clutter_actor_adjust_allocation (ClutterActor    *self,
                                 ClutterActorBox *allocation)
{
  ClutterActorBox adj_allocation;
  float alloc_width, alloc_height;
  float min_width, min_height;
  float nat_width, nat_height;
  ClutterRequestMode req_mode;

  adj_allocation = *allocation;

  clutter_actor_box_get_size (allocation, &alloc_width, &alloc_height);

  /* There's no point in trying to adjust a zero-sized actor */
  if (alloc_width == 0.f && alloc_height == 0.f)
    return;

  /* we want to hit the cache, so we use the public API */
  req_mode = clutter_actor_get_request_mode (self);

  if (req_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
    {
      clutter_actor_get_preferred_width (self, -1,
                                         &min_width,
                                         &nat_width);
      clutter_actor_get_preferred_height (self, alloc_width,
                                          &min_height,
                                          &nat_height);
    }
  else if (req_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
    {
      clutter_actor_get_preferred_height (self, -1,
                                          &min_height,
                                          &nat_height);
      clutter_actor_get_preferred_width (self, alloc_height,
                                         &min_width,
                                         &nat_width);
    }
  else if (req_mode == CLUTTER_REQUEST_CONTENT_SIZE)
    {
      min_width = min_height = 0;
      nat_width = nat_height = 0;

      if (self->priv->content != NULL)
        clutter_content_get_preferred_size (self->priv->content, &nat_width, &nat_height);
    }

#ifdef CLUTTER_ENABLE_DEBUG
  /* warn about underallocations */
  if (_clutter_diagnostic_enabled () &&
      (floorf (min_width - alloc_width) > 0 ||
       floorf (min_height - alloc_height) > 0))
    {
      ClutterActor *parent = clutter_actor_get_parent (self);

      /* the only actors that are allowed to be underallocated are the Stage,
       * as it doesn't have an implicit size, and Actors that specifically
       * told us that they want to opt-out from layout control mechanisms
       * through the NO_LAYOUT escape hatch.
       */
      if (parent != NULL &&
          !(self->flags & CLUTTER_ACTOR_NO_LAYOUT) != 0)
        {
          g_warning (G_STRLOC ": The actor '%s' is getting an allocation "
                     "of %.2f x %.2f from its parent actor '%s', but its "
                     "requested minimum size is of %.2f x %.2f",
                     _clutter_actor_get_debug_name (self),
                     alloc_width, alloc_height,
                     _clutter_actor_get_debug_name (parent),
                     min_width, min_height);
        }
    }
#endif

  clutter_actor_adjust_width (self,
                              &min_width,
                              &nat_width,
                              &adj_allocation.x1,
                              &adj_allocation.x2);

  clutter_actor_adjust_height (self,
                               &min_height,
                               &nat_height,
                               &adj_allocation.y1,
                               &adj_allocation.y2);

  /* we maintain the invariant that an allocation cannot be adjusted
   * to be outside the parent-given box
   */
  if (adj_allocation.x1 < allocation->x1 ||
      adj_allocation.y1 < allocation->y1 ||
      adj_allocation.x2 > allocation->x2 ||
      adj_allocation.y2 > allocation->y2)
    {
      g_warning (G_STRLOC ": The actor '%s' tried to adjust its allocation "
                 "to { %.2f, %.2f, %.2f, %.2f }, which is outside of its "
                 "original allocation of { %.2f, %.2f, %.2f, %.2f }",
                 _clutter_actor_get_debug_name (self),
                 adj_allocation.x1, adj_allocation.y1,
                 adj_allocation.x2 - adj_allocation.x1,
                 adj_allocation.y2 - adj_allocation.y1,
                 allocation->x1, allocation->y1,
                 allocation->x2 - allocation->x1,
                 allocation->y2 - allocation->y1);
      return;
    }

  *allocation = adj_allocation;
}

static void
clutter_actor_allocate_internal (ClutterActor           *self,
                                 const ClutterActorBox  *allocation)
{
  ClutterActorClass *klass;

  CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_IN_RELAYOUT);

  CLUTTER_NOTE (LAYOUT, "Calling %s::allocate()",
                _clutter_actor_get_debug_name (self));

  klass = CLUTTER_ACTOR_GET_CLASS (self);
  klass->allocate (self, allocation);

  CLUTTER_UNSET_PRIVATE_FLAGS (self, CLUTTER_IN_RELAYOUT);

  /* Caller should call clutter_actor_queue_redraw() if needed
   * for that particular case.
   */
}

/**
 * clutter_actor_allocate:
 * @self: A #ClutterActor
 * @box: new allocation of the actor, in parent-relative coordinates
 *
 * Assigns the size of a #ClutterActor from the given @box.
 *
 * This function should only be called on the children of an actor when
 * overriding the [vfunc@Clutter.Actor.allocate] virtual function.
 *
 * This function will adjust the stored allocation to take into account
 * the alignment flags set in the [property@Clutter.Actor:x-align] and
 * [property@Clutter.Actor:y-align] properties, as well as the margin values set in
 * the[property@Clutter.Actor:margin-top], [property@Clutter.Actor:margin-right],
 * [property@Clutter.Actor:margin-bottom], and
 * [property@Clutter.Actor:margin-left] properties.
 *
 * This function will respect the easing state of the #ClutterActor and
 * interpolate between the current allocation and the new one if the
 * easing state duration is a positive value.
 *
 * Actors can know from their allocation box whether they have moved
 * with respect to their parent actor. The @flags parameter describes
 * additional information about the allocation, for instance whether
 * the parent has moved with respect to the stage, for example because
 * a grandparent's origin has moved.
 */
void
clutter_actor_allocate (ClutterActor          *self,
                        const ClutterActorBox *box)
{
  ClutterActorBox old_allocation, real_allocation;
  gboolean origin_changed, size_changed;
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  if (G_UNLIKELY (_clutter_actor_get_stage_internal (self) == NULL))
    {
      g_warning ("Spurious clutter_actor_allocate called for actor %p/%s "
                 "which isn't a descendent of the stage!\n",
                 self, _clutter_actor_get_debug_name (self));
      return;
    }

  priv = self->priv;

  if (!CLUTTER_ACTOR_IS_TOPLEVEL (self) &&
      !clutter_actor_is_mapped (self) &&
      !clutter_actor_has_mapped_clones (self))
    return;

#ifdef HAVE_PROFILER
  COGL_TRACE_SCOPED_ANCHOR (ClutterActorAllocate);

  if (G_UNLIKELY (clutter_debug_flags & CLUTTER_DEBUG_DETAILED_TRACE))
    {
      COGL_TRACE_BEGIN_ANCHORED (ClutterActorAllocate,
                                 "Clutter::Actor::allocate()");
      COGL_TRACE_DESCRIBE (ClutterActorAllocate,
                           _clutter_actor_get_debug_name (self));
    }
#endif

  old_allocation = priv->allocation;
  real_allocation = *box;

  g_return_if_fail (!isnan (real_allocation.x1) &&
                    !isnan (real_allocation.x2) &&
                    !isnan (real_allocation.y1) &&
                    !isnan (real_allocation.y2));

  /* constraints are allowed to modify the allocation only here; we do
   * this prior to all the other checks so that we can bail out if the
   * allocation did not change
   */
  clutter_actor_update_constraints (self, &real_allocation);

  /* adjust the allocation depending on the align/margin properties */
  clutter_actor_adjust_allocation (self, &real_allocation);

  if (real_allocation.x2 < real_allocation.x1 ||
      real_allocation.y2 < real_allocation.y1)
    {
      g_warning (G_STRLOC ": Actor '%s' tried to allocate a size of %.2f x %.2f",
                 _clutter_actor_get_debug_name (self),
                 real_allocation.x2 - real_allocation.x1,
                 real_allocation.y2 - real_allocation.y1);
    }

  /* we allow 0-sized actors, but not negative-sized ones */
  real_allocation.x2 = MAX (real_allocation.x2, real_allocation.x1);
  real_allocation.y2 = MAX (real_allocation.y2, real_allocation.y1);

  origin_changed = (real_allocation.x1 != old_allocation.x1 ||
                    real_allocation.y1 != old_allocation.y1);

  size_changed = (real_allocation.x2 != old_allocation.x2 ||
                  real_allocation.y2 != old_allocation.y2);

  /* When needs_allocation is set but we didn't move nor resize, we still
   * want to call the allocate() vfunc because a child probably called
   * queue_relayout() and needs a new allocation.
   *
   * In case needs_allocation isn't set and we didn't move nor resize, we
   * can safely stop allocating.
   */
  if (!priv->needs_allocation && !origin_changed && !size_changed)
    {
      CLUTTER_NOTE (LAYOUT, "No allocation needed");
      return;
    }

  if (!origin_changed && !size_changed)
    {
      /* If the actor didn't move but needs_allocation is set, we just
       * need to allocate the children (see comment above) */
      clutter_actor_allocate_internal (self, &real_allocation);
      return;
    }

  if (_clutter_actor_create_transition (self, obj_props[PROP_ALLOCATION],
                                        &priv->allocation,
                                        &real_allocation))
    clutter_actor_allocate_internal (self, &priv->allocation);
}

/**
 * clutter_actor_set_allocation:
 * @self: a #ClutterActor
 * @box: a #ClutterActorBox
 *
 * Stores the allocation of @self as defined by @box.
 *
 * This function can only be called from within the implementation of
 * the [vfunc@Clutter.Actor.allocate] virtual function.
 *
 * The allocation @box should have been adjusted to take into account
 * constraints, alignment, and margin properties.
 *
 * This function should only be used by subclasses of #ClutterActor
 * that wish to store their allocation but cannot chain up to the
 * parent's implementation; the default implementation of the
 * [vfunc@Clutter.Actor.allocate] virtual function will call this
 * function.
 */
void
clutter_actor_set_allocation (ClutterActor           *self,
                              const ClutterActorBox  *box)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (box != NULL);

  if (G_UNLIKELY (!CLUTTER_ACTOR_IN_RELAYOUT (self)))
    {
      g_critical (G_STRLOC ": The clutter_actor_set_allocation() function "
                  "can only be called from within the implementation of "
                  "the ClutterActor::allocate() virtual function.");
      return;
    }

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_set_allocation_internal (self, box);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_set_position:
 * @self: A #ClutterActor
 * @x: New left position of actor in pixels.
 * @y: New top position of actor in pixels.
 *
 * Sets the actor's fixed position in pixels relative to any parent
 * actor.
 *
 * If a layout manager is in use, this position will override the
 * layout manager and force a fixed position.
 */
void
clutter_actor_set_position (ClutterActor *self,
			    gfloat        x,
			    gfloat        y)
{
  graphene_point_t new_position;
  graphene_point_t cur_position;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  graphene_point_init (&new_position, x, y);

  cur_position.x = clutter_actor_get_x (self);
  cur_position.y = clutter_actor_get_y (self);

  if (!graphene_point_equal (&cur_position, &new_position))
    _clutter_actor_create_transition (self, obj_props[PROP_POSITION],
                                      &cur_position,
                                      &new_position);
}

/**
 * clutter_actor_get_fixed_position_set:
 * @self: A #ClutterActor
 *
 * Checks whether an actor has a fixed position set (and will thus be
 * unaffected by any layout manager).
 *
 * Return value: %TRUE if the fixed position is set on the actor
 */
gboolean
clutter_actor_get_fixed_position_set (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->position_set;
}

/**
 * clutter_actor_set_fixed_position_set:
 * @self: A #ClutterActor
 * @is_set: whether to use fixed position
 *
 * Sets whether an actor has a fixed position set (and will thus be
 * unaffected by any layout manager).
 */
void
clutter_actor_set_fixed_position_set (ClutterActor *self,
                                      gboolean      is_set)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->position_set == (is_set != FALSE))
    return;

  if (!is_set)
    {
      ClutterLayoutInfo *info;

      /* Ensure we set back the default fixed position of 0,0 so that setting
	 just one of x/y always atomically gets 0 for the other */
      info = _clutter_actor_peek_layout_info (self);
      if (info != NULL)
	{
	  info->fixed_pos.x = 0;
	  info->fixed_pos.y = 0;
	}
    }

  self->priv->position_set = is_set != FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_FIXED_POSITION_SET]);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_move_by:
 * @self: A #ClutterActor
 * @dx: Distance to move Actor on X axis.
 * @dy: Distance to move Actor on Y axis.
 *
 * Moves an actor by the specified distance relative to its current
 * position in pixels.
 *
 * This function modifies the fixed position of an actor and thus removes
 * it from any layout management. Another way to move an actor is with an
 * additional translation, using clutter_actor_set_translation().
 */
void
clutter_actor_move_by (ClutterActor *self,
		       gfloat        dx,
		       gfloat        dy)
{
  const ClutterLayoutInfo *info;
  gfloat x, y;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_layout_info_or_defaults (self);
  x = info->fixed_pos.x;
  y = info->fixed_pos.y;

  clutter_actor_set_position (self, x + dx, y + dy);
}

static void
clutter_actor_set_min_width (ClutterActor *self,
                             gfloat        min_width)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };
  ClutterLayoutInfo *info;

  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      g_warning ("Can't set the minimal width of a stage");
      return;
    }

  info = _clutter_actor_get_layout_info (self);

  if (priv->min_width_set && min_width == info->minimum.width)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_store_old_geometry (self, &old);

  info->minimum.width = min_width;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MIN_WIDTH]);
  clutter_actor_set_min_width_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  g_object_thaw_notify (G_OBJECT (self));

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_min_height (ClutterActor *self,
                              gfloat        min_height)

{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };
  ClutterLayoutInfo *info;

  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      g_warning ("Can't set the minimal height of a stage");
      return;
    }

  info = _clutter_actor_get_layout_info (self);

  if (priv->min_height_set && min_height == info->minimum.height)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_store_old_geometry (self, &old);

  info->minimum.height = min_height;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MIN_HEIGHT]);
  clutter_actor_set_min_height_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  g_object_thaw_notify (G_OBJECT (self));

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_natural_width (ClutterActor *self,
                                 gfloat        natural_width)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };
  ClutterLayoutInfo *info;

  info = _clutter_actor_get_layout_info (self);

  if (priv->natural_width_set && natural_width == info->natural.width)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_store_old_geometry (self, &old);

  info->natural.width = natural_width;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_NATURAL_WIDTH]);
  clutter_actor_set_natural_width_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  g_object_thaw_notify (G_OBJECT (self));

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_natural_height (ClutterActor *self,
                                  gfloat        natural_height)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };
  ClutterLayoutInfo *info;

  info = _clutter_actor_get_layout_info (self);

  if (priv->natural_height_set && natural_height == info->natural.height)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_store_old_geometry (self, &old);

  info->natural.height = natural_height;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_NATURAL_HEIGHT]);
  clutter_actor_set_natural_height_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  g_object_thaw_notify (G_OBJECT (self));

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_min_width_set (ClutterActor *self,
                                 gboolean      use_min_width)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };

  if (priv->min_width_set == (use_min_width != FALSE))
    return;

  clutter_actor_store_old_geometry (self, &old);

  priv->min_width_set = use_min_width != FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MIN_WIDTH_SET]);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_min_height_set (ClutterActor *self,
                                  gboolean      use_min_height)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };

  if (priv->min_height_set == (use_min_height != FALSE))
    return;

  clutter_actor_store_old_geometry (self, &old);

  priv->min_height_set = use_min_height != FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_MIN_HEIGHT_SET]);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_natural_width_set (ClutterActor *self,
                                     gboolean      use_natural_width)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };

  if (priv->natural_width_set == (use_natural_width != FALSE))
    return;

  clutter_actor_store_old_geometry (self, &old);

  priv->natural_width_set = use_natural_width != FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_NATURAL_WIDTH_SET]);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_natural_height_set (ClutterActor *self,
                                      gboolean      use_natural_height)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActorBox old = { 0, };

  if (priv->natural_height_set == (use_natural_height != FALSE))
    return;

  clutter_actor_store_old_geometry (self, &old);

  priv->natural_height_set = use_natural_height != FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_NATURAL_HEIGHT_SET]);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_request_mode:
 * @self: a #ClutterActor
 * @mode: the request mode
 *
 * Sets the geometry request mode of @self.
 *
 * The @mode determines the order for invoking
 [method@Clutter.Actor.get_preferred_width] and
 [method@Clutter.Actor.get_preferred_height]
 */
void
clutter_actor_set_request_mode (ClutterActor       *self,
                                ClutterRequestMode  mode)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (priv->request_mode == mode)
    return;

  priv->request_mode = mode;

  priv->needs_width_request = TRUE;
  priv->needs_height_request = TRUE;

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_REQUEST_MODE]);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_get_request_mode:
 * @self: a #ClutterActor
 *
 * Retrieves the geometry request mode of @self
 *
 * Return value: the request mode for the actor
 */
ClutterRequestMode
clutter_actor_get_request_mode (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self),
                        CLUTTER_REQUEST_HEIGHT_FOR_WIDTH);

  return self->priv->request_mode;
}

/* variant of set_width() without checks and without notification
 * freeze+thaw, for internal usage only
 */
static inline void
clutter_actor_set_width_internal (ClutterActor *self,
                                  gfloat        width)
{
  if (width >= 0)
    {
      /* the Stage will use the :min-width to control the minimum
       * width to be resized to, so we should not be setting it
       * along with the :natural-width
       */
      if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
        clutter_actor_set_min_width (self, width);

      clutter_actor_set_natural_width (self, width);
    }
  else
    {
      /* we only unset the :natural-width for the Stage */
      if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
        clutter_actor_set_min_width_set (self, FALSE);

      clutter_actor_set_natural_width_set (self, FALSE);
    }
}

/* variant of set_height() without checks and without notification
 * freeze+thaw, for internal usage only
 */
static inline void
clutter_actor_set_height_internal (ClutterActor *self,
                                   gfloat        height)
{
  if (height >= 0)
    {
      /* see the comment above in set_width_internal() */
      if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
        clutter_actor_set_min_height (self, height);

      clutter_actor_set_natural_height (self, height);
    }
  else
    {
      /* see the comment above in set_width_internal() */
      if (!CLUTTER_ACTOR_IS_TOPLEVEL (self))
        clutter_actor_set_min_height_set (self, FALSE);

      clutter_actor_set_natural_height_set (self, FALSE);
    }
}

static void
clutter_actor_set_size_internal (ClutterActor          *self,
                                 const graphene_size_t *size)
{
  if (size != NULL)
    {
      clutter_actor_set_width_internal (self, size->width);
      clutter_actor_set_height_internal (self, size->height);
    }
  else
    {
      clutter_actor_set_width_internal (self, -1);
      clutter_actor_set_height_internal (self, -1);
    }
}

/**
 * clutter_actor_set_size:
 * @self: A #ClutterActor
 * @width: New width of actor in pixels, or -1
 * @height: New height of actor in pixels, or -1
 *
 * Sets the actor's size request in pixels. This overrides any
 * "normal" size request the actor would have. For example
 * a text actor might normally request the size of the text;
 * this function would force a specific size instead.
 *
 * If @width and/or @height are -1 the actor will use its
 * "normal" size request instead of overriding it, i.e.
 * you can "unset" the size with -1.
 *
 * This function sets or unsets both the minimum and natural size.
 */
void
clutter_actor_set_size (ClutterActor *self,
			gfloat        width,
			gfloat        height)
{
  graphene_size_t new_size;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  graphene_size_init (&new_size, width, height);

  /* minor optimization: if we don't have a duration then we can
   * skip the get_size() below, to avoid the chance of going through
   * get_preferred_width() and get_preferred_height() just to jump to
   * a new desired size
   */
  if (clutter_actor_get_easing_duration (self) == 0)
    {
      g_object_freeze_notify (G_OBJECT (self));

      clutter_actor_set_size_internal (self, &new_size);

      g_object_thaw_notify (G_OBJECT (self));

      return;
    }
  else
    {
      graphene_size_t cur_size;

      graphene_size_init (&cur_size,
                          clutter_actor_get_width (self),
                          clutter_actor_get_height (self));

      _clutter_actor_create_transition (self,
                                        obj_props[PROP_SIZE],
                                        &cur_size,
                                        &new_size);
    }
}

/**
 * clutter_actor_get_size:
 * @self: A #ClutterActor
 * @width: (out) (optional): return location for the width, or %NULL.
 * @height: (out) (optional): return location for the height, or %NULL.
 *
 * This function tries to "do what you mean" and return
 * the size an actor will have. If the actor has a valid
 * allocation, the allocation will be returned; otherwise,
 * the actors natural size request will be returned.
 *
 * If you care whether you get the request vs. the allocation, you
 * should probably call a different function like
 * [method@Clutter.Actor.get_allocation_box] or
 * [method@Clutter.Actor.get_preferred_width].
 */
void
clutter_actor_get_size (ClutterActor *self,
			gfloat       *width,
			gfloat       *height)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (width)
    *width = clutter_actor_get_width (self);

  if (height)
    *height = clutter_actor_get_height (self);
}

/**
 * clutter_actor_get_position:
 * @self: a #ClutterActor
 * @x: (out) (optional): return location for the X coordinate, or %NULL
 * @y: (out) (optional): return location for the Y coordinate, or %NULL
 *
 * This function tries to "do what you mean" and tell you where the
 * actor is, prior to any transformations. Retrieves the fixed
 * position of an actor in pixels, if one has been set; otherwise, if
 * the allocation is valid, returns the actor's allocated position;
 * otherwise, returns 0,0.
 *
 * The returned position is in pixels.
 */
void
clutter_actor_get_position (ClutterActor *self,
                            gfloat       *x,
                            gfloat       *y)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (x)
    *x = clutter_actor_get_x (self);

  if (y)
    *y = clutter_actor_get_y (self);
}

/**
 * clutter_actor_get_fixed_position:
 * @self: a #ClutterActor
 * @x: (out) (optional): return location for the X coordinate, or %NULL
 * @y: (out) (optional): return location for the Y coordinate, or %NULL
 *
 * This function gets the fixed position of the actor, if set. If there
 * is no fixed position set, this function returns %FALSE and doesn't set
 * the x and y coordinates.
 *
 * Returns: %TRUE if the fixed position is set, %FALSE if it isn't
 */
gboolean
clutter_actor_get_fixed_position (ClutterActor *self,
                                  float        *x,
                                  float        *y)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->position_set)
    {
      const ClutterLayoutInfo *info;

      info = _clutter_actor_get_layout_info_or_defaults (self);

      if (x)
        *x = info->fixed_pos.x;

      if (y)
        *y = info->fixed_pos.y;

      return TRUE;
    }

  return FALSE;
}

/**
 * clutter_actor_get_transformed_extents:
 * @self: A #ClutterActor
 * @rect: (out): return location for the transformed bounding rect
 *
 * Gets the transformed bounding rect of an actor, in pixels relative to the stage.
 */
void
clutter_actor_get_transformed_extents (ClutterActor    *self,
                                       graphene_rect_t *rect)
{
  graphene_quad_t quad;
  graphene_point3d_t v[4];
  ClutterActorBox box;

  box.x1 = 0;
  box.y1 = 0;
  box.x2 = clutter_actor_box_get_width (&self->priv->allocation);
  box.y2 = clutter_actor_box_get_height (&self->priv->allocation);
  if (_clutter_actor_transform_and_project_box (self, &box, v))
    {
      graphene_quad_init (&quad,
                          (graphene_point_t *) &v[0],
                          (graphene_point_t *) &v[1],
                          (graphene_point_t *) &v[2],
                          (graphene_point_t *) &v[3]);

      if (rect)
        graphene_quad_bounds (&quad, rect);
    }
}

/**
 * clutter_actor_get_transformed_position:
 * @self: A #ClutterActor
 * @x: (out) (optional): return location for the X coordinate, or %NULL
 * @y: (out) (optional): return location for the Y coordinate, or %NULL
 *
 * Gets the absolute position of an actor, in pixels relative to the stage.
 */
void
clutter_actor_get_transformed_position (ClutterActor *self,
                                        gfloat       *x,
                                        gfloat       *y)
{
  graphene_point3d_t v1;
  graphene_point3d_t v2;

  v1.x = v1.y = v1.z = 0;

  if (!_clutter_actor_fully_transform_vertices (self, &v1, &v2, 1))
    return;

  if (x)
    *x = v2.x;

  if (y)
    *y = v2.y;
}

/**
 * clutter_actor_get_transformed_size:
 * @self: A #ClutterActor
 * @width: (out) (optional): return location for the width, or %NULL
 * @height: (out) (optional): return location for the height, or %NULL
 *
 * Gets the absolute size of an actor in pixels, taking into account the
 * scaling factors.
 *
 * If the actor has a valid allocation, the allocated size will be used.
 * If the actor has not a valid allocation then the preferred size will
 * be transformed and returned.
 *
 * If you want the transformed allocation, see
 * [method@Clutter.Actor.get_abs_allocation_vertices] instead.
 *
 * When the actor (or one of its ancestors) is rotated around the
 * X or Y axis, it no longer appears as on the stage as a rectangle, but
 * as a generic quadrangle; in that case this function returns the size
 * of the smallest rectangle that encapsulates the entire quad. Please
 * note that in this case no assumptions can be made about the relative
 * position of this envelope to the absolute position of the actor, as
 * returned by [method@Clutter.Actor.get_transformed_position]; if you need this
 * information, you need to use [method@Clutter.Actor.get_abs_allocation_vertices]
 * to get the coords of the actual quadrangle.
 */
void
clutter_actor_get_transformed_size (ClutterActor *self,
                                    gfloat       *width,
                                    gfloat       *height)
{
  ClutterActorPrivate *priv;
  graphene_point3d_t v[4];
  gfloat x_min, x_max, y_min, y_max;
  gint i;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  /* if the actor hasn't been allocated yet, get the preferred
   * size and transform that
   */
  if (priv->needs_allocation)
    {
      gfloat natural_width, natural_height;
      ClutterActorBox box;

      /* Make a fake allocation to transform.
       *
       * NB: _clutter_actor_transform_and_project_box expects a box in
       * the actor's coordinate space... */

      box.x1 = 0;
      box.y1 = 0;

      natural_width = natural_height = 0;
      clutter_actor_get_preferred_size (self, NULL, NULL,
                                        &natural_width,
                                        &natural_height);

      box.x2 = natural_width;
      box.y2 = natural_height;

      _clutter_actor_transform_and_project_box (self, &box, v);
    }
  else
    clutter_actor_get_abs_allocation_vertices (self, v);

  x_min = x_max = v[0].x;
  y_min = y_max = v[0].y;

  for (i = 1; i < G_N_ELEMENTS (v); ++i)
    {
      if (v[i].x < x_min)
	x_min = v[i].x;

      if (v[i].x > x_max)
	x_max = v[i].x;

      if (v[i].y < y_min)
	y_min = v[i].y;

      if (v[i].y > y_max)
	y_max = v[i].y;
    }

  if (width)
    *width  = x_max - x_min;

  if (height)
    *height = y_max - y_min;
}

/**
 * clutter_actor_get_width:
 * @self: A #ClutterActor
 *
 * Retrieves the width of a #ClutterActor.
 *
 * If the actor has a valid allocation, this function will return the
 * width of the allocated area given to the actor.
 *
 * If the actor does not have a valid allocation, this function will
 * return the actor's natural width, that is the preferred width of
 * the actor.
 *
 * If you care whether you get the preferred width or the width that
 * has been assigned to the actor, you should probably call a different
 * function like [method@Clutter.Actor.get_allocation_box] to retrieve the
 * allocated size [method@Clutter.Actor.get_preferred_width] to retrieve the
 * preferred width.
 *
 * If an actor has a fixed width, for instance a width that has been
 * assigned using [method@Clutter.Actor.set_width], the width returned will
 * be the same value.
 *
 * Return value: the width of the actor, in pixels
 */
gfloat
clutter_actor_get_width (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  priv = self->priv;

  if (priv->needs_allocation)
    {
      gfloat natural_width = 0;

      if (priv->request_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
        {
          clutter_actor_get_preferred_width (self, -1, NULL, &natural_width);
        }
      else if (priv->request_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
        {
          gfloat natural_height = 0;

          clutter_actor_get_preferred_height (self, -1, NULL, &natural_height);
          clutter_actor_get_preferred_width (self, natural_height,
                                             NULL,
                                             &natural_width);
        }
      else if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE && priv->content != NULL)
        {
          clutter_content_get_preferred_size (priv->content, &natural_width, NULL);
        }

      return natural_width;
    }
  else
    return priv->allocation.x2 - priv->allocation.x1;
}

/**
 * clutter_actor_get_height:
 * @self: A #ClutterActor
 *
 * Retrieves the height of a #ClutterActor.
 *
 * If the actor has a valid allocation, this function will return the
 * height of the allocated area given to the actor.
 *
 * If the actor does not have a valid allocation, this function will
 * return the actor's natural height, that is the preferred height of
 * the actor.
 *
 * If you care whether you get the preferred height or the height that
 * has been assigned to the actor, you should probably call a different
 * function like [method@Clutter.Actor.get_allocation_box] to retrieve the
 * allocated size [method@Clutter.Actor.get_preferred_height] to retrieve the
 * preferred height.
 *
 * If an actor has a fixed height, for instance a height that has been
 * assigned using [method@Clutter.Actor.set_height], the height returned will
 * be the same value.
 *
 * Return value: the height of the actor, in pixels
 */
gfloat
clutter_actor_get_height (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  priv = self->priv;

  if (priv->needs_allocation)
    {
      gfloat natural_height = 0;

      if (priv->request_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
        {
          gfloat natural_width = 0;

          clutter_actor_get_preferred_width (self, -1, NULL, &natural_width);
          clutter_actor_get_preferred_height (self, natural_width,
                                              NULL, &natural_height);
        }
      else if (priv->request_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
        {
          clutter_actor_get_preferred_height (self, -1, NULL, &natural_height);
        }
      else if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE && priv->content != NULL)
        {
          clutter_content_get_preferred_size (priv->content, NULL, &natural_height);
        }

      return natural_height;
    }
  else
    return priv->allocation.y2 - priv->allocation.y1;
}

/**
 * clutter_actor_set_width:
 * @self: A #ClutterActor
 * @width: Requested new width for the actor, in pixels, or -1
 *
 * Forces a width on an actor, causing the actor's preferred width
 * and height (if any) to be ignored.
 *
 * If @width is -1 the actor will use its preferred width request
 * instead of overriding it, i.e. you can "unset" the width with -1.
 *
 * This function sets both the minimum and natural size of the actor.
 */
void
clutter_actor_set_width (ClutterActor *self,
                         gfloat        width)
{
  float cur_size;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* minor optimization: if we don't have a duration
   * then we can skip the get_width() below, to avoid
   * the chance of going through get_preferred_width()
   * just to jump to a new desired width.
   */
  if (clutter_actor_get_easing_duration (self) == 0)
    {
      g_object_freeze_notify (G_OBJECT (self));

      clutter_actor_set_width_internal (self, width);

      g_object_thaw_notify (G_OBJECT (self));

      return;
    }
  else
    cur_size = clutter_actor_get_width (self);

  _clutter_actor_create_transition (self,
                                    obj_props[PROP_WIDTH],
                                    cur_size,
                                    width);
}

/**
 * clutter_actor_set_height:
 * @self: A #ClutterActor
 * @height: Requested new height for the actor, in pixels, or -1
 *
 * Forces a height on an actor, causing the actor's preferred width
 * and height (if any) to be ignored.
 *
 * If @height is -1 the actor will use its preferred height instead of
 * overriding it, i.e. you can "unset" the height with -1.
 *
 * This function sets both the minimum and natural size of the actor.
 */
void
clutter_actor_set_height (ClutterActor *self,
                          gfloat        height)
{
  float cur_size;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* see the comment [method@Clutter.Actor.or_set_width] above */
  if (clutter_actor_get_easing_duration (self) == 0)
    {
      g_object_freeze_notify (G_OBJECT (self));

      clutter_actor_set_height_internal (self, height);

      g_object_thaw_notify (G_OBJECT (self));

      return;
    }
  else
    cur_size = clutter_actor_get_height (self);

  _clutter_actor_create_transition (self,
                                    obj_props[PROP_HEIGHT],
                                    cur_size,
                                    height);
}

static inline void
clutter_actor_set_x_internal (ClutterActor *self,
                              float         x)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterLayoutInfo *linfo;
  ClutterActorBox old = { 0, };

  linfo = _clutter_actor_get_layout_info (self);

  if (priv->position_set && linfo->fixed_pos.x == x)
    return;

  clutter_actor_store_old_geometry (self, &old);

  linfo->fixed_pos.x = x;
  clutter_actor_set_fixed_position_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

static inline void
clutter_actor_set_y_internal (ClutterActor *self,
                              float         y)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterLayoutInfo *linfo;
  ClutterActorBox old = { 0, };

  linfo = _clutter_actor_get_layout_info (self);

  if (priv->position_set && linfo->fixed_pos.y == y)
    return;

  clutter_actor_store_old_geometry (self, &old);

  linfo->fixed_pos.y = y;
  clutter_actor_set_fixed_position_set (self, TRUE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

static void
clutter_actor_set_position_internal (ClutterActor           *self,
                                     const graphene_point_t *position)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterLayoutInfo *linfo;
  ClutterActorBox old = { 0, };

  linfo = _clutter_actor_get_layout_info (self);

  if (priv->position_set &&
      graphene_point_equal (position, &linfo->fixed_pos))
    return;

  clutter_actor_store_old_geometry (self, &old);

  if (position != NULL)
    {
      linfo->fixed_pos = *position;
      clutter_actor_set_fixed_position_set (self, TRUE);
    }
  else
    clutter_actor_set_fixed_position_set (self, FALSE);

  clutter_actor_notify_if_geometry_changed (self, &old);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_x:
 * @self: a #ClutterActor
 * @x: the actor's position on the X axis
 *
 * Sets the actor's X coordinate, relative to its parent, in pixels.
 *
 * Overrides any layout manager and forces a fixed position for
 * the actor.
 *
 * The [property@Clutter.Actor:x] property is animatable.
 */
void
clutter_actor_set_x (ClutterActor *self,
                     gfloat        x)
{
  float cur_position = clutter_actor_get_x (self);

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  _clutter_actor_create_transition (self, obj_props[PROP_X],
                                    cur_position,
                                    x);
}

/**
 * clutter_actor_set_y:
 * @self: a #ClutterActor
 * @y: the actor's position on the Y axis
 *
 * Sets the actor's Y coordinate, relative to its parent, in pixels.#
 *
 * Overrides any layout manager and forces a fixed position for
 * the actor.
 *
 * The [property@Clutter.Actor:y] property is animatable.
 */
void
clutter_actor_set_y (ClutterActor *self,
                     gfloat        y)
{
  float cur_position = clutter_actor_get_y (self);

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  _clutter_actor_create_transition (self, obj_props[PROP_Y],
                                    cur_position,
                                    y);
}

/**
 * clutter_actor_get_x:
 * @self: A #ClutterActor
 *
 * Retrieves the X coordinate of a #ClutterActor.
 *
 * This function tries to "do what you mean", by returning the
 * correct value depending on the actor's state.
 *
 * If the actor has a valid allocation, this function will return
 * the X coordinate of the origin of the allocation box.
 *
 * If the actor has any fixed coordinate set using [method@Clutter.Actor.set_x],
 * [method@Clutter.Actor.set_position], this function will return that coordinate.
 *
 * If both the allocation and a fixed position are missing, this function
 * will return 0.
 *
 * Return value: the X coordinate, in pixels, ignoring any
 *   transformation (i.e. scaling, rotation)
 */
gfloat
clutter_actor_get_x (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  priv = self->priv;

  if (priv->needs_allocation)
    {
      if (priv->position_set)
        {
          const ClutterLayoutInfo *info;

          info = _clutter_actor_get_layout_info_or_defaults (self);

          return info->fixed_pos.x;
        }
      else
        return 0;
    }
  else
    return priv->allocation.x1;
}

/**
 * clutter_actor_get_y:
 * @self: A #ClutterActor
 *
 * Retrieves the Y coordinate of a #ClutterActor.
 *
 * This function tries to "do what you mean", by returning the
 * correct value depending on the actor's state.
 *
 * If the actor has a valid allocation, this function will return
 * the Y coordinate of the origin of the allocation box.
 *
 * If the actor has any fixed coordinate set using [method@Clutter.Actor.set_y],
 * [method@Clutter.Actor.set_position], this function will return that coordinate.
 *
 * If both the allocation and a fixed position are missing, this function
 * will return 0.
 *
 * Return value: the Y coordinate, in pixels, ignoring any
 *   transformation (i.e. scaling, rotation)
 */
gfloat
clutter_actor_get_y (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  priv = self->priv;

  if (priv->needs_allocation)
    {
      if (priv->position_set)
        {
          const ClutterLayoutInfo *info;

          info = _clutter_actor_get_layout_info_or_defaults (self);

          return info->fixed_pos.y;
        }
      else
        return 0;
    }
  else
    return priv->allocation.y1;
}

/**
 * clutter_actor_set_scale:
 * @self: A #ClutterActor
 * @scale_x: double factor to scale actor by horizontally.
 * @scale_y: double factor to scale actor by vertically.
 *
 * Scales an actor with the given factors.
 *
 * The scale transformation is relative the [property@Clutter.Actor:pivot-point].
 *
 * The [property@Clutter.Actor:scale-x] and [property@Clutter.Actor:scale-y]
 * properties are animatable.
 */
void
clutter_actor_set_scale (ClutterActor *self,
                         gdouble       scale_x,
                         gdouble       scale_y)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_set_scale_factor (self, CLUTTER_X_AXIS, scale_x);
  clutter_actor_set_scale_factor (self, CLUTTER_Y_AXIS, scale_y);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_set_scale_z:
 * @self: a #ClutterActor
 * @scale_z: the scaling factor along the Z axis
 *
 * Scales an actor on the Z axis by the given @scale_z factor.
 *
 * The scale transformation is relative the the [property@Clutter.Actor:pivot-point].
 *
 * The [property@Clutter.Actor:scale-z] property is animatable.
 */
void
clutter_actor_set_scale_z (ClutterActor *self,
                           gdouble       scale_z)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  clutter_actor_set_scale_factor (self, CLUTTER_Z_AXIS, scale_z);
}

/**
 * clutter_actor_get_scale:
 * @self: A #ClutterActor
 * @scale_x: (out) (optional): Location to store horizontal
 *   scale factor, or %NULL.
 * @scale_y: (out) (optional): Location to store vertical
 *   scale factor, or %NULL.
 *
 * Retrieves an actors scale factors.
 */
void
clutter_actor_get_scale (ClutterActor *self,
			 gdouble      *scale_x,
			 gdouble      *scale_y)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (scale_x)
    *scale_x = info->scale_x;

  if (scale_y)
    *scale_y = info->scale_y;
}

/**
 * clutter_actor_get_scale_z:
 * @self: A #ClutterActor
 *
 * Retrieves the scaling factor along the Z axis, as set using
 * [method@Clutter.Actor.set_scale_z].
 *
 * Return value: the scaling factor along the Z axis
 */
gdouble
clutter_actor_get_scale_z (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 1.0);

  return _clutter_actor_get_transform_info_or_defaults (self)->scale_z;
}

static inline void
clutter_actor_set_opacity_internal (ClutterActor *self,
                                    guint8        opacity)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->opacity != opacity)
    {
      priv->opacity = opacity;

      /* Queue a redraw from the flatten effect so that it can use
         its cached image if available instead of having to redraw the
         actual actor. If it doesn't end up using the FBO then the
         effect is still able to continue the paint anyway. If there
         is no flatten effect yet then this is equivalent to queueing
         a full redraw */
      _clutter_actor_queue_redraw_full (self,
                                        NULL, /* clip */
                                        priv->flatten_effect);

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_OPACITY]);
    }
}

/**
 * clutter_actor_set_opacity:
 * @self: A #ClutterActor
 * @opacity: New opacity value for the actor.
 *
 * Sets the actor's opacity, with zero being completely transparent and
 * 255 (0xff) being fully opaque.
 *
 * The [property@Clutter.Actor:opacity] property is animatable.
 */
void
clutter_actor_set_opacity (ClutterActor *self,
			   guint8        opacity)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  _clutter_actor_create_transition (self, obj_props[PROP_OPACITY],
                                    self->priv->opacity,
                                    opacity);
}

/*
 * clutter_actor_get_paint_opacity_internal:
 * @self: a #ClutterActor
 *
 * Retrieves the absolute opacity of the actor, as it appears on the stage
 *
 * This function does not do type checks
 *
 * Return value: the absolute opacity of the actor
 */
static guint8
clutter_actor_get_paint_opacity_internal (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *parent;

  /* override the top-level opacity to always be 255; even in
   * case of ClutterStage:use-alpha being TRUE we want the rest
   * of the scene to be painted
   */
  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    return 255;

  if (priv->opacity_override >= 0)
    return priv->opacity_override;

  parent = priv->parent;

  /* Factor in the actual actors opacity with parents */
  if (parent != NULL)
    {
      guint8 opacity = clutter_actor_get_paint_opacity_internal (parent);

      if (opacity != 0xff)
        return (opacity * priv->opacity) / 0xff;
    }

  return priv->opacity;

}

/**
 * clutter_actor_get_paint_opacity:
 * @self: A #ClutterActor
 *
 * Retrieves the absolute opacity of the actor, as it appears on the stage.
 *
 * This function traverses the hierarchy chain and composites the opacity of
 * the actor with that of its parents.
 *
 * This function is intended for subclasses to use in the paint virtual
 * function, to paint themselves with the correct opacity.
 *
 * Return value: The actor opacity value.
 */
guint8
clutter_actor_get_paint_opacity (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  return clutter_actor_get_paint_opacity_internal (self);
}

/**
 * clutter_actor_get_opacity:
 * @self: a #ClutterActor
 *
 * Retrieves the opacity value of an actor, as set by
 * clutter_actor_set_opacity().
 *
 * For retrieving the absolute opacity of the actor inside a paint
 * virtual function, see clutter_actor_get_paint_opacity().
 *
 * Return value: the opacity of the actor
 */
guint8
clutter_actor_get_opacity (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  return self->priv->opacity;
}

/**
 * clutter_actor_set_offscreen_redirect:
 * @self: A #ClutterActor
 * @redirect: New offscreen redirect flags for the actor.
 *
 * Defines the circumstances where the actor should be redirected into
 * an offscreen image. The offscreen image is used to flatten the
 * actor into a single image while painting for two main reasons.
 * Firstly, when the actor is painted a second time without any of its
 * contents changing it can simply repaint the cached image without
 * descending further down the actor hierarchy. Secondly, it will make
 * the opacity look correct even if there are overlapping primitives
 * in the actor.
 *
 * Caching the actor could in some cases be a performance win and in
 * some cases be a performance lose so it is important to determine
 * which value is right for an actor before modifying this value. For
 * example, there is never any reason to flatten an actor that is just
 * a single texture (such as a #ClutterTexture) because it is
 * effectively already cached in an image so the offscreen would be
 * redundant. Also if the actor contains primitives that are far apart
 * with a large transparent area in the middle (such as a large
 * CluterGroup with a small actor in the top left and a small actor in
 * the bottom right) then the cached image will contain the entire
 * image of the large area and the paint will waste time blending all
 * of the transparent pixels in the middle.
 *
 * The default method of implementing opacity on a container simply
 * forwards on the opacity to all of the children. If the children are
 * overlapping then it will appear as if they are two separate glassy
 * objects and there will be a break in the color where they
 * overlap. By redirecting to an offscreen buffer it will be as if the
 * two opaque objects are combined into one and then made transparent
 * which is usually what is expected.
 *
 * The image below demonstrates the difference between redirecting and
 * not. The image shows two Clutter groups, each containing a red and
 * a green rectangle which overlap. The opacity on the group is set to
 * 128 (which is 50%). When the offscreen redirect is not used, the
 * red rectangle can be seen through the blue rectangle as if the two
 * rectangles were separately transparent. When the redirect is used
 * the group as a whole is transparent instead so the red rectangle is
 * not visible where they overlap.
 *
 * <figure id="offscreen-redirect">
 *   <title>Sample of using an offscreen redirect for transparency</title>
 *   <graphic fileref="offscreen-redirect.png" format="PNG"/>
 * </figure>
 *
 * The default value for this property is 0, so we effectively will
 * never redirect an actor offscreen by default. This means that there
 * are times that transparent actors may look glassy as described
 * above. The reason this is the default is because there is a
 * performance trade off between quality and performance here. In many
 * cases the default form of glassy opacity looks good enough, but if
 * it's not you will need to set the
 * %CLUTTER_OFFSCREEN_REDIRECT_AUTOMATIC_FOR_OPACITY flag to enable
 * redirection for opacity.
 *
 * Custom actors that don't contain any overlapping primitives are
 * recommended to override the has_overlaps() virtual to return %FALSE
 * for maximum efficiency.
 */
void
clutter_actor_set_offscreen_redirect (ClutterActor *self,
                                      ClutterOffscreenRedirect redirect)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (priv->offscreen_redirect != redirect)
    {
      priv->offscreen_redirect = redirect;

      /* Queue a redraw from the effect so that it can use its cached
         image if available instead of having to redraw the actual
         actor. If it doesn't end up using the FBO then the effect is
         still able to continue the paint anyway. If there is no
         effect then this is equivalent to queuing a full redraw */
      _clutter_actor_queue_redraw_full (self,
                                        NULL, /* clip */
                                        priv->flatten_effect);

      g_object_notify_by_pspec (G_OBJECT (self),
                                obj_props[PROP_OFFSCREEN_REDIRECT]);
    }
}

/**
 * clutter_actor_get_offscreen_redirect:
 * @self: a #ClutterActor
 *
 * Retrieves whether to redirect the actor to an offscreen buffer, as
 * set by clutter_actor_set_offscreen_redirect().
 *
 * Return value: the value of the offscreen-redirect property of the actor
 */
ClutterOffscreenRedirect
clutter_actor_get_offscreen_redirect (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  return self->priv->offscreen_redirect;
}

/**
 * clutter_actor_set_name:
 * @self: A #ClutterActor
 * @name: (nullable): Textual tag to apply to actor
 *
 * Sets the given name to @self. The name can be used to identify
 * a #ClutterActor.
 */
void
clutter_actor_set_name (ClutterActor *self,
			const gchar  *name)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  g_set_str (&self->priv->name, name);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_NAME]);
}

/**
 * clutter_actor_get_name:
 * @self: A #ClutterActor
 *
 * Retrieves the name of @self.
 *
 * Return value: (nullable): the name of the actor, or %NULL. The returned
 *   string is owned by the actor and should not be modified or freed.
 */
const gchar *
clutter_actor_get_name (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->name;
}

static inline void
clutter_actor_set_z_position_internal (ClutterActor *self,
                                       float         z_position)
{
  ClutterTransformInfo *info;

  info = _clutter_actor_get_transform_info (self);

  if (memcmp (&info->z_position, &z_position, sizeof (float)) != 0)
    {
      info->z_position = z_position;

      transform_changed (self);

      clutter_actor_queue_redraw (self);

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_Z_POSITION]);
    }
}

/**
 * clutter_actor_set_z_position:
 * @self: a #ClutterActor
 * @z_position: the position on the Z axis
 *
 * Sets the actor's position on the Z axis.
 *
 * See [property@Clutter.Actor:z-position].
 */
void
clutter_actor_set_z_position (ClutterActor *self,
                              gfloat        z_position)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  _clutter_actor_create_transition (self, obj_props[PROP_Z_POSITION],
                                    info->z_position,
                                    z_position);
}

/**
 * clutter_actor_get_z_position:
 * @self: a #ClutterActor
 *
 * Retrieves the actor's position on the Z axis.
 *
 * Return value: the position on the Z axis.
 */
gfloat
clutter_actor_get_z_position (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_transform_info_or_defaults (self)->z_position;
}

/**
 * clutter_actor_set_pivot_point:
 * @self: a #ClutterActor
 * @pivot_x: the normalized X coordinate of the pivot point
 * @pivot_y: the normalized Y coordinate of the pivot point
 *
 * Sets the position of the [property@Clutter.Actor:pivot-point] around which the
 * scaling and rotation transformations occur.
 *
 * The pivot point's coordinates are in normalized space, with the (0, 0)
 * point being the top left corner of the actor, and the (1, 1) point being
 * the bottom right corner.
 */
void
clutter_actor_set_pivot_point (ClutterActor *self,
                               gfloat        pivot_x,
                               gfloat        pivot_y)
{
  graphene_point_t pivot = GRAPHENE_POINT_INIT (pivot_x, pivot_y);
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);
  _clutter_actor_create_transition (self, obj_props[PROP_PIVOT_POINT],
                                    &info->pivot,
                                    &pivot);
}

/**
 * clutter_actor_get_pivot_point:
 * @self: a #ClutterActor
 * @pivot_x: (out) (optional): return location for the normalized X
 *   coordinate of the pivot point, or %NULL
 * @pivot_y: (out) (optional): return location for the normalized Y
 *   coordinate of the pivot point, or %NULL
 *
 * Retrieves the coordinates of the [property@Clutter.Actor:pivot-point].
 */
void
clutter_actor_get_pivot_point (ClutterActor *self,
                               gfloat       *pivot_x,
                               gfloat       *pivot_y)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (pivot_x != NULL)
    *pivot_x = info->pivot.x;

  if (pivot_y != NULL)
    *pivot_y = info->pivot.y;
}

/**
 * clutter_actor_set_pivot_point_z:
 * @self: a #ClutterActor
 * @pivot_z: the Z coordinate of the actor's pivot point
 *
 * Sets the component on the Z axis of the [property@Clutter.Actor:pivot-point] around
 * which the scaling and rotation transformations occur.
 *
 * The @pivot_z value is expressed as a distance along the Z axis.
 */
void
clutter_actor_set_pivot_point_z (ClutterActor *self,
                                 gfloat        pivot_z)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);
  _clutter_actor_create_transition (self, obj_props[PROP_PIVOT_POINT_Z],
                                    info->pivot_z,
                                    pivot_z);
}

/**
 * clutter_actor_get_pivot_point_z:
 * @self: a #ClutterActor
 *
 * Retrieves the Z component of the [property@Clutter.Actor:pivot-point].
 */
gfloat
clutter_actor_get_pivot_point_z (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_transform_info_or_defaults (self)->pivot_z;
}

/**
 * clutter_actor_set_clip:
 * @self: A #ClutterActor
 * @xoff: X offset of the clip rectangle
 * @yoff: Y offset of the clip rectangle
 * @width: Width of the clip rectangle
 * @height: Height of the clip rectangle
 *
 * Sets clip area for @self. The clip area is always computed from the
 * upper left corner of the actor.
 */
void
clutter_actor_set_clip (ClutterActor *self,
                        gfloat        xoff,
                        gfloat        yoff,
                        gfloat        width,
                        gfloat        height)
{
  ClutterActorPrivate *priv;
  GObject *obj;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (priv->has_clip &&
      priv->clip.origin.x == xoff &&
      priv->clip.origin.y == yoff &&
      priv->clip.size.width == width &&
      priv->clip.size.height == height)
    return;

  obj = G_OBJECT (self);

  priv->clip.origin.x = xoff;
  priv->clip.origin.y = yoff;
  priv->clip.size.width = width;
  priv->clip.size.height = height;

  priv->has_clip = TRUE;

  queue_update_paint_volume (self);
  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (obj, obj_props[PROP_CLIP_RECT]);
  g_object_notify_by_pspec (obj, obj_props[PROP_HAS_CLIP]);
}

/**
 * clutter_actor_remove_clip:
 * @self: A #ClutterActor
 *
 * Removes clip area from @self.
 */
void
clutter_actor_remove_clip (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (!self->priv->has_clip)
    return;

  self->priv->has_clip = FALSE;

  queue_update_paint_volume (self);
  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HAS_CLIP]);
}

/**
 * clutter_actor_has_clip:
 * @self: a #ClutterActor
 *
 * Determines whether the actor has a clip area set or not.
 *
 * Return value: %TRUE if the actor has a clip area set.
 */
gboolean
clutter_actor_has_clip (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->has_clip;
}

/**
 * clutter_actor_get_clip:
 * @self: a #ClutterActor
 * @xoff: (out) (optional): return location for the X offset of
 *   the clip rectangle, or %NULL
 * @yoff: (out) (optional): return location for the Y offset of
 *   the clip rectangle, or %NULL
 * @width: (out) (optional): return location for the width of
 *   the clip rectangle, or %NULL
 * @height: (out) (optional): return location for the height of
 *   the clip rectangle, or %NULL
 *
 * Gets the clip area for @self, if any is set.
 */
void
clutter_actor_get_clip (ClutterActor *self,
                        gfloat       *xoff,
                        gfloat       *yoff,
                        gfloat       *width,
                        gfloat       *height)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (!priv->has_clip)
    return;

  if (xoff != NULL)
    *xoff = priv->clip.origin.x;

  if (yoff != NULL)
    *yoff = priv->clip.origin.y;

  if (width != NULL)
    *width = priv->clip.size.width;

  if (height != NULL)
    *height = priv->clip.size.height;
}

/**
 * clutter_actor_get_children:
 * @self: a #ClutterActor
 *
 * Retrieves the list of children of @self.
 *
 * Return value: (transfer container) (element-type ClutterActor): A newly
 *   allocated #GList of `ClutterActor`s. Use g_list_free() when
 *   done.
 */
GList *
clutter_actor_get_children (ClutterActor *self)
{
  ClutterActor *iter;
  GList *res;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  /* we walk the list backward so that we can use prepend(),
   * which is O(1)
   */
  for (iter = self->priv->last_child, res = NULL;
       iter != NULL;
       iter = iter->priv->prev_sibling)
    {
      res = g_list_prepend (res, iter);
    }

  return res;
}

/*< private >
 * insert_child_at_depth:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 *
 * Inserts @child inside the list of children held by @self, using
 * the depth as the insertion criteria.
 *
 * This sadly makes the insertion not O(1), but we can keep the
 * list sorted so that the painters algorithm we use for painting
 * the children will work correctly.
 */
static void
insert_child_at_depth (ClutterActor *self,
                       ClutterActor *child,
                       gpointer      dummy G_GNUC_UNUSED)
{
  ClutterActor *iter;
  float child_depth;

  child->priv->parent = self;

  child_depth =
    _clutter_actor_get_transform_info_or_defaults (child)->z_position;

  /* special-case the first child */
  if (self->priv->n_children == 0)
    {
      self->priv->first_child = child;
      self->priv->last_child = child;

      child->priv->next_sibling = NULL;
      child->priv->prev_sibling = NULL;

      return;
    }

  /* Find the right place to insert the child so that it will still be
     sorted and the child will be after all of the actors at the same
     dept */
  for (iter = self->priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    {
      float iter_depth;

      iter_depth =
        _clutter_actor_get_transform_info_or_defaults (iter)->z_position;

      if (iter_depth > child_depth)
        break;
    }

  if (iter != NULL)
    {
      ClutterActor *tmp = iter->priv->prev_sibling;

      if (tmp != NULL)
        tmp->priv->next_sibling = child;

      /* Insert the node before the found one */
      child->priv->prev_sibling = iter->priv->prev_sibling;
      child->priv->next_sibling = iter;
      iter->priv->prev_sibling = child;
    }
  else
    {
      ClutterActor *tmp = self->priv->last_child;

      if (tmp != NULL)
        tmp->priv->next_sibling = child;

      /* insert the node at the end of the list */
      child->priv->prev_sibling = self->priv->last_child;
      child->priv->next_sibling = NULL;
    }

  if (child->priv->prev_sibling == NULL)
    self->priv->first_child = child;

  if (child->priv->next_sibling == NULL)
    self->priv->last_child = child;
}

static void
insert_child_at_index (ClutterActor *self,
                       ClutterActor *child,
                       gpointer      data_)
{
  gint index_ = GPOINTER_TO_INT (data_);

  child->priv->parent = self;

  if (index_ == 0)
    {
      ClutterActor *tmp = self->priv->first_child;

      if (tmp != NULL)
        tmp->priv->prev_sibling = child;

      child->priv->prev_sibling = NULL;
      child->priv->next_sibling = tmp;
    }
  else if (index_ < 0 || index_ >= self->priv->n_children)
    {
      ClutterActor *tmp = self->priv->last_child;

      if (tmp != NULL)
        tmp->priv->next_sibling = child;

      child->priv->prev_sibling = tmp;
      child->priv->next_sibling = NULL;
    }
  else
    {
      ClutterActor *iter;
      int i;

      for (iter = self->priv->first_child, i = 0;
           iter != NULL;
           iter = iter->priv->next_sibling, i += 1)
        {
          if (index_ == i)
            {
              ClutterActor *tmp = iter->priv->prev_sibling;

              child->priv->prev_sibling = tmp;
              child->priv->next_sibling = iter;

              iter->priv->prev_sibling = child;

              if (tmp != NULL)
                tmp->priv->next_sibling = child;

              break;
            }
        }
    }

  if (child->priv->prev_sibling == NULL)
    self->priv->first_child = child;

  if (child->priv->next_sibling == NULL)
    self->priv->last_child = child;
}

static void
insert_child_above (ClutterActor *self,
                    ClutterActor *child,
                    gpointer      data)
{
  ClutterActor *sibling = data;

  child->priv->parent = self;

  if (sibling == NULL)
    sibling = self->priv->last_child;

  child->priv->prev_sibling = sibling;

  if (sibling != NULL)
    {
      ClutterActor *tmp = sibling->priv->next_sibling;

      child->priv->next_sibling = tmp;

      if (tmp != NULL)
        tmp->priv->prev_sibling = child;

      sibling->priv->next_sibling = child;
    }
  else
    child->priv->next_sibling = NULL;

  if (child->priv->prev_sibling == NULL)
    self->priv->first_child = child;

  if (child->priv->next_sibling == NULL)
    self->priv->last_child = child;
}

static void
insert_child_below (ClutterActor *self,
                    ClutterActor *child,
                    gpointer      data)
{
  ClutterActor *sibling = data;

  child->priv->parent = self;

  if (sibling == NULL)
    sibling = self->priv->first_child;

  child->priv->next_sibling = sibling;

  if (sibling != NULL)
    {
      ClutterActor *tmp = sibling->priv->prev_sibling;

      child->priv->prev_sibling = tmp;

      if (tmp != NULL)
        tmp->priv->next_sibling = child;

      sibling->priv->prev_sibling = child;
    }
  else
    child->priv->prev_sibling = NULL;

  if (child->priv->prev_sibling == NULL)
    self->priv->first_child = child;

  if (child->priv->next_sibling == NULL)
    self->priv->last_child = child;
}

typedef void (* ClutterActorAddChildFunc) (ClutterActor *parent,
                                           ClutterActor *child,
                                           gpointer      data);

typedef enum
{
  ADD_CHILD_EMIT_PARENT_SET    = 1 << 1,
  ADD_CHILD_EMIT_CHILD_ADDED   = 1 << 2,
  ADD_CHILD_CHECK_STATE        = 1 << 3,
  ADD_CHILD_NOTIFY_FIRST_LAST  = 1 << 4,
  ADD_CHILD_SHOW_ON_SET_PARENT = 1 << 5,

  /* default flags for public API */
  ADD_CHILD_DEFAULT_FLAGS    = ADD_CHILD_EMIT_PARENT_SET |
                               ADD_CHILD_EMIT_CHILD_ADDED |
                               ADD_CHILD_CHECK_STATE |
                               ADD_CHILD_NOTIFY_FIRST_LAST |
                               ADD_CHILD_SHOW_ON_SET_PARENT,
} ClutterActorAddChildFlags;

/*< private >
 * clutter_actor_add_child_internal:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 * @flags: control flags for actions
 * @add_func (closure data): delegate function
 * @data: data to pass to @add_func
 *
 * Adds @child to the list of children of @self.
 *
 * The actual insertion inside the list is delegated to @add_func: this
 * function will just set up the state, perform basic checks, and emit
 * signals.
 *
 * The @flags argument is used to perform additional operations.
 */
static inline void
clutter_actor_add_child_internal (ClutterActor              *self,
                                  ClutterActor              *child,
                                  ClutterActorAddChildFlags  flags,
                                  ClutterActorAddChildFunc   add_func,
                                  gpointer                   data)
{
  ClutterTextDirection text_dir;
  gboolean emit_parent_set, emit_child_added;
  gboolean check_state;
  gboolean notify_first_last;
  gboolean show_on_set_parent;
  ClutterActor *old_first_child, *old_last_child;
  GObject *obj;

  if (self == child)
    {
      g_warning ("Cannot add the actor '%s' to itself.",
                 _clutter_actor_get_debug_name (self));
      return;
    }

  if (child->priv->parent != NULL)
    {
      g_warning ("The actor '%s' already has a parent, '%s'. You must "
                 "use clutter_actor_remove_child() first.",
                 _clutter_actor_get_debug_name (child),
                 _clutter_actor_get_debug_name (child->priv->parent));
      return;
    }

  if (CLUTTER_ACTOR_IS_TOPLEVEL (child))
    {
      g_warning ("The actor '%s' is a top-level actor, and cannot be "
                 "a child of another actor.",
                 _clutter_actor_get_debug_name (child));
      return;
    }

  /* the following check disallows calling methods that change the stacking
   * order within the destruction sequence, by triggering a critical
   * warning first, and leaving the actor in an undefined state, which
   * then ends up being caught by an assertion.
   *
   * the reproducible sequence is:
   *
   *   - actor gets destroyed;
   *   - another actor, linked to the first, will try to change the
   *     stacking order of the first actor;
   *   - changing the stacking order is a composite operation composed
   *     by the following steps:
   *     1. ref() the child;
   *     2. remove_child_internal(), which removes the reference;
   *     3. add_child_internal(), which adds a reference;
   *   - the state of the actor is not changed between (2) and (3), as
   *     it could be an expensive recomputation;
   *   - if (3) bails out, then the actor is in an undefined state, but
   *     still alive;
   *   - the destruction sequence terminates, but the actor is unparented
   *     while its state indicates being parented instead.
   *   - assertion failure.
   *
   * the obvious fix would be to decompose each set_child_*_sibling()
   * method into proper remove_child()/add_child(), with state validation;
   * this may cause excessive work, though, and trigger a cascade of other
   * bugs in code that assumes that a change in the stacking order is an
   * atomic operation.
   *
   * another potential fix is to just remove this check here, and let
   * code doing stacking order changes inside the destruction sequence
   * of an actor continue doing the stacking changes as before; this
   * option still performs more work than necessary.
   *
   * the third fix is to silently bail out early from every
   * set_child_*_sibling() and set_child_at_index() method, and avoid
   * doing stack changes altogether; Clutter implements this last option.
   *
   * see bug: https://bugzilla.gnome.org/show_bug.cgi?id=670647
   */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (child))
    {
      g_warning ("The actor '%s' is currently being destroyed, and "
                 "cannot be added as a child of another actor.",
                 _clutter_actor_get_debug_name (child));
      return;
    }

  emit_parent_set = (flags & ADD_CHILD_EMIT_PARENT_SET) != 0;
  emit_child_added = (flags & ADD_CHILD_EMIT_CHILD_ADDED) != 0;
  check_state = (flags & ADD_CHILD_CHECK_STATE) != 0;
  notify_first_last = (flags & ADD_CHILD_NOTIFY_FIRST_LAST) != 0;
  show_on_set_parent = (flags & ADD_CHILD_SHOW_ON_SET_PARENT) != 0;

  old_first_child = self->priv->first_child;
  old_last_child = self->priv->last_child;

  obj = G_OBJECT (self);
  g_object_freeze_notify (obj);

  g_object_ref_sink (child);
  child->priv->parent = NULL;
  child->priv->next_sibling = NULL;
  child->priv->prev_sibling = NULL;

  /* delegate the actual insertion */
  add_func (self, child, data);

  g_assert (child->priv->parent == self);

  self->priv->n_children += 1;

  self->priv->age += 1;

  if (self->priv->in_cloned_branch)
    clutter_actor_push_in_cloned_branch (child, self->priv->in_cloned_branch);

  if (self->priv->unmapped_paint_branch_counter)
    push_in_paint_unmapped_branch (child, self->priv->unmapped_paint_branch_counter);

  /* children may cause their parent to expand, if they are set
   * to expand; if a child is not expanded then it cannot change
   * its parent's state. any further change later on will queue
   * an expand state check.
   *
   * this check, with the initial state of the needs_compute_expand
   * flag set to FALSE, should avoid recomputing the expand flags
   * state while building the actor tree.
   */
  if (clutter_actor_is_visible (child) &&
      (child->priv->needs_compute_expand ||
       child->priv->needs_x_expand ||
       child->priv->needs_y_expand))
    {
      clutter_actor_queue_compute_expand (self);
    }

  if (emit_parent_set)
    g_signal_emit (child, actor_signals[PARENT_SET], 0, NULL);

  if (check_state)
    {
      /* If parent is mapped or realized, we need to also be mapped or
       * realized once we're inside the parent.
       */
      clutter_actor_update_map_state (child, MAP_STATE_CHECK);

      /* propagate the parent's text direction to the child */
      text_dir = clutter_actor_get_text_direction (self);
      clutter_actor_set_text_direction (child, text_dir);
    }

  /* this may end up queueing a redraw, in case the actor is
   * not visible but the show-on-set-parent property is still
   * set.
   *
   * XXX:2.0 - remove this check and unconditionally show() the
   * actor once we remove the show-on-set-parent property
   */
  if (show_on_set_parent && child->priv->show_on_set_parent)
    clutter_actor_show (child);

  /* on the other hand, this will catch any other case where
   * the actor is supposed to be visible when it's added
   */
  if (clutter_actor_is_mapped (child))
    clutter_actor_queue_redraw (child);

  if (clutter_actor_has_mapped_clones (self))
    {
      ClutterActorPrivate *priv = self->priv;

      /* Avoid the early return in clutter_actor_queue_relayout() */
      priv->needs_width_request = FALSE;
      priv->needs_height_request = FALSE;
      priv->needs_allocation = FALSE;

      clutter_actor_queue_relayout (self);
    }

  if (emit_child_added)
    g_signal_emit (self, actor_signals[CHILD_ADDED], 0, child);

  if (notify_first_last)
    {
      if (old_first_child != self->priv->first_child)
        g_object_notify_by_pspec (obj, obj_props[PROP_FIRST_CHILD]);

      if (old_last_child != self->priv->last_child)
        g_object_notify_by_pspec (obj, obj_props[PROP_LAST_CHILD]);
    }

  g_object_thaw_notify (obj);
}

/**
 * clutter_actor_add_child:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 *
 * Adds @child to the children of @self.
 *
 * This function will acquire a reference on @child that will only
 * be released when calling [method@Clutter.Actor.remove_child].
 *
 * This function will take into consideration the depth
 * of @child, and will keep the list of children sorted.
 *
 * This function will emit the [signal@Clutter.Actor::child-added] signal
 * on @self.
 */
void
clutter_actor_add_child (ClutterActor *self,
                         ClutterActor *child)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (self != child);
  g_return_if_fail (child->priv->parent == NULL);

  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_DEFAULT_FLAGS,
                                    insert_child_at_depth,
                                    NULL);
}

/**
 * clutter_actor_insert_child_at_index:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 * @index_: the index
 *
 * Inserts @child into the list of children of @self, using the
 * given @index_. If @index_ is greater than the number of children
 * in @self, or is less than 0, then the new child is added at the end.
 *
 * This function will acquire a reference on @child that will only
 * be released when calling [method@Clutter.Actor.remove_child].
 *
 * This function will not take into consideration the depth
 * of @child.
 *
 * This function will emit the [signal@Clutter.Actor::child-added] signal
 * on @self.
 */
void
clutter_actor_insert_child_at_index (ClutterActor *self,
                                     ClutterActor *child,
                                     gint          index_)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (self != child);
  g_return_if_fail (child->priv->parent == NULL);

  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_DEFAULT_FLAGS,
                                    insert_child_at_index,
                                    GINT_TO_POINTER (index_));
}

/**
 * clutter_actor_insert_child_above:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 * @sibling: (nullable): a child of @self, or %NULL
 *
 * Inserts @child into the list of children of @self, above another
 * child of @self or, if @sibling is %NULL, above all the children
 * of @self.
 *
 * This function will acquire a reference on @child that will only
 * be released when calling [method@Clutter.Actor.remove_child].
 *
 * This function will not take into consideration the depth
 * of @child.
 *
 * This function will emit the [signal@Clutter.Actor::child-added] signal
 * on @self.
 */
void
clutter_actor_insert_child_above (ClutterActor *self,
                                  ClutterActor *child,
                                  ClutterActor *sibling)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (self != child);
  g_return_if_fail (child != sibling);
  g_return_if_fail (child->priv->parent == NULL);
  g_return_if_fail (sibling == NULL ||
                    (CLUTTER_IS_ACTOR (sibling) &&
                     sibling->priv->parent == self));

  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_DEFAULT_FLAGS,
                                    insert_child_above,
                                    sibling);
}

/**
 * clutter_actor_insert_child_below:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 * @sibling: (nullable): a child of @self, or %NULL
 *
 * Inserts @child into the list of children of @self, below another
 * child of @self or, if @sibling is %NULL, below all the children
 * of @self.
 *
 * This function will acquire a reference on @child that will only
 * be released when calling [method@Clutter.Actor.remove_child].
 *
 * This function will not take into consideration the depth
 * of @child.
 *
 * This function will emit the [signal@Clutter.Actor::child-added] signal
 * on @self.
 */
void
clutter_actor_insert_child_below (ClutterActor *self,
                                  ClutterActor *child,
                                  ClutterActor *sibling)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (self != child);
  g_return_if_fail (child != sibling);
  g_return_if_fail (child->priv->parent == NULL);
  g_return_if_fail (sibling == NULL ||
                    (CLUTTER_IS_ACTOR (sibling) &&
                     sibling->priv->parent == self));

  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_DEFAULT_FLAGS,
                                    insert_child_below,
                                    sibling);
}

/**
 * clutter_actor_get_parent:
 * @self: A #ClutterActor
 *
 * Retrieves the parent of @self.
 *
 * Return Value: (transfer none) (nullable): The #ClutterActor parent, or %NULL
 *  if no parent is set
 */
ClutterActor *
clutter_actor_get_parent (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->parent;
}

/**
 * clutter_actor_get_paint_visibility:
 * @self: A #ClutterActor
 *
 * Retrieves the 'paint' visibility of an actor recursively checking for non
 * visible parents.
 *
 * This is by definition the same as clutter_actor_is_mapped.
 *
 * Return Value: %TRUE if the actor is visible and will be painted.
 */
gboolean
clutter_actor_get_paint_visibility (ClutterActor *actor)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);

  return clutter_actor_is_mapped (actor);
}

/**
 * clutter_actor_remove_child:
 * @self: a #ClutterActor
 * @child: a #ClutterActor
 *
 * Removes @child from the children of @self.
 *
 * This function will release the reference added by
 * [method@Clutter.Actor.add_child], so if you want to keep using @child
 * you will have to acquire a referenced on it before calling this
 * function.
 *
 * This function will emit the [signal@Clutter.Actor::child-removed]
 * signal on @self.
 */
void
clutter_actor_remove_child (ClutterActor *self,
                            ClutterActor *child)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (self != child);
  g_return_if_fail (child->priv->parent != NULL);
  g_return_if_fail (child->priv->parent == self);

  clutter_actor_remove_child_internal (self, child,
                                       REMOVE_CHILD_DEFAULT_FLAGS);
}

/**
 * clutter_actor_remove_all_children:
 * @self: a #ClutterActor
 *
 * Removes all children of @self.
 *
 * This function releases the reference added by inserting a child actor
 * in the list of children of @self.
 *
 * If the reference count of a child drops to zero, the child will be
 * destroyed. If you want to ensure the destruction of all the children
 * of @self, use clutter_actor_destroy_all_children().
 */
void
clutter_actor_remove_all_children (ClutterActor *self)
{
  ClutterActorIter iter;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->n_children == 0)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, NULL))
    clutter_actor_iter_remove (&iter);

  g_object_thaw_notify (G_OBJECT (self));

  /* sanity check */
  g_assert (self->priv->first_child == NULL);
  g_assert (self->priv->last_child == NULL);
  g_assert (self->priv->n_children == 0);
}

/**
 * clutter_actor_destroy_all_children:
 * @self: a #ClutterActor
 *
 * Destroys all children of @self.
 *
 * This function releases the reference added by inserting a child
 * actor in the list of children of @self, and ensures that the
 * [signal@Clutter.Actor::destroy] signal is emitted on each child of the
 * actor.
 *
 * By default, #ClutterActor will emit the [signal@Clutter.Actor::destroy] signal
 * when its reference count drops to 0; the default handler of the
 * [signal@Clutter.Actor::destroy] signal will destroy all the children of an
 * actor. This function ensures that all children are destroyed, instead
 * of just removed from @self, unlike [method@Clutter.Actor.remove_all_children]
 * which will merely release the reference and remove each child.
 *
 * Unless you acquired an additional reference on each child of @self
 * prior to calling [method@Clutter.Actor.remove_all_children] and want to reuse
 * the actors, you should use [method@Clutter.Actor.destroy_all_children] in
 * order to make sure that children are destroyed and signal handlers
 * are disconnected even in cases where circular references prevent this
 * from automatically happening through reference counting alone.
 */
void
clutter_actor_destroy_all_children (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->n_children == 0)
    return;

  g_object_freeze_notify (G_OBJECT (self));

  while (self->priv->first_child != NULL)
    {
      gint prev_n_children = self->priv->n_children;

      clutter_actor_destroy (self->priv->first_child);

      g_assert (self->priv->n_children < prev_n_children);
    }

  g_object_thaw_notify (G_OBJECT (self));

  /* sanity check */
  g_assert (self->priv->first_child == NULL);
  g_assert (self->priv->last_child == NULL);
  g_assert (self->priv->n_children == 0);
}

typedef struct _InsertBetweenData {
  ClutterActor *prev_sibling;
  ClutterActor *next_sibling;
} InsertBetweenData;

static void
insert_child_between (ClutterActor *self,
                      ClutterActor *child,
                      gpointer      data_)
{
  InsertBetweenData *data = data_;
  ClutterActor *prev_sibling = data->prev_sibling;
  ClutterActor *next_sibling = data->next_sibling;

  child->priv->parent = self;
  child->priv->prev_sibling = prev_sibling;
  child->priv->next_sibling = next_sibling;

  if (prev_sibling != NULL)
    prev_sibling->priv->next_sibling = child;

  if (next_sibling != NULL)
    next_sibling->priv->prev_sibling = child;

  if (child->priv->prev_sibling == NULL)
    self->priv->first_child = child;

  if (child->priv->next_sibling == NULL)
    self->priv->last_child = child;
}

/**
 * clutter_actor_replace_child:
 * @self: a #ClutterActor
 * @old_child: the child of @self to replace
 * @new_child: the #ClutterActor to replace @old_child
 *
 * Replaces @old_child with @new_child in the list of children of @self.
 */
void
clutter_actor_replace_child (ClutterActor *self,
                             ClutterActor *old_child,
                             ClutterActor *new_child)
{
  ClutterActor *prev_sibling, *next_sibling;
  InsertBetweenData clos;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (old_child));
  g_return_if_fail (old_child->priv->parent == self);
  g_return_if_fail (CLUTTER_IS_ACTOR (new_child));
  g_return_if_fail (old_child != new_child);
  g_return_if_fail (new_child != self);
  g_return_if_fail (new_child->priv->parent == NULL);

  prev_sibling = old_child->priv->prev_sibling;
  next_sibling = old_child->priv->next_sibling;
  clutter_actor_remove_child_internal (self, old_child,
                                       REMOVE_CHILD_DEFAULT_FLAGS);

  clos.prev_sibling = prev_sibling;
  clos.next_sibling = next_sibling;
  clutter_actor_add_child_internal (self, new_child,
                                    ADD_CHILD_DEFAULT_FLAGS,
                                    insert_child_between,
                                    &clos);
}

/**
 * clutter_actor_contains:
 * @self: A #ClutterActor
 * @descendant: A #ClutterActor, possibly contained in @self
 *
 * Determines if @descendant is contained inside @self (either as an
 * immediate child, or as a deeper descendant). If @self and
 * @descendant point to the same actor then it will also return %TRUE.
 *
 * Return value: whether @descendent is contained within @self
 */
gboolean
clutter_actor_contains (ClutterActor *self,
			ClutterActor *descendant)
{
  ClutterActor *actor;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (descendant), FALSE);

  for (actor = descendant; actor; actor = actor->priv->parent)
    if (actor == self)
      return TRUE;

  return FALSE;
}

/**
 * clutter_actor_set_child_above_sibling:
 * @self: a #ClutterActor
 * @child: a #ClutterActor child of @self
 * @sibling: (nullable): a #ClutterActor child of @self, or %NULL
 *
 * Sets @child to be above @sibling in the list of children of @self.
 *
 * If @sibling is %NULL, @child will be the new last child of @self.
 *
 * This function is logically equivalent to removing @child and using
 * clutter_actor_insert_child_above(), but it will not emit signals
 * or change state on @child.
 */
void
clutter_actor_set_child_above_sibling (ClutterActor *self,
                                       ClutterActor *child,
                                       ClutterActor *sibling)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (child->priv->parent == self);
  g_return_if_fail (child != sibling);
  g_return_if_fail (sibling == NULL || CLUTTER_IS_ACTOR (sibling));

  if (sibling != NULL)
    g_return_if_fail (sibling->priv->parent == self);

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self) ||
      CLUTTER_ACTOR_IN_DESTRUCTION (child) ||
      (sibling != NULL && CLUTTER_ACTOR_IN_DESTRUCTION (sibling)))
    return;

  /* we don't want to change the state of child, or emit signals, or
   * regenerate ChildMeta instances here, but we still want to follow
   * the correct sequence of steps encoded in remove_child() and
   * add_child(), so that correctness is ensured, and we only go
   * through one known code path.
   */
  g_object_ref (child);
  clutter_actor_remove_child_internal (self, child, 0);
  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_NOTIFY_FIRST_LAST,
                                    insert_child_above,
                                    sibling);
  g_object_unref(child);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_child_below_sibling:
 * @self: a #ClutterActor
 * @child: a #ClutterActor child of @self
 * @sibling: (nullable): a #ClutterActor child of @self, or %NULL
 *
 * Sets @child to be below @sibling in the list of children of @self.
 *
 * If @sibling is %NULL, @child will be the new first child of @self.
 *
 * This function is logically equivalent to removing @self and using
 * clutter_actor_insert_child_below(), but it will not emit signals
 * or change state on @child.
 */
void
clutter_actor_set_child_below_sibling (ClutterActor *self,
                                       ClutterActor *child,
                                       ClutterActor *sibling)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (child->priv->parent == self);
  g_return_if_fail (child != sibling);
  g_return_if_fail (sibling == NULL || CLUTTER_IS_ACTOR (sibling));

  if (sibling != NULL)
    g_return_if_fail (sibling->priv->parent == self);

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self) ||
      CLUTTER_ACTOR_IN_DESTRUCTION (child) ||
      (sibling != NULL && CLUTTER_ACTOR_IN_DESTRUCTION (sibling)))
    return;

  /* see the comment in set_child_above_sibling() */
  g_object_ref (child);
  clutter_actor_remove_child_internal (self, child, 0);
  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_NOTIFY_FIRST_LAST,
                                    insert_child_below,
                                    sibling);
  g_object_unref(child);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_child_at_index:
 * @self: a #ClutterActor
 * @child: a #ClutterActor child of @self
 * @index_: the new index for @child
 *
 * Changes the index of @child in the list of children of @self.
 *
 * This function is logically equivalent to removing @child and
 * calling clutter_actor_insert_child_at_index(), but it will not
 * emit signals or change state on @child.
 */
void
clutter_actor_set_child_at_index (ClutterActor *self,
                                  ClutterActor *child,
                                  gint          index_)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTOR (child));
  g_return_if_fail (child->priv->parent == self);
  g_return_if_fail (index_ <= self->priv->n_children);

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self) ||
      CLUTTER_ACTOR_IN_DESTRUCTION (child))
    return;

  g_object_ref (child);
  clutter_actor_remove_child_internal (self, child, 0);
  clutter_actor_add_child_internal (self, child,
                                    ADD_CHILD_NOTIFY_FIRST_LAST,
                                    insert_child_at_index,
                                    GINT_TO_POINTER (index_));
  g_object_unref (child);

  clutter_actor_queue_relayout (self);
}

/*
 * Event handling
 */

/**
 * clutter_actor_event:
 * @actor: a #ClutterActor
 * @event: a #ClutterEvent
 * @capture: %TRUE if event in in capture phase, %FALSE otherwise.
 *
 * This function is used to emit an event on the main stage.
 * You should rarely need to use this function, except for
 * synthetising events.
 *
 * Return value: the return value from the signal emission: %TRUE
 *   if the actor handled the event, or %FALSE if the event was
 *   not handled
 */
gboolean
clutter_actor_event (ClutterActor       *actor,
                     const ClutterEvent *event,
                     gboolean            capture)
{
  gboolean retval = FALSE;
  gint signal_num = -1;
  GQuark detail = 0;
  ClutterEventType event_type;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  COGL_TRACE_BEGIN_SCOPED (Event, "Clutter::Actor::event()");
  COGL_TRACE_DESCRIBE (Event, _clutter_actor_get_debug_name (actor));

  g_object_ref (actor);

  event_type = clutter_event_type (event);

  switch (event_type)
    {
    case CLUTTER_NOTHING:
      break;
    case CLUTTER_BUTTON_PRESS:
      signal_num = BUTTON_PRESS_EVENT;
      detail = quark_button;
      break;
    case CLUTTER_BUTTON_RELEASE:
      signal_num = BUTTON_RELEASE_EVENT;
      detail = quark_button;
      break;
    case CLUTTER_SCROLL:
      signal_num = SCROLL_EVENT;
      detail = quark_scroll;
      break;
    case CLUTTER_KEY_PRESS:
      signal_num = KEY_PRESS_EVENT;
      detail = quark_key;
      break;
    case CLUTTER_KEY_RELEASE:
      signal_num = KEY_RELEASE_EVENT;
      detail = quark_key;
      break;
    case CLUTTER_MOTION:
      signal_num = MOTION_EVENT;
      detail = quark_motion;
      break;
    case CLUTTER_ENTER:
      signal_num = ENTER_EVENT;
      detail = quark_pointer_focus;
      break;
    case CLUTTER_LEAVE:
      signal_num = LEAVE_EVENT;
      detail = quark_pointer_focus;
      break;
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_END:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCH_CANCEL:
      signal_num = TOUCH_EVENT;
      detail = quark_touch;
      break;
    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_HOLD:
      signal_num = -1;
      detail = quark_touchpad;
      break;
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_PROXIMITY_OUT:
      signal_num = -1;
      detail = quark_proximity;
      break;
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
    case CLUTTER_PAD_STRIP:
    case CLUTTER_PAD_RING:
    case CLUTTER_PAD_DIAL:
      signal_num = -1;
      detail = quark_pad;
      break;
    case CLUTTER_IM_COMMIT:
    case CLUTTER_IM_DELETE:
    case CLUTTER_IM_PREEDIT:
      signal_num = -1;
      detail = quark_im;
    case CLUTTER_DEVICE_ADDED:
    case CLUTTER_DEVICE_REMOVED:
      break;
    case CLUTTER_EVENT_LAST:  /* Just keep compiler warnings quiet */
      break;
    }

  if (capture)
    g_signal_emit (actor, actor_signals[CAPTURED_EVENT], detail, event, &retval);
  else
    {
      g_signal_emit (actor, actor_signals[EVENT], detail, event, &retval);

      if (!retval && signal_num != -1)
        g_signal_emit (actor, actor_signals[signal_num], 0, event, &retval);
    }

  g_object_unref (actor);

  if (event_type == CLUTTER_ENTER || event_type == CLUTTER_LEAVE)
    {
      g_warn_if_fail (retval == CLUTTER_EVENT_PROPAGATE);
      return CLUTTER_EVENT_PROPAGATE;
    }

  return retval;
}

/**
 * clutter_actor_set_reactive:
 * @actor: a #ClutterActor
 * @reactive: whether the actor should be reactive to events
 *
 * Sets @actor as reactive. Reactive actors will receive events.
 */
void
clutter_actor_set_reactive (ClutterActor *actor,
                            gboolean      reactive)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  priv = actor->priv;

  if (reactive == clutter_actor_get_reactive (actor))
    return;

  if (reactive)
    actor->flags |= CLUTTER_ACTOR_REACTIVE;
  else
    actor->flags &= ~CLUTTER_ACTOR_REACTIVE;

  g_object_notify_by_pspec (G_OBJECT (actor), obj_props[PROP_REACTIVE]);

  if (reactive)
    {
      clutter_actor_add_accessible_state (actor, ATK_STATE_SENSITIVE);
      clutter_actor_add_accessible_state (actor, ATK_STATE_ENABLED);
    }
  else
    {
      clutter_actor_remove_accessible_state (actor, ATK_STATE_SENSITIVE);
      clutter_actor_remove_accessible_state (actor, ATK_STATE_ENABLED);
    }

  if (!clutter_actor_get_reactive (actor) && priv->n_pointers > 0)
    {
      ClutterActor *stage = _clutter_actor_get_stage_internal (actor);

      clutter_stage_invalidate_focus (CLUTTER_STAGE (stage), actor);
    }
  else if (clutter_actor_get_reactive (actor))
    {
      ClutterActor *parent;

      /* Check whether the closest parent has pointer focus,
       * and whether it should move to this actor.
       */
      parent = priv->parent;

      while (parent)
        {
          if (clutter_actor_get_reactive (parent))
            break;

          parent = parent->priv->parent;
        }

      if (parent && parent->priv->n_pointers > 0)
        {
          ClutterActor *stage = _clutter_actor_get_stage_internal (actor);

          clutter_stage_maybe_invalidate_focus (CLUTTER_STAGE (stage), parent);
        }
    }
}

/**
 * clutter_actor_get_reactive:
 * @actor: a #ClutterActor
 *
 * Checks whether @actor is marked as reactive.
 *
 * Return value: %TRUE if the actor is reactive
 */
gboolean
clutter_actor_get_reactive (ClutterActor *actor)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);

  return (actor->flags & CLUTTER_ACTOR_REACTIVE) != FALSE;
}

void
clutter_actor_set_no_layout (ClutterActor *actor,
                             gboolean      no_layout)
{
 g_return_if_fail (CLUTTER_IS_ACTOR (actor));

 if (no_layout == clutter_actor_is_no_layout (actor))
   return;

 if (no_layout)
   actor->flags |= CLUTTER_ACTOR_NO_LAYOUT;
 else
   actor->flags &= ~CLUTTER_ACTOR_NO_LAYOUT;
}

/**
* clutter_actor_is_no_layout:
* @actor: a #ClutterActor
*
* Checks whether @actor is marked as no layout.
*
* That means the @actor provides an explicit layout management
* policy for its children; this will prevent Clutter from automatic
* queueing of relayout and will defer all layouting to the actor itself
*
* Return value: %TRUE if the actor is marked as no layout
*/
gboolean
clutter_actor_is_no_layout (ClutterActor *actor)
{
 g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);

 return (actor->flags & CLUTTER_ACTOR_NO_LAYOUT) != FALSE;
}


static void
clutter_actor_store_content_box (ClutterActor *self,
                                 const ClutterActorBox *box)
{
  if (box != NULL)
    {
      self->priv->content_box = *box;
      self->priv->content_box_valid = TRUE;
    }
  else
    self->priv->content_box_valid = FALSE;

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONTENT_BOX]);
}

static gboolean
get_layout_from_animation_property (ClutterActor  *actor,
                                    const gchar   *name,
                                    gchar        **name_p)
{
  g_auto (GStrv) tokens = NULL;

  if (!g_str_has_prefix (name, "@layout"))
    return FALSE;

  tokens = g_strsplit (name, ".", -1);
  if (tokens[0] == NULL || tokens[1] == NULL || tokens[2] != NULL)
    {
      CLUTTER_NOTE (ANIMATION, "Invalid property name '%s'",
                    name + 1);
      return FALSE;
    }

  if (name_p != NULL)
    *name_p = g_steal_pointer (&tokens[1]);

  return TRUE;
}

static gboolean
get_content_from_animation_property (ClutterActor  *actor,
                                     const gchar   *name,
                                     gchar        **name_p)
{
  g_auto (GStrv) tokens = NULL;

  if (!g_str_has_prefix (name, "@content"))
    return FALSE;

  if (!actor->priv->content)
    {
      CLUTTER_NOTE (ANIMATION, "No ClutterContent available for '%s'",
                    name + 1);
      return FALSE;
    }

  tokens = g_strsplit (name, ".", -1);
  if (tokens[0] == NULL || tokens[1] == NULL || tokens[2] != NULL)
    {
      CLUTTER_NOTE (ANIMATION, "Invalid property name '%s'",
                    name + 1);
      return FALSE;
    }

  if (name_p != NULL)
    *name_p = g_steal_pointer (&tokens[1]);

  return TRUE;
}

static ClutterActorMeta *
get_meta_from_animation_property (ClutterActor  *actor,
                                  const gchar   *name,
                                  gchar        **name_p)
{
  ClutterActorPrivate *priv = actor->priv;
  ClutterActorMeta *meta = NULL;
  g_auto (GStrv) tokens = NULL;

  /* if this is not a special property, fall through */
  if (name[0] != '@')
    return NULL;

  /* detect the properties named using the following spec:
   *
   *   @<section>.<meta-name>.<property-name>
   *
   * where <section> can be one of the following:
   *
   *   - actions
   *   - constraints
   *   - effects
   *
   * and <meta-name> is the name set on a specific ActorMeta
   */

  tokens = g_strsplit (name + 1, ".", -1);
  if (g_strv_length (tokens) != 3)
    {
      CLUTTER_NOTE (ANIMATION, "Invalid property name '%s'",
                    name + 1);
      return NULL;
    }

  if (strcmp (tokens[0], "actions") == 0)
    meta = _clutter_meta_group_get_meta (priv->actions, tokens[1]);

  if (strcmp (tokens[0], "constraints") == 0)
    meta = _clutter_meta_group_get_meta (priv->constraints, tokens[1]);

  if (strcmp (tokens[0], "effects") == 0)
    meta = _clutter_meta_group_get_meta (priv->effects, tokens[1]);

  if (name_p != NULL)
    *name_p = g_steal_pointer (&tokens[2]);

  CLUTTER_NOTE (ANIMATION,
                "Looking for property '%s' of object '%s' in section '%s'",
                tokens[2],
                tokens[1],
                tokens[0]);

  return meta;
}

static GParamSpec *
clutter_actor_find_property (ClutterAnimatable *animatable,
                             const gchar       *property_name)
{
  ClutterActor *actor = CLUTTER_ACTOR (animatable);
  ClutterActorMeta *meta = NULL;
  GObjectClass *klass = NULL;
  GParamSpec *pspec = NULL;
  g_autofree char *p_name = NULL;
  gboolean use_content = FALSE;
  gboolean use_layout;

  use_layout = get_layout_from_animation_property (actor,
                                                   property_name,
                                                   &p_name);

  if (!use_layout)
    use_content = get_content_from_animation_property (actor,
                                                       property_name,
                                                       &p_name);

  if (!use_layout && !use_content)
    meta = get_meta_from_animation_property (actor,
                                             property_name,
                                             &p_name);

  if (meta != NULL)
    {
      klass = G_OBJECT_GET_CLASS (meta);

      pspec = g_object_class_find_property (klass, p_name);
    }
  else if (use_layout)
    {
      klass = G_OBJECT_GET_CLASS (actor->priv->layout_manager);

      pspec = g_object_class_find_property (klass, p_name);
    }
  else if (use_content)
    {
      klass = G_OBJECT_GET_CLASS (actor->priv->content);

      pspec = g_object_class_find_property (klass, p_name);
    }
  else
    {
      klass = G_OBJECT_GET_CLASS (animatable);

      pspec = g_object_class_find_property (klass, property_name);
    }

  return pspec;
}

static void
clutter_actor_get_initial_state (ClutterAnimatable *animatable,
                                 const gchar       *property_name,
                                 GValue            *initial)
{
  ClutterActor *actor = CLUTTER_ACTOR (animatable);
  ClutterActorMeta *meta = NULL;
  g_autofree char *p_name = NULL;
  gboolean use_content = FALSE;
  gboolean use_layout;

  use_layout = get_layout_from_animation_property (actor,
                                                   property_name,
                                                   &p_name);

  if (!use_layout)
    use_content = get_content_from_animation_property (actor,
                                                       property_name,
                                                       &p_name);

  if (!use_layout && !use_content)
    meta = get_meta_from_animation_property (actor,
                                             property_name,
                                             &p_name);

  if (meta != NULL)
    g_object_get_property (G_OBJECT (meta), p_name, initial);
  else if (use_layout)
    g_object_get_property (G_OBJECT (actor->priv->layout_manager), p_name, initial);
  else if (use_content)
    g_object_get_property (G_OBJECT (actor->priv->content), p_name, initial);
  else
    g_object_get_property (G_OBJECT (animatable), property_name, initial);
}

/*
 * clutter_actor_set_animatable_property:
 * @actor: a #ClutterActor
 * @prop_id: the paramspec id
 * @value: the value to set
 * @pspec: the paramspec
 *
 * Sets values of animatable properties.
 *
 * This is a variant of clutter_actor_set_property() that gets called
 * by the #ClutterAnimatable implementation of #ClutterActor for the
 * properties with the %CLUTTER_PARAM_ANIMATABLE flag set on their
 * #GParamSpec.
 *
 * Unlike the implementation of #GObjectClass.set_property(), this
 * function will not update the interval if a transition involving an
 * animatable property is in progress - this avoids cycles with the
 * transition API calling the public API.
 */
static void
clutter_actor_set_animatable_property (ClutterActor *actor,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  GObject *obj = G_OBJECT (actor);

  g_object_freeze_notify (obj);

  switch (prop_id)
    {
    case PROP_X:
      clutter_actor_set_x_internal (actor, g_value_get_float (value));
      break;

    case PROP_Y:
      clutter_actor_set_y_internal (actor, g_value_get_float (value));
      break;

    case PROP_POSITION:
      clutter_actor_set_position_internal (actor, g_value_get_boxed (value));
      break;

    case PROP_WIDTH:
      clutter_actor_set_width_internal (actor, g_value_get_float (value));
      break;

    case PROP_HEIGHT:
      clutter_actor_set_height_internal (actor, g_value_get_float (value));
      break;

    case PROP_SIZE:
      clutter_actor_set_size_internal (actor, g_value_get_boxed (value));
      break;

    case PROP_ALLOCATION:
      clutter_actor_allocate_internal (actor, g_value_get_boxed (value));
      clutter_actor_queue_redraw (actor);
      break;

    case PROP_Z_POSITION:
      clutter_actor_set_z_position_internal (actor, g_value_get_float (value));
      break;

    case PROP_OPACITY:
      clutter_actor_set_opacity_internal (actor, g_value_get_uint (value));
      break;

    case PROP_BACKGROUND_COLOR:
      clutter_actor_set_background_color_internal (actor, cogl_value_get_color (value));
      break;

    case PROP_PIVOT_POINT:
      clutter_actor_set_pivot_point_internal (actor, g_value_get_boxed (value));
      break;

    case PROP_PIVOT_POINT_Z:
      clutter_actor_set_pivot_point_z_internal (actor, g_value_get_float (value));
      break;

    case PROP_TRANSLATION_X:
    case PROP_TRANSLATION_Y:
    case PROP_TRANSLATION_Z:
      clutter_actor_set_translation_internal (actor,
                                              g_value_get_float (value),
                                              pspec);
      break;

    case PROP_SCALE_X:
    case PROP_SCALE_Y:
    case PROP_SCALE_Z:
      clutter_actor_set_scale_factor_internal (actor,
                                               g_value_get_double (value),
                                               pspec);
      break;

    case PROP_ROTATION_ANGLE_X:
    case PROP_ROTATION_ANGLE_Y:
    case PROP_ROTATION_ANGLE_Z:
      clutter_actor_set_rotation_angle_internal (actor,
                                                 g_value_get_double (value),
                                                 pspec);
      break;

    case PROP_CONTENT_BOX:
      clutter_actor_store_content_box (actor, g_value_get_boxed (value));
      break;

    case PROP_MARGIN_TOP:
    case PROP_MARGIN_BOTTOM:
    case PROP_MARGIN_LEFT:
    case PROP_MARGIN_RIGHT:
      clutter_actor_set_margin_internal (actor, g_value_get_float (value),
                                         pspec);
      break;

    case PROP_TRANSFORM:
      clutter_actor_set_transform_internal (actor, g_value_get_boxed (value));
      break;

    case PROP_CHILD_TRANSFORM:
      clutter_actor_set_child_transform_internal (actor, g_value_get_boxed (value));
      break;

    default:
      g_object_set_property (obj, pspec->name, value);
      break;
    }

  g_object_thaw_notify (obj);
}

static void
clutter_actor_update_devices (ClutterActor *self)
{
  ClutterStage *stage;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (self));
  if (stage)
    clutter_stage_invalidate_devices (stage);
}

static void
clutter_actor_set_final_state (ClutterAnimatable *animatable,
                               const gchar       *property_name,
                               const GValue      *final)
{
  ClutterActor *actor = CLUTTER_ACTOR (animatable);
  ClutterActorMeta *meta = NULL;
  g_autofree char *p_name = NULL;
  gboolean use_content = FALSE;
  gboolean use_layout;

  use_layout = get_layout_from_animation_property (actor,
                                                   property_name,
                                                   &p_name);

  if (!use_layout)
    use_content = get_content_from_animation_property (actor,
                                                       property_name,
                                                       &p_name);

  if (!use_layout && !use_content)
    meta = get_meta_from_animation_property (actor,
                                             property_name,
                                             &p_name);

  if (meta != NULL)
    g_object_set_property (G_OBJECT (meta), p_name, final);
  else if (use_layout)
    g_object_set_property (G_OBJECT (actor->priv->layout_manager), p_name, final);
  else if (use_content)
    g_object_set_property (G_OBJECT (actor->priv->content), p_name, final);
  else
    {
      GObjectClass *obj_class = G_OBJECT_GET_CLASS (animatable);
      GParamSpec *pspec;

      pspec = g_object_class_find_property (obj_class, property_name);

      if (pspec != NULL)
        {
          if ((pspec->flags & CLUTTER_PARAM_ANIMATABLE) != 0)
            {
              /* XXX - I'm going to the special hell for this */
              clutter_actor_set_animatable_property (actor, pspec->param_id, final, pspec);
            }
          else
            g_object_set_property (G_OBJECT (animatable), pspec->name, final);
        }
    }
}

static ClutterActor *
clutter_actor_get_actor (ClutterAnimatable *animatable)
{
  return CLUTTER_ACTOR (animatable);
}

static void
clutter_animatable_iface_init (ClutterAnimatableInterface *iface)
{
  iface->find_property = clutter_actor_find_property;
  iface->get_initial_state = clutter_actor_get_initial_state;
  iface->set_final_state = clutter_actor_set_final_state;
  iface->get_actor = clutter_actor_get_actor;
}

/**
 * clutter_actor_transform_stage_point:
 * @self: A #ClutterActor
 * @x: (in): x screen coordinate of the point to unproject
 * @y: (in): y screen coordinate of the point to unproject
 * @x_out: (out) (nullable): return location for the unprojected x coordinance
 * @y_out: (out) (nullable): return location for the unprojected y coordinance
 *
 * This function translates screen coordinates (@x, @y) to
 * coordinates relative to the actor. For example, it can be used to translate
 * screen events from global screen coordinates into actor-local coordinates.
 *
 * The conversion can fail, notably if the transform stack results in the
 * actor being projected on the screen as a mere line.
 *
 * The conversion should not be expected to be pixel-perfect due to the
 * nature of the operation. In general the error grows when the skewing
 * of the actor rectangle on screen increases.
 *
 * This function can be computationally intensive.
 *
 * This function only works when the allocation is up-to-date, i.e. inside of
 * the [vfunc@Clutter.Actor.paint] implementation
 *
 * Return value: %TRUE if conversion was successful.
 */
gboolean
clutter_actor_transform_stage_point (ClutterActor *self,
				     gfloat        x,
				     gfloat        y,
				     gfloat       *x_out,
				     gfloat       *y_out)
{
  graphene_point3d_t v[4];
  double ST[3][3];
  double RQ[3][3];
  int du, dv;
  double px, py;
  double det;
  float xf, yf, wf;
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  priv = self->priv;

  /* This implementation is based on the quad -> quad projection algorithm
   * described by Paul Heckbert in:
   *
   *   http://www.cs.cmu.edu/~ph/texfund/texfund.pdf
   *
   * and the sample implementation at:
   *
   *   http://www.cs.cmu.edu/~ph/src/texfund/
   *
   * Our texture is a rectangle with origin [0, 0], so we are mapping from
   * quad to rectangle only, which significantly simplifies things.
   * Function calls have been unrolled.
   */
  clutter_actor_get_abs_allocation_vertices (self, v);

  /* Keeping these as ints simplifies the multiplication (no significant
   * loss of precision here).
   */
  du = (int) ceilf (priv->allocation.x2 - priv->allocation.x1);
  dv = (int) ceilf (priv->allocation.y2 - priv->allocation.y1);

  if (du == 0 || dv == 0)
    return FALSE;

#define DET(a,b,c,d)    (((a) * (d)) - ((b) * (c)))

  /* First, find mapping from unit uv square to xy quadrilateral; this
   * equivalent to the pmap_square_quad() functions in the sample
   * implementation, which we can simplify, since our target is always
   * a rectangle.
   */
  px = v[0].x - v[1].x + v[3].x - v[2].x;
  py = v[0].y - v[1].y + v[3].y - v[2].y;

  if ((int) px == 0 && (int) py == 0)
    {
      /* affine transform */
      RQ[0][0] = v[1].x - v[0].x;
      RQ[1][0] = v[3].x - v[1].x;
      RQ[2][0] = v[0].x;
      RQ[0][1] = v[1].y - v[0].y;
      RQ[1][1] = v[3].y - v[1].y;
      RQ[2][1] = v[0].y;
      RQ[0][2] = 0.0;
      RQ[1][2] = 0.0;
      RQ[2][2] = 1.0;
    }
  else
    {
      /* projective transform */
      double dx1, dx2, dy1, dy2;

      dx1 = v[1].x - v[3].x;
      dx2 = v[2].x - v[3].x;
      dy1 = v[1].y - v[3].y;
      dy2 = v[2].y - v[3].y;

      det = DET (dx1, dx2, dy1, dy2);
      if (fabs (det) <= DBL_EPSILON)
        return FALSE;

      RQ[0][2] = DET (px, dx2, py, dy2) / det;
      RQ[1][2] = DET (dx1, px, dy1, py) / det;
      RQ[2][2] = 1.0;
      RQ[0][0] = v[1].x - v[0].x + (RQ[0][2] * v[1].x);
      RQ[1][0] = v[2].x - v[0].x + (RQ[1][2] * v[2].x);
      RQ[2][0] = v[0].x;
      RQ[0][1] = v[1].y - v[0].y + (RQ[0][2] * v[1].y);
      RQ[1][1] = v[2].y - v[0].y + (RQ[1][2] * v[2].y);
      RQ[2][1] = v[0].y;
    }

  /*
   * Now combine with transform from our rectangle (u0,v0,u1,v1) to unit
   * square. Since our rectangle is based at 0,0 we only need to scale.
   */
  RQ[0][0] /= du;
  RQ[1][0] /= dv;
  RQ[0][1] /= du;
  RQ[1][1] /= dv;
  RQ[0][2] /= du;
  RQ[1][2] /= dv;

  /*
   * Now RQ is transform from uv rectangle to xy quadrilateral; we need an
   * inverse of that.
   */
  ST[0][0] = DET (RQ[1][1], RQ[1][2], RQ[2][1], RQ[2][2]);
  ST[1][0] = DET (RQ[1][2], RQ[1][0], RQ[2][2], RQ[2][0]);
  ST[2][0] = DET (RQ[1][0], RQ[1][1], RQ[2][0], RQ[2][1]);
  ST[0][1] = DET (RQ[2][1], RQ[2][2], RQ[0][1], RQ[0][2]);
  ST[1][1] = DET (RQ[2][2], RQ[2][0], RQ[0][2], RQ[0][0]);
  ST[2][1] = DET (RQ[2][0], RQ[2][1], RQ[0][0], RQ[0][1]);
  ST[0][2] = DET (RQ[0][1], RQ[0][2], RQ[1][1], RQ[1][2]);
  ST[1][2] = DET (RQ[0][2], RQ[0][0], RQ[1][2], RQ[1][0]);
  ST[2][2] = DET (RQ[0][0], RQ[0][1], RQ[1][0], RQ[1][1]);

  /*
   * Check the resulting matrix is OK.
   */
  det = (RQ[0][0] * ST[0][0])
      + (RQ[0][1] * ST[0][1])
      + (RQ[0][2] * ST[0][2]);
  if (fabs (det) <= DBL_EPSILON)
    return FALSE;

  /*
   * Now transform our point with the ST matrix; the notional w
   * coordinate is 1, hence the last part is simply added.
   */
  xf = (float) (x * ST[0][0] + y * ST[1][0] + ST[2][0]);
  yf = (float) (x * ST[0][1] + y * ST[1][1] + ST[2][1]);
  wf = (float) (x * ST[0][2] + y * ST[1][2] + ST[2][2]);

  if (x_out)
    *x_out = xf / wf;

  if (y_out)
    *y_out = yf / wf;

#undef DET

  return TRUE;
}

/**
 * clutter_actor_is_rotated:
 * @self: a #ClutterActor
 *
 * Checks whether any rotation is applied to the actor.
 *
 * Return value: %TRUE if the actor is rotated.
 */
gboolean
clutter_actor_is_rotated (ClutterActor *self)
{
  const ClutterTransformInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (info->rx_angle || info->ry_angle || info->rz_angle)
    return TRUE;

  return FALSE;
}

/**
 * clutter_actor_is_scaled:
 * @self: a #ClutterActor
 *
 * Checks whether the actor is scaled in either dimension.
 *
 * Return value: %TRUE if the actor is scaled.
 */
gboolean
clutter_actor_is_scaled (ClutterActor *self)
{
  const ClutterTransformInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (info->scale_x != 1.0 || info->scale_y != 1.0)
    return TRUE;

  return FALSE;
}

/**
 * clutter_actor_get_context:
 * @actor: a #ClutterActor
 *
 * Returns: (transfer none): the Clutter context
 */
ClutterContext *
clutter_actor_get_context (ClutterActor *actor)
{
  return actor->priv->context;
}

ClutterActor *
_clutter_actor_get_stage_internal (ClutterActor *actor)
{
  while (actor && !CLUTTER_ACTOR_IS_TOPLEVEL (actor))
    actor = actor->priv->parent;

  return actor;
}

/**
 * clutter_actor_get_stage:
 * @actor: a #ClutterActor
 *
 * Retrieves the #ClutterStage where @actor is contained.
 *
 * Return value: (transfer none) (type Clutter.Stage): the stage
 *   containing the actor, or %NULL
 */
ClutterActor *
clutter_actor_get_stage (ClutterActor *actor)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  return _clutter_actor_get_stage_internal (actor);
}

/**
 * clutter_actor_allocate_available_size:
 * @self: a #ClutterActor
 * @x: the actor's X coordinate
 * @y: the actor's Y coordinate
 * @available_width: the maximum available width, or -1 to use the
 *   actor's natural width
 * @available_height: the maximum available height, or -1 to use the
 *   actor's natural height
 *
 * Allocates @self taking into account the #ClutterActor's
 * preferred size, but limiting it to the maximum available width
 * and height provided.
 *
 * This function will do the right thing when dealing with the
 * actor's request mode.
 *
 * The implementation of this function is equivalent to:
 *
 * ```c
 *   if (request_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
 *     {
 *       clutter_actor_get_preferred_width (self, available_height,
 *                                          &min_width,
 *                                          &natural_width);
 *       width = CLAMP (natural_width, min_width, available_width);
 *
 *       clutter_actor_get_preferred_height (self, width,
 *                                           &min_height,
 *                                           &natural_height);
 *       height = CLAMP (natural_height, min_height, available_height);
 *     }
 *   else if (request_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
 *     {
 *       clutter_actor_get_preferred_height (self, available_width,
 *                                           &min_height,
 *                                           &natural_height);
 *       height = CLAMP (natural_height, min_height, available_height);
 *
 *       clutter_actor_get_preferred_width (self, height,
 *                                          &min_width,
 *                                          &natural_width);
 *       width = CLAMP (natural_width, min_width, available_width);
 *     }
 *   else if (request_mode == CLUTTER_REQUEST_CONTENT_SIZE)
 *     {
 *       clutter_content_get_preferred_size (content, &natural_width, &natural_height);
 *
 *       width = CLAMP (natural_width, 0, available_width);
 *       height = CLAMP (natural_height, 0, available_height);
 *     }
 *
 *   box.x1 = x; box.y1 = y;
 *   box.x2 = box.x1 + available_width;
 *   box.y2 = box.y1 + available_height;
 *   clutter_actor_allocate (self, &box);
 * ```
 *
 * This function can be used by fluid layout managers to allocate
 * an actor's preferred size without making it bigger than the area
 * available for the container.
 */
void
clutter_actor_allocate_available_size (ClutterActor           *self,
                                       gfloat                  x,
                                       gfloat                  y,
                                       gfloat                  available_width,
                                       gfloat                  available_height)
{
  ClutterActorPrivate *priv;
  gfloat width, height;
  gfloat min_width, min_height;
  gfloat natural_width, natural_height;
  ClutterActorBox box;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  width = height = 0.0;

  switch (priv->request_mode)
    {
    case CLUTTER_REQUEST_HEIGHT_FOR_WIDTH:
      clutter_actor_get_preferred_width (self, available_height,
                                         &min_width,
                                         &natural_width);
      width  = CLAMP (natural_width, min_width, available_width);

      clutter_actor_get_preferred_height (self, width,
                                          &min_height,
                                          &natural_height);
      height = CLAMP (natural_height, min_height, available_height);
      break;

    case CLUTTER_REQUEST_WIDTH_FOR_HEIGHT:
      clutter_actor_get_preferred_height (self, available_width,
                                          &min_height,
                                          &natural_height);
      height = CLAMP (natural_height, min_height, available_height);

      clutter_actor_get_preferred_width (self, height,
                                         &min_width,
                                         &natural_width);
      width  = CLAMP (natural_width, min_width, available_width);
      break;

    case CLUTTER_REQUEST_CONTENT_SIZE:
      if (priv->content != NULL)
        {
          clutter_content_get_preferred_size (priv->content, &natural_width, &natural_height);

          width = CLAMP (natural_width, 0, available_width);
          height = CLAMP (natural_height, 0, available_height);
        }
      break;
    }


  box.x1 = x;
  box.y1 = y;
  box.x2 = box.x1 + width;
  box.y2 = box.y1 + height;
  clutter_actor_allocate (self, &box);
}

/**
 * clutter_actor_allocate_preferred_size:
 * @self: a #ClutterActor
 * @x: the actor's X coordinate
 * @y: the actor's Y coordinate
 *
 * Allocates the natural size of @self.
 *
 * This function is a utility call for #ClutterActor implementations
 * that allocates the actor's preferred natural size. It can be used
 * by fixed layout managers (like #ClutterGroup or so called
 * 'composite actors') inside the [vfunc@Clutter.Actor.allocate]
 * implementation to give each child exactly how much space it
 * requires, regardless of the size of the parent.
 *
 * This function is not meant to be used by applications. It is also
 * not meant to be used outside the implementation of the
 * #ClutterActorClass.allocate virtual function.
 */
void
clutter_actor_allocate_preferred_size (ClutterActor *self,
                                       float         x,
                                       float         y)
{
  gfloat natural_width, natural_height;
  ClutterActorBox actor_box;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  clutter_actor_get_preferred_size (self,
                                    NULL, NULL,
                                    &natural_width,
                                    &natural_height);

  actor_box.x1 = x;
  actor_box.y1 = y;
  actor_box.x2 = actor_box.x1 + natural_width;
  actor_box.y2 = actor_box.y1 + natural_height;

  clutter_actor_allocate (self, &actor_box);
}

/**
 * clutter_actor_allocate_align_fill:
 * @self: a #ClutterActor
 * @box: a #ClutterActorBox, containing the available width and height
 * @x_align: the horizontal alignment, between 0 and 1
 * @y_align: the vertical alignment, between 0 and 1
 * @x_fill: whether the actor should fill horizontally
 * @y_fill: whether the actor should fill vertically
 *
 * Allocates @self by taking into consideration the available allocation
 * area; an alignment factor on either axis; and whether the actor should
 * fill the allocation on either axis.
 *
 * The @box should contain the available allocation width and height;
 * if the x1 and y1 members of #ClutterActorBox are not set to 0, the
 * allocation will be offset by their value.
 *
 * This function takes into consideration the geometry request specified by
 * the [property@Clutter.Actor:request-mode] property, and the text direction.
 *
 * This function is useful for fluid layout managers using legacy alignment
 * flags. Newly written layout managers should use the
 * [property@Clutter.Actor:x-align] and [property@Clutter.Actor:y-align]
 * properties, instead, and just call [method@Clutter.Actor.allocate]
 * inside their [vfunc@Clutter.Actor.allocate] implementation.
 */
void
clutter_actor_allocate_align_fill (ClutterActor           *self,
                                   const ClutterActorBox  *box,
                                   gdouble                 x_align,
                                   gdouble                 y_align,
                                   gboolean                x_fill,
                                   gboolean                y_fill)
{
  ClutterActorPrivate *priv;
  ClutterActorBox allocation = CLUTTER_ACTOR_BOX_INIT_ZERO;
  gfloat x_offset, y_offset;
  gfloat available_width, available_height;
  gfloat child_width = 0.f, child_height = 0.f;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (box != NULL);
  g_return_if_fail (x_align >= 0.0 && x_align <= 1.0);
  g_return_if_fail (y_align >= 0.0 && y_align <= 1.0);

  priv = self->priv;

  clutter_actor_box_get_origin (box, &x_offset, &y_offset);
  clutter_actor_box_get_size (box, &available_width, &available_height);

  if (available_width <= 0)
    available_width = 0.f;

  if (available_height <= 0)
    available_height = 0.f;

  allocation.x1 = x_offset;
  allocation.y1 = y_offset;

  if (available_width == 0.f && available_height == 0.f)
    goto out;

  if (x_fill)
    child_width = available_width;

  if (y_fill)
    child_height = available_height;

  /* if we are filling horizontally and vertically then we're done */
  if (x_fill && y_fill)
    goto out;

  if (priv->request_mode == CLUTTER_REQUEST_HEIGHT_FOR_WIDTH)
    {
      gfloat min_width, natural_width;
      gfloat min_height, natural_height;

      if (!x_fill)
        {
          clutter_actor_get_preferred_width (self, available_height,
                                             &min_width,
                                             &natural_width);

          child_width = CLAMP (natural_width, min_width, available_width);
        }

      if (!y_fill)
        {
          clutter_actor_get_preferred_height (self, child_width,
                                              &min_height,
                                              &natural_height);

          child_height = CLAMP (natural_height, min_height, available_height);
        }
    }
  else if (priv->request_mode == CLUTTER_REQUEST_WIDTH_FOR_HEIGHT)
    {
      gfloat min_width, natural_width;
      gfloat min_height, natural_height;

      if (!y_fill)
        {
          clutter_actor_get_preferred_height (self, available_width,
                                              &min_height,
                                              &natural_height);

          child_height = CLAMP (natural_height, min_height, available_height);
        }

      if (!x_fill)
        {
          clutter_actor_get_preferred_width (self, child_height,
                                             &min_width,
                                             &natural_width);

          child_width = CLAMP (natural_width, min_width, available_width);
        }
    }
  else if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE && priv->content != NULL)
    {
      gfloat natural_width, natural_height;

      clutter_content_get_preferred_size (priv->content, &natural_width, &natural_height);

      if (!x_fill)
        child_width = CLAMP (natural_width, 0, available_width);

      if (!y_fill)
        child_height = CLAMP (natural_height, 0, available_height);
    }

  /* invert the horizontal alignment for RTL languages */
  if (priv->text_direction == CLUTTER_TEXT_DIRECTION_RTL)
    x_align = 1.0 - x_align;

  if (!x_fill)
    allocation.x1 += (float) ((available_width - child_width) * x_align);

  if (!y_fill)
    allocation.y1 += (float) ((available_height - child_height) * y_align);

out:

  allocation.x1 = floorf (allocation.x1);
  allocation.y1 = floorf (allocation.y1);
  allocation.x2 = ceilf (allocation.x1 + MAX (child_width, 0));
  allocation.y2 = ceilf (allocation.y1 + MAX (child_height, 0));

  clutter_actor_allocate (self, &allocation);
}

/**
 * clutter_actor_grab_key_focus:
 * @self: a #ClutterActor
 *
 * Sets the key focus of the #ClutterStage including @self
 * to this #ClutterActor.
 */
void
clutter_actor_grab_key_focus (ClutterActor *self)
{
  ClutterActor *stage;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->has_key_focus)
    return;

  stage = _clutter_actor_get_stage_internal (self);
  if (stage != NULL)
    clutter_stage_set_key_focus (CLUTTER_STAGE (stage), self);
}

#ifdef HAVE_FONTS
static void
update_pango_context (ClutterBackend *backend,
                      PangoContext   *context)
{
  ClutterSettings *settings;
  PangoFontDescription *font_desc;
  ClutterTextDirection dir;
  PangoDirection pango_dir;
  g_autofree char *font_name = NULL;
  gdouble resolution;

  settings = clutter_context_get_settings (backend->context);

  /* update the text direction */
  dir = clutter_context_get_text_direction (backend->context);
  pango_dir = clutter_text_direction_to_pango_direction (dir);

  pango_context_set_base_dir (context, pango_dir);

  g_object_get (settings, "font-name", &font_name, NULL);

  /* get the configuration for the PangoContext from the backend */
  resolution = clutter_backend_get_resolution (backend);

  font_desc = pango_font_description_from_string (font_name);

  if (resolution < 0)
    resolution = 96.0; /* fall back */

  pango_context_set_font_description (context, font_desc);
  pango_cairo_context_set_font_options (context, backend->font_options);
  pango_cairo_context_set_resolution (context, resolution);

  pango_font_description_free (font_desc);
}

/**
 * clutter_actor_get_pango_context:
 * @self: a #ClutterActor
 *
 * Retrieves the #PangoContext for @self. The actor's #PangoContext
 * is already configured using the appropriate font map, resolution
 * and font options.
 *
 * Unlike clutter_actor_create_pango_context(), this context is owend
 * by the #ClutterActor and it will be updated each time the options
 * stored by the #ClutterBackend change.
 *
 * You can use the returned #PangoContext to create a #PangoLayout
 * and render text using clutter_show_layout() to reuse the
 * glyphs cache also used by Clutter.
 *
 * Return value: (transfer none): the #PangoContext for a #ClutterActor.
 *   The returned #PangoContext is owned by the actor and should not be
 *   unreferenced by the application code
 */
PangoContext *
clutter_actor_get_pango_context (ClutterActor *self)
{
  ClutterActorPrivate *priv;
  ClutterContext *context = clutter_actor_get_context (self);
  ClutterBackend *backend = clutter_context_get_backend (context);

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  priv = self->priv;

  if (G_UNLIKELY (priv->pango_context == NULL))
    {
      priv->pango_context = clutter_actor_create_pango_context (self);

      priv->resolution_changed_id =
        g_signal_connect (backend, "resolution-changed",
                          G_CALLBACK (update_pango_context), priv->pango_context);
      priv->font_changed_id =
        g_signal_connect (backend, "font-changed",
                          G_CALLBACK (update_pango_context), priv->pango_context);
    }
  else
    update_pango_context (backend, priv->pango_context);

  return priv->pango_context;
}

/**
 * clutter_actor_create_pango_context:
 * @self: a #ClutterActor
 *
 * Creates a #PangoContext for the given actor. The #PangoContext
 * is already configured using the appropriate font map, resolution
 * and font options.
 *
 * See also [method@Clutter.Actor.get_pango_context].
 *
 * Return value: (transfer full): the newly created #PangoContext.
 *   Use g_object_unref() on the returned value to deallocate its
 *   resources
 */
PangoContext *
clutter_actor_create_pango_context (ClutterActor *self)
{
  PangoFontMap *font_map;
  ClutterContext *context = clutter_actor_get_context (self);
  PangoContext *pango_context;

  font_map = clutter_context_get_pango_fontmap (context);

  pango_context = pango_font_map_create_context (font_map);
  update_pango_context (clutter_context_get_backend (context), pango_context);
  pango_context_set_language (pango_context, pango_language_get_default ());

  return pango_context;
}

/**
 * clutter_actor_create_pango_layout:
 * @self: a #ClutterActor
 * @text: (nullable): the text to set on the #PangoLayout, or %NULL
 *
 * Creates a new #PangoLayout from the same #PangoContext used
 * by the #ClutterActor. The #PangoLayout is already configured
 * with the font map, resolution and font options, and the
 * given @text.
 *
 * If you want to keep around a #PangoLayout created by this
 * function you will have to connect to the #ClutterBackend::font-changed
 * and #ClutterBackend::resolution-changed signals, and call
 * pango_layout_context_changed() in response to them.
 *
 * Return value: (transfer full): the newly created #PangoLayout.
 *   Use g_object_unref() when done
 */
PangoLayout *
clutter_actor_create_pango_layout (ClutterActor *self,
                                   const gchar  *text)
{
  PangoContext *context;
  PangoLayout *layout;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  context = clutter_actor_get_pango_context (self);
  layout = pango_layout_new (context);

  if (text)
    pango_layout_set_text (layout, text, -1);

  return layout;
}
#endif

/**
 * clutter_actor_set_opacity_override:
 * @self: a #ClutterActor
 * @opacity: the override opacity value, or -1 to reset
 *
 * Allows overriding the calculated paint opacity (as returned by
 * clutter_actor_get_paint_opacity()). This is used internally by
 * ClutterClone and ClutterOffscreenEffect, and should be used by
 * actors that need to mimic those.
 *
 * In almost all cases this should not used by applications.
 */
void
clutter_actor_set_opacity_override (ClutterActor *self,
                                     gint          opacity)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  /* ensure bounds */
  if (opacity >= 0)
    opacity = CLAMP (opacity, 0, 255);
  else
    opacity = -1;

  self->priv->opacity_override = opacity;
}

/**
 * clutter_actor_get_opacity_override:
 * @self: a #ClutterActor
 *
 * See clutter_actor_set_opacity_override()
 *
 * Returns: the override value for the actor's opacity, or -1 if no override
 *   is set.2
 */
gint
clutter_actor_get_opacity_override (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), -1);

  return self->priv->opacity_override;
}

/**
 * clutter_actor_inhibit_culling:
 * @actor: a #ClutterActor
 *
 * Increases the culling inhibitor counter. Inhibiting culling
 * forces the actor to be painted even when outside the visible
 * bounds of the stage view.
 *
 * This is usually necessary when an actor is being painted on
 * another paint context.
 *
 * Pair with clutter_actor_uninhibit_culling() when the actor doesn't
 * need to be painted anymore.
 */
void
clutter_actor_inhibit_culling (ClutterActor *actor)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  priv = actor->priv;

  priv->inhibit_culling_counter++;
  _clutter_actor_set_enable_paint_unmapped (actor, TRUE);
}

/**
 * clutter_actor_uninhibit_culling:
 * @actor: a #ClutterActor
 *
 * Decreases the culling inhibitor counter. See clutter_actor_inhibit_culling()
 * for when inhibit culling is necessary.
 *
 * Calling this function without a matching call to
 * clutter_actor_inhibit_culling() is a programming error.
 */
void
clutter_actor_uninhibit_culling (ClutterActor *actor)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  priv = actor->priv;

  if (priv->inhibit_culling_counter == 0)
    {
      g_critical ("Unpaired call to clutter_actor_uninhibit_culling");
      return;
    }

  priv->inhibit_culling_counter--;
  if (priv->inhibit_culling_counter == 0)
    _clutter_actor_set_enable_paint_unmapped (actor, FALSE);
}

/* Allows you to disable applying the actors model view transform during
 * a paint. Used by ClutterClone. */
void
_clutter_actor_set_enable_model_view_transform (ClutterActor *self,
                                                gboolean      enable)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  self->priv->enable_model_view_transform = enable;
}

void
_clutter_actor_set_enable_paint_unmapped (ClutterActor *self,
                                          gboolean      enable)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (priv->enable_paint_unmapped == enable)
    return;

  priv->enable_paint_unmapped = enable;

  if (enable)
    {
      push_in_paint_unmapped_branch (self, 1);

      /* Make sure that the parents of the widget are realized first;
       * otherwise checks in clutter_actor_update_map_state() will
       * fail.
       */
      clutter_actor_realize (self);

      /* If the actor isn't ultimately connected to a toplevel, it can't be
       * realized or painted.
       */
      if (clutter_actor_is_realized (self))
          clutter_actor_update_map_state (self, MAP_STATE_MAKE_MAPPED);
    }
  else
    {
      clutter_actor_update_map_state (self, MAP_STATE_CHECK);
      pop_in_paint_unmapped_branch (self, 1);
    }
}

static void
clutter_actor_set_transform_internal (ClutterActor            *self,
                                      const graphene_matrix_t *transform)
{
  ClutterTransformInfo *info;
  gboolean was_set;
  GObject *obj;

  obj = G_OBJECT (self);

  info = _clutter_actor_get_transform_info (self);

  was_set = info->transform_set;

  info->transform = *transform;
  info->transform_set = !graphene_matrix_is_identity (&info->transform);

  transform_changed (self);

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (obj, obj_props[PROP_TRANSFORM]);

  if (was_set != info->transform_set)
    g_object_notify_by_pspec (obj, obj_props[PROP_TRANSFORM_SET]);
}

/**
 * clutter_actor_set_transform:
 * @self: a #ClutterActor
 * @transform: (nullable): a #graphene_matrix_t, or %NULL to
 *   unset a custom transformation
 *
 * Overrides the transformations of a #ClutterActor with a custom
 * matrix, which will be applied relative to the origin of the
 * actor's allocation and to the actor's pivot point.
 *
 * The [property@Clutter.Actor:transform] property is animatable.
 */
void
clutter_actor_set_transform (ClutterActor            *self,
                             const graphene_matrix_t *transform)
{
  const ClutterTransformInfo *info;
  graphene_matrix_t new_transform;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (transform != NULL)
    graphene_matrix_init_from_matrix (&new_transform, transform);
  else
    graphene_matrix_init_identity (&new_transform);

  _clutter_actor_create_transition (self, obj_props[PROP_TRANSFORM],
                                    &info->transform,
                                    &new_transform);
}

/**
 * clutter_actor_get_transform:
 * @self: a #ClutterActor
 * @transform: (out caller-allocates): a #graphene_matrix_t
 *
 * Retrieves the current transformation matrix of a #ClutterActor.
 */
void
clutter_actor_get_transform (ClutterActor      *self,
                             graphene_matrix_t *transform)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (transform != NULL);

  graphene_matrix_init_identity (transform);
  _clutter_actor_apply_modelview_transform (self, transform);
}

void
_clutter_actor_set_in_clone_paint (ClutterActor *self,
                                   gboolean      is_in_clone_paint)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  self->priv->in_clone_paint = is_in_clone_paint;
}

/**
 * clutter_actor_is_in_clone_paint:
 * @self: a #ClutterActor
 *
 * Checks whether @self is being currently painted by a #ClutterClone
 *
 * This function is useful only inside implementations of the
 * [vfunc@Clutter.Actor.paint] virtual function.
 *
 * This function should not be used by applications
 *
 * Return value: %TRUE if the #ClutterActor is currently being painted
 *   by a #ClutterClone, and %FALSE otherwise
 */
gboolean
clutter_actor_is_in_clone_paint (ClutterActor *self)
{
  ClutterActor *parent;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->in_clone_paint)
    return TRUE;

  if (self->priv->in_cloned_branch == 0)
    return FALSE;

  parent = self->priv->parent;
  while (parent != NULL)
    {
      if (parent->priv->in_cloned_branch == 0)
        break;

      if (parent->priv->in_clone_paint)
        return TRUE;

      parent = parent->priv->parent;
    }

  return FALSE;
}

gboolean
clutter_actor_is_painting_unmapped (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->unmapped_paint_branch_counter > 0;
}

gboolean
clutter_actor_has_damage (ClutterActor *actor)
{
  return actor->priv->is_dirty;
}

static gboolean
set_direction_recursive (ClutterActor *actor,
                         gpointer      user_data)
{
  ClutterTextDirection text_dir = GPOINTER_TO_INT (user_data);

  clutter_actor_set_text_direction (actor, text_dir);

  return TRUE;
}

/**
 * clutter_actor_set_text_direction:
 * @self: a #ClutterActor
 * @text_dir: the text direction for @self
 *
 * Sets the #ClutterTextDirection for an actor
 *
 * The passed text direction must not be %CLUTTER_TEXT_DIRECTION_DEFAULT
 *
 * This function will recurse inside all the children of @self
 */
void
clutter_actor_set_text_direction (ClutterActor         *self,
                                  ClutterTextDirection  text_dir)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (text_dir != CLUTTER_TEXT_DIRECTION_DEFAULT);

  priv = self->priv;

  if (priv->text_direction != text_dir)
    {
      priv->text_direction = text_dir;

      /* we need to emit the notify::text-direction first, so that
       * the sub-classes can catch that and do specific handling of
       * the text direction; see clutter_text_direction_changed_cb()
       * inside clutter-text.c
       */
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_TEXT_DIRECTION]);

      _clutter_actor_foreach_child (self, set_direction_recursive,
                                    GINT_TO_POINTER (text_dir));

      clutter_actor_queue_relayout (self);
    }
}

void
_clutter_actor_set_has_pointer (ClutterActor *self,
                                gboolean      has_pointer)
{
  ClutterActorPrivate *priv = self->priv;

  if (has_pointer)
    {
      g_assert (CLUTTER_IS_STAGE (self) || clutter_actor_is_mapped (self));

      priv->n_pointers++;
    }
  else
    {
      g_assert (priv->n_pointers > 0);

      priv->n_pointers--;
    }

  if (priv->n_pointers == 0 || priv->n_pointers == 1)
    g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HAS_POINTER]);
}

void
_clutter_actor_set_has_key_focus (ClutterActor *self,
                                  gboolean      has_key_focus)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->has_key_focus != has_key_focus)
    {
      priv->has_key_focus = has_key_focus;

      if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
        return;

      if (has_key_focus)
        clutter_actor_add_accessible_state (self,
                                            ATK_STATE_FOCUSED);
      else
        clutter_actor_remove_accessible_state (self,
                                               ATK_STATE_FOCUSED);

      if (has_key_focus)
        g_signal_emit (self, actor_signals[KEY_FOCUS_IN], 0);
      else
        g_signal_emit (self, actor_signals[KEY_FOCUS_OUT], 0);
    }
}

/**
 * clutter_actor_get_text_direction:
 * @self: a #ClutterActor
 *
 * Retrieves the value set using clutter_actor_set_text_direction()
 *
 * If no text direction has been previously set, the default text
 * direction, as returned by [method@Clutter.Context.get_text_direction], will
 * be returned instead
 *
 * Return value: the #ClutterTextDirection for the actor
 */
ClutterTextDirection
clutter_actor_get_text_direction (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self),
                        CLUTTER_TEXT_DIRECTION_LTR);

  priv = self->priv;

  /* if no direction has been set yet use the default */
  if (priv->text_direction == CLUTTER_TEXT_DIRECTION_DEFAULT)
    {
      ClutterContext *context = clutter_actor_get_context (self);

      priv->text_direction = clutter_context_get_text_direction (context);
    }

  return priv->text_direction;
}

/**
 * clutter_actor_has_pointer:
 * @self: a #ClutterActor
 *
 * Checks whether an actor contains the pointer of a
 * #ClutterInputDevice
 *
 * Return value: %TRUE if the actor contains the pointer, and
 *   %FALSE otherwise
 */
gboolean
clutter_actor_has_pointer (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->n_pointers > 0;
}

/**
 * clutter_actor_has_allocation:
 * @self: a #ClutterActor
 *
 * Checks if the actor has an up-to-date allocation assigned to
 * it. This means that the actor should have an allocation: it's
 * visible and has a parent. It also means that there is no
 * outstanding relayout request in progress for the actor or its
 * children (There might be other outstanding layout requests in
 * progress that will cause the actor to get a new allocation
 * when the stage is laid out, however).
 *
 * If this function returns %FALSE, then the actor will normally
 * be allocated before it is next drawn on the screen.
 *
 * Return value: %TRUE if the actor has an up-to-date allocation
 */
gboolean
clutter_actor_has_allocation (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  priv = self->priv;

  return priv->parent != NULL &&
         clutter_actor_is_visible (self) &&
         !priv->needs_allocation;
}

static void
clutter_actor_add_action_internal (ClutterActor      *self,
                                   ClutterAction     *action,
                                   ClutterEventPhase  phase)
{
  ClutterActorPrivate *priv;

  priv = self->priv;

  if (priv->actions == NULL)
    {
      priv->actions = g_object_new (CLUTTER_TYPE_META_GROUP, NULL);
      priv->actions->actor = self;
    }

  clutter_action_set_phase (action, phase);
  _clutter_meta_group_add_meta (priv->actions, CLUTTER_ACTOR_META (action));

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ACTIONS]);
}

/**
 * clutter_actor_add_action:
 * @self: a #ClutterActor
 * @action: a #ClutterAction
 *
 * Adds @action to the list of actions applied to @self
 *
 * A #ClutterAction can only belong to one actor at a time
 *
 * The #ClutterActor will hold a reference on @action until either
 * [method@Clutter.Actor.remove_action] or [method@Clutter.Actor.clear_actions]
 * is called
 */
void
clutter_actor_add_action (ClutterActor  *self,
                          ClutterAction *action)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTION (action));

  clutter_actor_add_action_internal (self, action, CLUTTER_PHASE_BUBBLE);
}

/**
 * clutter_actor_add_action_with_name:
 * @self: a #ClutterActor
 * @name: the name to set on the action
 * @action: a #ClutterAction
 *
 * A convenience function for setting the name of a #ClutterAction
 * while adding it to the list of actions applied to @self
 *
 * This function is the logical equivalent of:
 *
 * ```c
 *   clutter_actor_meta_set_name (CLUTTER_ACTOR_META (action), name);
 *   clutter_actor_add_action (self, action);
 * ```
 */
void
clutter_actor_add_action_with_name (ClutterActor  *self,
                                    const gchar   *name,
                                    ClutterAction *action)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (CLUTTER_IS_ACTION (action));

  clutter_actor_meta_set_name (CLUTTER_ACTOR_META (action), name);
  clutter_actor_add_action (self, action);
}

void
clutter_actor_add_action_full (ClutterActor      *self,
                               const char        *name,
                               ClutterEventPhase  phase,
                               ClutterAction     *action)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (CLUTTER_IS_ACTION (action));
  g_return_if_fail (phase == CLUTTER_PHASE_BUBBLE ||
                    phase == CLUTTER_PHASE_CAPTURE);

  clutter_actor_meta_set_name (CLUTTER_ACTOR_META (action), name);
  clutter_actor_add_action_internal (self, action, phase);
}

/**
 * clutter_actor_remove_action:
 * @self: a #ClutterActor
 * @action: a #ClutterAction
 *
 * Removes @action from the list of actions applied to @self
 *
 * The reference held by @self on the #ClutterAction will be released
 */
void
clutter_actor_remove_action (ClutterActor  *self,
                             ClutterAction *action)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_ACTION (action));

  priv = self->priv;

  if (priv->actions == NULL)
    return;

  /* Remove any transitions on the actions’s properties. */
  _clutter_actor_remove_transitions_for_meta_internal (self, "actions",
                                                       CLUTTER_ACTOR_META (action));

  _clutter_meta_group_remove_meta (priv->actions, CLUTTER_ACTOR_META (action));

  if (_clutter_meta_group_peek_metas (priv->actions) == NULL)
    g_clear_object (&priv->actions);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ACTIONS]);
}

/**
 * clutter_actor_remove_action_by_name:
 * @self: a #ClutterActor
 * @name: the name of the action to remove
 *
 * Removes the #ClutterAction with the given name from the list
 * of actions applied to @self
 */
void
clutter_actor_remove_action_by_name (ClutterActor *self,
                                     const gchar  *name)
{
  ClutterActorPrivate *priv;
  ClutterActorMeta *meta;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);

  priv = self->priv;

  if (priv->actions == NULL)
    return;

  meta = _clutter_meta_group_get_meta (priv->actions, name);
  if (meta == NULL)
    return;

  /* Remove any transitions on the actions’s properties. */
  _clutter_actor_remove_transitions_for_meta_internal (self, "actions", meta);

  _clutter_meta_group_remove_meta (priv->actions, meta);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ACTIONS]);
}

/**
 * clutter_actor_get_actions:
 * @self: a #ClutterActor
 *
 * Retrieves the list of actions applied to @self
 *
 * Return value: (transfer container) (element-type Clutter.Action): a copy
 *   of the list of `ClutterAction`s. The contents of the list are
 *   owned by the #ClutterActor. Use g_list_free() to free the resources
 *   allocated by the returned #GList
 */
GList *
clutter_actor_get_actions (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  if (self->priv->actions == NULL)
    return NULL;

  return _clutter_meta_group_get_metas_no_internal (self->priv->actions);
}

/**
 * clutter_actor_get_action:
 * @self: a #ClutterActor
 * @name: the name of the action to retrieve
 *
 * Retrieves the #ClutterAction with the given name in the list
 * of actions applied to @self
 *
 * Return value: (transfer none) (nullable): a #ClutterAction for the given
 *   name, or %NULL. The returned #ClutterAction is owned by the
 *   actor and it should not be unreferenced directly
 */
ClutterAction *
clutter_actor_get_action (ClutterActor *self,
                          const gchar  *name)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (self->priv->actions == NULL)
    return NULL;

  return CLUTTER_ACTION (_clutter_meta_group_get_meta (self->priv->actions, name));
}

/**
 * clutter_actor_clear_actions:
 * @self: a #ClutterActor
 *
 * Clears the list of actions applied to @self
 */
void
clutter_actor_clear_actions (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->actions == NULL)
    return;

  _clutter_actor_remove_transitions_for_meta_section_internal (self, "actions");
  _clutter_meta_group_clear_metas_no_internal (self->priv->actions);
}

/**
 * clutter_actor_add_constraint:
 * @self: a #ClutterActor
 * @constraint: a #ClutterConstraint
 *
 * Adds @constraint to the list`of `ClutterConstraint`s applied
 * to @self
 *
 * The #ClutterActor will hold a reference on the @constraint until
 * either [method@Clutter.Actor.remove_constraint] or
 * [method@Clutter.Actor.clear_constraints] is called.
 */
void
clutter_actor_add_constraint (ClutterActor      *self,
                              ClutterConstraint *constraint)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_CONSTRAINT (constraint));

  priv = self->priv;

  if (priv->constraints == NULL)
    {
      priv->constraints = g_object_new (CLUTTER_TYPE_META_GROUP, NULL);
      priv->constraints->actor = self;
    }

  _clutter_meta_group_add_meta (priv->constraints,
                                CLUTTER_ACTOR_META (constraint));
  clutter_actor_queue_relayout (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONSTRAINTS]);
}

/**
 * clutter_actor_add_constraint_with_name:
 * @self: a #ClutterActor
 * @name: the name to set on the constraint
 * @constraint: a #ClutterConstraint
 *
 * A convenience function for setting the name of a #ClutterConstraint
 * while adding it to the list of constraints applied to @self
 *
 * This function is the logical equivalent of:
 *
 * ```c
 *   clutter_actor_meta_set_name (CLUTTER_ACTOR_META (constraint), name);
 *   clutter_actor_add_constraint (self, constraint);
 * ```
 */
void
clutter_actor_add_constraint_with_name (ClutterActor      *self,
                                        const gchar       *name,
                                        ClutterConstraint *constraint)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (CLUTTER_IS_CONSTRAINT (constraint));

  clutter_actor_meta_set_name (CLUTTER_ACTOR_META (constraint), name);
  clutter_actor_add_constraint (self, constraint);
}

/**
 * clutter_actor_remove_constraint:
 * @self: a #ClutterActor
 * @constraint: a #ClutterConstraint
 *
 * Removes @constraint from the list of constraints applied to @self
 *
 * The reference held by @self on the #ClutterConstraint will be released
 */
void
clutter_actor_remove_constraint (ClutterActor      *self,
                                 ClutterConstraint *constraint)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_CONSTRAINT (constraint));

  priv = self->priv;

  if (priv->constraints == NULL)
    return;

  /* Remove any transitions on the constraint’s properties. */
  _clutter_actor_remove_transitions_for_meta_internal (self, "constraints",
                                                       CLUTTER_ACTOR_META (constraint));

  _clutter_meta_group_remove_meta (priv->constraints,
                                   CLUTTER_ACTOR_META (constraint));

  if (_clutter_meta_group_peek_metas (priv->constraints) == NULL)
    g_clear_object (&priv->constraints);

  clutter_actor_queue_relayout (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONSTRAINTS]);
}

/**
 * clutter_actor_remove_constraint_by_name:
 * @self: a #ClutterActor
 * @name: the name of the constraint to remove
 *
 * Removes the #ClutterConstraint with the given name from the list
 * of constraints applied to @self
 */
void
clutter_actor_remove_constraint_by_name (ClutterActor *self,
                                         const gchar  *name)
{
  ClutterActorPrivate *priv;
  ClutterActorMeta *meta;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);

  priv = self->priv;

  if (priv->constraints == NULL)
    return;

  meta = _clutter_meta_group_get_meta (priv->constraints, name);
  if (meta == NULL)
    return;

  /* Remove any transitions on the constraint’s properties. */
  _clutter_actor_remove_transitions_for_meta_internal (self, "constraints", meta);

  _clutter_meta_group_remove_meta (priv->constraints, meta);
  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_get_constraints:
 * @self: a #ClutterActor
 *
 * Retrieves the list of constraints applied to @self
 *
 * Return value: (transfer container) (element-type Clutter.Constraint): a copy
 *   of the list of `ClutterConstraint`s. The contents of the list are
 *   owned by the #ClutterActor. Use g_list_free() to free the resources
 *   allocated by the returned #GList
 */
GList *
clutter_actor_get_constraints (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  if (self->priv->constraints == NULL)
    return NULL;

  return _clutter_meta_group_get_metas_no_internal (self->priv->constraints);
}

/**
 * clutter_actor_get_constraint:
 * @self: a #ClutterActor
 * @name: the name of the constraint to retrieve
 *
 * Retrieves the #ClutterConstraint with the given name in the list
 * of constraints applied to @self
 *
 * Return value: (transfer none) (nullable): a #ClutterConstraint for the given
 *   name, or %NULL. The returned #ClutterConstraint is owned by the
 *   actor and it should not be unreferenced directly
 */
ClutterConstraint *
clutter_actor_get_constraint (ClutterActor *self,
                              const gchar  *name)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (self->priv->constraints == NULL)
    return NULL;

  return CLUTTER_CONSTRAINT (_clutter_meta_group_get_meta (self->priv->constraints, name));
}

/**
 * clutter_actor_clear_constraints:
 * @self: a #ClutterActor
 *
 * Clears the list of constraints applied to @self
 */
void
clutter_actor_clear_constraints (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->constraints == NULL)
    return;

  _clutter_actor_remove_transitions_for_meta_section_internal (self, "constraints");
  _clutter_meta_group_clear_metas_no_internal (self->priv->constraints);

  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_clip_to_allocation:
 * @self: a #ClutterActor
 * @clip_set: %TRUE to apply a clip tracking the allocation
 *
 * Sets whether @self should be clipped to the same size as its
 * allocation
 */
void
clutter_actor_set_clip_to_allocation (ClutterActor *self,
                                      gboolean      clip_set)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  clip_set = !!clip_set;

  priv = self->priv;

  if (priv->clip_to_allocation != clip_set)
    {
      priv->clip_to_allocation = clip_set;

      queue_update_paint_volume (self);
      clutter_actor_queue_redraw (self);

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CLIP_TO_ALLOCATION]);
      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HAS_CLIP]);
    }
}

/**
 * clutter_actor_get_clip_to_allocation:
 * @self: a #ClutterActor
 *
 * Retrieves the value set using clutter_actor_set_clip_to_allocation()
 *
 * Return value: %TRUE if the #ClutterActor is clipped to its allocation
 */
gboolean
clutter_actor_get_clip_to_allocation (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->clip_to_allocation;
}

/**
 * clutter_actor_add_effect:
 * @self: a #ClutterActor
 * @effect: a #ClutterEffect
 *
 * Adds @effect to the list of [class@Clutter.Effect]s applied to @self
 *
 * The #ClutterActor will hold a reference on the @effect until either
 * [method@Clutter.Actor.remove_effect] or [method@Clutter.Actor.clear_effects] is
 * called.
 */
void
clutter_actor_add_effect (ClutterActor  *self,
                          ClutterEffect *effect)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  _clutter_actor_add_effect_internal (self, effect);

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_EFFECT]);
}

/**
 * clutter_actor_add_effect_with_name:
 * @self: a #ClutterActor
 * @name: the name to set on the effect
 * @effect: a #ClutterEffect
 *
 * A convenience function for setting the name of a #ClutterEffect
 * while adding it to the list of effectss applied to @self
 *
 * This function is the logical equivalent of:
 *
 * ```c
 *   clutter_actor_meta_set_name (CLUTTER_ACTOR_META (effect), name);
 *   clutter_actor_add_effect (self, effect);
 * ```
 */
void
clutter_actor_add_effect_with_name (ClutterActor  *self,
                                    const gchar   *name,
                                    ClutterEffect *effect)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  clutter_actor_meta_set_name (CLUTTER_ACTOR_META (effect), name);
  clutter_actor_add_effect (self, effect);
}

/**
 * clutter_actor_remove_effect:
 * @self: a #ClutterActor
 * @effect: a #ClutterEffect
 *
 * Removes @effect from the list of effects applied to @self
 *
 * The reference held by @self on the #ClutterEffect will be released
 */
void
clutter_actor_remove_effect (ClutterActor  *self,
                             ClutterEffect *effect)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  _clutter_actor_remove_effect_internal (self, effect);

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_EFFECT]);
}

/**
 * clutter_actor_remove_effect_by_name:
 * @self: a #ClutterActor
 * @name: the name of the effect to remove
 *
 * Removes the #ClutterEffect with the given name from the list
 * of effects applied to @self
 */
void
clutter_actor_remove_effect_by_name (ClutterActor *self,
                                     const gchar  *name)
{
  ClutterActorPrivate *priv;
  ClutterActorMeta *meta;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);

  priv = self->priv;

  if (priv->effects == NULL)
    return;

  meta = _clutter_meta_group_get_meta (priv->effects, name);
  if (meta == NULL)
    return;

  clutter_actor_remove_effect (self, CLUTTER_EFFECT (meta));
}

/**
 * clutter_actor_get_effects:
 * @self: a #ClutterActor
 *
 * Retrieves the `ClutterEffect`s applied on @self, if any
 *
 * Return value: (transfer container) (element-type Clutter.Effect): a list
 *   of `ClutterEffect`s, or %NULL. The elements of the returned
 *   list are owned by Clutter and they should not be freed. You should
 *   free the returned list using g_list_free() when done
 */
GList *
clutter_actor_get_effects (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  priv = self->priv;

  if (priv->effects == NULL)
    return NULL;

  return _clutter_meta_group_get_metas_no_internal (priv->effects);
}

/**
 * clutter_actor_get_effect:
 * @self: a #ClutterActor
 * @name: the name of the effect to retrieve
 *
 * Retrieves the #ClutterEffect with the given name in the list
 * of effects applied to @self
 *
 * Return value: (transfer none) (nullable): a #ClutterEffect for the given
 *   name, or %NULL. The returned #ClutterEffect is owned by the
 *   actor and it should not be unreferenced directly
 */
ClutterEffect *
clutter_actor_get_effect (ClutterActor *self,
                          const gchar  *name)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (self->priv->effects == NULL)
    return NULL;

  return CLUTTER_EFFECT (_clutter_meta_group_get_meta (self->priv->effects, name));
}

/**
 * clutter_actor_clear_effects:
 * @self: a #ClutterActor
 *
 * Clears the list of effects applied to @self
 */
void
clutter_actor_clear_effects (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->effects == NULL)
    return;

  _clutter_actor_remove_transitions_for_meta_section_internal (self, "effects");
  _clutter_meta_group_clear_metas_no_internal (self->priv->effects);

  clutter_actor_queue_redraw (self);
}

/**
 * clutter_actor_has_key_focus:
 * @self: a #ClutterActor
 *
 * Checks whether @self is the #ClutterActor that has key focus
 *
 * Return value: %TRUE if the actor has key focus, and %FALSE otherwise
 */
gboolean
clutter_actor_has_key_focus (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return self->priv->has_key_focus;
}

static gboolean
_clutter_actor_get_paint_volume_real (ClutterActor *self,
                                      ClutterPaintVolume *pv)
{
  ClutterActorPrivate *priv = self->priv;

  /* Actors are only expected to report a valid paint volume
   * while they have a valid allocation. */
  if (G_UNLIKELY (priv->needs_allocation))
    {
      CLUTTER_NOTE (CLIPPING, "Bail from get_paint_volume (%s): "
                    "Actor needs allocation",
                    _clutter_actor_get_debug_name (self));
      return FALSE;
    }

  clutter_paint_volume_init_from_actor (pv, self);

  if (!CLUTTER_ACTOR_GET_CLASS (self)->get_paint_volume (self, pv))
    {
      CLUTTER_NOTE (CLIPPING, "Bail from get_paint_volume (%s): "
                    "Actor failed to report a volume",
                    _clutter_actor_get_debug_name (self));
      return FALSE;
    }

  /* since effects can modify the paint volume, we allow them to actually
   * do this by making get_paint_volume() "context sensitive"
   */
  if (priv->effects != NULL)
    {
      if (priv->current_effect != NULL)
        {
          const GList *effects, *l;

          /* if we are being called from within the paint sequence of
           * an actor, get the paint volume up to the current effect
           */
          effects = _clutter_meta_group_peek_metas (priv->effects);
          for (l = effects;
               l != NULL && l->data != priv->current_effect;
               l = l->next)
            {
              if (!_clutter_effect_modify_paint_volume (l->data, pv))
                {
                  CLUTTER_NOTE (CLIPPING, "Bail from get_paint_volume (%s): "
                                "Effect (%s) failed to report a volume",
                                _clutter_actor_get_debug_name (self),
                                _clutter_actor_meta_get_debug_name (l->data));
                  return FALSE;
                }
            }
        }
      else
        {
          const GList *effects, *l;

          /* otherwise, get the cumulative volume */
          effects = _clutter_meta_group_peek_metas (priv->effects);
          for (l = effects; l != NULL; l = l->next)
            if (!_clutter_effect_modify_paint_volume (l->data, pv))
              {
                CLUTTER_NOTE (CLIPPING, "Bail from get_paint_volume (%s): "
                              "Effect (%s) failed to report a volume",
                              _clutter_actor_get_debug_name (self),
                              _clutter_actor_meta_get_debug_name (l->data));
                return FALSE;
              }
        }
    }

  return TRUE;
}

static gboolean
_clutter_actor_has_active_paint_volume_override_effects (ClutterActor *self)
{
  const GList *l;

  if (self->priv->effects == NULL)
    return FALSE;

  /* We just need to all effects current effect to see
   * if anyone wants to override the paint volume. If so, then
   * we need to recompute, since the paint volume returned can
   * change from call to call. */
  for (l = _clutter_meta_group_peek_metas (self->priv->effects);
       l != NULL;
       l = l->next)
    {
      ClutterEffect *effect = l->data;

      if (clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (effect)) &&
          _clutter_effect_has_custom_paint_volume (effect))
        return TRUE;
    }

  return FALSE;
}

static void
ensure_paint_volume (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  gboolean has_paint_volume_override_effects;
  gboolean must_update_paint_volume;

  has_paint_volume_override_effects = _clutter_actor_has_active_paint_volume_override_effects (self);

  /* If effects are applied, the actor paint volume
   * needs to be recomputed on each paint, since those
   * paint volumes could change over the duration of the
   * effect.
   *
   * We also need to update the paint volume if we went
   * from having effects to not having effects on the last
   * paint volume update.
   *
   * FIXME: This opens the door for some tricky issues: If our paint volume
   * is invalid, it's implied that all parent paint volumes are invalid. If
   * we don't want to break that invariant, we should find a better solution
   * to deal with effects.
   */
  must_update_paint_volume =
    priv->current_effect != NULL ||
    has_paint_volume_override_effects ||
    priv->had_effects_on_last_paint_volume_update;

  priv->needs_paint_volume_update |= must_update_paint_volume;

  if (priv->needs_paint_volume_update)
    {
      priv->had_effects_on_last_paint_volume_update = has_paint_volume_override_effects;
      priv->has_paint_volume = FALSE;

      if (_clutter_actor_get_paint_volume_real (self, &priv->paint_volume))
        {
          priv->has_paint_volume = TRUE;
          priv->needs_paint_volume_update = FALSE;
        }
    }
}

/* The public clutter_actor_get_paint_volume API returns a const
 * pointer since we return a pointer directly to the cached
 * PaintVolume associated with the actor and don't want the user to
 * inadvertently modify it, but for internal uses we sometimes need
 * access to the same PaintVolume but need to apply some book-keeping
 * modifications to it so we don't want a const pointer.
 */
static ClutterPaintVolume *
_clutter_actor_get_paint_volume_mutable (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  ensure_paint_volume (self);

  if (priv->has_paint_volume)
    return &priv->paint_volume;

  return NULL;
}

/**
 * clutter_actor_get_paint_volume:
 * @self: a #ClutterActor
 *
 * Retrieves the paint volume of the passed #ClutterActor, or %NULL
 * when a paint volume can't be determined.
 *
 * The paint volume is defined as the 3D space occupied by an actor
 * when being painted.
 *
 * This function will call the [vfunc@Clutter.Actor.get_paint_volume]
 * virtual function of the #ClutterActor class. Sub-classes of #ClutterActor
 * should not usually care about overriding the default implementation,
 * unless they are, for instance: painting outside their allocation, or
 * actors with a depth factor (not in terms of depth but real
 * 3D depth).
 *
 * Note: 2D actors overriding [vfunc@Clutter.Actor.get_paint_volume]
 * should ensure that their volume has a depth of 0. (This will be true
 * as long as you don't call [method@Clutter.PaintVolume.set_depth].)
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterPaintVolume,
 *   or %NULL if no volume could be determined. The returned pointer
 *   is not guaranteed to be valid across multiple frames; if you want
 *   to keep it, you will need to copy it using [method@Clutter.PaintVolume.copy].
 */
const ClutterPaintVolume *
clutter_actor_get_paint_volume (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return _clutter_actor_get_paint_volume_mutable (self);
}

/**
 * clutter_actor_get_transformed_paint_volume:
 * @self: a #ClutterActor
 * @relative_to_ancestor: A #ClutterActor that is an ancestor of @self
 *    (or %NULL for the stage)
 *
 * Retrieves the 3D paint volume of an actor like
 * [method@Clutter.Actor.get_paint_volume] does and it additionally
 * transforms the paint volume into the coordinate
 * space of @relative_to_ancestor. (Or the stage if %NULL
 * is passed for @relative_to_ancestor)
 *
 * This can be used by containers that base their paint volume on
 * the volume of their children. Such containers can query the
 * transformed paint volume of all of its children and union them
 * together using [method@Clutter.PaintVolume.union].
 *
 * Return value: (transfer full) (nullable): a pointer to a #ClutterPaintVolume,
 *   or %NULL if no volume could be determined.
 */
ClutterPaintVolume *
clutter_actor_get_transformed_paint_volume (ClutterActor *self,
                                            ClutterActor *relative_to_ancestor)
{
  const ClutterPaintVolume *volume;
  ClutterActor *stage;
  ClutterPaintVolume *transformed_volume;

  stage = _clutter_actor_get_stage_internal (self);
  if (G_UNLIKELY (stage == NULL))
    return NULL;

  if (relative_to_ancestor == NULL)
    relative_to_ancestor = stage;

  volume = clutter_actor_get_paint_volume (self);
  if (volume == NULL)
    return NULL;

  transformed_volume = clutter_paint_volume_copy (volume);

  _clutter_paint_volume_transform_relative (transformed_volume,
                                            relative_to_ancestor);

  return transformed_volume;
}

/**
 * clutter_actor_get_paint_box:
 * @self: a #ClutterActor
 * @box: (out): return location for a #ClutterActorBox
 *
 * Retrieves the paint volume of the passed #ClutterActor, and
 * transforms it into a 2D bounding box in stage coordinates.
 *
 * This function is useful to determine the on screen area occupied by
 * the actor. The box is only an approximation and may often be
 * considerably larger due to the optimizations used to calculate the
 * box. The box is never smaller though, so it can reliably be used
 * for culling.
 *
 * There are times when a 2D paint box can't be determined, e.g.
 * because the actor isn't yet parented under a stage or because
 * the actor is unable to determine a paint volume.
 *
 * Return value: %TRUE if a 2D paint box could be determined, else
 * %FALSE.
 */
gboolean
clutter_actor_get_paint_box (ClutterActor    *self,
                             ClutterActorBox *box)
{
  ClutterActor *stage;
  ClutterPaintVolume *pv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);
  g_return_val_if_fail (box != NULL, FALSE);

  stage = _clutter_actor_get_stage_internal (self);
  if (G_UNLIKELY (!stage))
    return FALSE;

  pv = _clutter_actor_get_paint_volume_mutable (self);
  if (G_UNLIKELY (!pv))
    return FALSE;

  _clutter_paint_volume_get_stage_paint_box (pv, CLUTTER_STAGE (stage), box);

  return TRUE;
}

static ClutterActorTraverseVisitFlags
clear_stage_views_cb (ClutterActor *actor,
                      int           depth,
                      gpointer      user_data)
{
  gboolean stop_transitions = GPOINTER_TO_INT (user_data);
  g_autoptr (GList) old_stage_views = NULL;

  if (stop_transitions)
    _clutter_actor_stop_transitions (actor);

  actor->priv->needs_update_stage_views = TRUE;
  actor->priv->needs_finish_layout = TRUE;

  old_stage_views = g_steal_pointer (&actor->priv->stage_views);

  if (old_stage_views || CLUTTER_ACTOR_IS_TOPLEVEL (actor))
    actor->priv->clear_stage_views_needs_stage_views_changed = TRUE;

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

static ClutterActorTraverseVisitFlags
maybe_emit_stage_views_changed_cb (ClutterActor *actor,
                                   int           depth,
                                   gpointer      user_data)
{
  if (actor->priv->clear_stage_views_needs_stage_views_changed)
    {
      actor->priv->clear_stage_views_needs_stage_views_changed = FALSE;
      g_signal_emit (actor, actor_signals[STAGE_VIEWS_CHANGED], 0);
    }

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

void
clutter_actor_clear_stage_views_recursive (ClutterActor *self,
                                           gboolean      stop_transitions)
{
  _clutter_actor_traverse (self,
                           CLUTTER_ACTOR_TRAVERSE_DEPTH_FIRST,
                           clear_stage_views_cb,
                           NULL,
                           GINT_TO_POINTER (stop_transitions));
  _clutter_actor_traverse (self,
                           CLUTTER_ACTOR_TRAVERSE_DEPTH_FIRST,
                           maybe_emit_stage_views_changed_cb,
                           NULL,
                           NULL);
}

float
clutter_actor_get_real_resource_scale (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  float guessed_scale;

  if (priv->resource_scale != -1.f)
    return priv->resource_scale;

  /* If the scale hasn't been computed yet, we return a best guess */

  if (priv->parent != NULL)
    {
      /* If the scale hasn't been calculated yet, assume this actor is located
       * inside its parents box and go up the hierarchy.
       */
      guessed_scale = clutter_actor_get_real_resource_scale (priv->parent);
    }
  else if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    {
      /* This must be the first allocation cycle and the resource scale of
       * the stage has not been updated yet, so return it manually.
       */
      GList *l;
      ClutterStage *stage = CLUTTER_STAGE (self);
      float max_scale = -1.f;

      for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
        {
          ClutterStageView *view = l->data;

          max_scale = MAX (clutter_stage_view_get_scale (view), max_scale);
        }

      if (max_scale < 0.f)
        max_scale = 1.f;

      guessed_scale = max_scale;
    }
  else
    {
      ClutterContext *context = clutter_actor_get_context (self);
      ClutterBackend *backend = clutter_context_get_backend (context);

      guessed_scale = clutter_backend_get_fallback_resource_scale (backend);
    }

  g_assert (guessed_scale >= 0.5);

  /* Always return this value until we compute the correct one later.
   * If our guess turns out to be wrong, we'll emit "resource-scale-changed"
   * and correct it before painting.
   */
  priv->resource_scale = guessed_scale;

  return priv->resource_scale;
}

/**
 * clutter_actor_get_resource_scale:
 * @self: A #ClutterActor
 *
 * Retrieves the resource scale for this actor.
 *
 * The resource scale refers to the scale the actor should use for its resources.
 * For example if an actor draws a a picture of size 100 x 100 in the stage
 * coordinate space, it should use a texture of twice the size (i.e. 200 x 200)
 * if the resource scale is 2.
 *
 * The resource scale is determined by calculating the highest #ClutterStageView
 * scale the actor will get painted on.
 *
 * Note that the scale returned by this function is only guaranteed to be
 * correct when queried during the paint cycle, in all other cases this
 * function will only return a best guess. If your implementation really
 * needs to get a resource scale outside of the paint cycle, make sure to
 * subscribe to the "resource-scale-changed" signal to get notified about
 * the new, correct resource scale before painting.
 *
 * Also avoid getting the resource scale for actors that are not attached
 * to a stage. There's no sane way for Clutter to guess which #ClutterStageView
 * the actor is going to be painted on, so you'll probably end up receiving
 * the "resource-scale-changed" signal and having to rebuild your resources.
 *
 * The best guess this function may return is usually just the last resource
 * scale the actor got painted with. If this resource scale couldn't be found
 * because the actor was never painted so far or Clutter was unable to
 * determine its position and size, this function will return the resource
 * scale of a parent.
 *
 * Returns: The resource scale the actor should use for its textures
 */
float
clutter_actor_get_resource_scale (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 1.f);

  return ceilf (clutter_actor_get_real_resource_scale (self));
}

static void
add_actor_to_redraw_clip (ClutterActor       *self,
                          gboolean            actor_moved,
                          ClutterPaintVolume *old_visible_paint_volume)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterStage *stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (self));

  if (priv->next_redraw_clips->len != 0)
    {
      unsigned int i;

      for (i = 0; i < priv->next_redraw_clips->len; i++)
        clutter_stage_add_to_redraw_clip (stage, &g_array_index (priv->next_redraw_clips, ClutterPaintVolume, i));

      priv->next_redraw_clips->len = 0;
    }
  else if (actor_moved)
    {
      /* For a clipped redraw to work we need both the old paint volume and the new
       * one, if any is missing we'll need to do an unclipped redraw.
       */
      if (old_visible_paint_volume == NULL || !priv->visible_paint_volume_valid)
        goto full_stage_redraw;

      clutter_stage_add_to_redraw_clip (stage, old_visible_paint_volume);
      clutter_stage_add_to_redraw_clip (stage, &priv->visible_paint_volume);
    }
  else
    {
      if (!priv->visible_paint_volume_valid)
        goto full_stage_redraw;

      clutter_stage_add_to_redraw_clip (stage, &priv->visible_paint_volume);
    }

  return;

full_stage_redraw:
  clutter_stage_add_to_redraw_clip (stage, NULL);
}

static gboolean
sorted_lists_equal (GList *list_a,
                    GList *list_b)
{
  GList *a, *b;

  if (!list_a && !list_b)
    return TRUE;

  for (a = list_a, b = list_b;
       a && b;
       a = a->next, b = b->next)
    {
      if (a->data != b->data)
        break;

      if (!a->next && !b->next)
        return TRUE;
    }

  return FALSE;
}

static void
update_stage_views (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  g_autoptr (GList) old_stage_views = NULL;
  ClutterStage *stage;
  graphene_rect_t bounding_rect;

  if (CLUTTER_ACTOR_IS_TOPLEVEL (self))
    return;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (self));
  g_return_if_fail (stage);

  old_stage_views = g_steal_pointer (&priv->stage_views);

  if (priv->needs_allocation)
    {
      g_warning ("Can't update stage views actor %s is on because it needs an "
                 "allocation.", _clutter_actor_get_debug_name (self));
      priv->stage_views = g_list_copy (clutter_stage_peek_stage_views (stage));
      goto out;
    }

  clutter_actor_get_transformed_extents (self, &bounding_rect);

  if (bounding_rect.size.width == 0.0 ||
      bounding_rect.size.height == 0.0)
    goto out;

  priv->stage_views = clutter_stage_get_views_for_rect (stage,
                                                        &bounding_rect);

out:
  if (!sorted_lists_equal (old_stage_views, priv->stage_views))
    g_signal_emit (self, actor_signals[STAGE_VIEWS_CHANGED], 0);
}

static void
update_resource_scale (ClutterActor *self,
                       int           phase)
{
  ClutterActorPrivate *priv = self->priv;
  float new_resource_scale, old_resource_scale;

  new_resource_scale =
    CLUTTER_ACTOR_GET_CLASS (self)->calculate_resource_scale (self, phase);

  if (priv->resource_scale == new_resource_scale)
    return;

  /* If the actor moved out of the stage, simply keep the last scale */
  if (new_resource_scale == -1.f)
    return;

  old_resource_scale = priv->resource_scale;
  priv->resource_scale = new_resource_scale;

  /* Never notify the initial change, otherwise, to be consistent,
   * we'd also have to notify if we guessed correctly in
   * clutter_actor_get_real_resource_scale().
   */
  if (old_resource_scale == -1.f)
    return;

  if (ceilf (old_resource_scale) != ceilf (priv->resource_scale))
    g_signal_emit (self, actor_signals[RESOURCE_SCALE_CHANGED], 0);
}

void
clutter_actor_finish_layout (ClutterActor *self,
                             gboolean      use_max_scale)
{
  ClutterActorPrivate *priv = self->priv;
  ClutterActor *child;
  gboolean actor_moved = FALSE;
  gboolean old_visible_paint_volume_valid = FALSE;
  ClutterPaintVolume old_visible_paint_volume;

  if (!priv->needs_finish_layout)
    return;

  if ((!clutter_actor_is_mapped (self) &&
       !clutter_actor_has_mapped_clones (self)) ||
      CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return;

  if (priv->needs_visible_paint_volume_update)
    {
      ensure_paint_volume (self);

      actor_moved = TRUE;
      old_visible_paint_volume = priv->visible_paint_volume;
      old_visible_paint_volume_valid = priv->visible_paint_volume_valid;

      if (priv->has_paint_volume)
        {
          clutter_paint_volume_init_from_paint_volume (&priv->visible_paint_volume,
                                                       &priv->paint_volume);
          _clutter_paint_volume_transform_relative (&priv->visible_paint_volume,
                                                    NULL); /* eye coordinates */
        }

      priv->visible_paint_volume_valid = priv->has_paint_volume;
      priv->needs_visible_paint_volume_update = FALSE;
    }

  if (priv->needs_update_stage_views)
    {
      update_stage_views (self);
      update_resource_scale (self, use_max_scale);

      priv->needs_update_stage_views = FALSE;
    }

  if (priv->needs_redraw)
    {
      add_actor_to_redraw_clip (self,
                                actor_moved,
                                old_visible_paint_volume_valid ? &old_visible_paint_volume : NULL);
      priv->needs_redraw = FALSE;
    }

  priv->needs_finish_layout = FALSE;

  for (child = priv->first_child; child; child = child->priv->next_sibling)
    clutter_actor_finish_layout (child, use_max_scale);
}

/**
 * clutter_actor_peek_stage_views:
 * @self: A #ClutterActor
 *
 * Retrieves the list of `ClutterStageView`s the actor is being
 * painted on.
 *
 * If this function is called during the paint cycle, the list is guaranteed
 * to be up-to-date, if called outside the paint cycle, the list will
 * contain the views the actor was painted on last.
 *
 * The list returned by this function is not updated when the actors
 * visibility changes: If an actor gets hidden and is not being painted
 * anymore, this function will return the list of views the actor was
 * painted on last.
 *
 * If an actor is not attached to a stage (realized), this function will
 * always return an empty list.
 *
 * Returns: (transfer none) (element-type Clutter.StageView): The list of
 *   `ClutterStageView`s the actor is being painted on. The list and
 *   its contents are owned by the #ClutterActor and the list may not be
 *   freed or modified.
 */
GList *
clutter_actor_peek_stage_views (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return CLUTTER_ACTOR_IS_TOPLEVEL (self)
    ? clutter_stage_peek_stage_views (CLUTTER_STAGE (self))
    : self->priv->stage_views;
}

gboolean
clutter_actor_is_effectively_on_stage_view (ClutterActor     *self,
                                            ClutterStageView *view)
{
  ClutterActor *actor;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (!clutter_actor_is_mapped (self) &&
      !clutter_actor_has_mapped_clones (self))
    return FALSE;

  if (g_list_find (clutter_actor_peek_stage_views (self), view))
    return TRUE;

  for (actor = self; actor; actor = actor->priv->parent)
    {
      if (actor->priv->clones)
        {
          GHashTableIter iter;
          gpointer key;

          g_hash_table_iter_init (&iter, actor->priv->clones);
          while (g_hash_table_iter_next (&iter, &key, NULL))
            {
              ClutterActor *clone = key;
              GList *clone_views;

              if (!clutter_actor_is_mapped (clone))
                continue;

              clone_views = clutter_actor_peek_stage_views (clone);
              if (g_list_find (clone_views, view))
                return TRUE;
            }
        }

      /* Clones will force-show their own source actor but not children of
       * it, so if we're hidden and an actor up the hierarchy has a clone,
       * we won't be visible.
       */
      if (!clutter_actor_is_visible (actor))
        return FALSE;
    }

  return FALSE;
}

/**
 * clutter_actor_pick_frame_clock: (skip)
 * @self: a #ClutterActor
 * @out_actor: (out) (nullable) (optional): a pointer to an #ClutterActor
 *
 * Pick the most suitable frame clock for driving animations for this actor.
 *
 * The #ClutterActor used for picking the frame clock is written @out_actor.
 *
 * Returns: (transfer none) (nullable): a #ClutterFrameClock
 */
ClutterFrameClock *
clutter_actor_pick_frame_clock (ClutterActor  *self,
                                ClutterActor **out_actor)
{
  ClutterActorPrivate *priv = self->priv;
  GList *stage_views_list;
  int max_priority = -1;
  ClutterStageView *best_view = NULL;
  GList *l;

  stage_views_list = clutter_actor_peek_stage_views (self);

  if (!stage_views_list)
    {
      if (priv->parent)
        return clutter_actor_pick_frame_clock (priv->parent, out_actor);
      else
        return NULL;
    }

  for (l = stage_views_list; l; l = l->next)
    {
      ClutterStageView *view = CLUTTER_STAGE_VIEW (l->data);
      int priority;

      priority = clutter_stage_view_get_priority (view);
      if (priority > max_priority)
        {
          best_view = view;
          max_priority = priority;
        }
    }

  if (best_view)
    {
      if (out_actor)
        *out_actor = self;
      return clutter_stage_view_get_frame_clock (best_view);
    }
  else
    {
      return NULL;
    }
}

/**
 * clutter_actor_has_overlaps:
 * @self: A #ClutterActor
 *
 * Asks the actor's implementation whether it may contain overlapping
 * primitives.
 *
 * For example; Clutter may use this to determine whether the painting
 * should be redirected to an offscreen buffer to correctly implement
 * the opacity property.
 *
 * Custom actors can override the default response by implementing the
 * [vfunc@Clutter.Actor.has_overlaps]. See
 * [method@Clutter.Actor.set_offscreen_redirect] for more information.
 *
 * Return value: %TRUE if the actor may have overlapping primitives, and
 *   %FALSE otherwise
 */
gboolean
clutter_actor_has_overlaps (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), TRUE);

  return CLUTTER_ACTOR_GET_CLASS (self)->has_overlaps (self);
}

/**
 * clutter_actor_has_effects:
 * @self: A #ClutterActor
 *
 * Returns whether the actor has any effects applied.
 *
 * Return value: %TRUE if the actor has any effects,
 *   %FALSE otherwise
 */
gboolean
clutter_actor_has_effects (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->effects == NULL)
    return FALSE;

  return _clutter_meta_group_has_metas_no_internal (self->priv->effects);
}

/**
 * clutter_actor_has_constraints:
 * @self: A #ClutterActor
 *
 * Returns whether the actor has any constraints applied.
 *
 * Return value: %TRUE if the actor has any constraints,
 *   %FALSE otherwise
 */
gboolean
clutter_actor_has_constraints (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->constraints == NULL)
    return FALSE;

  return _clutter_meta_group_has_metas_no_internal (self->priv->constraints);
}

/**
 * clutter_actor_has_actions:
 * @self: A #ClutterActor
 *
 * Returns whether the actor has any actions applied.
 *
 * Return value: %TRUE if the actor has any actions,
 *   %FALSE otherwise
 */
gboolean
clutter_actor_has_actions (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->actions == NULL)
    return FALSE;

  return _clutter_meta_group_has_metas_no_internal (self->priv->actions);
}

/**
 * clutter_actor_get_n_children:
 * @self: a #ClutterActor
 *
 * Retrieves the number of children of @self.
 *
 * Return value: the number of children of an actor
 */
gint
clutter_actor_get_n_children (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  return self->priv->n_children;
}

/**
 * clutter_actor_get_child_at_index:
 * @self: a #ClutterActor
 * @index_: the position in the list of children
 *
 * Retrieves the actor at the given @index_ inside the list of
 * children of @self.
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterActor,
 *   or %NULL
 */
ClutterActor *
clutter_actor_get_child_at_index (ClutterActor *self,
                                  gint          index_)
{
  ClutterActor *iter;
  int i;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (index_ <= self->priv->n_children, NULL);

  for (iter = self->priv->first_child, i = 0;
       iter != NULL && i < index_;
       iter = iter->priv->next_sibling, i += 1)
    ;

  return iter;
}

/*< private >
 * _clutter_actor_foreach_child:
 * @actor: The actor whose children you want to iterate
 * @callback: The function to call for each child
 * @user_data: Private data to pass to @callback
 *
 * Calls a given @callback once for each child of the specified @actor and
 * passing the @user_data pointer each time.
 *
 * Return value: returns %TRUE if all children were iterated, else
 *    %FALSE if a callback broke out of iteration early.
 */
gboolean
_clutter_actor_foreach_child (ClutterActor           *self,
                              ClutterForeachCallback  callback,
                              gpointer                user_data)
{
  ClutterActor *iter;
  gboolean cont;

  if (self->priv->first_child == NULL)
    return TRUE;

  cont = TRUE;
  iter = self->priv->first_child;

  /* we use this form so that it's safe to change the children
   * list while iterating it
   */
  while (cont && iter != NULL)
    {
      ClutterActor *next = iter->priv->next_sibling;

      cont = callback (iter, user_data);

      iter = next;
    }

  return cont;
}

#if 0
/* For debugging purposes this gives us a simple way to print out
 * the scenegraph e.g in gdb using:
 * [|
 *   _clutter_actor_traverse (stage,
 *                            0,
 *                            clutter_debug_print_actor_cb,
 *                            NULL,
 *                            NULL);
 * |]
 */
static ClutterActorTraverseVisitFlags
clutter_debug_print_actor_cb (ClutterActor *actor,
                              int depth,
                              void *user_data)
{
  g_print ("%*s%s:%p\n",
           depth * 2, "",
           _clutter_actor_get_debug_name (actor),
           actor);

  return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}
#endif

static void
_clutter_actor_traverse_breadth (ClutterActor           *actor,
                                 ClutterTraverseCallback callback,
                                 gpointer                user_data)
{
  GQueue *queue = g_queue_new ();
  ClutterActor dummy;
  int current_depth = 0;

  g_queue_push_tail (queue, actor);
  g_queue_push_tail (queue, &dummy); /* use to delimit depth changes */

  while ((actor = g_queue_pop_head (queue)))
    {
      ClutterActorTraverseVisitFlags flags;

      if (actor == &dummy)
        {
          current_depth++;
          g_queue_push_tail (queue, &dummy);
          continue;
        }

      flags = callback (actor, current_depth, user_data);
      if (flags & CLUTTER_ACTOR_TRAVERSE_VISIT_BREAK)
        break;
      else if (!(flags & CLUTTER_ACTOR_TRAVERSE_VISIT_SKIP_CHILDREN))
        {
          ClutterActor *iter;

          for (iter = actor->priv->first_child;
               iter != NULL;
               iter = iter->priv->next_sibling)
            {
              g_queue_push_tail (queue, iter);
            }
        }
    }

  g_queue_free (queue);
}

static ClutterActorTraverseVisitFlags
_clutter_actor_traverse_depth (ClutterActor           *actor,
                               ClutterTraverseCallback before_children_callback,
                               ClutterTraverseCallback after_children_callback,
                               int                     current_depth,
                               gpointer                user_data)
{
  ClutterActorTraverseVisitFlags flags;

  flags = before_children_callback (actor, current_depth, user_data);
  if (flags & CLUTTER_ACTOR_TRAVERSE_VISIT_BREAK)
    return CLUTTER_ACTOR_TRAVERSE_VISIT_BREAK;

  if (!(flags & CLUTTER_ACTOR_TRAVERSE_VISIT_SKIP_CHILDREN))
    {
      ClutterActor *iter;

      for (iter = actor->priv->first_child;
           iter != NULL;
           iter = iter->priv->next_sibling)
        {
          flags = _clutter_actor_traverse_depth (iter,
                                                 before_children_callback,
                                                 after_children_callback,
                                                 current_depth + 1,
                                                 user_data);

          if (flags & CLUTTER_ACTOR_TRAVERSE_VISIT_BREAK)
            return CLUTTER_ACTOR_TRAVERSE_VISIT_BREAK;
        }
    }

  if (after_children_callback)
    return after_children_callback (actor, current_depth, user_data);
  else
    return CLUTTER_ACTOR_TRAVERSE_VISIT_CONTINUE;
}

/* _clutter_actor_traverse:
 * @actor: The actor to start traversing the graph from
 * @flags: These flags may affect how the traversal is done
 * @before_children_callback: A function to call before visiting the
 *   children of the current actor.
 * @after_children_callback: A function to call after visiting the
 *   children of the current actor. (Ignored if
 *   %CLUTTER_ACTOR_TRAVERSE_BREADTH_FIRST is passed to @flags.)
 * @user_data: The private data to pass to the callbacks
 *
 * Traverses the scenegraph starting at the specified @actor and
 * descending through all its children and its children's children.
 * For each actor traversed @before_children_callback and
 * @after_children_callback are called with the specified
 * @user_data, before and after visiting that actor's children.
 *
 * The callbacks can return flags that affect the ongoing traversal
 * such as by skipping over an actors children or bailing out of
 * any further traversing.
 */
void
_clutter_actor_traverse (ClutterActor              *actor,
                         ClutterActorTraverseFlags  flags,
                         ClutterTraverseCallback    before_children_callback,
                         ClutterTraverseCallback    after_children_callback,
                         gpointer                   user_data)
{
  if (flags & CLUTTER_ACTOR_TRAVERSE_BREADTH_FIRST)
    _clutter_actor_traverse_breadth (actor,
                                     before_children_callback,
                                     user_data);
  else /* DEPTH_FIRST */
    _clutter_actor_traverse_depth (actor,
                                   before_children_callback,
                                   after_children_callback,
                                   0, /* start depth */
                                   user_data);
}

static void
on_layout_manager_changed (ClutterLayoutManager *manager,
                           ClutterActor         *self)
{
  clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_layout_manager:
 * @self: a #ClutterActor
 * @manager: (nullable): a #ClutterLayoutManager, or %NULL to unset it
 *
 * Sets the #ClutterLayoutManager delegate object that will be used to
 * lay out the children of @self.
 *
 * The #ClutterActor will take a reference on the passed @manager which
 * will be released either when the layout manager is removed, or when
 * the actor is destroyed.
 */
void
clutter_actor_set_layout_manager (ClutterActor         *self,
                                  ClutterLayoutManager *manager)
{
  ClutterActorPrivate *priv;
  GType expected_type, manager_type;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (manager == NULL || CLUTTER_IS_LAYOUT_MANAGER (manager));

  priv = self->priv;

  expected_type = clutter_actor_class_get_layout_manager_type (CLUTTER_ACTOR_GET_CLASS (self));
  manager_type = manager != NULL ? G_TYPE_FROM_INSTANCE (manager) : G_TYPE_INVALID;

  if (expected_type != G_TYPE_INVALID &&
      manager_type != G_TYPE_INVALID &&
      !g_type_is_a (manager_type, expected_type))
    {
      g_warning ("Trying to set layout manager of type %s, but actor only accepts %s",
                 g_type_name (manager_type), g_type_name (expected_type));
      return;
    }

  if (priv->layout_manager != NULL)
    {
      g_clear_signal_handler (&priv->layout_changed_id, priv->layout_manager);
      clutter_layout_manager_set_container (priv->layout_manager, NULL);
      g_clear_object (&priv->layout_manager);
    }

  priv->layout_manager = manager;

  if (priv->layout_manager != NULL)
    {
      g_object_ref_sink (priv->layout_manager);
      clutter_layout_manager_set_container (priv->layout_manager, self);
      priv->layout_changed_id =
        g_signal_connect (priv->layout_manager, "layout-changed",
                          G_CALLBACK (on_layout_manager_changed),
                          self);
    }

  clutter_actor_queue_relayout (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_LAYOUT_MANAGER]);
}

/**
 * clutter_actor_get_layout_manager:
 * @self: a #ClutterActor
 *
 * Retrieves the #ClutterLayoutManager used by @self.
 *
 * Return value: (transfer none) (nullable): a pointer to the
 *   #ClutterLayoutManager, or %NULL
 */
ClutterLayoutManager *
clutter_actor_get_layout_manager (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->layout_manager;
}

static const ClutterLayoutInfo default_layout_info = {
  GRAPHENE_POINT_INIT_ZERO,     /* fixed-pos */
  { 0, 0, 0, 0 },               /* margin */
  CLUTTER_ACTOR_ALIGN_FILL,     /* x-align */
  CLUTTER_ACTOR_ALIGN_FILL,     /* y-align */
  FALSE, FALSE,                 /* expand */
  GRAPHENE_SIZE_INIT_ZERO,       /* minimum */
  GRAPHENE_SIZE_INIT_ZERO,       /* natural */
};

static void
layout_info_free (gpointer data)
{
  g_free (data);
}

/*< private >
 * _clutter_actor_peek_layout_info:
 * @self: a #ClutterActor
 *
 * Retrieves a pointer to the ClutterLayoutInfo structure.
 *
 * If the actor does not have a ClutterLayoutInfo associated to it, %NULL is returned.
 *
 * Return value: (transfer none) (nullable): a pointer to the ClutterLayoutInfo
 *   structure
 */
ClutterLayoutInfo *
_clutter_actor_peek_layout_info (ClutterActor *self)
{
  return g_object_get_qdata (G_OBJECT (self), quark_actor_layout_info);
}

/*< private >
 * _clutter_actor_get_layout_info:
 * @self: a #ClutterActor
 *
 * Retrieves a pointer to the ClutterLayoutInfo structure.
 *
 * If the actor does not have a ClutterLayoutInfo associated to it, one
 * will be created and initialized to the default values.
 *
 * This function should be used for setters.
 *
 * For getters, you should use _clutter_actor_get_layout_info_or_defaults()
 * instead.
 *
 * Return value: (transfer none): a pointer to the ClutterLayoutInfo structure
 */
ClutterLayoutInfo *
_clutter_actor_get_layout_info (ClutterActor *self)
{
  ClutterLayoutInfo *retval;

  retval = _clutter_actor_peek_layout_info (self);
  if (retval == NULL)
    {
      retval = g_new0 (ClutterLayoutInfo, 1);

      *retval = default_layout_info;

      g_object_set_qdata_full (G_OBJECT (self), quark_actor_layout_info,
                               retval,
                               layout_info_free);
    }

  return retval;
}

/*< private >
 * _clutter_actor_get_layout_info_or_defaults:
 * @self: a #ClutterActor
 *
 * Retrieves the ClutterLayoutInfo structure associated to an actor.
 *
 * If the actor does not have a ClutterLayoutInfo structure associated to it,
 * then the default structure will be returned.
 *
 * This function should only be used for getters.
 *
 * Return value: (transfer none): a const pointer to the ClutterLayoutInfo
 *   structure
 */
const ClutterLayoutInfo *
_clutter_actor_get_layout_info_or_defaults (ClutterActor *self)
{
  const ClutterLayoutInfo *info;

  info = _clutter_actor_peek_layout_info (self);
  if (info == NULL)
    return &default_layout_info;

  return info;
}

/**
 * clutter_actor_set_x_align:
 * @self: a #ClutterActor
 * @x_align: the horizontal alignment policy
 *
 * Sets the horizontal alignment policy of a #ClutterActor, in case the
 * actor received extra horizontal space.
 *
 * See also the [property@Clutter.Actor:x-align] property.
 */
void
clutter_actor_set_x_align (ClutterActor      *self,
                           ClutterActorAlign  x_align)
{
  ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_layout_info (self);

  if (info->x_align != x_align)
    {
      info->x_align = x_align;

      clutter_actor_queue_relayout (self);

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_X_ALIGN]);
    }
}

/**
 * clutter_actor_get_x_align:
 * @self: a #ClutterActor
 *
 * Retrieves the horizontal alignment policy set using
 * [method@Clutter.Actor.set_x_align].
 *
 * Return value: the horizontal alignment policy.
 */
ClutterActorAlign
clutter_actor_get_x_align (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), CLUTTER_ACTOR_ALIGN_FILL);

  return _clutter_actor_get_layout_info_or_defaults (self)->x_align;
}

/**
 * clutter_actor_set_y_align:
 * @self: a #ClutterActor
 * @y_align: the vertical alignment policy
 *
 * Sets the vertical alignment policy of a #ClutterActor, in case the
 * actor received extra vertical space.
 *
 * See also the [property@Clutter.Actor:y-align] property.
 */
void
clutter_actor_set_y_align (ClutterActor      *self,
                           ClutterActorAlign  y_align)
{
  ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_layout_info (self);

  if (info->y_align != y_align)
    {
      info->y_align = y_align;

      clutter_actor_queue_relayout (self);

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_Y_ALIGN]);
    }
}

/**
 * clutter_actor_get_y_align:
 * @self: a #ClutterActor
 *
 * Retrieves the vertical alignment policy set using
 * [method@Clutter.Actor.set_y_align].
 *
 * Return value: the vertical alignment policy.
 */
ClutterActorAlign
clutter_actor_get_y_align (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), CLUTTER_ACTOR_ALIGN_FILL);

  return _clutter_actor_get_layout_info_or_defaults (self)->y_align;
}

static inline void
clutter_actor_set_margin_internal (ClutterActor *self,
                                   gfloat        margin,
                                   GParamSpec   *pspec)
{
  ClutterLayoutInfo *info;

  info = _clutter_actor_get_layout_info (self);

  if (pspec == obj_props[PROP_MARGIN_TOP])
    info->margin.top = margin;
  else if (pspec == obj_props[PROP_MARGIN_RIGHT])
    info->margin.right = margin;
  else if (pspec == obj_props[PROP_MARGIN_BOTTOM])
    info->margin.bottom = margin;
  else
    info->margin.left = margin;

  clutter_actor_queue_relayout (self);
  g_object_notify_by_pspec (G_OBJECT (self), pspec);
}

/**
 * clutter_actor_set_margin:
 * @self: a #ClutterActor
 * @margin: a #ClutterMargin
 *
 * Sets all the components of the margin of a #ClutterActor.
 */
void
clutter_actor_set_margin (ClutterActor        *self,
                          const ClutterMargin *margin)
{
  ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin != NULL);

  info = _clutter_actor_get_layout_info (self);

  if (info->margin.top != margin->top)
    clutter_actor_set_margin_top (self, margin->top);

  if (info->margin.right != margin->right)
    clutter_actor_set_margin_right (self, margin->right);

  if (info->margin.bottom != margin->bottom)
    clutter_actor_set_margin_bottom (self, margin->bottom);

  if (info->margin.left != margin->left)
    clutter_actor_set_margin_left (self, margin->left);
}

/**
 * clutter_actor_get_margin:
 * @self: a #ClutterActor
 * @margin: (out caller-allocates): return location for a #ClutterMargin
 *
 * Retrieves all the components of the margin of a #ClutterActor.
 */
void
clutter_actor_get_margin (ClutterActor  *self,
                          ClutterMargin *margin)
{
  const ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin != NULL);

  info = _clutter_actor_get_layout_info_or_defaults (self);

  *margin = info->margin;
}

/**
 * clutter_actor_set_margin_top:
 * @self: a #ClutterActor
 * @margin: the top margin
 *
 * Sets the margin from the top of a #ClutterActor.
 *
 * The [property@Clutter.Actor:margin-top] property is animatable.
 */
void
clutter_actor_set_margin_top (ClutterActor *self,
                              gfloat        margin)
{
  const ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin >= 0.f);

  info = _clutter_actor_get_layout_info_or_defaults (self);

  if (info->margin.top == margin)
    return;

  _clutter_actor_create_transition (self, obj_props[PROP_MARGIN_TOP],
                                    info->margin.top,
                                    margin);
}

/**
 * clutter_actor_get_margin_top:
 * @self: a #ClutterActor
 *
 * Retrieves the top margin of a #ClutterActor.
 *
 * Return value: the top margin
 */
gfloat
clutter_actor_get_margin_top (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_layout_info_or_defaults (self)->margin.top;
}

/**
 * clutter_actor_set_margin_bottom:
 * @self: a #ClutterActor
 * @margin: the bottom margin
 *
 * Sets the margin from the bottom of a #ClutterActor.
 *
 * The [property@Clutter.Actor:margin-bottom] property is animatable.
 */
void
clutter_actor_set_margin_bottom (ClutterActor *self,
                                 gfloat        margin)
{
  const ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin >= 0.f);

  info = _clutter_actor_get_layout_info_or_defaults (self);

  if (info->margin.bottom == margin)
    return;

  _clutter_actor_create_transition (self, obj_props[PROP_MARGIN_BOTTOM],
                                    info->margin.bottom,
                                    margin);
}

/**
 * clutter_actor_get_margin_bottom:
 * @self: a #ClutterActor
 *
 * Retrieves the bottom margin of a #ClutterActor.
 *
 * Return value: the bottom margin
 */
gfloat
clutter_actor_get_margin_bottom (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_layout_info_or_defaults (self)->margin.bottom;
}

/**
 * clutter_actor_set_margin_left:
 * @self: a #ClutterActor
 * @margin: the left margin
 *
 * Sets the margin from the left of a #ClutterActor.
 *
 * The [property@Clutter.Actor:margin-left] property is animatable.
 */
void
clutter_actor_set_margin_left (ClutterActor *self,
                               gfloat        margin)
{
  const ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin >= 0.f);

  info = _clutter_actor_get_layout_info_or_defaults (self);

  if (info->margin.left == margin)
    return;

  _clutter_actor_create_transition (self, obj_props[PROP_MARGIN_LEFT],
                                    info->margin.left,
                                    margin);
}

/**
 * clutter_actor_get_margin_left:
 * @self: a #ClutterActor
 *
 * Retrieves the left margin of a #ClutterActor.
 *
 * Return value: the left margin
 */
gfloat
clutter_actor_get_margin_left (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_layout_info_or_defaults (self)->margin.left;
}

/**
 * clutter_actor_set_margin_right:
 * @self: a #ClutterActor
 * @margin: the right margin
 *
 * Sets the margin from the right of a #ClutterActor.
 *
 * The [property@Clutter.Actor:margin-right] property is animatable.
 */
void
clutter_actor_set_margin_right (ClutterActor *self,
                                gfloat        margin)
{
  const ClutterLayoutInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (margin >= 0.f);

  info = _clutter_actor_get_layout_info_or_defaults (self);

  if (info->margin.right == margin)
    return;

  _clutter_actor_create_transition (self, obj_props[PROP_MARGIN_RIGHT],
                                    info->margin.right,
                                    margin);
}

/**
 * clutter_actor_get_margin_right:
 * @self: a #ClutterActor
 *
 * Retrieves the right margin of a #ClutterActor.
 *
 * Return value: the right margin
 */
gfloat
clutter_actor_get_margin_right (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0.f);

  return _clutter_actor_get_layout_info_or_defaults (self)->margin.right;
}

static inline void
clutter_actor_set_background_color_internal (ClutterActor *self,
                                             const CoglColor *color)
{
  ClutterActorPrivate *priv = self->priv;
  GObject *obj;

  if (priv->bg_color_set && cogl_color_equal (color, &priv->bg_color))
    return;

  obj = G_OBJECT (self);

  priv->bg_color = *color;
  priv->bg_color_set = TRUE;

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (obj, obj_props[PROP_BACKGROUND_COLOR_SET]);
  g_object_notify_by_pspec (obj, obj_props[PROP_BACKGROUND_COLOR]);
}

/**
 * clutter_actor_set_background_color:
 * @self: a #ClutterActor
 * @color: (nullable): a #CoglColor, or %NULL to unset a previously
 *  set color
 *
 * Sets the background color of a #ClutterActor.
 *
 * The background color will be used to cover the whole allocation of the
 * actor. The default background color of an actor is transparent.
 *
 * To check whether an actor has a background color, you can use the
 * [property@Clutter.Actor:background-color-set] actor property.
 *
 * The [property@Clutter.Actor:background-color] property is animatable.
 */
void
clutter_actor_set_background_color (ClutterActor    *self,
                                    const CoglColor *color)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (color == NULL)
    {
      GObject *obj = G_OBJECT (self);

      priv->bg_color_set = FALSE;

      clutter_actor_queue_redraw (self);

      g_object_notify_by_pspec (obj, obj_props[PROP_BACKGROUND_COLOR_SET]);
    }
  else
    _clutter_actor_create_transition (self,
                                      obj_props[PROP_BACKGROUND_COLOR],
                                      &priv->bg_color,
                                      color);
}

/**
 * clutter_actor_get_background_color:
 * @self: a #ClutterActor
 * @color: (out caller-allocates): return location for a #CoglColor
 *
 * Retrieves the color set using [method@Clutter.Actor.set_background_color].
 */
void
clutter_actor_get_background_color (ClutterActor *self,
                                    CoglColor    *color)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (color != NULL);

  *color = self->priv->bg_color;
}

/**
 * clutter_actor_get_previous_sibling:
 * @self: a #ClutterActor
 *
 * Retrieves the sibling of @self that comes before it in the list
 * of children of @self's parent.
 *
 * The returned pointer is only valid until the scene graph changes; it
 * is not safe to modify the list of children of @self while iterating
 * it.
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterActor,
 *   or %NULL
 */
ClutterActor *
clutter_actor_get_previous_sibling (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->prev_sibling;
}

/**
 * clutter_actor_get_next_sibling:
 * @self: a #ClutterActor
 *
 * Retrieves the sibling of @self that comes after it in the list
 * of children of @self's parent.
 *
 * The returned pointer is only valid until the scene graph changes; it
 * is not safe to modify the list of children of @self while iterating
 * it.
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterActor,
 *   or %NULL
 */
ClutterActor *
clutter_actor_get_next_sibling (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->next_sibling;
}

/**
 * clutter_actor_get_first_child:
 * @self: a #ClutterActor
 *
 * Retrieves the first child of @self.
 *
 * The returned pointer is only valid until the scene graph changes; it
 * is not safe to modify the list of children of @self while iterating
 * it.
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterActor,
 *   or %NULL
 */
ClutterActor *
clutter_actor_get_first_child (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->first_child;
}

/**
 * clutter_actor_get_last_child:
 * @self: a #ClutterActor
 *
 * Retrieves the last child of @self.
 *
 * The returned pointer is only valid until the scene graph changes; it
 * is not safe to modify the list of children of @self while iterating
 * it.
 *
 * Return value: (transfer none) (nullable): a pointer to a #ClutterActor,
 *   or %NULL
 */
ClutterActor *
clutter_actor_get_last_child (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->last_child;
}

/* easy way to have properly named fields instead of the dummy ones
 * we use in the public structure
 */
typedef struct _RealActorIter
{
  ClutterActor *root;           /* dummy1 */
  ClutterActor *current;        /* dummy2 */
  gint age;                     /* dummy3 */
} RealActorIter;

/**
 * clutter_actor_iter_init:
 * @iter: a #ClutterActorIter
 * @root: a #ClutterActor
 *
 * Initializes a #ClutterActorIter, which can then be used to iterate
 * efficiently over a section of the scene graph, and associates it
 * with @root.
 *
 * Modifying the scene graph section that contains @root will invalidate
 * the iterator.
 *
 * ```c
 *   ClutterActorIter iter;
 *   ClutterActor *child;
 *
 *   clutter_actor_iter_init (&iter, container);
 *   while (clutter_actor_iter_next (&iter, &child))
 *     {
 *       // do something with child
 *     }
 * ```
 */
void
clutter_actor_iter_init (ClutterActorIter *iter,
                         ClutterActor     *root)
{
  RealActorIter *ri = (RealActorIter *) iter;

  g_return_if_fail (iter != NULL);
  g_return_if_fail (CLUTTER_IS_ACTOR (root));

  ri->root = root;
  ri->current = NULL;
  ri->age = root->priv->age;
}

/**
 * clutter_actor_iter_is_valid:
 * @iter: a #ClutterActorIter
 *
 * Checks whether a #ClutterActorIter is still valid.
 *
 * An iterator is considered valid if it has been initialized, and
 * if the #ClutterActor that it refers to hasn't been modified after
 * the initialization.
 *
 * Return value: %TRUE if the iterator is valid, and %FALSE otherwise
 */
gboolean
clutter_actor_iter_is_valid (const ClutterActorIter *iter)
{
  RealActorIter *ri = (RealActorIter *) iter;

  g_return_val_if_fail (iter != NULL, FALSE);

  if (ri->root == NULL)
    return FALSE;

  return ri->root->priv->age == ri->age;
}

/**
 * clutter_actor_iter_next:
 * @iter: a #ClutterActorIter
 * @child: (out) (transfer none) (optional) (nullable): return location for a
 * #ClutterActor
 *
 * Advances the @iter and retrieves the next child of the root #ClutterActor
 * that was used to initialize the #ClutterActorIterator.
 *
 * If the iterator can advance, this function returns %TRUE and sets the
 * @child argument.
 *
 * If the iterator cannot advance, this function returns %FALSE, and
 * the contents of @child are undefined.
 *
 * Return value: %TRUE if the iterator could advance, and %FALSE otherwise.
 */
gboolean
clutter_actor_iter_next (ClutterActorIter  *iter,
                         ClutterActor     **child)
{
  RealActorIter *ri = (RealActorIter *) iter;

  g_return_val_if_fail (iter != NULL, FALSE);
  g_return_val_if_fail (ri->root != NULL, FALSE);
#ifndef G_DISABLE_ASSERT
  g_return_val_if_fail (ri->age == ri->root->priv->age, FALSE);
#endif

  if (ri->current == NULL)
    ri->current = ri->root->priv->first_child;
  else
    ri->current = ri->current->priv->next_sibling;

  if (child != NULL)
    *child = ri->current;

  return ri->current != NULL;
}

/**
 * clutter_actor_iter_prev:
 * @iter: a #ClutterActorIter
 * @child: (out) (transfer none) (optional) (nullable): return location for a
 *   #ClutterActor
 *
 * Advances the @iter and retrieves the previous child of the root
 * #ClutterActor that was used to initialize the #ClutterActorIterator.
 *
 * If the iterator can advance, this function returns %TRUE and sets the
 * @child argument.
 *
 * If the iterator cannot advance, this function returns %FALSE, and
 * the contents of @child are undefined.
 *
 * Return value: %TRUE if the iterator could advance, and %FALSE otherwise.
 */
gboolean
clutter_actor_iter_prev (ClutterActorIter  *iter,
                         ClutterActor     **child)
{
  RealActorIter *ri = (RealActorIter *) iter;

  g_return_val_if_fail (iter != NULL, FALSE);
  g_return_val_if_fail (ri->root != NULL, FALSE);
#ifndef G_DISABLE_ASSERT
  g_return_val_if_fail (ri->age == ri->root->priv->age, FALSE);
#endif

  if (ri->current == NULL)
    ri->current = ri->root->priv->last_child;
  else
    ri->current = ri->current->priv->prev_sibling;

  if (child != NULL)
    *child = ri->current;

  return ri->current != NULL;
}

/**
 * clutter_actor_iter_remove:
 * @iter: a #ClutterActorIter
 *
 * Safely removes the #ClutterActor currently pointer to by the iterator
 * from its parent.
 *
 * This function can only be called after [method@Clutter.ActorIter.next] or
  [method@Clutter.ActorIter.prev] returned %TRUE, and cannot be called more
 * than once for the same actor.
 *
 * This function will call [method@Clutter.Actor.remove_child] internally.
 */
void
clutter_actor_iter_remove (ClutterActorIter *iter)
{
  RealActorIter *ri = (RealActorIter *) iter;
  ClutterActor *cur;

  g_return_if_fail (iter != NULL);
  g_return_if_fail (ri->root != NULL);
#ifndef G_DISABLE_ASSERT
  g_return_if_fail (ri->age == ri->root->priv->age);
#endif
  g_return_if_fail (ri->current != NULL);

  cur = ri->current;

  if (cur != NULL)
    {
      ri->current = cur->priv->prev_sibling;

      clutter_actor_remove_child_internal (ri->root, cur,
                                           REMOVE_CHILD_DEFAULT_FLAGS);

      ri->age += 1;
    }
}

/**
 * clutter_actor_iter_destroy:
 * @iter: a #ClutterActorIter
 *
 * Safely destroys the #ClutterActor currently pointer to by the iterator
 * from its parent.
 *
 * This function can only be called after [method@Clutter.ActorIter.next] or
 * [method@Clutter.ActorIter.prev] returned %TRUE, and cannot be called more
 * than once for the same actor.
 *
 * This function will call [method@Clutter.Actor.destroy] internally.
 */
void
clutter_actor_iter_destroy (ClutterActorIter *iter)
{
  RealActorIter *ri = (RealActorIter *) iter;
  ClutterActor *cur;

  g_return_if_fail (iter != NULL);
  g_return_if_fail (ri->root != NULL);
#ifndef G_DISABLE_ASSERT
  g_return_if_fail (ri->age == ri->root->priv->age);
#endif
  g_return_if_fail (ri->current != NULL);

  cur = ri->current;

  if (cur != NULL)
    {
      ri->current = cur->priv->prev_sibling;

      clutter_actor_destroy (cur);

      ri->age += 1;
    }
}

static const ClutterAnimationInfo default_animation_info = {
  NULL,         /* states */
  NULL,         /* cur_state */
  NULL,         /* transitions */
};

static void
clutter_animation_info_free (gpointer data)
{
  if (data != NULL)
    {
      g_autofree ClutterAnimationInfo *info = data;

      g_clear_pointer (&info->transitions, g_hash_table_unref);
      g_clear_pointer (&info->states, g_array_unref);
    }
}

const ClutterAnimationInfo *
_clutter_actor_get_animation_info_or_defaults (ClutterActor *self)
{
  const ClutterAnimationInfo *res;
  GObject *obj = G_OBJECT (self);

  res = g_object_get_qdata (obj, quark_actor_animation_info);
  if (res != NULL)
    return res;

  return &default_animation_info;
}

ClutterAnimationInfo *
_clutter_actor_get_animation_info (ClutterActor *self)
{
  GObject *obj = G_OBJECT (self);
  ClutterAnimationInfo *res;

  res = g_object_get_qdata (obj, quark_actor_animation_info);
  if (res == NULL)
    {
      res = g_new0 (ClutterAnimationInfo, 1);

      *res = default_animation_info;

      g_object_set_qdata_full (obj, quark_actor_animation_info,
                               res,
                               clutter_animation_info_free);
    }

  return res;
}

static void
transition_closure_free (gpointer data)
{
  if (G_LIKELY (data != NULL))
    {
      TransitionClosure *clos = data;
      ClutterTimeline *timeline;

      timeline = CLUTTER_TIMELINE (clos->transition);

      /* we disconnect the signal handler before stopping the timeline,
       * so that we don't end up inside on_transition_stopped() from
       * a call to g_hash_table_remove().
       */
      g_clear_signal_handler (&clos->completed_id, clos->transition);

      if (clutter_timeline_is_playing (timeline))
        clutter_timeline_stop (timeline);
      else if (clutter_timeline_get_delay (timeline) > 0)
        clutter_timeline_cancel_delay (timeline);

      /* remove the reference added in add_transition_internal() */
      g_object_unref (clos->transition);

      g_free (clos->name);

      g_free (clos);
    }
}

static void
on_transition_stopped (ClutterTransition *transition,
                       gboolean           is_finished,
                       TransitionClosure *clos)
{
  ClutterActor *actor = clos->actor;
  ClutterAnimationInfo *info;
  GQuark t_quark;
  g_autofree char *t_name = NULL;

  if (clos->name == NULL)
    return;

  /* reset the caches used by animations */
  clutter_actor_store_content_box (actor, NULL);

  info = _clutter_actor_get_animation_info (actor);

  /* we need copies because we emit the signal after the
   * TransitionClosure data structure has been freed
   */
  t_quark = g_quark_from_string (clos->name);
  t_name = g_strdup (clos->name);

  if (clutter_transition_get_remove_on_complete (transition))
    {
      /* this is safe, because the timeline has now stopped,
       * so we won't recurse; the reference on the Animatable
       * will be dropped by the ::stopped signal closure in
       * ClutterTransition, which is RUN_LAST, and thus will
       * be called after this handler
       */
      g_hash_table_remove (info->transitions, clos->name);
    }

  /* we emit the ::transition-stopped after removing the
   * transition, so that we can chain up new transitions
   * without interfering with the one that just finished
   */
  g_signal_emit (actor, actor_signals[TRANSITION_STOPPED], t_quark,
                 t_name,
                 is_finished);

  /* if it's the last transition then we clean up */
  if (g_hash_table_size (info->transitions) == 0)
    {
      g_clear_pointer (&info->transitions, g_hash_table_unref);

      CLUTTER_NOTE (ANIMATION, "Transitions for '%s' completed",
                    _clutter_actor_get_debug_name (actor));

      g_signal_emit (actor, actor_signals[TRANSITIONS_COMPLETED], 0);
      clutter_actor_update_devices (actor);
    }
}

static void
clutter_actor_add_transition_internal (ClutterActor *self,
                                       const gchar  *name,
                                       ClutterTransition *transition)
{
  ClutterTimeline *timeline;
  TransitionClosure *clos;
  ClutterAnimationInfo *info;

  info = _clutter_actor_get_animation_info (self);

  if (info->transitions == NULL)
    info->transitions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL,
                                               transition_closure_free);

  if (g_hash_table_lookup (info->transitions, name) != NULL)
    {
      g_warning ("A transition with name '%s' already exists for "
                 "the actor '%s'",
                 name,
                 _clutter_actor_get_debug_name (self));
      return;
    }

  clutter_transition_set_animatable (transition, CLUTTER_ANIMATABLE (self));

  timeline = CLUTTER_TIMELINE (transition);

  clos = g_new0 (TransitionClosure, 1);
  clos->actor = self;
  clos->transition = g_object_ref (transition);
  clos->name = g_strdup (name);
  clos->completed_id = g_signal_connect (timeline, "stopped",
                                         G_CALLBACK (on_transition_stopped),
                                         clos);

  CLUTTER_NOTE (ANIMATION,
                "Adding transition '%s' [%p] to actor '%s'",
                clos->name,
                clos->transition,
                _clutter_actor_get_debug_name (self));

  g_hash_table_insert (info->transitions, clos->name, clos);
  clutter_timeline_start (timeline);
}

static gboolean
should_skip_implicit_transition (ClutterActor *self,
                                 GParamSpec   *pspec)
{
  ClutterActorPrivate *priv = self->priv;
  const ClutterAnimationInfo *info;

  /* this function is called from _clutter_actor_create_transition() which
   * calls _clutter_actor_get_animation_info() first, so we're guaranteed
   * to have the correct ClutterAnimationInfo pointer
   */
  info = _clutter_actor_get_animation_info_or_defaults (self);

  /* if the easing state has a non-zero duration we always want an
   * implicit transition to occur
   */
  if (info->cur_state->easing_duration == 0)
    return TRUE;

  /* on the other hand, if the actor hasn't been allocated yet, we want to
   * skip all transitions on the :allocation, to avoid actors "flying in"
   * into their new position and size
   */
  if (pspec == obj_props[PROP_ALLOCATION] &&
      !clutter_actor_box_is_initialized (&priv->allocation))
    return TRUE;

  /* if the actor is not mapped and is not part of a branch of the scene
   * graph that is being cloned, then we always skip implicit transitions
   * on the account of the fact that the actor is not going to be visible
   * when those transitions happen
   */
  if (!clutter_actor_is_mapped (self) &&
      !clutter_actor_has_mapped_clones (self))
    return TRUE;

  return FALSE;
}

/*< private >*
 * _clutter_actor_create_transition:
 * @actor: a #ClutterActor
 * @pspec: the property used for the transition
 * @...: initial and final state
 *
 * Creates a #ClutterTransition for the property represented by @pspec.
 *
 * Return value: a #ClutterTransition
 */
ClutterTransition *
_clutter_actor_create_transition (ClutterActor *actor,
                                  GParamSpec   *pspec,
                                  ...)
{
  ClutterTimeline *timeline;
  ClutterInterval *interval;
  ClutterAnimationInfo *info;
  ClutterTransition *res = NULL;
  gboolean call_restore = FALSE;
  TransitionClosure *clos;
  va_list var_args;
  g_auto (GValue) initial = G_VALUE_INIT;
  g_auto (GValue) final = G_VALUE_INIT;
  GType ptype;
  g_autofree char *error = NULL;

  g_assert (pspec != NULL);
  g_assert ((pspec->flags & CLUTTER_PARAM_ANIMATABLE) != 0);

  info = _clutter_actor_get_animation_info (actor);

  /* XXX - this will go away in 2.0
   *
   * if no state has been pushed, we assume that the easing state is
   * in "compatibility mode": all transitions have a duration of 0
   * msecs, which means that they happen immediately. in Clutter 2.0
   * this will turn into a g_assert(info->states != NULL), as every
   * actor will start with a predefined easing state
   */
  if (info->states == NULL)
    {
      clutter_actor_save_easing_state (actor);
      clutter_actor_set_easing_duration (actor, 0);
      call_restore = TRUE;
    }

  if (info->transitions == NULL)
    info->transitions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL,
                                               transition_closure_free);

  va_start (var_args, pspec);

  ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);

  G_VALUE_COLLECT_INIT (&initial, ptype,
                        var_args, 0,
                        &error);
  if (error != NULL)
    {
      g_critical ("%s: %s", G_STRLOC, error);
      goto out;
    }

  G_VALUE_COLLECT_INIT (&final, ptype,
                        var_args, 0,
                        &error);
  if (error != NULL)
    {
      g_critical ("%s: %s", G_STRLOC, error);
      goto out;
    }

  if (should_skip_implicit_transition (actor, pspec))
    {
      CLUTTER_NOTE (ANIMATION, "Skipping implicit transition for '%s::%s'",
                    _clutter_actor_get_debug_name (actor),
                    pspec->name);

      /* remove a transition, if one exists */
      clutter_actor_remove_transition (actor, pspec->name);

      /* we don't go through the Animatable interface because we
       * already know we got here through an animatable property.
       */
      clutter_actor_set_animatable_property (actor,
                                             pspec->param_id,
                                             &final,
                                             pspec);

      goto out;
    }

  clos = g_hash_table_lookup (info->transitions, pspec->name);
  if (clos == NULL)
    {
      res = clutter_property_transition_new (pspec->name);

      clutter_transition_set_remove_on_complete (res, TRUE);

      interval = clutter_interval_new_with_values (ptype, &initial, &final);
      clutter_transition_set_interval (res, interval);

      timeline = CLUTTER_TIMELINE (res);
      clutter_timeline_set_delay (timeline, info->cur_state->easing_delay);
      clutter_timeline_set_duration (timeline, info->cur_state->easing_duration);
      clutter_timeline_set_progress_mode (timeline, info->cur_state->easing_mode);

#ifdef CLUTTER_ENABLE_DEBUG
      if (CLUTTER_HAS_DEBUG (ANIMATION))
        {
          g_autofree char *initial_v = NULL;
          g_autofree char *final_v = NULL;

          initial_v = g_strdup_value_contents (&initial);
          final_v = g_strdup_value_contents (&final);

          CLUTTER_NOTE (ANIMATION,
                        "Created transition for %s:%s "
                        "(len:%u, mode:%s, delay:%u) "
                        "initial:%s, final:%s",
                        _clutter_actor_get_debug_name (actor),
                        pspec->name,
                        info->cur_state->easing_duration,
                        clutter_get_easing_name_for_mode (info->cur_state->easing_mode),
                        info->cur_state->easing_delay,
                        initial_v, final_v);
        }
#endif /* CLUTTER_ENABLE_DEBUG */

      /* this will start the transition as well */
      clutter_actor_add_transition_internal (actor, pspec->name, res);

      /* the actor now owns the transition */
      g_object_unref (res);
    }
  else
    {
      ClutterAnimationMode cur_mode;
      guint cur_duration;

      CLUTTER_NOTE (ANIMATION, "Existing transition for %s:%s",
                    _clutter_actor_get_debug_name (actor),
                    pspec->name);

      timeline = CLUTTER_TIMELINE (clos->transition);

      cur_duration = clutter_timeline_get_duration (timeline);
      if (cur_duration != info->cur_state->easing_duration)
        clutter_timeline_set_duration (timeline, info->cur_state->easing_duration);

      cur_mode = clutter_timeline_get_progress_mode (timeline);
      if (cur_mode != info->cur_state->easing_mode)
        clutter_timeline_set_progress_mode (timeline, info->cur_state->easing_mode);

      clutter_timeline_rewind (timeline);

      interval = clutter_transition_get_interval (clos->transition);
      clutter_interval_set_initial_value (interval, &initial);
      clutter_interval_set_final_value (interval, &final);

      res = clos->transition;
    }

out:
  if (call_restore)
    clutter_actor_restore_easing_state (actor);

  va_end (var_args);

  return res;
}

/**
 * clutter_actor_add_transition:
 * @self: a #ClutterActor
 * @name: the name of the transition to add
 * @transition: the #ClutterTransition to add
 *
 * Adds a @transition to the #ClutterActor's list of animations.
 *
 * The @name string is a per-actor unique identifier of the @transition: only
 * one #ClutterTransition can be associated to the specified @name.
 *
 * The @transition will be started once added.
 *
 * This function will take a reference on the @transition.
 *
 * This function is usually called implicitly when modifying an animatable
 * property.
 */
void
clutter_actor_add_transition (ClutterActor      *self,
                              const char        *name,
                              ClutterTransition *transition)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);
  g_return_if_fail (CLUTTER_IS_TRANSITION (transition));

  clutter_actor_add_transition_internal (self, name, transition);
}

/**
 * clutter_actor_remove_transition:
 * @self: a #ClutterActor
 * @name: the name of the transition to remove
 *
 * Removes the transition stored inside a #ClutterActor using @name
 * identifier.
 *
 * If the transition is currently in progress, it will be stopped.
 *
 * This function releases the reference acquired when the transition
 * was added to the #ClutterActor.
 */
void
clutter_actor_remove_transition (ClutterActor *self,
                                 const char   *name)
{
  const ClutterAnimationInfo *info;
  TransitionClosure *clos;
  gboolean was_playing;
  GQuark t_quark;
  g_autofree char *t_name = NULL;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (name != NULL);

  info = _clutter_actor_get_animation_info_or_defaults (self);

  if (info->transitions == NULL)
    return;

  clos = g_hash_table_lookup (info->transitions, name);
  if (clos == NULL)
    return;

  was_playing =
    clutter_timeline_is_playing (CLUTTER_TIMELINE (clos->transition));
  t_quark = g_quark_from_string (clos->name);
  t_name = g_strdup (clos->name);

  g_hash_table_remove (info->transitions, name);

  /* we want to maintain the invariant that ::transition-stopped is
   * emitted after the transition has been removed, to allow replacing
   * or chaining; removing the transition from the hash table will
   * stop it, but transition_closure_free() will disconnect the signal
   * handler we install in add_transition_internal(), to avoid loops
   * or segfaults.
   *
   * since we know already that a transition will stop once it's removed
   * from an actor, we can simply emit the ::transition-stopped here
   * ourselves, if the timeline was playing (if it wasn't, then the
   * signal was already emitted at least once).
   */
  if (was_playing)
    {
      g_signal_emit (self, actor_signals[TRANSITION_STOPPED],
                     t_quark,
                     t_name,
                     FALSE);
    }
}

/**
 * clutter_actor_remove_all_transitions:
 * @self: a #ClutterActor
 *
 * Removes all transitions associated to @self.
 */
void
clutter_actor_remove_all_transitions (ClutterActor *self)
{
  const ClutterAnimationInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_animation_info_or_defaults (self);
  if (info->transitions == NULL)
    return;

  g_hash_table_remove_all (info->transitions);
}

/**
 * clutter_actor_set_easing_duration:
 * @self: a #ClutterActor
 * @msecs: the duration of the easing, or %NULL
 *
 * Sets the duration of the tweening for animatable properties
 * of @self for the current easing state.
 */
void
clutter_actor_set_easing_duration (ClutterActor *self,
                                   guint         msecs)
{
  ClutterAnimationInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_animation_info (self);

  if (info->cur_state == NULL)
    {
      g_warning ("You must call clutter_actor_save_easing_state() prior "
                 "to calling clutter_actor_set_easing_duration().");
      return;
    }

  if (info->cur_state->easing_duration != msecs)
    info->cur_state->easing_duration = msecs;
}

/**
 * clutter_actor_get_easing_duration:
 * @self: a #ClutterActor
 *
 * Retrieves the duration of the tweening for animatable
 * properties of @self for the current easing state.
 *
 * Return value: the duration of the tweening, in milliseconds
 */
guint
clutter_actor_get_easing_duration (ClutterActor *self)
{
  const ClutterAnimationInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  info = _clutter_actor_get_animation_info_or_defaults (self);

  if (info->cur_state != NULL)
    return info->cur_state->easing_duration;

  return 0;
}

/**
 * clutter_actor_set_easing_mode:
 * @self: a #ClutterActor
 * @mode: an easing mode, excluding %CLUTTER_CUSTOM_MODE
 *
 * Sets the easing mode for the tweening of animatable properties
 * of @self.
 */
void
clutter_actor_set_easing_mode (ClutterActor         *self,
                               ClutterAnimationMode  mode)
{
  ClutterAnimationInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (mode != CLUTTER_CUSTOM_MODE);
  g_return_if_fail (mode < CLUTTER_ANIMATION_LAST);

  info = _clutter_actor_get_animation_info (self);

  if (info->cur_state == NULL)
    {
      g_warning ("You must call clutter_actor_save_easing_state() prior "
                 "to calling clutter_actor_set_easing_mode().");
      return;
    }

  if (info->cur_state->easing_mode != mode)
    info->cur_state->easing_mode = mode;
}

/**
 * clutter_actor_get_easing_mode:
 * @self: a #ClutterActor
 *
 * Retrieves the easing mode for the tweening of animatable properties
 * of @self for the current easing state.
 *
 * Return value: an easing mode
 */
ClutterAnimationMode
clutter_actor_get_easing_mode (ClutterActor *self)
{
  const ClutterAnimationInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), CLUTTER_EASE_OUT_CUBIC);

  info = _clutter_actor_get_animation_info_or_defaults (self);

  if (info->cur_state != NULL)
    return info->cur_state->easing_mode;

  return CLUTTER_EASE_OUT_CUBIC;
}

/**
 * clutter_actor_set_easing_delay:
 * @self: a #ClutterActor
 * @msecs: the delay before the start of the tweening, in milliseconds
 *
 * Sets the delay that should be applied before tweening animatable
 * properties.
 */
void
clutter_actor_set_easing_delay (ClutterActor *self,
                                guint         msecs)
{
  ClutterAnimationInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_animation_info (self);

  if (info->cur_state == NULL)
    {
      g_warning ("You must call clutter_actor_save_easing_state() prior "
                 "to calling clutter_actor_set_easing_delay().");
      return;
    }

  if (info->cur_state->easing_delay != msecs)
    info->cur_state->easing_delay = msecs;
}

/**
 * clutter_actor_get_easing_delay:
 * @self: a #ClutterActor
 *
 * Retrieves the delay that should be applied when tweening animatable
 * properties.
 *
 * Return value: a delay, in milliseconds
 */
guint
clutter_actor_get_easing_delay (ClutterActor *self)
{
  const ClutterAnimationInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), 0);

  info = _clutter_actor_get_animation_info_or_defaults (self);

  if (info->cur_state != NULL)
    return info->cur_state->easing_delay;

  return 0;
}

/**
 * clutter_actor_get_transition:
 * @self: a #ClutterActor
 * @name: the name of the transition
 *
 * Retrieves the #ClutterTransition of a #ClutterActor by using the
 * transition @name.
 *
 * Transitions created for animatable properties use the name of the
 * property itself, for instance the code below:
 *
 * ```c
 *   clutter_actor_set_easing_duration (actor, 1000);
 *   clutter_actor_set_rotation_angle (actor, CLUTTER_Y_AXIS, 360.0);
 *
 *   transition = clutter_actor_get_transition (actor, "rotation-angle-y");
 *   g_signal_connect (transition, "stopped",
 *                     G_CALLBACK (on_transition_stopped),
 *                     actor);
 * ```
 *
 * will call the `on_transition_stopped` callback when the transition
 * is finished.
 *
 * If you just want to get notifications of the completion of a transition,
 * you should use the [signal@Clutter.Actor::transition-stopped] signal, using the
 * transition name as the signal detail.
 *
 * Return value: (transfer none) (nullable): a #ClutterTransition, or %NULL if
 *   none was found to match the passed name; the returned instance is owned
 *   by Clutter and it should not be freed
 */
ClutterTransition *
clutter_actor_get_transition (ClutterActor *self,
                              const char   *name)
{
  TransitionClosure *clos;
  const ClutterAnimationInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  info = _clutter_actor_get_animation_info_or_defaults (self);
  if (info->transitions == NULL)
    return NULL;

  clos = g_hash_table_lookup (info->transitions, name);
  if (clos == NULL)
    return NULL;

  return clos->transition;
}

/**
 * clutter_actor_has_transitions: (skip)
 */
gboolean
clutter_actor_has_transitions (ClutterActor *self)
{
  const ClutterAnimationInfo *info;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  info = _clutter_actor_get_animation_info_or_defaults (self);
  if (info->transitions == NULL)
    return FALSE;

  return g_hash_table_size (info->transitions) > 0;
}

/**
 * clutter_actor_save_easing_state:
 * @self: a #ClutterActor
 *
 * Saves the current easing state for animatable properties, and creates
 * a new state with the default values for easing mode and duration.
 *
 * New transitions created after calling this function will inherit the
 * duration, easing mode, and delay of the new easing state; this also
 * applies to transitions modified in flight.
 */
void
clutter_actor_save_easing_state (ClutterActor *self)
{
  ClutterAnimationInfo *info;
  AState new_state;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_animation_info (self);

  if (info->states == NULL)
    info->states = g_array_new (FALSE, FALSE, sizeof (AState));

  new_state.easing_mode = CLUTTER_EASE_OUT_CUBIC;
  new_state.easing_duration = 250;
  new_state.easing_delay = 0;

  g_array_append_val (info->states, new_state);

  info->cur_state = &g_array_index (info->states, AState, info->states->len - 1);
}

/**
 * clutter_actor_restore_easing_state:
 * @self: a #ClutterActor
 *
 * Restores the easing state as it was prior to a call to
 * [method@Clutter.Actor.save_easing_state].
 */
void
clutter_actor_restore_easing_state (ClutterActor *self)
{
  ClutterAnimationInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_animation_info (self);

  if (info->states == NULL)
    {
      g_critical ("The function clutter_actor_restore_easing_state() has "
                  "been called without a previous call to "
                  "clutter_actor_save_easing_state().");
      return;
    }

  g_array_remove_index (info->states, info->states->len - 1);

  if (info->states->len > 0)
    info->cur_state = &g_array_index (info->states, AState, info->states->len - 1);
  else
    {
      g_clear_pointer (&info->states, g_array_unref);
      info->cur_state = NULL;
    }
}

/**
 * clutter_actor_set_content:
 * @self: a #ClutterActor
 * @content: (nullable): a #ClutterContent, or %NULL
 *
 * Sets the contents of a #ClutterActor.
 */
void
clutter_actor_set_content (ClutterActor   *self,
                           ClutterContent *content)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (content == NULL || CLUTTER_IS_CONTENT (content));

  priv = self->priv;

  if (priv->content == content)
    return;

  if (priv->content != NULL)
    {
      _clutter_content_detached (priv->content, self);
      g_clear_object (&priv->content);
    }

  priv->content = content;

  if (priv->content != NULL)
    {
      g_object_ref (priv->content);
      _clutter_content_attached (priv->content, self);
    }

  /* if the actor's preferred size is the content's preferred size,
   * then we need to conditionally queue a relayout here...
   */
  if (priv->request_mode == CLUTTER_REQUEST_CONTENT_SIZE)
    _clutter_actor_queue_only_relayout (self);

  clutter_actor_queue_redraw (self);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONTENT]);

  /* if the content gravity is not resize-fill, and the new content has a
   * different preferred size than the previous one, then the content box
   * may have been changed. since we compute that lazily, we just notify
   * here, and let whomever watches :content-box do whatever they need to
   * do.
   */
  if (priv->content_gravity != CLUTTER_CONTENT_GRAVITY_RESIZE_FILL)
    {
      if (priv->content_box_valid)
        {
          ClutterActorBox from_box, to_box;

          clutter_actor_get_content_box (self, &from_box);

          /* invalidate the cached content box */
          priv->content_box_valid = FALSE;
          clutter_actor_get_content_box (self, &to_box);

          if (!clutter_actor_box_equal (&from_box, &to_box))
            _clutter_actor_create_transition (self, obj_props[PROP_CONTENT_BOX],
                                              &from_box,
                                              &to_box);
        }

      g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONTENT_BOX]);
   }
}

/**
 * clutter_actor_get_content:
 * @self: a #ClutterActor
 *
 * Retrieves the contents of @self.
 *
 * Return value: (transfer none) (nullable): a pointer to the #ClutterContent
 *   instance, or %NULL if none was set
 */
ClutterContent *
clutter_actor_get_content (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  return self->priv->content;
}

/**
 * clutter_actor_set_content_gravity:
 * @self: a #ClutterActor
 * @gravity: the #ClutterContentGravity
 *
 * Sets the gravity of the #ClutterContent used by @self.
 *
 * See the description of the [property@Clutter.Actor:content-gravity] property for
 * more information.
 *
 * The [property@Clutter.Actor:content-gravity] property is animatable.
 */
void
clutter_actor_set_content_gravity (ClutterActor *self,
                                   ClutterContentGravity  gravity)
{
  ClutterActorPrivate *priv;
  ClutterActorBox from_box, to_box;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;

  if (priv->content_gravity == gravity)
    return;

  priv->content_box_valid = FALSE;

  clutter_actor_get_content_box (self, &from_box);

  priv->content_gravity = gravity;

  clutter_actor_get_content_box (self, &to_box);

  _clutter_actor_create_transition (self, obj_props[PROP_CONTENT_BOX],
                                    &from_box,
                                    &to_box);

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CONTENT_GRAVITY]);
}

/**
 * clutter_actor_get_content_gravity:
 * @self: a #ClutterActor
 *
 * Retrieves the content gravity as set using
 * [method@Clutter.Actor.set_content_gravity].
 *
 * Return value: the content gravity
 */
ClutterContentGravity
clutter_actor_get_content_gravity (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self),
                        CLUTTER_CONTENT_GRAVITY_RESIZE_FILL);

  return self->priv->content_gravity;
}

/**
 * clutter_actor_get_content_box:
 * @self: a #ClutterActor
 * @box: (out caller-allocates): the return location for the bounding
 *   box for the #ClutterContent
 *
 * Retrieves the bounding box for the #ClutterContent of @self.
 *
 * The bounding box is relative to the actor's allocation.
 *
 * If no #ClutterContent is set for @self, or if @self has not been
 * allocated yet, then the result is undefined.
 *
 * The content box is guaranteed to be, at most, as big as the allocation
 * of the #ClutterActor.
 *
 * If the #ClutterContent used by the actor has a preferred size, then
 * it is possible to modify the content box by using the
 * [property@Clutter.Actor:content-gravity] property.
 */
void
clutter_actor_get_content_box (ClutterActor    *self,
                               ClutterActorBox *box)
{
  ClutterActorPrivate *priv;
  gfloat content_w, content_h;
  gfloat alloc_w, alloc_h;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (box != NULL);

  priv = self->priv;

  box->x1 = 0.f;
  box->y1 = 0.f;
  box->x2 = priv->allocation.x2 - priv->allocation.x1;
  box->y2 = priv->allocation.y2 - priv->allocation.y1;

  if (priv->content_box_valid)
    {
      *box = priv->content_box;
      return;
    }

  /* no need to do any more work */
  if (priv->content_gravity == CLUTTER_CONTENT_GRAVITY_RESIZE_FILL)
    return;

  if (priv->content == NULL)
    return;

  /* if the content does not have a preferred size then there is
   * no point in computing the content box
   */
  if (!clutter_content_get_preferred_size (priv->content,
                                           &content_w,
                                           &content_h))
    return;

  alloc_w = box->x2;
  alloc_h = box->y2;

  switch (priv->content_gravity)
    {
    case CLUTTER_CONTENT_GRAVITY_TOP_LEFT:
      box->x2 = box->x1 + MIN (content_w, alloc_w);
      box->y2 = box->y1 + MIN (content_h, alloc_h);
      break;

    case CLUTTER_CONTENT_GRAVITY_TOP:
      if (alloc_w > content_w)
        {
          box->x1 += ceilf ((alloc_w - content_w) / 2.0f);
          box->x2 = box->x1 + content_w;
        }
      box->y2 = box->y1 + MIN (content_h, alloc_h);
      break;

    case CLUTTER_CONTENT_GRAVITY_TOP_RIGHT:
      if (alloc_w > content_w)
        {
          box->x1 += (alloc_w - content_w);
          box->x2 = box->x1 + content_w;
        }
      box->y2 = box->y1 + MIN (content_h, alloc_h);
      break;

    case CLUTTER_CONTENT_GRAVITY_LEFT:
      box->x2 = box->x1 + MIN (content_w, alloc_w);
      if (alloc_h > content_h)
        {
          box->y1 += ceilf ((alloc_h - content_h) / 2.0f);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_CENTER:
      if (alloc_w > content_w)
        {
          box->x1 += ceilf ((alloc_w - content_w) / 2.0f);
          box->x2 = box->x1 + content_w;
        }
      if (alloc_h > content_h)
        {
          box->y1 += ceilf ((alloc_h - content_h) / 2.0f);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_RIGHT:
      if (alloc_w > content_w)
        {
          box->x1 += (alloc_w - content_w);
          box->x2 = box->x1 + content_w;
        }
      if (alloc_h > content_h)
        {
          box->y1 += ceilf ((alloc_h - content_h) / 2.0f);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_BOTTOM_LEFT:
      box->x2 = box->x1 + MIN (content_w, alloc_w);
      if (alloc_h > content_h)
        {
          box->y1 += (alloc_h - content_h);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_BOTTOM:
      if (alloc_w > content_w)
        {
          box->x1 += ceilf ((alloc_w - content_w) / 2.0f);
          box->x2 = box->x1 + content_w;
        }
      if (alloc_h > content_h)
        {
          box->y1 += (alloc_h - content_h);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_BOTTOM_RIGHT:
      if (alloc_w > content_w)
        {
          box->x1 += (alloc_w - content_w);
          box->x2 = box->x1 + content_w;
        }
      if (alloc_h > content_h)
        {
          box->y1 += (alloc_h - content_h);
          box->y2 = box->y1 + content_h;
        }
      break;

    case CLUTTER_CONTENT_GRAVITY_RESIZE_FILL:
      g_assert_not_reached ();
      break;

    case CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT:
      {
        double r_c = content_w / content_h;

        if ((alloc_w / r_c) > alloc_h)
          {
            box->y1 = 0.f;
            box->y2 = alloc_h;

            box->x1 = (float) ((alloc_w - (alloc_h * r_c)) / 2.0);
            box->x2 = (float) (box->x1 + (alloc_h * r_c));
          }
        else
          {
            box->x1 = 0.f;
            box->x2 = alloc_w;

            box->y1 = (float) ((alloc_h - (alloc_w / r_c)) / 2.0);
            box->y2 = (float) (box->y1 + (alloc_w / r_c));
          }

        CLUTTER_NOTE (LAYOUT,
                      "r_c: %.3f, r_a: %.3f\t"
                      "a: [%.2fx%.2f], c: [%.2fx%.2f]\t"
                      "b: [%.2f, %.2f, %.2f, %.2f]",
                      r_c, alloc_w / alloc_h,
                      alloc_w, alloc_h,
                      content_w, content_h,
                      box->x1, box->y1, box->x2, box->y2);
      }
      break;
    }
}

/**
 * clutter_actor_set_content_scaling_filters:
 * @self: a #ClutterActor
 * @min_filter: the minification filter for the content
 * @mag_filter: the magnification filter for the content
 *
 * Sets the minification and magnification filter to be applied when
 * scaling the [property@Clutter.Actor:content] of a #ClutterActor.
 *
 * The [property@Clutter.Actor:minification-filter] will be used when reducing
 * the size of the content; the [property@Clutter.Actor:magnification-filter]
 * will be used when increasing the size of the content.
 */
void
clutter_actor_set_content_scaling_filters (ClutterActor         *self,
                                           ClutterScalingFilter  min_filter,
                                           ClutterScalingFilter  mag_filter)
{
  ClutterActorPrivate *priv;
  gboolean changed;
  GObject *obj;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;
  obj = G_OBJECT (self);

  g_object_freeze_notify (obj);

  changed = FALSE;

  if (priv->min_filter != min_filter)
    {
      priv->min_filter = min_filter;
      changed = TRUE;

      g_object_notify_by_pspec (obj, obj_props[PROP_MINIFICATION_FILTER]);
    }

  if (priv->mag_filter != mag_filter)
    {
      priv->mag_filter = mag_filter;
      changed = TRUE;

      g_object_notify_by_pspec (obj, obj_props[PROP_MAGNIFICATION_FILTER]);
    }

  if (changed)
    clutter_actor_queue_redraw (self);

  g_object_thaw_notify (obj);
}

/**
 * clutter_actor_get_content_scaling_filters:
 * @self: a #ClutterActor
 * @min_filter: (out) (optional): return location for the minification
 *   filter, or %NULL
 * @mag_filter: (out) (optional): return location for the magnification
 *   filter, or %NULL
 *
 * Retrieves the values set using [method@Clutter.Actor.set_content_scaling_filters].
 */
void
clutter_actor_get_content_scaling_filters (ClutterActor         *self,
                                           ClutterScalingFilter *min_filter,
                                           ClutterScalingFilter *mag_filter)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (min_filter != NULL)
    *min_filter = self->priv->min_filter;

  if (mag_filter != NULL)
    *mag_filter = self->priv->mag_filter;
}

/*
 * clutter_actor_queue_compute_expand:
 * @self: a #ClutterActor
 *
 * Invalidates the needs_x_expand and needs_y_expand flags on @self
 * and its parents up to the top-level actor.
 *
 * This function also queues a relayout if anything changed.
 */
static inline void
clutter_actor_queue_compute_expand (ClutterActor *self)
{
  ClutterActor *parent;
  gboolean changed;

  if (self->priv->needs_compute_expand)
    return;

  changed = FALSE;
  parent = self;
  while (parent != NULL)
    {
      if (!parent->priv->needs_compute_expand)
        {
          parent->priv->needs_compute_expand = TRUE;
          changed = TRUE;
        }

      parent = parent->priv->parent;
    }

  if (changed)
    clutter_actor_queue_relayout (self);
}

/**
 * clutter_actor_set_x_expand:
 * @self: a #ClutterActor
 * @expand: whether the actor should expand horizontally
 *
 * Sets whether a #ClutterActor should expand horizontally; this means
 * that layout manager should allocate extra space for the actor, if
 * possible.
 *
 * Setting an actor to expand will also make all its parent expand, so
 * that it's possible to build an actor tree and only set this flag on
 * its leaves and not on every single actor.
 */
void
clutter_actor_set_x_expand (ClutterActor *self,
                            gboolean      expand)
{
  ClutterLayoutInfo *info;
  gboolean changed;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  expand = !!expand;

  info = _clutter_actor_get_layout_info (self);
  changed = info->x_expand != expand;

  if (changed || !self->priv->x_expand_set)
    {
      info->x_expand = expand;

      self->priv->x_expand_set = TRUE;

      clutter_actor_queue_compute_expand (self);

      if (changed)
        g_object_notify_by_pspec (G_OBJECT (self),
                                  obj_props[PROP_X_EXPAND]);
    }
}

/**
 * clutter_actor_get_x_expand:
 * @self: a #ClutterActor
 *
 * Retrieves the value set with [method@Clutter.Actor.set_x_expand].
 *
 * See also: [method@Clutter.Actor.needs_expand]
 *
 * Return value: %TRUE if the actor has been set to expand
 */
gboolean
clutter_actor_get_x_expand (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return _clutter_actor_get_layout_info_or_defaults (self)->x_expand;
}

/**
 * clutter_actor_set_y_expand:
 * @self: a #ClutterActor
 * @expand: whether the actor should expand vertically
 *
 * Sets whether a #ClutterActor should expand horizontally; this means
 * that layout manager should allocate extra space for the actor, if
 * possible.
 *
 * Setting an actor to expand will also make all its parent expand, so
 * that it's possible to build an actor tree and only set this flag on
 * its leaves and not on every single actor.
 */
void
clutter_actor_set_y_expand (ClutterActor *self,
                            gboolean      expand)
{
  ClutterLayoutInfo *info;
  gboolean changed;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  expand = !!expand;

  info = _clutter_actor_get_layout_info (self);
  changed = info->y_expand != expand;

  if (changed || !self->priv->y_expand_set)
    {
      info->y_expand = expand;

      self->priv->y_expand_set = TRUE;

      clutter_actor_queue_compute_expand (self);

      if (changed)
        g_object_notify_by_pspec (G_OBJECT (self),
                                  obj_props[PROP_Y_EXPAND]);
    }
}

/**
 * clutter_actor_get_y_expand:
 * @self: a #ClutterActor
 *
 * Retrieves the value set with [method@Clutter.Actor.set_y_expand].
 *
 * See also: [method@Clutter.Actor.needs_expand]
 *
 * Return value: %TRUE if the actor has been set to expand
 */
gboolean
clutter_actor_get_y_expand (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  return _clutter_actor_get_layout_info_or_defaults (self)->y_expand;
}

static void
clutter_actor_compute_expand_recursive (ClutterActor *self,
                                        gboolean     *x_expand_p,
                                        gboolean     *y_expand_p)
{
  ClutterActorIter iter;
  ClutterActor *child;
  gboolean x_expand, y_expand;

  x_expand = y_expand = FALSE;

  /* note that we don't recurse into children if we're already set to expand;
   * this avoids traversing the whole actor tree, even if it may lead to some
   * child left with the needs_compute_expand flag set.
   */
  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, &child))
    {
      x_expand = x_expand ||
        clutter_actor_needs_expand (child, CLUTTER_ORIENTATION_HORIZONTAL);

      y_expand = y_expand ||
        clutter_actor_needs_expand (child, CLUTTER_ORIENTATION_VERTICAL);
    }

  *x_expand_p = x_expand;
  *y_expand_p = y_expand;
}

static void
clutter_actor_compute_expand (ClutterActor *self)
{
  if (self->priv->needs_compute_expand)
    {
      const ClutterLayoutInfo *info;
      gboolean x_expand, y_expand;

      info = _clutter_actor_get_layout_info_or_defaults (self);

      if (self->priv->x_expand_set)
        x_expand = info->x_expand;
      else
        x_expand = FALSE;

      if (self->priv->y_expand_set)
        y_expand = info->y_expand;
      else
        y_expand = FALSE;

      /* we don't need to recurse down to the children if the
       * actor has been forcibly set to expand
       */
      if (!(self->priv->x_expand_set && self->priv->y_expand_set))
        {
          if (self->priv->n_children != 0)
            {
              gboolean *x_expand_p, *y_expand_p;
              gboolean ignored = FALSE;

              x_expand_p = self->priv->x_expand_set ? &ignored : &x_expand;
              y_expand_p = self->priv->y_expand_set ? &ignored : &y_expand;

              clutter_actor_compute_expand_recursive (self,
                                                      x_expand_p,
                                                      y_expand_p);
            }
        }

      self->priv->needs_compute_expand = FALSE;
      self->priv->needs_x_expand = (x_expand != FALSE);
      self->priv->needs_y_expand = (y_expand != FALSE);
    }
}

/**
 * clutter_actor_needs_expand:
 * @self: a #ClutterActor
 * @orientation: the direction of expansion
 *
 * Checks whether an actor, or any of its children, is set to expand
 * horizontally or vertically.
 *
 * This function should only be called by layout managers that can
 * assign extra space to their children.
 *
 * If you want to know whether the actor was explicitly set to expand,
 * use [method@Clutter.Actor.get_x_expand] or [method@Clutter.Actor.get_y_expand].
 *
 * Return value: %TRUE if the actor should expand
 */
gboolean
clutter_actor_needs_expand (ClutterActor       *self,
                            ClutterOrientation  orientation)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (!clutter_actor_is_visible (self))
    return FALSE;

  if (CLUTTER_ACTOR_IN_DESTRUCTION (self))
    return FALSE;

  clutter_actor_compute_expand (self);

  switch (orientation)
    {
    case CLUTTER_ORIENTATION_HORIZONTAL:
      return self->priv->needs_x_expand;

    case CLUTTER_ORIENTATION_VERTICAL:
      return self->priv->needs_y_expand;
    }

  return FALSE;
}

/**
 * clutter_actor_set_content_repeat:
 * @self: a #ClutterActor
 * @repeat: the repeat policy
 *
 * Sets the policy for repeating the [property@Clutter.Actor:content] of a
 * #ClutterActor. The behaviour is deferred to the #ClutterContent
 * implementation.
 */
void
clutter_actor_set_content_repeat (ClutterActor         *self,
                                  ClutterContentRepeat  repeat)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->priv->content_repeat == repeat)
    return;

  self->priv->content_repeat = repeat;

  clutter_actor_queue_redraw (self);
}

/**
 * clutter_actor_get_content_repeat:
 * @self: a #ClutterActor
 *
 * Retrieves the repeat policy for a #ClutterActor set by
 * [method@Clutter.Actor.set_content_repeat].
 *
 * Return value: the content repeat policy
 */
ClutterContentRepeat
clutter_actor_get_content_repeat (ClutterActor *self)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), CLUTTER_REPEAT_NONE);

  return self->priv->content_repeat;
}

static ClutterColorState *
get_default_color_state (ClutterActor *self)
{
  ClutterContext *context = clutter_actor_get_context (self);
  ClutterColorManager *color_manager =
    clutter_context_get_color_manager (context);
  ClutterColorState *color_state =
    clutter_color_manager_get_default_color_state (color_manager);

  return color_state;
}

static void
clutter_actor_set_color_state_internal (ClutterActor      *self,
                                        ClutterColorState *color_state)
{
  ClutterActorPrivate *priv = clutter_actor_get_instance_private (self);

  if (g_set_object (&priv->color_state, color_state))
    g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_COLOR_STATE]);
}

/**
 * clutter_actor_unset_color_state:
 * @self: a #ClutterActor
 *
 * Set @self's color state to the default.
 */
void
clutter_actor_unset_color_state (ClutterActor *self)
{
  ClutterColorState *default_color_state;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  default_color_state = get_default_color_state (self);
  clutter_actor_set_color_state_internal (self, default_color_state);
}

/**
 * clutter_actor_set_color_state:
 * @self: a #ClutterActor
 * @color_state: a #ClutterColorState
 *
 * Set @self's color state to @color_state.
 */
void
clutter_actor_set_color_state (ClutterActor      *self,
                               ClutterColorState *color_state)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_IS_COLOR_STATE (color_state));

  clutter_actor_set_color_state_internal (self, color_state);
}

/**
 * clutter_actor_get_color_state:
 * @self: a #ClutterActor
 *
 * Retrieves the color_state of a [class@Actor] set by
 * [method@Actor.set_color_state].
 *
 * Returns: (transfer none): the #ClutterColorState
 */
ClutterColorState *
clutter_actor_get_color_state (ClutterActor *self)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);

  priv = clutter_actor_get_instance_private (self);

  return priv->color_state;
}

static void
clutter_actor_set_child_transform_internal (ClutterActor            *self,
                                            const graphene_matrix_t *transform)
{
  ClutterTransformInfo *info = _clutter_actor_get_transform_info (self);
  ClutterActorIter iter;
  ClutterActor *child;
  GObject *obj;
  gboolean was_set = info->child_transform_set;

  graphene_matrix_init_from_matrix (&info->child_transform, transform);

  /* if it's the identity matrix, we need to toggle the boolean flag */
  info->child_transform_set = !graphene_matrix_is_identity (transform);

  /* we need to reset the transform_valid flag on each child */
  clutter_actor_iter_init (&iter, self);
  while (clutter_actor_iter_next (&iter, &child))
    transform_changed (child);

  clutter_actor_queue_redraw (self);

  obj = G_OBJECT (self);
  g_object_notify_by_pspec (obj, obj_props[PROP_CHILD_TRANSFORM]);

  if (was_set != info->child_transform_set)
    g_object_notify_by_pspec (obj, obj_props[PROP_CHILD_TRANSFORM_SET]);
}

/**
 * clutter_actor_set_child_transform:
 * @self: a #ClutterActor
 * @transform: (nullable): a #graphene_matrix_t, or %NULL
 *
 * Sets the transformation matrix to be applied to all the children
 * of @self prior to their own transformations. The default child
 * transformation is the identity matrix.
 *
 * If @transform is %NULL, the child transform will be unset.
 *
 * The [property@Clutter.Actor:child-transform] property is animatable.
 */
void
clutter_actor_set_child_transform (ClutterActor            *self,
                                   const graphene_matrix_t *transform)
{
  const ClutterTransformInfo *info;
  graphene_matrix_t new_transform;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (transform != NULL)
    graphene_matrix_init_from_matrix (&new_transform, transform);
  else
    graphene_matrix_init_identity (&new_transform);

  _clutter_actor_create_transition (self, obj_props[PROP_CHILD_TRANSFORM],
                                    &info->child_transform,
                                    &new_transform);
}

/**
 * clutter_actor_get_child_transform:
 * @self: a #ClutterActor
 * @transform: (out caller-allocates): a #graphene_matrix_t
 *
 * Retrieves the child transformation matrix set using
 * [method@Clutter.Actor.set_child_transform]; if none is currently set,
 * the @transform matrix will be initialized to the identity matrix.
 */
void
clutter_actor_get_child_transform (ClutterActor      *self,
                                   graphene_matrix_t *transform)
{
  const ClutterTransformInfo *info;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (transform != NULL);

  info = _clutter_actor_get_transform_info_or_defaults (self);

  if (info->child_transform_set)
    graphene_matrix_init_from_matrix (transform, &info->child_transform);
  else
    graphene_matrix_init_identity (transform);
}

static void
clutter_actor_push_in_cloned_branch (ClutterActor *self,
                                     gulong        count)
{
  ClutterActor *iter;

  for (iter = self->priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    clutter_actor_push_in_cloned_branch (iter, count);

  self->priv->in_cloned_branch += count;
}

static void
clutter_actor_pop_in_cloned_branch (ClutterActor *self,
                                    gulong        count)
{
  ClutterActor *iter;

  self->priv->in_cloned_branch -= count;

  for (iter = self->priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    clutter_actor_pop_in_cloned_branch (iter, count);
}

void
_clutter_actor_attach_clone (ClutterActor *actor,
                             ClutterActor *clone)
{
  ClutterActorPrivate *priv = actor->priv;

  g_assert (clone != NULL);

  if (priv->clones == NULL)
    priv->clones = g_hash_table_new (NULL, NULL);

  g_hash_table_add (priv->clones, clone);

  clutter_actor_push_in_cloned_branch (actor, 1);

  g_signal_emit (actor, actor_signals[CLONED], 0, clone);
}

void
_clutter_actor_detach_clone (ClutterActor *actor,
                             ClutterActor *clone)
{
  ClutterActorPrivate *priv = actor->priv;

  g_assert (clone != NULL);

  if (priv->clones == NULL ||
      g_hash_table_lookup (priv->clones, clone) == NULL)
    return;

  clutter_actor_pop_in_cloned_branch (actor, 1);

  g_hash_table_remove (priv->clones, clone);

  if (g_hash_table_size (priv->clones) == 0)
    g_clear_pointer (&priv->clones, g_hash_table_unref);

  g_signal_emit (actor, actor_signals[DECLONED], 0, clone);
}

/**
 * clutter_actor_has_mapped_clones:
 * @self: a #ClutterActor
 *
 * Returns whether a #ClutterActor or any parent actors have mapped clones
 * that are clone-painting @self.
 *
 * Returns: %TRUE if the actor has mapped clones, %FALSE otherwise
 */
gboolean
clutter_actor_has_mapped_clones (ClutterActor *self)
{
  ClutterActor *actor;
  GHashTableIter iter;
  gpointer key;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), FALSE);

  if (self->priv->in_cloned_branch == 0)
    return FALSE;

  for (actor = self; actor; actor = actor->priv->parent)
    {
      if (actor->priv->clones)
        {
          g_hash_table_iter_init (&iter, actor->priv->clones);
          while (g_hash_table_iter_next (&iter, &key, NULL))
            {
              if (clutter_actor_is_mapped (key))
                return TRUE;
            }
        }

      /* Clones will force-show their own source actor but not children of
       * it, so if we're hidden and an actor up the hierarchy has a clone,
       * we won't be visible.
       */
      if (!clutter_actor_is_visible (actor))
        return FALSE;
    }

  return FALSE;
}

static void
push_in_paint_unmapped_branch (ClutterActor *self,
                               guint         count)
{
  ClutterActor *iter;

  for (iter = self->priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    push_in_paint_unmapped_branch (iter, count);

  self->priv->unmapped_paint_branch_counter += count;
}

static void
pop_in_paint_unmapped_branch (ClutterActor *self,
                              guint         count)
{
  ClutterActor *iter;

  self->priv->unmapped_paint_branch_counter -= count;

  for (iter = self->priv->first_child;
       iter != NULL;
       iter = iter->priv->next_sibling)
    pop_in_paint_unmapped_branch (iter, count);
}

static void
clutter_actor_child_model__items_changed (GListModel *model,
                                          guint       position,
                                          guint       removed,
                                          guint       added,
                                          gpointer    user_data)
{
  ClutterActor *parent = user_data;
  ClutterActorPrivate *priv = parent->priv;
  guint i;

  while (removed--)
    {
      ClutterActor *child = clutter_actor_get_child_at_index (parent, position);
      clutter_actor_destroy (child);
    }

  for (i = 0; i < added; i++)
    {
      g_autoptr (GObject) item = g_list_model_get_item (model, position + i);
      g_autoptr (ClutterActor) child = priv->create_child_func (
        item, priv->create_child_data);

      /* The actor returned by the function can have a floating reference,
       * if the implementation is in pure C, or have a full reference, usually
       * the case for language bindings. To avoid leaking references, we
       * try to assume ownership of the instance, and release the reference
       * at the end unconditionally, leaving the only reference to the actor
       * itself.
       */
      if (g_object_is_floating (child))
        g_object_ref_sink (child);

      clutter_actor_insert_child_at_index (parent, child, position + i);
    }
}

/**
 * clutter_actor_bind_model:
 * @self: a #ClutterActor
 * @model: (nullable): a #GListModel
 * @create_child_func: a function that creates #ClutterActor instances
 *   from the contents of the @model
 * @user_data: user data passed to @create_child_func
 * @notify: function called when unsetting the @model
 *
 * Binds a #GListModel to a #ClutterActor.
 *
 * If the #ClutterActor was already bound to a #GListModel, the previous
 * binding is destroyed.
 *
 * The existing children of #ClutterActor are destroyed when setting a
 * model, and new children are created and added, representing the contents
 * of the @model. The #ClutterActor is updated whenever the @model changes.
 * If @model is %NULL, the #ClutterActor is left empty.
 *
 * When a #ClutterActor is bound to a model, adding and removing children
 * directly is undefined behaviour.4
 */
void
clutter_actor_bind_model (ClutterActor                *self,
                          GListModel                  *model,
                          ClutterActorCreateChildFunc  create_child_func,
                          gpointer                     user_data,
                          GDestroyNotify               notify)
{
  ClutterActorPrivate *priv = clutter_actor_get_instance_private (self);

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));
  g_return_if_fail (model == NULL || create_child_func != NULL);

  if (priv->child_model != NULL)
    {
      if (priv->create_child_notify != NULL)
        priv->create_child_notify (priv->create_child_data);

      g_signal_handlers_disconnect_by_func (priv->child_model,
                                            clutter_actor_child_model__items_changed,
                                            self);
      g_clear_object (&priv->child_model);
      priv->create_child_func = NULL;
      priv->create_child_data = NULL;
      priv->create_child_notify = NULL;
    }

  clutter_actor_destroy_all_children (self);

  if (model == NULL)
    return;

  priv->child_model = g_object_ref (model);
  priv->create_child_func = create_child_func;
  priv->create_child_data = user_data;
  priv->create_child_notify = notify;

  g_signal_connect (priv->child_model, "items-changed",
                    G_CALLBACK (clutter_actor_child_model__items_changed),
                    self);

  clutter_actor_child_model__items_changed (priv->child_model,
                                            0,
                                            0,
                                            g_list_model_get_n_items (priv->child_model),
                                            self);
}

typedef struct {
  GType child_type;
  GArray *props;
} BindClosure;

typedef struct {
  const char *model_property;
  const char *child_property;
  GBindingFlags flags;
} BindProperty;

static void
bind_closure_free (gpointer data_)
{
  BindClosure *data = data_;

  if (data == NULL)
    return;

  g_array_unref (data->props);
  g_free (data);
}

static ClutterActor *
bind_child_with_properties (gpointer item,
                            gpointer data_)
{
  BindClosure *data = data_;
  ClutterActor *res;
  guint i;

  res = g_object_new (data->child_type, NULL);

  for (i = 0; i < data->props->len; i++)
    {
      const BindProperty *prop = &g_array_index (data->props, BindProperty, i);

      g_object_bind_property (item, prop->model_property,
                              res, prop->child_property,
                              prop->flags);
    }

  return res;
}

/**
 * clutter_actor_bind_model_with_properties:
 * @self: a #ClutterActor
 * @model: a #GListModel
 * @child_type: the type of #ClutterActor to use when creating
 *   children mapping to items inside the @model
 * @first_model_property: the first property of @model to bind
 * @...: tuples of property names on the @model, on the child, and the
 *   #GBindingFlags used to bind them, terminated by %NULL
 *
 * Binds a #GListModel to a #ClutterActor.
 *
 * Unlike clutter_actor_bind_model(), this function automatically creates
 * a child #ClutterActor of type @child_type, and binds properties on the
 * items inside the @model to the corresponding properties on the child,
 * for instance:
 *
 * ```c
 *   clutter_actor_bind_model_with_properties (actor, model,
 *                                             MY_TYPE_CHILD_VIEW,
 *                                             "label", "text", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
 *                                             "icon", "image", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
 *                                             "selected", "selected", G_BINDING_BIDIRECTIONAL,
 *                                             "active", "active", G_BINDING_BIDIRECTIONAL,
 *                                             NULL);
 * ```
 *
 * is the equivalent of calling clutter_actor_bind_model() with a
 * #ClutterActorCreateChildFunc of:
 *
 * ```c
 *   ClutterActor *res = g_object_new (MY_TYPE_CHILD_VIEW, NULL);
 *
 *   g_object_bind_property (item, "label", res, "text", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
 *   g_object_bind_property (item, "icon", res, "image", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
 *   g_object_bind_property (item, "selected", res, "selected", G_BINDING_BIDIRECTIONAL);
 *   g_object_bind_property (item, "active", res, "active", G_BINDING_BIDIRECTIONAL);
 *
 *   return res;
 * ```
 *
 * If the #ClutterActor was already bound to a #GListModel, the previous
 * binding is destroyed.
 *
 * When a #ClutterActor is bound to a model, adding and removing children
 * directly is undefined behaviour.
 *
 * See also: clutter_actor_bind_model()4
 */
void
clutter_actor_bind_model_with_properties (ClutterActor *self,
                                          GListModel   *model,
                                          GType         child_type,
                                          const char   *first_model_property,
                                          ...)
{
  va_list args;
  BindClosure *clos;
  const char *model_property;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (G_IS_LIST_MODEL (model));
  g_return_if_fail (g_type_is_a (child_type, CLUTTER_TYPE_ACTOR));

  clos = g_new0 (BindClosure, 1);
  clos->child_type = child_type;
  clos->props = g_array_new (FALSE, FALSE, sizeof (BindProperty));

  va_start (args, first_model_property);
  model_property = first_model_property;
  while (model_property != NULL)
    {
      const char *child_property = va_arg (args, char *);
      GBindingFlags binding_flags = va_arg (args, guint);
      BindProperty bind;

      bind.model_property = g_intern_string (model_property);
      bind.child_property = g_intern_string (child_property);
      bind.flags = binding_flags;

      g_array_append_val (clos->props, bind);

      model_property = va_arg (args, char *);
    }
  va_end (args);

  clutter_actor_bind_model (self, model, bind_child_with_properties, clos, bind_closure_free);
}

/**
 * clutter_actor_create_texture_paint_node:
 * @self: a #ClutterActor
 * @texture: a #CoglTexture
 *
 * Creates a #ClutterPaintNode initialized using the state of the
 * given #ClutterActor, ready to be used inside the implementation
 * of the #ClutterActorClass.paint_node virtual function.
 *
 * The returned paint node has the geometry set to the size of the
 * [property@Clutter.Actor:content-box] property; it uses the filters specified
 * in the [property@Clutter.Actor:minification-filter]
 * and [property@Clutter.Actor:magnification-filter]
 * properties; and respects the [property@Clutter.Actor:content-repeat] property.
 *
 * Returns: (transfer full): The newly created #ClutterPaintNode4
 */
ClutterPaintNode *
clutter_actor_create_texture_paint_node (ClutterActor *self,
                                         CoglTexture  *texture)
{
  ClutterActorPrivate *priv = clutter_actor_get_instance_private (self);
  ClutterPaintNode *node;
  ClutterActorBox box;
  CoglColor color;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), NULL);
  g_return_val_if_fail (texture != NULL, NULL);

  clutter_actor_get_content_box (self, &box);

  /* ClutterTextureNode will premultiply the blend color, so we
   * want it to be white with the paint opacity
   */
  color.red = 255;
  color.green = 255;
  color.blue = 255;
  color.alpha = clutter_actor_get_paint_opacity_internal (self);

  node = clutter_texture_node_new (texture, &color, priv->min_filter, priv->mag_filter);
  clutter_paint_node_set_static_name (node, "Texture");

  if (priv->content_repeat == CLUTTER_REPEAT_NONE)
    clutter_paint_node_add_rectangle (node, &box);
  else
    {
      float t_w = 1.f, t_h = 1.f;

      if ((priv->content_repeat & CLUTTER_REPEAT_X_AXIS) != FALSE)
        t_w = (box.x2 - box.x1) / cogl_texture_get_width (texture);

      if ((priv->content_repeat & CLUTTER_REPEAT_Y_AXIS) != FALSE)
        t_h = (box.y2 - box.y1) / cogl_texture_get_height (texture);

      clutter_paint_node_add_texture_rectangle (node, &box,
                                                0.f, 0.f,
                                                t_w, t_h);
    }

  return node;
}

/**
 * clutter_actor_set_accessible:
 * @self: A #ClutterActor
 * @accessible: an accessible
 *
 * This method allows to set a customly created accessible object to
 * this widget
 *
 * NULL is a valid value for @accessible. That contemplates the
 * hypothetical case of not needing anymore a custom accessible object
 * for the widget. Next call of [method@Clutter.Actor.get_accessible] would
 * create and return a default accessible.
 *
 * It assumes that the call to atk_object_initialize that bound the
 * gobject with the custom accessible object was already called, so
 * not a responsibility of this method.
 */
void
clutter_actor_set_accessible (ClutterActor *self,
                              AtkObject    *accessible)
{
  ClutterActorPrivate *priv;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (accessible == NULL || ATK_IS_GOBJECT_ACCESSIBLE (accessible));

  priv = self->priv;
  if (priv->accessible != accessible)
    {
      if (priv->accessible)
        {
          g_object_remove_weak_pointer (G_OBJECT (self),
                                        (gpointer *)&priv->accessible);
          g_clear_object (&priv->accessible);
        }

      if (accessible)
        {
          priv->accessible = g_object_ref (accessible);
          /* See note in clutter_actor_get_accessible() */
          g_object_add_weak_pointer (G_OBJECT (self),
                                     (gpointer *)&priv->accessible);
        }
      else
        priv->accessible = NULL;
    }
}

void
clutter_actor_queue_immediate_relayout (ClutterActor *self)
{
  ClutterStage *stage;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  clutter_actor_queue_relayout (self);

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (self));
  if (stage)
    clutter_stage_set_actor_needs_immediate_relayout (stage);
}

/**
 * clutter_actor_invalidate_transform:
 * @self: A #ClutterActor
 *
 * Invalidate the cached transformation matrix of @self.
 * This is needed for implementations overriding the apply_transform()
 * vfunc and has to be called if the matrix returned by apply_transform()
 * would change.
 */
void
clutter_actor_invalidate_transform (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  transform_changed (self);
}

/**
 * clutter_actor_invalidate_paint_volume:
 * @self: A #ClutterActor
 *
 * Invalidates the cached paint volume of @self. This is needed for
 * implementations overriding the [vfunc@Clutter.Actor.get_paint_volume]
 * virtual function and has to be called every time the paint volume
 * returned by that function would change.
 */
void
clutter_actor_invalidate_paint_volume (ClutterActor *self)
{
  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  queue_update_paint_volume (self);
}

void
clutter_actor_attach_grab (ClutterActor *self,
                           ClutterGrab  *grab)
{
  ClutterActorPrivate *priv = self->priv;

  priv->grabs = g_list_prepend (priv->grabs, grab);
}

void
clutter_actor_detach_grab (ClutterActor *self,
                           ClutterGrab  *grab)
{
  ClutterActorPrivate *priv = self->priv;

  priv->grabs = g_list_remove (priv->grabs, grab);
}

void
clutter_actor_collect_event_actors (ClutterActor *self,
                                    ClutterActor *deepmost,
                                    GPtrArray    *actors)
{
  ClutterActor *iter;
  gboolean in_root = FALSE;

  g_assert (actors->len == 0);

  iter = deepmost;
  while (iter != NULL)
    {
      ClutterActor *parent = iter->priv->parent;

      if (clutter_actor_get_reactive (iter) || /* an actor must be reactive */
          parent == NULL)                     /* unless it's the stage */
        g_ptr_array_add (actors, iter);

      if (iter == self)
        {
          in_root = TRUE;
          break;
        }

      iter = parent;
    }

  /* The grab root conceptually extends infinitely in all
   * directions, so it handles the events that fall outside of
   * the actor.
   */
  if (!in_root)
    {
      g_ptr_array_remove_range (actors, 0, actors->len);
      g_ptr_array_add (actors, self);
    }
}

const GList *
clutter_actor_peek_actions (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;

  if (priv->actions == NULL)
    return NULL;

  return _clutter_meta_group_peek_metas (priv->actions);
}

void clutter_actor_set_implicitly_grabbed (ClutterActor *self,
                                           gboolean      is_implicitly_grabbed)
{
  ClutterActorPrivate *priv = self->priv;

  if (is_implicitly_grabbed)
    priv->implicitly_grabbed_count++;
  else
    priv->implicitly_grabbed_count--;

  g_assert (priv->implicitly_grabbed_count >= 0);
}

/**
 * clutter_actor_notify_transform_invalid:
 * @self: A #ClutterActor
 *
 * Invalidate the cached transformation matrix of @self and queue a redraw
 * if the transformation matrix has changed.
 * This is needed for implementations overriding the apply_transform()
 * vfunc and has to be called if the matrix returned by apply_transform()
 * would change due to state outside of the object itself.
 */
void
clutter_actor_notify_transform_invalid (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  graphene_matrix_t old_transform;

  if (!priv->transform_valid)
    {
      clutter_actor_queue_redraw (self);
      return;
    }

  graphene_matrix_init_from_matrix (&old_transform, &priv->transform);

  transform_changed (self);
  ensure_valid_actor_transform (self);


  g_assert (priv->transform_valid);

  if (!graphene_matrix_equal (&old_transform, &priv->transform))
    clutter_actor_queue_redraw (self);
}

/**
 * clutter_actor_class_set_layout_manager_type
 * @actor_class: A #ClutterActor class
 * @type: A #GType
 *
 * Sets the type to be used for creating layout managers for
 * actors of @actor_class.
 *
 * The given @type must be a subtype of [class@Clutter.LayoutManager].
 *
 * This function should only be called from class init functions of actors.
 */
void
clutter_actor_class_set_layout_manager_type (ClutterActorClass *actor_class,
                                             GType              type)
{
  g_return_if_fail (CLUTTER_IS_ACTOR_CLASS (actor_class));
  g_return_if_fail (g_type_is_a (type, CLUTTER_TYPE_LAYOUT_MANAGER));

  actor_class->layout_manager_type = type;
}

/**
 * clutter_actor_class_get_layout_manager_type
 * @actor_class: A #ClutterActor class
 *
 * Retrieves the type of the [class@Clutter.LayoutManager]
 * used by actors of class @actor_class.
 *
 * See also: [method@Clutter.ActorClass.set_layout_manager_type].
 *
 * Returns: type of a `ClutterLayoutManager` subclass, or %G_TYPE_INVALID
 */
GType
clutter_actor_class_get_layout_manager_type (ClutterActorClass *actor_class)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR_CLASS (actor_class), G_TYPE_INVALID);

  return actor_class->layout_manager_type;
}

/**
 * clutter_actor_set_accessible_name:
 * @self: widget to set the accessible name for
 * @name: (nullable): a character string to be set as the accessible name
 *
 * This method sets @name as the accessible name for @self.
 *
 * Usually you will have no need to set the accessible name for an
 * object, as usually there is a label for most of the interface
 * elements.
 */
void
clutter_actor_set_accessible_name (ClutterActor *self,
                                   const gchar  *name)
{
  ClutterActorPrivate *priv;
  AtkObject *accessible;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  priv = self->priv;
  if (g_strcmp0 (name, priv->accessible_name) == 0)
    return;

  accessible = clutter_actor_get_accessible (self);
  g_set_str (&priv->accessible_name, name);

  if (accessible)
    g_object_notify (G_OBJECT (accessible), "accessible-name");

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ACCESSIBLE_NAME]);
}

/**
 * clutter_actor_get_accessible_name:
 * @self: widget to get the accessible name for
 *
 * Gets the accessible name for this widget. See
 * clutter_actor_set_accessible_name() for more information.
 *
 * Returns: a character string representing the accessible name
 * of the widget.
 */
const gchar *
clutter_actor_get_accessible_name (ClutterActor *actor)
{
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  return actor->priv->accessible_name;
}

/**
 * clutter_actor_set_accessible_role:
 * @self: widget to set the accessible role for
 * @role: The role to use
 *
 * This method sets @role as the accessible role for @self. This
 * role describes what kind of user interface element @self is and
 * is provided so that assistive technologies know how to present
 * @self to the user.
 *
 * Usually you will have no need to set the accessible role for an
 * object, as this information is extracted from the context of the
 * object (ie: a #StButton has by default a push button role). This
 * method is only required when you need to redefine the role
 * currently associated with the widget, for instance if it is being
 * used in an unusual way (ie: a #StButton used as a togglebutton), or
 * if a generic object is used directly (ie: a container as a menu
 * item).
 *
 * If @role is #ATK_ROLE_INVALID, the role will not be changed
 * and the accessible's default role will be used instead.
 */
void
clutter_actor_set_accessible_role (ClutterActor *self,
                                   AtkRole       role)
{
  AtkObject *accessible;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));

  if (self->accessible_role == role)
    return;

  accessible = clutter_actor_get_accessible (self);
  self->accessible_role = role;

  if (accessible)
    g_object_notify (G_OBJECT (accessible), "accessible-role");

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ACCESSIBLE_ROLE]);
}


/**
 * clutter_actor_get_accessible_role:
 * @self: widget to get the accessible role for
 *
 * Gets the #AtkRole for this widget. See
 * clutter_actor_set_accessible_role() for more information.
 *
 * Returns: accessible #AtkRole for this widget
 */
AtkRole
clutter_actor_get_accessible_role (ClutterActor *self)
{
  AtkRole role = ATK_ROLE_INVALID;
  AtkObject *accessible;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (self), role);

  accessible = clutter_actor_get_accessible (self);

  if (self->accessible_role != ATK_ROLE_INVALID)
    role = self->accessible_role;
  else if (accessible != NULL)
    role = atk_object_get_role (accessible);

  return role;
}

AtkStateSet *
clutter_actor_get_accessible_state (ClutterActor *actor)
{
  ClutterActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  priv = clutter_actor_get_instance_private (actor);

  return priv->accessible_state;
}

/**
 * clutter_actor_add_accessible_state:
 * @actor: A #ClutterActor
 * @state: #AtkStateType state to add
 *
 * This method adds @state as one of the accessible states for
 * @actor. The list of states of an actor describes the current state
 * of user interface element @actor and is provided so that assistive
 * technologies know how to present @actor to the user.
 *
 * Usually you will have no need to add accessible states for an
 * object, as the accessible object can extract most of the states
 * from the object itself.
 * This method is only required when one cannot extract the
 * information automatically from the object itself (i.e.: a generic
 * container used as a toggle menu item will not automatically include
 * the toggled state).
 */
void
clutter_actor_add_accessible_state (ClutterActor *actor,
                                    AtkStateType  state)
{
  ClutterActorPrivate *priv;
  AtkObject *accessible;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  priv = clutter_actor_get_instance_private (actor);
  accessible = clutter_actor_get_accessible (actor);

  if (G_UNLIKELY (priv->accessible_state == NULL))
    {
      priv->accessible_state = atk_state_set_new ();
      /* Actors are all focusable until we merge focus management from St */
      atk_state_set_add_state (priv->accessible_state, ATK_STATE_FOCUSABLE);
    }

  if (atk_state_set_add_state (priv->accessible_state, state) && accessible)
    atk_object_notify_state_change (accessible, state, TRUE);
}

/**
 * clutter_actor_remove_accessible_state:
 * @actor: A #ClutterActor
 * @state: #AtkState state to remove
 *
 * This method removes @state as on of the accessible states for
 * @actor. See [method@Clutter.Actor.add_accessible_state] for more information.
 *
 */
void
clutter_actor_remove_accessible_state (ClutterActor *actor,
                                       AtkStateType  state)
{
  ClutterActorPrivate *priv;
  AtkObject *accessible;

  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  priv = clutter_actor_get_instance_private (actor);
  accessible = clutter_actor_get_accessible (actor);

  if (G_UNLIKELY (priv->accessible_state == NULL))
    return;

  if (atk_state_set_remove_state (priv->accessible_state, state) && accessible)
    atk_object_notify_state_change (accessible, state, FALSE);
}
