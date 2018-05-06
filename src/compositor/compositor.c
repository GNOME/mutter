/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:compositor
 * @Title: MetaCompositor
 * @Short_Description: Compositor API
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
 * - window group, accessible with meta_get_window_group_for_screen()
 * - top window group, accessible with meta_get_top_window_group_for_screen()
 *
 * Mutter will place actors representing windows in the window group, except for
 * override-redirect windows (ie. popups and menus) which will be placed in the
 * top window group.
 */

#include <config.h>

#include <clutter/x11/clutter-x11.h>

#include "core.h"
#include <meta/screen.h>
#include <meta/errors.h>
#include <meta/window.h>
#include "compositor-private.h"
#include <meta/compositor-mutter.h>
#include <meta/prefs.h>
#include <meta/main.h>
#include <meta/meta-backend.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-group.h>
#include <meta/meta-shadow-factory.h>
#include "meta-window-actor-private.h"
#include "meta-window-group-private.h"
#include "window-private.h" /* to check window->hidden */
#include "display-private.h" /* for meta_display_lookup_x_window() and meta_display_cancel_touch() */
#include "util-private.h"
#include "backends/meta-dnd-private.h"
#include "frame.h"
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include "meta-sync-ring.h"

#include "backends/x11/meta-backend-x11.h"
#include "clutter/clutter-mutter.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-private.h"
#endif

static void
on_presented (ClutterStage     *stage,
              CoglFrameEvent    event,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor);

static gboolean
is_modal (MetaDisplay *display)
{
  return display->event_route == META_EVENT_ROUTE_COMPOSITOR_GRAB;
}

static void sync_actor_stacking (MetaCompositor *compositor);

static void
meta_finish_workspace_switch (MetaCompositor *compositor)
{
  GList *l;

  /* Finish hiding and showing actors for the new workspace */
  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_sync_visibility (l->data);

  /* Fix up stacking order. */
  sync_actor_stacking (compositor);
}

void
meta_switch_workspace_completed (MetaCompositor *compositor)
{
  /* FIXME -- must redo stacking order */
  compositor->switch_workspace_in_progress--;
  if (compositor->switch_workspace_in_progress < 0)
    {
      g_warning ("Error in workspace_switch accounting!");
      compositor->switch_workspace_in_progress = 0;
    }

  if (!compositor->switch_workspace_in_progress)
    meta_finish_workspace_switch (compositor);
}

void
meta_compositor_destroy (MetaCompositor *compositor)
{
  clutter_threads_remove_repaint_func (compositor->pre_paint_func_id);
  clutter_threads_remove_repaint_func (compositor->post_paint_func_id);

  if (compositor->have_x11_sync_object)
    meta_sync_ring_destroy ();
}

static void
process_damage (MetaCompositor     *compositor,
                XDamageNotifyEvent *event,
                MetaWindow         *window)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_process_x11_damage (window_actor, event);

  compositor->frame_has_updated_xsurfaces = TRUE;
}

/* compat helper */
static MetaCompositor *
get_compositor_for_screen (MetaScreen *screen)
{
  return screen->display->compositor;
}

/**
 * meta_get_stage_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The #ClutterStage for the screen
 */
ClutterActor *
meta_get_stage_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->stage;
}

/**
 * meta_get_window_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The window group corresponding to @screen
 */
ClutterActor *
meta_get_window_group_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->window_group;
}

/**
 * meta_get_top_window_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The top window group corresponding to @screen
 */
ClutterActor *
meta_get_top_window_group_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->top_window_group;
}

/**
 * meta_get_feedback_group_for_screen:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none): The feedback group corresponding to @screen
 */
ClutterActor *
meta_get_feedback_group_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->feedback_group;
}

/**
 * meta_get_window_actors:
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none) (element-type Clutter.Actor): The set of #MetaWindowActor on @screen
 */
GList *
meta_get_window_actors (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->windows;
}

