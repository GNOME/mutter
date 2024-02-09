/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * MetaCompositor:
 *
 * Compositor API
 *
 * At a high-level, a window is not-visible or visible. When a
 * window is added (with meta_compositor_add_window()) it is not visible.
 * meta_compositor_show_window() indicates a transition from not-visible to
 * visible. Some of the reasons for this:
 *
 * - Window newly created
 * - Window is unminimized
 * - Window is moved to the current desktop
 * - Window was made sticky
 *
 * meta_compositor_hide_window() indicates that the window has transitioned from
 * visible to not-visible. Some reasons include:
 *
 * - Window was destroyed
 * - Window is minimized
 * - Window is moved to a different desktop
 * - Window no longer sticky.
 *
 * Note that combinations are possible - a window might have first
 * been minimized and then moved to a different desktop. The 'effect' parameter
 * to meta_compositor_show_window() and meta_compositor_hide_window() is a hint
 * as to the appropriate effect to show the user and should not
 * be considered to be indicative of a state change.
 *
 * When the active workspace is changed, meta_compositor_switch_workspace() is
 * called first, then meta_compositor_show_window() and
 * meta_compositor_hide_window() are called individually for each window
 * affected, with an effect of META_COMP_EFFECT_NONE.
 * If hiding windows will affect the switch workspace animation, the
 * compositor needs to delay hiding the windows until the switch
 * workspace animation completes.
 *
 * # Containers #
 *
 * There's two containers in the stage that are used to place window actors, here
 * are listed in the order in which they are painted:
 *
 * - window group, accessible with meta_get_window_group_for_display()
 * - top window group, accessible with meta_get_top_window_group_for_display()
 *
 * Mutter will place actors representing windows in the window group, except for
 * override-redirect windows (ie. popups and menus) which will be placed in the
 * top window group.
 */

#include "config.h"

#include "compositor/compositor-private.h"

#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-later-private.h"
#include "compositor/meta-window-actor-private.h"
#include "compositor/meta-window-group-private.h"
#include "core/frame.h"
#include "core/util-private.h"
#include "core/window-private.h"
#include "meta/compositor-mutter.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "meta/meta-background-actor.h"
#include "meta/meta-background-group.h"
#include "meta/meta-context.h"
#include "meta/meta-shadow-factory.h"
#include "meta/prefs.h"
#include "meta/window.h"

#ifdef HAVE_WAYLAND
#include "compositor/meta-window-actor-wayland.h"
#include "wayland/meta-wayland-private.h"
#endif

#ifdef HAVE_X11_CLIENT
#include <X11/extensions/Xcomposite.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-stage-x11.h"

#include "compositor/meta-window-actor-x11.h"

#include "x11/meta-x11-display-private.h"
#endif

enum
{
  PROP_0,

  PROP_DISPLAY,
  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { NULL, };

typedef struct _MetaCompositorPrivate
{
  GObject parent;

  MetaDisplay *display;
  MetaBackend *backend;

  gulong stage_presented_id;
  gulong before_paint_handler_id;
  gulong after_paint_handler_id;
  gulong window_visibility_updated_id;
  gulong monitors_changed_internal_id;
  gulong grabbed_notify_handler_id;

  int64_t server_time_query_time;
  int64_t server_time_offset;

  gboolean server_time_is_monotonic_time;

  ClutterActor *window_group;
  ClutterActor *top_window_group;
  ClutterActor *feedback_group;

  GList *windows;

  CoglContext *context;

  gboolean needs_update_top_window_actors;

  MetaWindowActor *top_window_actor;
  gulong top_window_actor_destroy_id;

  int disable_unredirect_count;

  int switch_workspace_in_progress;

  gboolean frame_in_progress;

  MetaPluginManager *plugin_mgr;

  MetaWindowDrag *current_drag;

  MetaLaters *laters;
} MetaCompositorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaCompositor, meta_compositor,
                                     G_TYPE_OBJECT)

static GQuark quark_compositor_view;

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *stage_view,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor);

static void
on_top_window_actor_destroyed (MetaWindowActor *window_actor,
                               MetaCompositor  *compositor);

static void sync_actor_stacking (MetaCompositor *compositor);

static void
meta_finish_workspace_switch (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;

  /* Finish hiding and showing actors for the new workspace */
  for (l = priv->windows; l; l = l->next)
    meta_window_actor_sync_visibility (l->data);

  /* Fix up stacking order. */
  sync_actor_stacking (compositor);
}

void
meta_switch_workspace_completed (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  /* FIXME -- must redo stacking order */
  priv->switch_workspace_in_progress--;
  if (priv->switch_workspace_in_progress < 0)
    {
      g_warning ("Error in workspace_switch accounting!");
      priv->switch_workspace_in_progress = 0;
    }

  if (!priv->switch_workspace_in_progress)
    meta_finish_workspace_switch (compositor);
}