void
meta_set_stage_input_region (MetaScreen   *screen,
                             XserverRegion region)
{
  /* As a wayland compositor we can simply ignore all this trickery
   * for setting an input region on the stage for capturing events in
   * clutter since all input comes to us first and we get to choose
   * who else sees them.
   */
  if (!meta_is_wayland_compositor ())
    {
      MetaDisplay *display = screen->display;
      MetaCompositor *compositor = display->compositor;
      Display *xdpy = meta_display_get_xdisplay (display);
      Window xstage = clutter_x11_get_stage_window (CLUTTER_STAGE (compositor->stage));

      XFixesSetWindowShapeRegion (xdpy, xstage, ShapeInput, 0, 0, region);

      /* It's generally a good heuristic that when a crossing event is generated because
       * we reshape the overlay, we don't want it to affect focus-follows-mouse focus -
       * it's not the user doing something, it's the environment changing under the user.
       */
      meta_display_add_ignored_crossing_serial (display, XNextRequest (xdpy));
      XFixesSetWindowShapeRegion (xdpy, compositor->output, ShapeInput, 0, 0, region);
    }
}

void
meta_empty_stage_input_region (MetaScreen *screen)
{
  /* Using a static region here is a bit hacky, but Metacity never opens more than
   * one XDisplay, so it works fine. */
  static XserverRegion region = None;

  if (region == None)
    {
      MetaDisplay  *display = meta_screen_get_display (screen);
      Display      *xdpy    = meta_display_get_xdisplay (display);
      region = XFixesCreateRegion (xdpy, NULL, 0);
    }

  meta_set_stage_input_region (screen, region);
}

void
meta_focus_stage_window (MetaScreen *screen,
                         guint32     timestamp)
{
  ClutterStage *stage;
  Window window;

  stage = CLUTTER_STAGE (meta_get_stage_for_screen (screen));
  if (!stage)
    return;

  window = clutter_x11_get_stage_window (stage);

  if (window == None)
    return;

  meta_display_set_input_focus_xwindow (screen->display,
                                        screen,
                                        window,
                                        timestamp);
}

gboolean
meta_stage_is_focused (MetaScreen *screen)
{
  ClutterStage *stage;
  Window window;

  if (meta_is_wayland_compositor ())
    return TRUE;

  stage = CLUTTER_STAGE (meta_get_stage_for_screen (screen));
  if (!stage)
    return FALSE;

  window = clutter_x11_get_stage_window (stage);

  if (window == None)
    return FALSE;

  return (screen->display->focus_xwindow == window);
}

static gboolean
grab_devices (MetaModalOptions  options,
              guint32           timestamp)
{
  MetaBackend *backend = META_BACKEND (meta_get_backend ());
  gboolean pointer_grabbed = FALSE;
  gboolean keyboard_grabbed = FALSE;

  if ((options & META_MODAL_POINTER_ALREADY_GRABBED) == 0)
    {
      if (!meta_backend_grab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp))
        goto fail;

      pointer_grabbed = TRUE;
    }

  if ((options & META_MODAL_KEYBOARD_ALREADY_GRABBED) == 0)
    {
      if (!meta_backend_grab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp))
        goto fail;

      keyboard_grabbed = TRUE;
    }

  return TRUE;

 fail:
  if (pointer_grabbed)
    meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp);
  if (keyboard_grabbed)
    meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);

  return FALSE;
}

gboolean
meta_begin_modal_for_plugin (MetaCompositor   *compositor,
                             MetaPlugin       *plugin,
                             MetaModalOptions  options,
                             guint32           timestamp)
{
  /* To some extent this duplicates code in meta_display_begin_grab_op(), but there
   * are significant differences in how we handle grabs that make it difficult to
   * merge the two.
   */
  MetaDisplay *display = compositor->display;

#ifdef HAVE_WAYLAND
  if (display->grab_op == META_GRAB_OP_WAYLAND_POPUP)
    {
      MetaWaylandSeat *seat = meta_wayland_compositor_get_default ()->seat;
      meta_wayland_pointer_end_popup_grab (seat->pointer);
    }
#endif

  if (is_modal (display) || display->grab_op != META_GRAB_OP_NONE)
    return FALSE;

  /* XXX: why is this needed? */
  XIUngrabDevice (display->xdisplay,
                  META_VIRTUAL_CORE_POINTER_ID,
                  timestamp);
  XSync (display->xdisplay, False);

  if (!grab_devices (options, timestamp))
    return FALSE;

  display->grab_op = META_GRAB_OP_COMPOSITOR;
  display->event_route = META_EVENT_ROUTE_COMPOSITOR_GRAB;
  display->grab_window = NULL;
  display->grab_have_pointer = TRUE;
  display->grab_have_keyboard = TRUE;

  g_signal_emit_by_name (display, "grab-op-begin",
                         meta_plugin_get_screen (plugin),
                         display->grab_window, display->grab_op);

  if (meta_is_wayland_compositor ())
    {
      meta_display_sync_wayland_input_focus (display);
      meta_display_cancel_touch (display);

#ifdef HAVE_WAYLAND
      meta_dnd_wayland_handle_begin_modal (compositor);
#endif
    }

  return TRUE;
}