void
meta_compositor_destroy (MetaCompositor *compositor)
{
  g_object_run_dispose (G_OBJECT (compositor));
  g_object_unref (compositor);
}

/* compat helper */
static MetaCompositor *
get_compositor_for_display (MetaDisplay *display)
{
  return display->compositor;
}

/**
 * meta_get_stage_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The #ClutterStage for the display
 */
ClutterActor *
meta_get_stage_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return meta_backend_get_stage (priv->backend);
}

/**
 * meta_get_window_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The window group corresponding to @display
 */
ClutterActor *
meta_get_window_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->window_group;
}

/**
 * meta_get_top_window_group_for_display:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none): The top window group corresponding to @display
 */
ClutterActor *
meta_get_top_window_group_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->top_window_group;
}

/**
 * meta_compositor_get_feedback_group:
 * @compositor: a #MetaCompositor
 *
 * Returns: (transfer none): The feedback group corresponding to @display
 */
ClutterActor *
meta_compositor_get_feedback_group (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->feedback_group;
}

/**
 * meta_get_window_actors:
 * @display: a #MetaDisplay
 *
 * Returns: (transfer none) (element-type Clutter.Actor): The set of #MetaWindowActor on @display
 */
GList *
meta_get_window_actors (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (display, NULL);

  compositor = get_compositor_for_display (display);
  g_return_val_if_fail (compositor, NULL);
  priv = meta_compositor_get_instance_private (compositor);

  return priv->windows;
}

void
meta_compositor_grab_begin (MetaCompositor *compositor)
{
  META_COMPOSITOR_GET_CLASS (compositor)->grab_begin (compositor);
}

void
meta_compositor_grab_end (MetaCompositor *compositor)
{
  META_COMPOSITOR_GET_CLASS (compositor)->grab_end (compositor);
}

static MetaCompositorView *
meta_compositor_create_view (MetaCompositor   *compositor,
                             ClutterStageView *stage_view)
{
  return META_COMPOSITOR_GET_CLASS (compositor)->create_view (compositor,
                                                              stage_view);
}

gboolean
meta_compositor_manage (MetaCompositor  *compositor,
                        GError         **error)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaDisplay *display = priv->display;
  MetaBackend *backend = priv->backend;
  ClutterActor *stage = meta_backend_get_stage (backend);

  priv->stage_presented_id =
    g_signal_connect (stage, "presented",
                      G_CALLBACK (on_presented),
                      compositor);

  priv->window_group = meta_window_group_new (display);
  priv->top_window_group = meta_window_group_new (display);
  priv->feedback_group = meta_window_group_new (display);

  clutter_actor_add_child (stage, priv->window_group);
  clutter_actor_add_child (stage, priv->top_window_group);
  clutter_actor_add_child (stage, priv->feedback_group);

  if (!META_COMPOSITOR_GET_CLASS (compositor)->manage (compositor, error))
    return FALSE;

  priv->plugin_mgr = meta_plugin_manager_new (compositor);
  meta_plugin_manager_start (priv->plugin_mgr);

  return TRUE;
}

static void
meta_compositor_real_unmanage (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  g_clear_signal_handler (&priv->top_window_actor_destroy_id,
                          priv->top_window_actor);

  g_clear_pointer (&priv->window_group, clutter_actor_destroy);
  g_clear_pointer (&priv->top_window_group, clutter_actor_destroy);
  g_clear_pointer (&priv->feedback_group, clutter_actor_destroy);
}

void
meta_compositor_unmanage (MetaCompositor *compositor)
{
  META_COMPOSITOR_GET_CLASS (compositor)->unmanage (compositor);
}

void
meta_compositor_add_window (MetaCompositor    *compositor,
                            MetaWindow        *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor;
  ClutterActor *window_group;
  GType window_actor_type = G_TYPE_INVALID;

  switch (window->client_type)
    {
#ifdef HAVE_X11_CLIENT
    case META_WINDOW_CLIENT_TYPE_X11:
      window_actor_type = META_TYPE_WINDOW_ACTOR_X11;
      break;
#endif

#ifdef HAVE_WAYLAND
    case META_WINDOW_CLIENT_TYPE_WAYLAND:
      window_actor_type = META_TYPE_WINDOW_ACTOR_WAYLAND;
      break;
#endif

    default:
      g_return_if_reached ();
    }

  window_actor = g_object_new (window_actor_type,
                               "meta-window", window,
                               "show-on-set-parent", FALSE,
                               NULL);

  if (window->layer == META_LAYER_OVERRIDE_REDIRECT)
    window_group = priv->top_window_group;
  else
    window_group = priv->window_group;

  clutter_actor_add_child (window_group, CLUTTER_ACTOR (window_actor));

  /* Initial position in the stack is arbitrary; stacking will be synced
   * before we first paint.
   */
  priv->windows = g_list_append (priv->windows, window_actor);
  sync_actor_stacking (compositor);
}