void
meta_end_modal_for_plugin (MetaCompositor *compositor,
                           MetaPlugin     *plugin,
                           guint32         timestamp)
{
  MetaDisplay *display = compositor->display;
  MetaBackend *backend = meta_get_backend ();

  g_return_if_fail (is_modal (display));

  g_signal_emit_by_name (display, "grab-op-end",
                         meta_plugin_get_screen (plugin),
                         display->grab_window, display->grab_op);

  display->grab_op = META_GRAB_OP_NONE;
  display->event_route = META_EVENT_ROUTE_NORMAL;
  display->grab_window = NULL;
  display->grab_have_pointer = FALSE;
  display->grab_have_keyboard = FALSE;

  meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_POINTER_ID, timestamp);
  meta_backend_ungrab_device (backend, META_VIRTUAL_CORE_KEYBOARD_ID, timestamp);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
      meta_dnd_wayland_handle_end_modal (compositor);
      meta_display_sync_wayland_input_focus (display);
    }
#endif
}

static void
after_stage_paint (ClutterStage *stage,
                   gpointer      data)
{
  MetaCompositor *compositor = data;
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_post_paint (l->data);

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    meta_wayland_compositor_paint_finished (meta_wayland_compositor_get_default ());
#endif
}

static void
redirect_windows (MetaScreen *screen)
{
  MetaDisplay *display       = meta_screen_get_display (screen);
  Display     *xdisplay      = meta_display_get_xdisplay (display);
  Window       xroot         = meta_screen_get_xroot (screen);
  int          screen_number = meta_screen_get_screen_number (screen);
  guint        n_retries;
  guint        max_retries;

  if (meta_get_replace_current_wm ())
    max_retries = 5;
  else
    max_retries = 1;

  n_retries = 0;

  /* Some compositors (like old versions of Mutter) might not properly unredirect
   * subwindows before destroying the WM selection window; so we wait a while
   * for such a compositor to exit before giving up.
   */
  while (TRUE)
    {
      meta_error_trap_push (display);
      XCompositeRedirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
      XSync (xdisplay, FALSE);

      if (!meta_error_trap_pop_with_return (display))
        break;

      if (n_retries == max_retries)
        {
          /* This probably means that a non-WM compositor like xcompmgr is running;
           * we have no way to get it to exit */
          meta_fatal (_("Another compositing manager is already running on screen %i on display “%s”."),
                      screen_number, display->name);
        }

      n_retries++;
      g_usleep (G_USEC_PER_SEC);
    }
}

void
meta_compositor_manage (MetaCompositor *compositor)
{
  MetaDisplay *display = compositor->display;
  Display *xdisplay = display->xdisplay;
  MetaScreen *screen = display->screen;
  MetaBackend *backend = meta_get_backend ();

  meta_screen_set_cm_selection (display->screen);

  compositor->stage = meta_backend_get_stage (backend);

  g_signal_connect (compositor->stage, "presented",
                    G_CALLBACK (on_presented),
                    compositor);

  /* We use connect_after() here to accomodate code in GNOME Shell that,
   * when benchmarking drawing performance, connects to ::after-paint
   * and calls glFinish(). The timing information from that will be
   * more accurate if we hold off until that completes before we signal
   * apps to begin drawing the next frame. If there are no other
   * connections to ::after-paint, connect() vs. connect_after() doesn't
   * matter.
   */
  g_signal_connect_after (CLUTTER_STAGE (compositor->stage), "after-paint",
                          G_CALLBACK (after_stage_paint), compositor);

  clutter_stage_set_sync_delay (CLUTTER_STAGE (compositor->stage), META_SYNC_DELAY);

  compositor->window_group = meta_window_group_new (screen);
  compositor->top_window_group = meta_window_group_new (screen);
  compositor->feedback_group = meta_window_group_new (screen);

  clutter_actor_add_child (compositor->stage, compositor->window_group);
  clutter_actor_add_child (compositor->stage, compositor->top_window_group);
  clutter_actor_add_child (compositor->stage, compositor->feedback_group);

  if (meta_is_wayland_compositor ())
    {
      /* NB: When running as a wayland compositor we don't need an X
       * composite overlay window, and we don't need to play any input
       * region tricks to redirect events into clutter. */
      compositor->output = None;
    }
  else
    {
      Window xwin;

      compositor->output = screen->composite_overlay_window;

      xwin = meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend));

      XReparentWindow (xdisplay, xwin, compositor->output, 0, 0);

      meta_empty_stage_input_region (screen);

      /* Make sure there isn't any left-over output shape on the
       * overlay window by setting the whole screen to be an
       * output region.
       *
       * Note: there doesn't seem to be any real chance of that
       *  because the X server will destroy the overlay window
       *  when the last client using it exits.
       */
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);

      /* Map overlay window before redirecting windows offscreen so we catch their
       * contents until we show the stage.
       */
      XMapWindow (xdisplay, compositor->output);

      compositor->have_x11_sync_object = meta_sync_ring_init (xdisplay);
    }

  redirect_windows (display->screen);

  compositor->plugin_mgr = meta_plugin_manager_new (compositor);
}