static void
meta_compositor_real_remove_window (MetaCompositor *compositor,
                                    MetaWindow     *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_queue_destroy (window_actor);
}

void
meta_compositor_remove_window (MetaCompositor *compositor,
                               MetaWindow     *window)
{
  META_COMPOSITOR_GET_CLASS (compositor)->remove_window (compositor, window);
}

void
meta_compositor_remove_window_actor (MetaCompositor  *compositor,
                                     MetaWindowActor *window_actor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  priv->windows = g_list_remove (priv->windows, window_actor);
}

void
meta_compositor_sync_updates_frozen (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_sync_updates_frozen (window_actor);
}

void
meta_compositor_queue_frame_drawn (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   gboolean        no_delay_frame)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_queue_frame_drawn (window_actor, no_delay_frame);
}

void
meta_compositor_window_shape_changed (MetaCompositor *compositor,
                                      MetaWindow     *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  if (!window_actor)
    return;

#ifdef HAVE_X11_CLIENT
  meta_window_actor_x11_update_shape (META_WINDOW_ACTOR_X11 (window_actor));
#endif
}

void
meta_compositor_window_opacity_changed (MetaCompositor *compositor,
                                        MetaWindow     *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  if (!window_actor)
    return;

  meta_window_actor_update_opacity (window_actor);
}

static void
invalidate_top_window_actor_for_views (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  g_assert (!priv->frame_in_progress);

  priv->needs_update_top_window_actors = TRUE;
}

void
meta_compositor_window_actor_stage_views_changed (MetaCompositor *compositor)
{
  invalidate_top_window_actor_for_views (compositor);
}

gboolean
meta_compositor_filter_keybinding (MetaCompositor *compositor,
                                   MetaKeyBinding *binding)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_filter_keybinding (priv->plugin_mgr, binding);
}

void
meta_compositor_show_window (MetaCompositor *compositor,
                             MetaWindow     *window,
                             MetaCompEffect  effect)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_show (window_actor, effect);
}

void
meta_compositor_hide_window (MetaCompositor *compositor,
                             MetaWindow     *window,
                             MetaCompEffect  effect)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_hide (window_actor, effect);
  meta_stack_tracker_queue_sync_stack (priv->display->stack_tracker);
}

void
meta_compositor_size_change_window (MetaCompositor *compositor,
                                    MetaWindow     *window,
                                    MetaSizeChange  which_change,
                                    MtkRectangle   *old_frame_rect,
                                    MtkRectangle   *old_buffer_rect)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);

  meta_window_actor_size_change (window_actor, which_change, old_frame_rect, old_buffer_rect);
}

void
meta_compositor_switch_workspace (MetaCompositor     *compositor,
                                  MetaWorkspace      *from,
                                  MetaWorkspace      *to,
                                  MetaMotionDirection direction)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  gint to_indx, from_indx;

  to_indx   = meta_workspace_index (to);
  from_indx = meta_workspace_index (from);

  priv->switch_workspace_in_progress++;

  if (!meta_plugin_manager_switch_workspace (priv->plugin_mgr,
                                             from_indx,
                                             to_indx,
                                             direction))
    {
      priv->switch_workspace_in_progress--;

      /* We have to explicitly call this to fix up stacking order of the
       * actors; this is because the abs stacking position of actors does not
       * necessarily change during the window hiding/unhiding, only their
       * relative position toward the desktop window.
       */
      meta_finish_workspace_switch (compositor);
    }
}