void
meta_compositor_unmanage (MetaCompositor *compositor)
{
  if (!meta_is_wayland_compositor ())
    {
      MetaDisplay *display = compositor->display;
      Display *xdisplay = meta_display_get_xdisplay (display);
      Window xroot = display->screen->xroot;

      /* This is the most important part of cleanup - we have to do this
       * before giving up the window manager selection or the next
       * window manager won't be able to redirect subwindows */
      XCompositeUnredirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
    }
}

/**
 * meta_shape_cow_for_window:
 * @compositor: A #MetaCompositor
 * @window: (nullable): A #MetaWindow to shape the COW for
 *
 * Sets an bounding shape on the COW so that the given window
 * is exposed. If @window is %NULL it clears the shape again.
 *
 * Used so we can unredirect windows, by shaping away the part
 * of the COW, letting the raw window be seen through below.
 */
static void
meta_shape_cow_for_window (MetaCompositor *compositor,
                           MetaWindow *window)
{
  MetaDisplay *display = compositor->display;
  Display *xdisplay = meta_display_get_xdisplay (display);

  if (window == NULL)
    XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, None);
  else
    {
      XserverRegion output_region;
      XRectangle screen_rect, window_bounds;
      int width, height;
      MetaRectangle rect;

      meta_window_get_frame_rect (window, &rect);

      window_bounds.x = rect.x;
      window_bounds.y = rect.y;
      window_bounds.width = rect.width;
      window_bounds.height = rect.height;

      meta_screen_get_size (display->screen, &width, &height);
      screen_rect.x = 0;
      screen_rect.y = 0;
      screen_rect.width = width;
      screen_rect.height = height;

      output_region = XFixesCreateRegion (xdisplay, &window_bounds, 1);

      XFixesInvertRegion (xdisplay, output_region, &screen_rect, output_region);
      XFixesSetWindowShapeRegion (xdisplay, compositor->output, ShapeBounding, 0, 0, output_region);
      XFixesDestroyRegion (xdisplay, output_region);
    }
}

static void
set_unredirected_window (MetaCompositor *compositor,
                         MetaWindow     *window)
{
  if (compositor->unredirected_window == window)
    return;

  if (compositor->unredirected_window != NULL)
    {
      MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (compositor->unredirected_window));
      meta_window_actor_set_unredirected (window_actor, FALSE);
    }

  meta_shape_cow_for_window (compositor, window);
  compositor->unredirected_window = window;

  if (compositor->unredirected_window != NULL)
    {
      MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (compositor->unredirected_window));
      meta_window_actor_set_unredirected (window_actor, TRUE);
    }
}

void
meta_compositor_add_window (MetaCompositor    *compositor,
                            MetaWindow        *window)
{
  MetaDisplay *display = compositor->display;

  meta_error_trap_push (display);

  meta_window_actor_new (window);
  sync_actor_stacking (compositor);

  meta_error_trap_pop (display);
}

void
meta_compositor_remove_window (MetaCompositor *compositor,
                               MetaWindow     *window)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));

  if (compositor->unredirected_window == window)
    set_unredirected_window (compositor, NULL);

  meta_window_actor_queue_destroy (window_actor);
}

void
meta_compositor_sync_updates_frozen (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_sync_updates_frozen (window_actor);
}

void
meta_compositor_queue_frame_drawn (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   gboolean        no_delay_frame)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_queue_frame_drawn (window_actor, no_delay_frame);
}

void
meta_compositor_window_shape_changed (MetaCompositor *compositor,
                                      MetaWindow     *window)
{
  MetaWindowActor *window_actor;
  window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!window_actor)
    return;

  meta_window_actor_update_shape (window_actor);
}

void
meta_compositor_window_opacity_changed (MetaCompositor *compositor,
                                        MetaWindow     *window)
{
  MetaWindowActor *window_actor;
  window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!window_actor)
    return;

  meta_window_actor_update_opacity (window_actor);
}

void
meta_compositor_window_surface_changed (MetaCompositor *compositor,
                                        MetaWindow     *window)
{
  MetaWindowActor *window_actor;
  window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!window_actor)
    return;

  meta_window_actor_update_surface (window_actor);
}

/**
 * meta_compositor_process_event: (skip)
 * @compositor:
 * @event:
 * @window:
 *
 */
gboolean
meta_compositor_process_event (MetaCompositor *compositor,
                               XEvent         *event,
                               MetaWindow     *window)
{
  if (!meta_is_wayland_compositor () &&
      event->type == meta_display_get_damage_event_base (compositor->display) + XDamageNotify)
    {
      /* Core code doesn't handle damage events, so we need to extract the MetaWindow
       * ourselves
       */
      if (window == NULL)
        {
          Window xwin = ((XDamageNotifyEvent *) event)->drawable;
          window = meta_display_lookup_x_window (compositor->display, xwin);
        }

      if (window)
        process_damage (compositor, (XDamageNotifyEvent *) event, window);
    }

  if (compositor->have_x11_sync_object)
    meta_sync_ring_handle_event (event);

  /* Clutter needs to know about MapNotify events otherwise it will
     think the stage is invisible */
  if (!meta_is_wayland_compositor () && event->type == MapNotify)
    clutter_x11_handle_event (event);

  /* The above handling is basically just "observing" the events, so we return
   * FALSE to indicate that the event should not be filtered out; if we have
   * GTK+ windows in the same process, GTK+ needs the ConfigureNotify event, for example.
   */
  return FALSE;
}

gboolean
meta_compositor_filter_keybinding (MetaCompositor *compositor,
                                   MetaKeyBinding *binding)
{
  return meta_plugin_manager_filter_keybinding (compositor->plugin_mgr, binding);
}

void
meta_compositor_show_window (MetaCompositor *compositor,
			     MetaWindow	    *window,
                             MetaCompEffect  effect)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
 meta_window_actor_show (window_actor, effect);
}

void
meta_compositor_hide_window (MetaCompositor *compositor,
                             MetaWindow     *window,
                             MetaCompEffect  effect)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_hide (window_actor, effect);
}

void
meta_compositor_size_change_window (MetaCompositor    *compositor,
                                    MetaWindow        *window,
                                    MetaSizeChange     which_change,
                                    MetaRectangle     *old_frame_rect,
                                    MetaRectangle     *old_buffer_rect)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_size_change (window_actor, which_change, old_frame_rect, old_buffer_rect);
}

void
meta_compositor_switch_workspace (MetaCompositor     *compositor,
                                  MetaWorkspace      *from,
                                  MetaWorkspace      *to,
                                  MetaMotionDirection direction)
{
  gint to_indx, from_indx;

  to_indx   = meta_workspace_index (to);
  from_indx = meta_workspace_index (from);

  compositor->switch_workspace_in_progress++;

  if (!meta_plugin_manager_switch_workspace (compositor->plugin_mgr,
                                             from_indx,
                                             to_indx,
                                             direction))
    {
      compositor->switch_workspace_in_progress--;

      /* We have to explicitely call this to fix up stacking order of the
       * actors; this is because the abs stacking position of actors does not
       * necessarily change during the window hiding/unhiding, only their
       * relative position toward the destkop window.
       */
      meta_finish_workspace_switch (compositor);
    }
}