static void
sync_actor_stacking (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *children;
  GList *expected_window_node;
  GList *tmp;
  GList *old;
  GList *backgrounds;
  gboolean has_windows;
  gboolean reordered;

  /* NB: The first entries in the lists are stacked the lowest */

  /* Restacking will trigger full screen redraws, so it's worth a
   * little effort to make sure we actually need to restack before
   * we go ahead and do it */

  children = clutter_actor_get_children (priv->window_group);
  has_windows = FALSE;
  reordered = FALSE;

  /* We allow for actors in the window group other than the actors we
   * know about, but it's up to a plugin to try and keep them stacked correctly
   * (we really need extra API to make that reliable.)
   */

  /* First we collect a list of all backgrounds, and check if they're at the
   * bottom. Then we check if the window actors are in the correct sequence */
  backgrounds = NULL;
  expected_window_node = priv->windows;
  for (old = children; old != NULL; old = old->next)
    {
      ClutterActor *actor = old->data;

      if (META_IS_BACKGROUND_GROUP (actor) ||
          META_IS_BACKGROUND_ACTOR (actor))
        {
          backgrounds = g_list_prepend (backgrounds, actor);

          if (has_windows)
            reordered = TRUE;
        }
      else if (META_IS_WINDOW_ACTOR (actor) && !reordered)
        {
          has_windows = TRUE;

          if (expected_window_node != NULL && actor == expected_window_node->data)
            expected_window_node = expected_window_node->next;
          else
            reordered = TRUE;
        }
    }

  g_list_free (children);

  if (!reordered)
    {
      g_list_free (backgrounds);
      return;
    }

  /* reorder the actors by lowering them in turn to the bottom of the stack.
   * windows first, then background.
   *
   * We reorder the actors even if they're not parented to the window group,
   * to allow stacking to work with intermediate actors (eg during effects)
   */
  for (tmp = g_list_last (priv->windows); tmp != NULL; tmp = tmp->prev)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }

  /* we prepended the backgrounds above so the last actor in the list
   * should get lowered to the bottom last.
   */
  for (tmp = backgrounds; tmp != NULL; tmp = tmp->next)
    {
      ClutterActor *actor = tmp->data, *parent;

      parent = clutter_actor_get_parent (actor);
      clutter_actor_set_child_below_sibling (parent, actor, NULL);
    }
  g_list_free (backgrounds);
}

/*
 * Find the top most window that is visible on the screen. The intention of
 * this is to avoid offscreen windows that isn't actually part of the visible
 * desktop (such as the UI frames override redirect window).
 */
static void
update_top_window_actor (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *l;
  MetaWindowActor *top_window_actor = NULL;

  for (l = g_list_last (priv->windows); l; l = l->prev)
    {
      MetaWindowActor *window_actor = l->data;
      MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
      MtkRectangle buffer_rect;
      MtkRectangle display_rect = { 0 };

      if (!window->visible_to_compositor)
        continue;

      meta_window_get_buffer_rect (window, &buffer_rect);
      meta_display_get_size (priv->display,
                             &display_rect.width, &display_rect.height);

      if (mtk_rectangle_overlap (&display_rect, &buffer_rect))
        {
          top_window_actor = window_actor;
          break;
        }
    }

  if (priv->top_window_actor == top_window_actor)
    return;

  g_clear_signal_handler (&priv->top_window_actor_destroy_id,
                          priv->top_window_actor);

  priv->top_window_actor = top_window_actor;

  if (priv->top_window_actor)
    {
      priv->top_window_actor_destroy_id =
        g_signal_connect (priv->top_window_actor, "destroy",
                          G_CALLBACK (on_top_window_actor_destroyed),
                          compositor);
    }
}

static void
on_top_window_actor_destroyed (MetaWindowActor *window_actor,
                               MetaCompositor  *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  priv->top_window_actor = NULL;
  priv->top_window_actor_destroy_id = 0;
  priv->windows = g_list_remove (priv->windows, window_actor);

  meta_stack_tracker_queue_sync_stack (priv->display->stack_tracker);
}

void
meta_compositor_sync_stack (MetaCompositor  *compositor,
                            GList           *stack)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  GList *old_stack;

  /* This is painful because hidden windows that we are in the process
   * of animating out of existence. They'll be at the bottom of the
   * stack of X windows, but we want to leave them in their old position
   * until the animation effect finishes.
   */

  /* Sources: first window is the highest */
  stack = g_list_copy (stack); /* The new stack of MetaWindow */
  old_stack = g_list_reverse (priv->windows); /* The old stack of MetaWindowActor */
  priv->windows = NULL;

  while (TRUE)
    {
      MetaWindowActor *old_actor = NULL, *stack_actor = NULL, *actor;
      MetaWindow *old_window = NULL, *stack_window = NULL, *window;

      /* Find the remaining top actor in our existing stack (ignoring
       * windows that have been hidden and are no longer animating) */
      while (old_stack)
        {
          old_actor = old_stack->data;
          old_window = meta_window_actor_get_meta_window (old_actor);

          if ((old_window->hidden || old_window->unmanaging) &&
              !meta_window_actor_effect_in_progress (old_actor))
            {
              old_stack = g_list_delete_link (old_stack, old_stack);
              old_actor = NULL;
            }
          else
            break;
        }

      /* And the remaining top actor in the new stack */
      while (stack)
        {
          stack_window = stack->data;
          stack_actor = meta_window_actor_from_window (stack_window);
          if (!stack_actor)
            {
              meta_verbose ("Failed to find corresponding MetaWindowActor "
                            "for window %s", meta_window_get_description (stack_window));
              stack = g_list_delete_link (stack, stack);
            }
          else
            break;
        }

      if (!old_actor && !stack_actor) /* Nothing more to stack */
        break;

      /* We usually prefer the window in the new stack, but if if we
       * found a hidden window in the process of being animated out
       * of existence in the old stack we use that instead. We've
       * filtered out non-animating hidden windows above.
       */
      if (old_actor &&
          (!stack_actor || old_window->hidden || old_window->unmanaging))
        {
          actor = old_actor;
          window = old_window;
        }
      else
        {
          actor = stack_actor;
          window = stack_window;
        }

      /* OK, we know what actor we want next. Add it to our window
       * list, and remove it from both source lists. (It will
       * be at the front of at least one, hopefully it will be
       * near the front of the other.)
       */
      priv->windows = g_list_prepend (priv->windows, actor);

      stack = g_list_remove (stack, window);
      old_stack = g_list_remove (old_stack, actor);
    }

  sync_actor_stacking (compositor);

  update_top_window_actor (compositor);
  invalidate_top_window_actor_for_views (compositor);
}