static void
sync_actor_stacking (MetaCompositor *compositor)
{
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

  children = clutter_actor_get_children (compositor->window_group);
  has_windows = FALSE;
  reordered = FALSE;

  /* We allow for actors in the window group other than the actors we
   * know about, but it's up to a plugin to try and keep them stacked correctly
   * (we really need extra API to make that reliable.)
   */

  /* First we collect a list of all backgrounds, and check if they're at the
   * bottom. Then we check if the window actors are in the correct sequence */
  backgrounds = NULL;
  expected_window_node = compositor->windows;
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
  for (tmp = g_list_last (compositor->windows); tmp != NULL; tmp = tmp->prev)
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
static MetaWindowActor *
get_top_visible_window_actor (MetaCompositor *compositor)
{
  GList *l;

  for (l = g_list_last (compositor->windows); l; l = l->prev)
    {
      MetaWindowActor *window_actor = l->data;
      MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
      MetaRectangle buffer_rect;

      meta_window_get_buffer_rect (window, &buffer_rect);

      if (meta_rectangle_overlap (&compositor->display->screen->rect,
                                  &buffer_rect))
        return window_actor;
    }

  return NULL;
}

static void
on_top_window_actor_destroyed (MetaWindowActor *window_actor,
                               MetaCompositor  *compositor)
{
  compositor->top_window_actor = NULL;
  compositor->windows = g_list_remove (compositor->windows, window_actor);

  meta_stack_tracker_queue_sync_stack (compositor->display->screen->stack_tracker);
}

void
meta_compositor_sync_stack (MetaCompositor  *compositor,
			    GList	    *stack)
{
  GList *old_stack;

  /* This is painful because hidden windows that we are in the process
   * of animating out of existence. They'll be at the bottom of the
   * stack of X windows, but we want to leave them in their old position
   * until the animation effect finishes.
   */

  /* Sources: first window is the highest */
  stack = g_list_copy (stack); /* The new stack of MetaWindow */
  old_stack = g_list_reverse (compositor->windows); /* The old stack of MetaWindowActor */
  compositor->windows = NULL;

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
          stack_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (stack_window));
          if (!stack_actor)
            {
              meta_verbose ("Failed to find corresponding MetaWindowActor "
                            "for window %s\n", meta_window_get_description (stack_window));
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
      compositor->windows = g_list_prepend (compositor->windows, actor);

      stack = g_list_remove (stack, window);
      old_stack = g_list_remove (old_stack, actor);
    }

  sync_actor_stacking (compositor);

  if (compositor->top_window_actor)
    g_signal_handlers_disconnect_by_func (compositor->top_window_actor,
                                          on_top_window_actor_destroyed,
                                          compositor);

  compositor->top_window_actor = get_top_visible_window_actor (compositor);

  if (compositor->top_window_actor)
    g_signal_connect (compositor->top_window_actor, "destroy",
                      G_CALLBACK (on_top_window_actor_destroyed),
                      compositor);
}

void
meta_compositor_sync_window_geometry (MetaCompositor *compositor,
				      MetaWindow *window,
                                      gboolean did_placement)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  meta_window_actor_sync_actor_geometry (window_actor, did_placement);
  meta_plugin_manager_event_size_changed (compositor->plugin_mgr, window_actor);
}

static void
on_presented (ClutterStage     *stage,
              CoglFrameEvent    event,
              ClutterFrameInfo *frame_info,
              MetaCompositor   *compositor)
{
  GList *l;

  if (event == COGL_FRAME_EVENT_COMPLETE)
    {
      gint64 presentation_time_cogl = frame_info->presentation_time;
      gint64 presentation_time;

      if (presentation_time_cogl != 0)
        {
          /* Cogl reports presentation in terms of its own clock, which is
           * guaranteed to be in nanoseconds but with no specified base. The
           * normal case with the open source GPU drivers on Linux 3.8 and
           * newer is that the base of cogl_get_clock_time() is that of
           * clock_gettime(CLOCK_MONOTONIC), so the same as g_get_monotonic_time),
           * but there's no exposure of that through the API. clock_gettime()
           * is fairly fast, so calling it twice and subtracting to get a
           * nearly-zero number is acceptable, if a litle ugly.
           */
          gint64 current_cogl_time = cogl_get_clock_time (compositor->context);
          gint64 current_monotonic_time = g_get_monotonic_time ();

          presentation_time =
            current_monotonic_time + (presentation_time_cogl - current_cogl_time) / 1000;
        }
      else
        {
          presentation_time = 0;
        }

      for (l = compositor->windows; l; l = l->next)
        meta_window_actor_frame_complete (l->data, frame_info, presentation_time);
    }
}

static gboolean
meta_pre_paint_func (gpointer data)
{
  GList *l;
  MetaWindowActor *top_window_actor;
  MetaCompositor *compositor = data;

  if (compositor->windows == NULL)
    return TRUE;

  top_window_actor = compositor->top_window_actor;
  if (top_window_actor &&
      meta_window_actor_should_unredirect (top_window_actor) &&
      compositor->disable_unredirect_count == 0)
    {
      MetaWindow *top_window;

      top_window = meta_window_actor_get_meta_window (top_window_actor);
      set_unredirected_window (compositor, top_window);
    }
  else
    {
      set_unredirected_window (compositor, NULL);
    }

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_pre_paint (l->data);

  if (compositor->frame_has_updated_xsurfaces)
    {
      /* We need to make sure that any X drawing that happens before
       * the XDamageSubtract() for each window above is visible to
       * subsequent GL rendering; the standardized way to do this is
       * GL_EXT_X11_sync_object. Since this isn't implemented yet in
       * mesa, we also have a path that relies on the implementation
       * of the open source drivers.
       *
       * Anything else, we just hope for the best.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before
       * sending out damage events, but since we use
       * DamageReportBoundingBox there may be drawing between the last
       * damage event and the XDamageSubtract() that needs to be
       * flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel
       * before writing events or responses to the client, so any
       * round trip request at this point is sufficient to flush the
       * GLX buffers.
       */
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = meta_sync_ring_insert_wait ();
      else
        XSync (compositor->display->xdisplay, False);
    }

  return TRUE;
}

static gboolean
meta_post_paint_func (gpointer data)
{
  MetaCompositor *compositor = data;
  CoglGraphicsResetStatus status;

  if (compositor->frame_has_updated_xsurfaces)
    {
      if (compositor->have_x11_sync_object)
        compositor->have_x11_sync_object = meta_sync_ring_after_frame ();

      compositor->frame_has_updated_xsurfaces = FALSE;
    }

  status = cogl_get_graphics_reset_status (compositor->context);
  switch (status)
    {
    case COGL_GRAPHICS_RESET_STATUS_NO_ERROR:
      break;

    case COGL_GRAPHICS_RESET_STATUS_PURGED_CONTEXT_RESET:
      g_signal_emit_by_name (compositor->display, "gl-video-memory-purged");
      clutter_actor_queue_redraw (CLUTTER_ACTOR (compositor->stage));
      break;

    default:
      /* The ARB_robustness spec says that, on error, the application
         should destroy the old context and create a new one. Since we
         don't have the necessary plumbing to do this we'll simply
         restart the process. Obviously we can't do this when we are
         a wayland compositor but in that case we shouldn't get here
         since we don't enable robustness in that case. */
      g_assert (!meta_is_wayland_compositor ());
      meta_restart (NULL);
      break;
    }

  return TRUE;
}

static void
on_shadow_factory_changed (MetaShadowFactory *factory,
                           MetaCompositor    *compositor)
{
  GList *l;

  for (l = compositor->windows; l; l = l->next)
    meta_window_actor_invalidate_shadow (l->data);
}

/**
 * meta_compositor_new: (skip)
 * @display:
 *
 */
MetaCompositor *
meta_compositor_new (MetaDisplay *display)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  MetaCompositor *compositor;

  compositor = g_new0 (MetaCompositor, 1);
  compositor->display = display;
  compositor->context = clutter_backend->cogl_context;

  if (g_getenv("META_DISABLE_MIPMAPS"))
    compositor->no_mipmaps = TRUE;

  g_signal_connect (meta_shadow_factory_get_default (),
                    "changed",
                    G_CALLBACK (on_shadow_factory_changed),
                    compositor);

  compositor->pre_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                           meta_pre_paint_func,
                                           compositor,
                                           NULL);
  compositor->post_paint_func_id =
    clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_POST_PAINT,
                                           meta_post_paint_func,
                                           compositor,
                                           NULL);
  return compositor;
}

/**
 * meta_get_overlay_window: (skip)
 * @screen: a #MetaScreen
 *
 */