void
meta_compositor_sync_window_geometry (MetaCompositor *compositor,
                                      MetaWindow     *window,
                                      gboolean        did_placement)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);
  MetaWindowActorChanges changes;

  changes = meta_window_actor_sync_actor_geometry (window_actor, did_placement);

  if (changes & META_WINDOW_ACTOR_CHANGE_SIZE)
    meta_plugin_manager_event_size_changed (priv->plugin_mgr, window_actor);
}

static void
maybe_update_top_window_actor_for_views (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterStage *stage;
  GList *l;

  if (!priv->needs_update_top_window_actors)
    return;

  priv->needs_update_top_window_actors = FALSE;

  COGL_TRACE_BEGIN_SCOPED (UpdateTopWindowActorForViews,
                           "Meta::Compositor::update_top_window_actor_for_views()");

  stage = CLUTTER_STAGE (meta_backend_get_stage (priv->backend));

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      MetaCompositorView *compositor_view;

      compositor_view = g_object_get_qdata (G_OBJECT (stage_view),
                                            quark_compositor_view);

      g_assert (compositor_view != NULL);

      meta_compositor_view_update_top_window_actor (compositor_view,
                                                    priv->windows);
    }
}

static void
meta_compositor_ensure_compositor_views (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterStage *stage =
    CLUTTER_STAGE (meta_backend_get_stage (priv->backend));
  GList *l;

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      MetaCompositorView *compositor_view;

      compositor_view = g_object_get_qdata (G_OBJECT (stage_view),
                                            quark_compositor_view);

      if (compositor_view)
        continue;

      compositor_view = meta_compositor_create_view (compositor,
                                                     stage_view);

      g_object_set_qdata_full (G_OBJECT (stage_view),
                               quark_compositor_view,
                               compositor_view,
                               g_object_unref);
    }
}

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *stage_view,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  int64_t presentation_time = frame_info->presentation_time;
  GList *l;

  for (l = priv->windows; l; l = l->next)
    {
      ClutterActor *actor = l->data;
      GList *actor_stage_views;

      actor_stage_views = clutter_actor_peek_stage_views (actor);
      if (g_list_find (actor_stage_views, stage_view))
        {
          meta_window_actor_frame_complete (META_WINDOW_ACTOR (actor),
                                            frame_info,
                                            presentation_time);
        }
    }
}

static void
meta_compositor_real_before_paint (MetaCompositor     *compositor,
                                   MetaCompositorView *compositor_view)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  ClutterStageView *stage_view;
  MtkRectangle stage_rect;
  MtkRegion *unobscured_region;
  GList *l;

  stage_rect = (MtkRectangle) {
    0, 0,
    clutter_actor_get_width (stage),
    clutter_actor_get_height (stage),
  };

  unobscured_region = mtk_region_create_rectangle (&stage_rect);
  meta_cullable_cull_unobscured (META_CULLABLE (priv->window_group), unobscured_region);
  mtk_region_unref (unobscured_region);

  unobscured_region = mtk_region_create_rectangle (&stage_rect);
  meta_cullable_cull_unobscured (META_CULLABLE (priv->top_window_group), unobscured_region);
  mtk_region_unref (unobscured_region);

  unobscured_region = mtk_region_create_rectangle (&stage_rect);
  meta_cullable_cull_unobscured (META_CULLABLE (priv->feedback_group), unobscured_region);
  mtk_region_unref (unobscured_region);

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  for (l = priv->windows; l; l = l->next)
    meta_window_actor_before_paint (l->data, stage_view);
}

static void
meta_compositor_before_paint (MetaCompositor     *compositor,
                              MetaCompositorView *compositor_view)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  COGL_TRACE_BEGIN_SCOPED (MetaCompositorPrePaint,
                           "Meta::Compositor::before_paint()");

  maybe_update_top_window_actor_for_views (compositor);

  priv->frame_in_progress = TRUE;

  META_COMPOSITOR_GET_CLASS (compositor)->before_paint (compositor, compositor_view);
}

static void
meta_compositor_real_after_paint (MetaCompositor     *compositor,
                                  MetaCompositorView *compositor_view)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterActor *stage_actor = meta_backend_get_stage (priv->backend);
  CoglGraphicsResetStatus status;
  ClutterStageView *stage_view;
  GList *l;

  status = cogl_get_graphics_reset_status (priv->context);
  switch (status)
    {
    case COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
      break;

    case COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
      g_signal_emit_by_name (priv->display, "gl-video-memory-purged");
      g_signal_emit_by_name (stage_actor, "gl-video-memory-purged");
      clutter_actor_queue_redraw (stage_actor);
      break;

    default:
      /* The ARB_robustness spec says that, on error, the application
         should destroy the old context and create a new one. Since we
         don't have the necessary plumbing to do this we'll simply
         restart the process. Obviously we can't do this when we are
         a wayland compositor but in that case we shouldn't get here
         since we don't enable robustness in that case. */
      g_assert (!meta_is_wayland_compositor ());
      meta_restart (NULL, meta_display_get_context (priv->display));
      break;
    }

  stage_view = meta_compositor_view_get_stage_view (compositor_view);

  for (l = priv->windows; l; l = l->next)
    {
      ClutterActor *actor = l->data;
      GList *actor_stage_views;

      actor_stage_views = clutter_actor_peek_stage_views (actor);
      if (g_list_find (actor_stage_views, stage_view))
        meta_window_actor_after_paint (META_WINDOW_ACTOR (actor), stage_view);
    }
}

static void
meta_compositor_after_paint (MetaCompositor     *compositor,
                             MetaCompositorView *compositor_view)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  COGL_TRACE_BEGIN_SCOPED (MetaCompositorPostPaint,
                           "Meta::Compositor::after_paint()");
  META_COMPOSITOR_GET_CLASS (compositor)->after_paint (compositor, compositor_view);

  priv->frame_in_progress = FALSE;
}

static void
on_before_paint (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 ClutterFrame     *frame,
                 MetaCompositor   *compositor)
{
  MetaCompositorView *compositor_view;

  compositor_view = g_object_get_qdata (G_OBJECT (stage_view),
                                        quark_compositor_view);

  g_assert (compositor_view != NULL);

  meta_compositor_before_paint (compositor, compositor_view);
}

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *stage_view,
                ClutterFrame     *frame,
                MetaCompositor   *compositor)
{
  MetaCompositorView *compositor_view;

  compositor_view = g_object_get_qdata (G_OBJECT (stage_view),
                                        quark_compositor_view);

  g_assert (compositor_view != NULL);

  meta_compositor_after_paint (compositor, compositor_view);
}

static void
on_window_visibility_updated (MetaDisplay    *display,
                              GList          *unplaced,
                              GList          *should_show,
                              GList          *should_hide,
                              MetaCompositor *compositor)
{
  update_top_window_actor (compositor);
  invalidate_top_window_actor_for_views (compositor);
}

static void
on_monitors_changed_internal (MetaMonitorManager *monitor_manager,
                              MetaCompositor     *compositor)
{
  meta_compositor_ensure_compositor_views (compositor);
}

static void
on_is_grabbed_changed_cb (ClutterStage   *stage,
                          GParamSpec     *pspec,
                          MetaCompositor *compositor)
{
  if (clutter_stage_get_grab_actor (stage) != NULL)
    meta_compositor_grab_begin (compositor);
  else
    meta_compositor_grab_end (compositor);
}

static void
meta_compositor_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_compositor_init (MetaCompositor *compositor)
{
  invalidate_top_window_actor_for_views (compositor);
}

static void
meta_compositor_constructed (GObject *object)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (priv->backend);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (priv->backend);

  priv->context = clutter_backend->cogl_context;

  priv->before_paint_handler_id =
    g_signal_connect (stage,
                      "before-paint",
                      G_CALLBACK (on_before_paint),
                      compositor);
  priv->after_paint_handler_id =
    g_signal_connect_after (stage,
                            "after-paint",
                            G_CALLBACK (on_after_paint),
                            compositor);
  priv->grabbed_notify_handler_id =
    g_signal_connect (stage,
                      "notify::is-grabbed",
                      G_CALLBACK (on_is_grabbed_changed_cb),
                      compositor);

  priv->window_visibility_updated_id =
    g_signal_connect (priv->display,
                      "window-visibility-updated",
                      G_CALLBACK (on_window_visibility_updated),
                      compositor);

  priv->monitors_changed_internal_id =
    g_signal_connect (monitor_manager,
                      "monitors-changed-internal",
                      G_CALLBACK (on_monitors_changed_internal),
                      compositor);

  priv->laters = meta_laters_new (compositor);

  G_OBJECT_CLASS (meta_compositor_parent_class)->constructed (object);

  meta_compositor_ensure_compositor_views (compositor);
}