Window
meta_get_overlay_window (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  return compositor->output;
}

/**
 * meta_disable_unredirect_for_screen:
 * @screen: a #MetaScreen
 *
 * Disables unredirection, can be usefull in situations where having
 * unredirected windows is undesireable like when recording a video.
 *
 */
void
meta_disable_unredirect_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  compositor->disable_unredirect_count++;
}

/**
 * meta_enable_unredirect_for_screen:
 * @screen: a #MetaScreen
 *
 * Enables unredirection which reduces the overhead for apps like games.
 *
 */
void
meta_enable_unredirect_for_screen (MetaScreen *screen)
{
  MetaCompositor *compositor = get_compositor_for_screen (screen);
  if (compositor->disable_unredirect_count == 0)
    g_warning ("Called enable_unredirect_for_screen while unredirection is enabled.");
  if (compositor->disable_unredirect_count > 0)
    compositor->disable_unredirect_count--;
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
meta_compositor_flash_screen (MetaCompositor *compositor,
                              MetaScreen     *screen)
{
  ClutterActor *stage;
  ClutterActor *flash;
  ClutterTransition *transition;
  gfloat width, height;

  stage = meta_get_stage_for_screen (screen);
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
    CLUTTER_ACTOR (meta_window_get_compositor_private (window));
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
 * meta_compositor_monotonic_time_to_server_time:
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
gint64
meta_compositor_monotonic_time_to_server_time (MetaDisplay *display,
                                               gint64       monotonic_time)
{
  MetaCompositor *compositor = display->compositor;

  if (compositor->server_time_query_time == 0 ||
      (!compositor->server_time_is_monotonic_time &&
       monotonic_time > compositor->server_time_query_time + 10*1000*1000)) /* 10 seconds */
    {
      guint32 server_time = meta_display_get_current_time_roundtrip (display);
      gint64 server_time_usec = (gint64)server_time * 1000;
      gint64 current_monotonic_time = g_get_monotonic_time ();
      compositor->server_time_query_time = current_monotonic_time;

      /* If the server time is within a second of the monotonic time,
       * we assume that they are identical. This seems like a big margin,
       * but we want to be as robust as possible even if the system
       * is under load and our processing of the server response is
       * delayed.
       */
      if (server_time_usec > current_monotonic_time - 1000*1000 &&
          server_time_usec < current_monotonic_time + 1000*1000)
        compositor->server_time_is_monotonic_time = TRUE;

      compositor->server_time_offset = server_time_usec - current_monotonic_time;
    }

  if (compositor->server_time_is_monotonic_time)
    return monotonic_time;
  else
    return monotonic_time + compositor->server_time_offset;
}

void
meta_compositor_show_tile_preview (MetaCompositor *compositor,
                                   MetaWindow     *window,
                                   MetaRectangle  *tile_rect,
                                   int             tile_monitor_number)
{
  meta_plugin_manager_show_tile_preview (compositor->plugin_mgr,
                                         window, tile_rect, tile_monitor_number);
}

void
meta_compositor_hide_tile_preview (MetaCompositor *compositor)
{
  meta_plugin_manager_hide_tile_preview (compositor->plugin_mgr);
}

void
meta_compositor_show_window_menu (MetaCompositor     *compositor,
                                  MetaWindow         *window,
                                  MetaWindowMenuType  menu,
                                  int                 x,
                                  int                 y)
{
  meta_plugin_manager_show_window_menu (compositor->plugin_mgr, window, menu, x, y);
}

void
meta_compositor_show_window_menu_for_rect (MetaCompositor     *compositor,
                                           MetaWindow         *window,
                                           MetaWindowMenuType  menu,
					   MetaRectangle      *rect)
{
  meta_plugin_manager_show_window_menu_for_rect (compositor->plugin_mgr, window, menu, rect);
}

MetaCloseDialog *
meta_compositor_create_close_dialog (MetaCompositor *compositor,
                                     MetaWindow     *window)
{
  return meta_plugin_manager_create_close_dialog (compositor->plugin_mgr,
                                                  window);
}

MetaInhibitShortcutsDialog *
meta_compositor_create_inhibit_shortcuts_dialog (MetaCompositor *compositor,
                                                 MetaWindow     *window)
{
  return meta_plugin_manager_create_inhibit_shortcuts_dialog (compositor->plugin_mgr,
                                                              window);
}