static void
meta_compositor_dispose (GObject *object)
{
  MetaCompositor *compositor = META_COMPOSITOR (object);
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  ClutterActor *stage = meta_backend_get_stage (priv->backend);

  g_clear_object (&priv->laters);

  g_clear_signal_handler (&priv->stage_presented_id, stage);
  g_clear_signal_handler (&priv->before_paint_handler_id, stage);
  g_clear_signal_handler (&priv->after_paint_handler_id, stage);
  g_clear_signal_handler (&priv->grabbed_notify_handler_id, stage);
  g_clear_signal_handler (&priv->window_visibility_updated_id, priv->display);

  g_clear_pointer (&priv->windows, g_list_free);

  G_OBJECT_CLASS (meta_compositor_parent_class)->dispose (object);
}

static void
meta_compositor_class_init (MetaCompositorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_compositor_set_property;
  object_class->get_property = meta_compositor_get_property;
  object_class->constructed = meta_compositor_constructed;
  object_class->dispose = meta_compositor_dispose;

  klass->unmanage = meta_compositor_real_unmanage;
  klass->remove_window = meta_compositor_real_remove_window;
  klass->before_paint = meta_compositor_real_before_paint;
  klass->after_paint = meta_compositor_real_after_paint;

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display", NULL, NULL,
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  quark_compositor_view =
    g_quark_from_static_string ("-meta-compositor-view");
}

/**
 * meta_disable_unredirect_for_display:
 * @display: a #MetaDisplay
 *
 * Disables unredirection, can be useful in situations where having
 * unredirected windows is undesirable like when recording a video.
 *
 */
void
meta_disable_unredirect_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  if (display->closing)
    return;

  compositor = get_compositor_for_display (display);
  priv = meta_compositor_get_instance_private (compositor);

  priv->disable_unredirect_count++;
}

/**
 * meta_enable_unredirect_for_display:
 * @display: a #MetaDisplay
 *
 * Enables unredirection which reduces the overhead for apps like games.
 *
 */
void
meta_enable_unredirect_for_display (MetaDisplay *display)
{
  MetaCompositor *compositor;
  MetaCompositorPrivate *priv;

  if (display->closing)
    return;

  compositor = get_compositor_for_display (display);
  priv = meta_compositor_get_instance_private (compositor);

  if (priv->disable_unredirect_count == 0)
    g_warning ("Called enable_unredirect_for_display while unredirection is enabled.");
  if (priv->disable_unredirect_count > 0)
    priv->disable_unredirect_count--;
}

gboolean
meta_compositor_is_unredirect_inhibited (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->disable_unredirect_count > 0;
}

#define FLASH_TIME_MS 50

static void
flash_out_completed (ClutterTimeline *timeline,
                     gboolean         is_finished,
                     gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
meta_compositor_flash_display (MetaCompositor *compositor,
                               MetaDisplay    *display)
{
  ClutterActor *stage;
  ClutterActor *flash;
  ClutterTransition *transition;
  gfloat width, height;

  stage = meta_get_stage_for_display (display);
  clutter_actor_get_size (stage, &width, &height);

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, width, height);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (stage, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

static void
window_flash_out_completed (ClutterTimeline *timeline,
                            gboolean         is_finished,
                            gpointer         user_data)
{
  ClutterActor *flash = CLUTTER_ACTOR (user_data);
  clutter_actor_destroy (flash);
}

void
meta_compositor_flash_window (MetaCompositor *compositor,
                              MetaWindow     *window)
{
  ClutterActor *window_actor =
    CLUTTER_ACTOR (meta_window_actor_from_window (window));
  ClutterActor *flash;
  ClutterTransition *transition;

  flash = clutter_actor_new ();
  clutter_actor_set_background_color (flash, CLUTTER_COLOR_Black);
  clutter_actor_set_size (flash, window->rect.width, window->rect.height);
  clutter_actor_set_position (flash,
                              window->custom_frame_extents.left,
                              window->custom_frame_extents.top);
  clutter_actor_set_opacity (flash, 0);
  clutter_actor_add_child (window_actor, flash);

  clutter_actor_save_easing_state (flash);
  clutter_actor_set_easing_mode (flash, CLUTTER_EASE_IN_QUAD);
  clutter_actor_set_easing_duration (flash, FLASH_TIME_MS);
  clutter_actor_set_opacity (flash, 192);

  transition = clutter_actor_get_transition (flash, "opacity");
  clutter_timeline_set_auto_reverse (CLUTTER_TIMELINE (transition), TRUE);
  clutter_timeline_set_repeat_count (CLUTTER_TIMELINE (transition), 2);

  g_signal_connect (transition, "stopped",
                    G_CALLBACK (window_flash_out_completed), flash);

  clutter_actor_restore_easing_state (flash);
}

/**
 * meta_compositor_monotonic_to_high_res_xserver_time:
 * @display: a #MetaDisplay
 * @monotonic_time: time in the units of g_get_monotonic_time()
 *
 * _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS messages represent time
 * as a "high resolution server time" - this is the server time interpolated
 * to microsecond resolution. The advantage of this time representation
 * is that if  X server is running on the same computer as a client, and
 * the Xserver uses 'clock_gettime(CLOCK_MONOTONIC, ...)' for the server
 * time, the client can detect this, and all such clients will share a
 * a time representation with high accuracy. If there is not a common
 * time source, then the time synchronization will be less accurate.
 */
int64_t
meta_compositor_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                   int64_t         monotonic_time_us)
{
  MetaCompositorClass *klass = META_COMPOSITOR_GET_CLASS (compositor);

  return klass->monotonic_to_high_res_xserver_time (compositor, monotonic_time_us);
}

void
meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   MtkRectangle   *tile_rect,
                                   int             tile_monitor_number)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_show_tile_preview (priv->plugin_mgr,
                                         window, tile_rect, tile_monitor_number);
}

void
meta_compositor_hide_tile_preview (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_hide_tile_preview (priv->plugin_mgr);
}

void
meta_compositor_show_window_menu (MetaCompositor     *compositor,
                                  MetaWindow         *window,
                                  MetaWindowMenuType  menu,
                                  int                 x,
                                  int                 y)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_show_window_menu (priv->plugin_mgr, window, menu, x, y);
}

MetaCloseDialog *
meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_create_close_dialog (priv->plugin_mgr,
                                                  window);
}

MetaInhibitShortcutsDialog *
meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                 MetaWindow     *window)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return meta_plugin_manager_create_inhibit_shortcuts_dialog (priv->plugin_mgr,
                                                              window);
}

void
meta_compositor_locate_pointer (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  meta_plugin_manager_locate_pointer (priv->plugin_mgr);
}

MetaPluginManager *
meta_compositor_get_plugin_manager (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->plugin_mgr;
}

MetaDisplay *
meta_compositor_get_display (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->display;
}

ClutterStage *
meta_compositor_get_stage (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return CLUTTER_STAGE (meta_backend_get_stage (priv->backend));
}

MetaBackend *
meta_compositor_get_backend (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->backend;
}

MetaWindowActor *
meta_compositor_get_top_window_actor (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->top_window_actor;
}

gboolean
meta_compositor_is_switching_workspace (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->switch_workspace_in_progress > 0;
}

/**
 * meta_compositor_get_laters:
 * @compositor: a #MetaCompositor
 *
 * Returns: (transfer none): a #MetaLaters
 */
MetaLaters *
meta_compositor_get_laters (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv;

  g_return_val_if_fail (META_IS_COMPOSITOR (compositor), NULL);

  priv = meta_compositor_get_instance_private (compositor);
  return priv->laters;
}

static void
on_window_drag_ended (MetaWindowDrag *window_drag,
                      MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  g_assert (priv->current_drag == window_drag);
  g_clear_object (&priv->current_drag);
}

gboolean
meta_compositor_drag_window (MetaCompositor       *compositor,
                             MetaWindow           *window,
                             MetaGrabOp            grab_op,
                             ClutterInputDevice   *device,
                             ClutterEventSequence *sequence,
                             uint32_t              timestamp)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);
  g_autoptr (MetaWindowDrag) window_drag = NULL;

  if (priv->current_drag)
    return FALSE;

  window_drag = meta_window_drag_new (window, grab_op);

  if (!meta_window_drag_begin (window_drag, device, sequence, timestamp))
    return FALSE;

  g_signal_connect (window_drag, "ended",
                    G_CALLBACK (on_window_drag_ended), compositor);
  priv->current_drag = g_steal_pointer (&window_drag);
  return TRUE;
}

MetaWindowDrag *
meta_compositor_get_current_window_drag (MetaCompositor *compositor)
{
  MetaCompositorPrivate *priv =
    meta_compositor_get_instance_private (compositor);

  return priv->current_drag;
}
