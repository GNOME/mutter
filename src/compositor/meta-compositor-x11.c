/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "compositor/meta-compositor-x11.h"

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "compositor/meta-compositor-view.h"
#include "compositor/meta-sync-ring.h"
#include "compositor/meta-window-actor-x11.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

#define IS_GESTURE_EVENT(et) ((et) == CLUTTER_TOUCH_BEGIN || \
                              (et) == CLUTTER_TOUCH_UPDATE || \
                              (et) == CLUTTER_TOUCH_END || \
                              (et) == CLUTTER_TOUCH_CANCEL)

struct _MetaCompositorX11
{
  MetaCompositor parent;

  Window output;

  gulong before_update_handler_id;
  gulong after_update_handler_id;
  gulong focus_window_handler_id;

  gboolean frame_has_updated_xsurfaces;
  gboolean have_x11_sync_object;
  gboolean have_root_window_key_grab;

  MetaWindow *unredirected_window;
  MetaWindow *focus_window;

  gboolean xserver_uses_monotonic_clock;
  int64_t xserver_time_query_time_us;
  int64_t xserver_time_offset_us;
};

typedef struct
{
  MetaCompositorX11 *compositor_x11;
  Window xwindow;
  gboolean grab;
} KeyGrabData;

G_DEFINE_TYPE (MetaCompositorX11, meta_compositor_x11, META_TYPE_COMPOSITOR)

static void meta_compositor_x11_grab_root_window_keys (MetaCompositorX11 *compositor_x11);

static void
process_damage (MetaCompositorX11  *compositor_x11,
                XDamageNotifyEvent *damage_xevent,
                MetaWindow         *window)
{
  MetaWindowActor *window_actor = meta_window_actor_from_window (window);
  MetaWindowActorX11 *window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);

  meta_window_actor_x11_process_damage (window_actor_x11, damage_xevent);

  compositor_x11->frame_has_updated_xsurfaces = TRUE;
}

void
meta_compositor_x11_process_xevent (MetaCompositorX11 *compositor_x11,
                                    XEvent            *xevent,
                                    MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  int damage_event_base;

  damage_event_base = meta_x11_display_get_damage_event_base (x11_display);
  if (xevent->type == damage_event_base + XDamageNotify)
    {
      /*
       * Core code doesn't handle damage events, so we need to extract the
       * MetaWindow ourselves.
       */
      if (!window)
        {
          Window xwindow;

          xwindow = ((XDamageNotifyEvent *) xevent)->drawable;
          window = meta_x11_display_lookup_x_window (x11_display, xwindow);
        }

      if (window)
        process_damage (compositor_x11, (XDamageNotifyEvent *) xevent, window);
    }

  if (compositor_x11->have_x11_sync_object)
    meta_sync_ring_handle_event (xevent);
}

static void
determine_server_clock_source (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  uint32_t server_time_ms;
  int64_t server_time_us;
  int64_t translated_monotonic_now_us;

  server_time_ms = meta_x11_display_get_current_time_roundtrip (x11_display);
  server_time_us = ms2us (server_time_ms);
  translated_monotonic_now_us =
    meta_translate_to_high_res_xserver_time (g_get_monotonic_time ());

  /* If the server time offset is within a second of the monotonic time, we
   * assume that they are identical. This seems like a big margin, but we want
   * to be as robust as possible even if the system is under load and our
   * processing of the server response is delayed.
   */
  if (ABS (server_time_us - translated_monotonic_now_us) < s2us (1))
    compositor_x11->xserver_uses_monotonic_clock = TRUE;
  else
    compositor_x11->xserver_uses_monotonic_clock = FALSE;
}

static gboolean
meta_compositor_x11_manage (MetaCompositor  *compositor,
                            GError         **error)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  MetaX11Display *x11_display = display->x11_display;
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  int composite_version;
  Window xwindow;

  if (!META_X11_DISPLAY_HAS_COMPOSITE (x11_display) ||
      !META_X11_DISPLAY_HAS_DAMAGE (x11_display))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing required extension %s",
                   !META_X11_DISPLAY_HAS_COMPOSITE (x11_display) ?
                   "composite" : "damage");
      return FALSE;
    }

  composite_version = ((x11_display->composite_major_version * 10) +
                       x11_display->composite_minor_version);
  if (composite_version < 3)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "COMPOSITE extension 3.0 required (found %d.%d)",
                   x11_display->composite_major_version,
                   x11_display->composite_minor_version);
      return FALSE;
    }

  determine_server_clock_source (compositor_x11);

  compositor_x11->output = display->x11_display->composite_overlay_window;

  xwindow = meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend));

  XReparentWindow (xdisplay, xwindow, compositor_x11->output, 0, 0);

  meta_x11_display_set_stage_input_region (display->x11_display, NULL, 0);

  /*
   * Make sure there isn't any left-over output shape on the overlay window by
   * setting the whole screen to be an output region.
   *
   * Note: there doesn't seem to be any real chance of that because the X
   * server will destroy the overlay window when the last client using it
   * exits.
   */
  XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                              ShapeBounding, 0, 0, None);

  /*
   * Map overlay window before redirecting windows offscreen so we catch their
   * contents until we show the stage.
   */
  XMapWindow (xdisplay, compositor_x11->output);

  compositor_x11->have_x11_sync_object = meta_sync_ring_init (cogl_context, xdisplay);

  meta_x11_display_redirect_windows (x11_display, display);

  meta_compositor_x11_grab_root_window_keys (compositor_x11);

  return TRUE;
}

static void
meta_compositor_x11_unmanage (MetaCompositor *compositor)
{
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaX11Display *x11_display = display->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  Window xroot = x11_display->xroot;
  Window backend_xwindow;
  MetaCompositorClass *parent_class;

  backend_xwindow = meta_backend_x11_get_xwindow (META_BACKEND_X11 (backend));
  XReparentWindow (xdisplay, backend_xwindow, xroot, 0, 0);

  /*
   * This is the most important part of cleanup - we have to do this before
   * giving up the window manager selection or the next window manager won't be
   * able to redirect subwindows
   */
  XCompositeUnredirectSubwindows (xdisplay, xroot, CompositeRedirectManual);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->unmanage (compositor);
}

/*
 * Sets an bounding shape on the COW so that the given window
 * is exposed. If window is %NULL it clears the shape again.
 *
 * Used so we can unredirect windows, by shaping away the part
 * of the COW, letting the raw window be seen through below.
 */
static void
shape_cow_for_window (MetaCompositorX11 *compositor_x11,
                      MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (!window)
    {
      XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                                  ShapeBounding, 0, 0, None);
    }
  else
    {
      XserverRegion output_region;
      XRectangle screen_rect, window_bounds;
      int width, height;
      MtkRectangle rect;

      meta_window_get_frame_rect (window, &rect);

      window_bounds.x = rect.x;
      window_bounds.y = rect.y;
      window_bounds.width = rect.width;
      window_bounds.height = rect.height;

      meta_display_get_size (display, &width, &height);
      screen_rect.x = 0;
      screen_rect.y = 0;
      screen_rect.width = width;
      screen_rect.height = height;

      output_region = XFixesCreateRegion (xdisplay, &window_bounds, 1);

      XFixesInvertRegion (xdisplay, output_region, &screen_rect, output_region);
      XFixesSetWindowShapeRegion (xdisplay, compositor_x11->output,
                                  ShapeBounding, 0, 0, output_region);
      XFixesDestroyRegion (xdisplay, output_region);
    }
}

static void
set_unredirected_window (MetaCompositorX11 *compositor_x11,
                         MetaWindow        *window)
{
  MetaWindow *prev_unredirected_window = compositor_x11->unredirected_window;

  if (prev_unredirected_window == window)
    return;

  if (prev_unredirected_window)
    {
      MetaWindowActor *window_actor;
      MetaWindowActorX11 *window_actor_x11;

      window_actor = meta_window_actor_from_window (prev_unredirected_window);
      window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
      meta_window_actor_x11_set_unredirected (window_actor_x11, FALSE);
    }

  shape_cow_for_window (compositor_x11, window);
  compositor_x11->unredirected_window = window;

  if (window)
    {
      MetaWindowActor *window_actor;
      MetaWindowActorX11 *window_actor_x11;

      window_actor = meta_window_actor_from_window (window);
      window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
      meta_window_actor_x11_set_unredirected (window_actor_x11, TRUE);
    }
}

static void
maybe_unredirect_top_window (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaWindow *window_to_unredirect = NULL;
  MetaWindowActor *window_actor;
  MetaWindowActorX11 *window_actor_x11;

  if (meta_compositor_is_unredirect_inhibited (compositor))
    goto out;

  window_actor = meta_compositor_get_top_window_actor (compositor);
  if (!window_actor)
    goto out;

  window_actor_x11 = META_WINDOW_ACTOR_X11 (window_actor);
  if (!meta_window_actor_x11_should_unredirect (window_actor_x11))
    goto out;

  window_to_unredirect = meta_window_actor_get_meta_window (window_actor);

out:
  set_unredirected_window (compositor_x11, window_to_unredirect);
}

static void
maybe_do_sync (MetaCompositor *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);

  if (compositor_x11->frame_has_updated_xsurfaces)
    {
      MetaDisplay *display = meta_compositor_get_display (compositor);
      MetaBackend *backend = meta_compositor_get_backend (compositor);
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);

      /*
       * We need to make sure that any X drawing that happens before the
       * XDamageSubtract() for each window above is visible to subsequent GL
       * rendering; the standardized way to do this is GL_EXT_X11_sync_object.
       * Since this isn't implemented yet in mesa, we also have a path that
       * relies on the implementation of the open source drivers.
       *
       * Anything else, we just hope for the best.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before sending
       * out damage events, but since we use DamageReportBoundingBox there may
       * be drawing between the last damage event and the XDamageSubtract()
       * that needs to be flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel before
       * writing events or responses to the client, so any round trip request
       * at this point is sufficient to flush the GLX buffers.
       */
      if (compositor_x11->have_x11_sync_object)
        compositor_x11->have_x11_sync_object = meta_sync_ring_insert_wait (cogl_context);
      else
        XSync (display->x11_display->xdisplay, False);
    }
}

static void
on_before_update (ClutterStage     *stage,
                  ClutterStageView *stage_view,
                  ClutterFrame     *frame,
                  MetaCompositor   *compositor)
{
  maybe_do_sync (compositor);
}

static void
on_after_update (ClutterStage     *stage,
                 ClutterStageView *stage_view,
                 ClutterFrame     *frame,
                 MetaCompositor   *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);

  if (compositor_x11->frame_has_updated_xsurfaces)
    {
      MetaBackend *backend = meta_compositor_get_backend (compositor);
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);

      if (compositor_x11->have_x11_sync_object)
        compositor_x11->have_x11_sync_object = meta_sync_ring_after_frame (cogl_context);

      compositor_x11->frame_has_updated_xsurfaces = FALSE;
    }
}

static void
meta_change_button_grab (MetaCompositorX11   *compositor_x11,
                         MetaWindow          *window,
                         gboolean             grab,
                         MetaPassiveGrabMode  grab_mode,
                         int                  button,
                         int                  modmask)
{
  MetaBackend *backend =
    meta_compositor_get_backend (META_COMPOSITOR (compositor_x11));
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Window xwindow;

  xwindow = meta_window_x11_get_toplevel_xwindow (window);

  if (grab)
    {
      meta_backend_x11_passive_button_grab (backend_x11,
                                            xwindow,
                                            button,
                                            grab_mode,
                                            modmask);
    }
  else
    {
      meta_backend_x11_passive_button_ungrab (backend_x11,
                                              xwindow,
                                              button,
                                              modmask);
    }
}

static void
meta_change_buttons_grab (MetaCompositorX11   *compositor_x11,
                          MetaWindow          *window,
                          gboolean             grab,
                          MetaPassiveGrabMode  grab_mode,
                          int                  modmask)
{
#define MAX_BUTTON 3
  int i;

  /* Grab Alt + button1 for moving window.
   * Grab Alt + button2 for resizing window.
   * Grab Alt + button3 for popping up window menu.
   */
  for (i = 1; i <= MAX_BUTTON; i++)
    {
      meta_change_button_grab (compositor_x11, window, grab,
                               grab_mode, i, modmask);
    }

  /* Grab Alt + Shift + button1 for snap-moving window. */
  meta_change_button_grab (compositor_x11, window,
                           grab, grab_mode,
                           1, modmask | CLUTTER_SHIFT_MASK);

#undef MAX_BUTTON
}

static void
meta_compositor_x11_grab_window_buttons (MetaCompositorX11 *compositor_x11,
                                         MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  int modmask;

  meta_topic (META_DEBUG_X11, "Grabbing window buttons for %s", window->desc);

  modmask = meta_display_get_compositor_modifiers (display);

  if (modmask != 0)
    {
      meta_change_buttons_grab (compositor_x11, window, TRUE,
                                META_GRAB_MODE_ASYNC, modmask);
    }
}

static void
meta_compositor_x11_ungrab_window_buttons (MetaCompositorX11 *compositor_x11,
                                           MetaWindow        *window)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  int modmask;

  meta_topic (META_DEBUG_X11, "Ungrabbing window buttons for %s", window->desc);

  modmask = meta_display_get_compositor_modifiers (display);

  if (modmask != 0)
    {
      meta_change_buttons_grab (compositor_x11, window, FALSE,
                                META_GRAB_MODE_ASYNC, modmask);
    }
}

static void
meta_compositor_x11_grab_focus_window_button (MetaCompositorX11 *compositor_x11,
                                              MetaWindow        *window)
{
  /* Grab button 1 for activating unfocused windows */
  meta_topic (META_DEBUG_X11, "Grabbing unfocused window buttons for %s",
              window->desc);

  meta_change_buttons_grab (compositor_x11, window, TRUE,
                            META_GRAB_MODE_SYNC, 0);
}

static void
meta_compositor_x11_ungrab_focus_window_button (MetaCompositorX11 *compositor_x11,
                                                MetaWindow        *window)
{
  meta_topic (META_DEBUG_X11, "Ungrabbing unfocused window buttons for %s",
              window->desc);

  meta_change_buttons_grab (compositor_x11, window, FALSE,
                            META_GRAB_MODE_ASYNC, 0);
}

static void
meta_compositor_x11_change_keygrab (MetaCompositorX11    *compositor_x11,
                                    Window                xwindow,
                                    gboolean              grab,
                                    MetaResolvedKeyCombo *resolved_combo)
{
  MetaBackend *backend =
    meta_compositor_get_backend (META_COMPOSITOR (compositor_x11));
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  int i;

  for (i = 0; i < resolved_combo->len; i++)
    {
      xkb_keycode_t keycode = resolved_combo->keycodes[i];

      meta_topic (META_DEBUG_KEYBINDINGS,
                  "%s keybinding keycode %d mask 0x%x on 0x%lx",
                  grab ? "Grabbing" : "Ungrabbing",
                  keycode, resolved_combo->mask, xwindow);

      if (grab)
        {
          meta_backend_x11_passive_key_grab (backend_x11, xwindow,
                                             keycode,
                                             META_GRAB_MODE_SYNC,
                                             resolved_combo->mask);
        }
      else
        {
          meta_backend_x11_passive_key_ungrab (backend_x11, xwindow,
                                               keycode,
                                               resolved_combo->mask);
        }
    }
}

static void
passive_key_grab_foreach (MetaDisplay          *display,
                          MetaKeyBindingFlags   flags,
                          MetaResolvedKeyCombo *resolved_combo,
                          gpointer              user_data)
{
  MetaX11Display *x11_display = display->x11_display;
  Window xroot = x11_display->xroot;
  KeyGrabData *data = user_data;

  /* Ignore the key bindings marked as META_KEY_BINDING_NO_AUTO_GRAB. */
  if ((flags & META_KEY_BINDING_NO_AUTO_GRAB) != 0 &&
      data->grab)
    return;

  if ((flags & META_KEY_BINDING_PER_WINDOW) != 0 &&
      data->xwindow == xroot)
    return;

  meta_compositor_x11_change_keygrab (data->compositor_x11,
                                      data->xwindow,
                                      data->grab,
                                      resolved_combo);
}

static void
meta_compositor_x11_grab_window_keys (MetaCompositorX11 *compositor_x11,
                                      Window             xwindow)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  KeyGrabData data = { compositor_x11, xwindow, TRUE };

  meta_display_keybinding_foreach (display,
                                   passive_key_grab_foreach,
                                   &data);
}

static void
meta_compositor_x11_ungrab_window_keys (MetaCompositorX11 *compositor_x11,
                                        Window             xwindow)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  KeyGrabData data = { compositor_x11, xwindow, FALSE };

  meta_display_keybinding_foreach (display,
                                   passive_key_grab_foreach,
                                   &data);
}

static void
meta_compositor_x11_grab_root_window_keys (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  Window xroot = x11_display->xroot;
  KeyGrabData data = { compositor_x11, xroot, TRUE };

  if (compositor_x11->have_root_window_key_grab)
    return;

  meta_display_keybinding_foreach (display,
                                   passive_key_grab_foreach,
                                   &data);
  compositor_x11->have_root_window_key_grab = TRUE;
}

static void
meta_compositor_x11_ungrab_root_window_keys (MetaCompositorX11 *compositor_x11)
{
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaX11Display *x11_display = display->x11_display;
  Window xroot = x11_display->xroot;
  KeyGrabData data = { compositor_x11, xroot, FALSE };

  if (!compositor_x11->have_root_window_key_grab)
    return;

  meta_display_keybinding_foreach (display,
                                   passive_key_grab_foreach,
                                   &data);
  compositor_x11->have_root_window_key_grab = FALSE;
}

static gboolean
should_have_passive_grab (MetaWindow *window)
{
  return window->type != META_WINDOW_DOCK && !window->override_redirect;
}

static void
on_focus_window_change (MetaDisplay    *display,
                        GParamSpec     *pspec,
                        MetaCompositor *compositor)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaWindow *focus, *old_focus;
  gboolean needs_grab_change;

  old_focus = compositor_x11->focus_window;
  focus = meta_display_get_focus_window (display);

  if (focus && !should_have_passive_grab (focus))
    focus = NULL;

  if (focus == old_focus)
    return;

  needs_grab_change =
    (meta_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK ||
     !meta_prefs_get_raise_on_click());

  if (old_focus && needs_grab_change)
    {
      /* Restore passive grabs applying to out of focus windows */
      meta_compositor_x11_ungrab_window_buttons (compositor_x11, old_focus);
      meta_compositor_x11_grab_focus_window_button (compositor_x11, old_focus);
    }

  if (focus && needs_grab_change)
    {
      /* Ungrab click to focus button since the sync grab can interfere
       * with some things you might do inside the focused window, by
       * causing the client to get funky enter/leave events.
       *
       * The reason we usually have a passive grab on the window is
       * so that we can intercept clicks and raise the window in
       * response. For click-to-focus we don't need that since the
       * focused window is already raised. When raise_on_click is
       * FALSE we also don't need that since we don't do anything
       * when the window is clicked.
       *
       * There is dicussion in bugs 102209, 115072, and 461577
       */
      meta_compositor_x11_ungrab_focus_window_button (compositor_x11, focus);
      meta_compositor_x11_grab_window_buttons (compositor_x11, focus);
    }

  compositor_x11->focus_window = focus;
}

static void
on_window_type_changed (MetaWindow        *window,
                        GParamSpec        *pspec,
                        MetaCompositorX11 *compositor_x11)
{
  Window xwindow;

  xwindow = meta_window_x11_get_toplevel_xwindow (window);

  if (should_have_passive_grab (window))
    meta_compositor_x11_grab_window_keys (compositor_x11, xwindow);
  else
    meta_compositor_x11_ungrab_window_keys (compositor_x11, xwindow);
}

static void
on_window_decorated_changed (MetaWindow        *window,
                             GParamSpec        *pspec,
                             MetaCompositorX11 *compositor_x11)
{
  Window old_effective_toplevel = None, xwindow;

  /* We must clean up the passive grab on the prior effective toplevel */
  if (window->decorated)
    {
      old_effective_toplevel = meta_window_x11_get_xwindow (window);
    }
  else
    {
      MetaFrame *frame;

      frame = meta_window_x11_get_frame (window);
      if (frame)
        old_effective_toplevel = meta_frame_get_xwindow (frame);
    }

  if (old_effective_toplevel != None)
    {
      meta_compositor_x11_ungrab_window_keys (compositor_x11,
                                              old_effective_toplevel);
    }

  xwindow = meta_window_x11_get_toplevel_xwindow (window);
  meta_compositor_x11_grab_window_keys (compositor_x11, xwindow);
}

static void
meta_compositor_x11_before_paint (MetaCompositor     *compositor,
                                  MetaCompositorView *compositor_view,
                                  ClutterFrame       *frame)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;

  maybe_unredirect_top_window (compositor_x11);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->before_paint (compositor, compositor_view, frame);

  /* We must sync after MetaCompositor's before_paint because that's the final
   * time XDamageSubtract may happen before painting (when it calls
   * meta_window_actor_x11_before_paint -> handle_updates ->
   * meta_surface_actor_x11_handle_updates). If a client was to redraw between
   * the last damage event and XDamageSubtract, and the bounding box of the
   * region didn't grow, then we will not receive a new damage report for it
   * (because XDamageReportBoundingBox). Then if we haven't synchronized again
   * and the same region doesn't change on subsequent frames, we have lost some
   * part of the update from the client. So to ensure the correct pixels get
   * composited we must sync at least once between XDamageSubtract and
   * compositing, which is here. More related documentation can be found in
   * maybe_do_sync.
   */

  maybe_do_sync (compositor);
}

static void
meta_compositor_x11_add_window (MetaCompositor *compositor,
                                MetaWindow     *window)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;
  Window xwindow;

  if (should_have_passive_grab (window))
    {
      xwindow = meta_window_x11_get_toplevel_xwindow (window);

      meta_compositor_x11_grab_focus_window_button (compositor_x11, window);
      meta_compositor_x11_grab_window_keys (compositor_x11, xwindow);

      g_signal_connect_object (window, "notify::window-type",
                               G_CALLBACK (on_window_type_changed),
                               compositor,
                               G_CONNECT_DEFAULT);
      g_signal_connect_object (window, "notify::decorated",
                               G_CALLBACK (on_window_decorated_changed),
                               compositor,
                               G_CONNECT_DEFAULT);
    }

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->add_window (compositor, window);
}

static void
meta_compositor_x11_remove_window (MetaCompositor *compositor,
                                   MetaWindow     *window)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaCompositorClass *parent_class;
  Window xwindow;

  if (compositor_x11->unredirected_window == window)
    set_unredirected_window (compositor_x11, NULL);

  if (window == compositor_x11->focus_window)
    {
      meta_compositor_x11_ungrab_window_buttons (compositor_x11, window);
      compositor_x11->focus_window = NULL;
    }
  else if (should_have_passive_grab (window))
    {
      meta_compositor_x11_ungrab_focus_window_button (compositor_x11, window);
    }

  xwindow = meta_window_x11_get_toplevel_xwindow (window);
  meta_compositor_x11_ungrab_window_keys (compositor_x11, xwindow);

  g_signal_handlers_disconnect_by_func (window, on_window_type_changed,
                                        compositor);
  g_signal_handlers_disconnect_by_func (window, on_window_decorated_changed,
                                        compositor);

  parent_class = META_COMPOSITOR_CLASS (meta_compositor_x11_parent_class);
  parent_class->remove_window (compositor, window);
}

static int64_t
meta_compositor_x11_monotonic_to_high_res_xserver_time (MetaCompositor *compositor,
                                                        int64_t         monotonic_time_us)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  int64_t now_us;

  if (compositor_x11->xserver_uses_monotonic_clock)
    return meta_translate_to_high_res_xserver_time (monotonic_time_us);

  now_us = g_get_monotonic_time ();

  if (compositor_x11->xserver_time_query_time_us == 0 ||
      now_us > (compositor_x11->xserver_time_query_time_us + s2us (10)))
    {
      MetaDisplay *display = meta_compositor_get_display (compositor);
      MetaX11Display *x11_display = display->x11_display;
      uint32_t xserver_time_ms;
      int64_t xserver_time_us;

      compositor_x11->xserver_time_query_time_us = now_us;

      xserver_time_ms =
        meta_x11_display_get_current_time_roundtrip (x11_display);
      xserver_time_us = ms2us (xserver_time_ms);
      compositor_x11->xserver_time_offset_us = xserver_time_us - now_us;
    }

  return monotonic_time_us + compositor_x11->xserver_time_offset_us;
}

static MetaCompositorView *
meta_compositor_x11_create_view (MetaCompositor   *compositor,
                                 ClutterStageView *stage_view)
{
  return meta_compositor_view_new (stage_view);
}

static gboolean
meta_compositor_x11_handle_event (MetaCompositor     *compositor,
                                  const ClutterEvent *event,
                                  MetaWindow         *event_window,
                                  MetaEventMode       mode_hint)
{
  MetaBackend *backend = meta_compositor_get_backend (compositor);
  ClutterEventType event_type = clutter_event_type (event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_KEY_PRESS)
    {
      meta_backend_x11_allow_events (META_BACKEND_X11 (backend),
                                     event, mode_hint);
    }

  if (event_window && !IS_GESTURE_EVENT (clutter_event_type (event)))
    return CLUTTER_EVENT_STOP;

  return CLUTTER_EVENT_PROPAGATE;
}

static void
meta_compositor_x11_notify_mapping_change (MetaCompositor   *compositor,
                                           MetaMappingType   type,
                                           MetaMappingState  state)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (compositor);
  MetaDisplay *display = meta_compositor_get_display (compositor);
  g_autoptr (GSList) windows = NULL;
  GSList *l;
  /* Ungrab before change, grab again after it */
  gboolean grab = state == META_MAPPING_STATE_POST_CHANGE;

  switch (type)
    {
    case META_MAPPING_TYPE_BUTTON:
      windows = meta_display_list_windows (display, META_LIST_DEFAULT);

      for (l = windows; l; l = l->next)
        {
          MetaWindow *window = l->data;
          void (*func) (MetaCompositorX11*, MetaWindow*);

          if (window == compositor_x11->focus_window)
            {
              func = grab ?
                meta_compositor_x11_grab_window_buttons :
                meta_compositor_x11_ungrab_window_buttons;
            }
          else
            {
              func = grab ?
                meta_compositor_x11_grab_focus_window_button :
                meta_compositor_x11_ungrab_focus_window_button;
            }

          func (compositor_x11, window);
        }

      break;
    case META_MAPPING_TYPE_KEY:
      windows = meta_display_list_windows (display, META_LIST_DEFAULT);

      if (grab)
        meta_compositor_x11_grab_root_window_keys (compositor_x11);
      else
        meta_compositor_x11_ungrab_root_window_keys (compositor_x11);

      for (l = windows; l; l = l->next)
        {
          MetaWindow *window = l->data;
          Window xwindow = meta_window_x11_get_toplevel_xwindow (window);

          if (grab)
            meta_compositor_x11_grab_window_keys (compositor_x11, xwindow);
          else
            meta_compositor_x11_ungrab_window_keys (compositor_x11, xwindow);
        }
      break;
    }
}

Window
meta_compositor_x11_get_output_xwindow (MetaCompositorX11 *compositor_x11)
{
  return compositor_x11->output;
}

MetaCompositorX11 *
meta_compositor_x11_new (MetaDisplay *display,
                         MetaBackend *backend)
{
  return g_object_new (META_TYPE_COMPOSITOR_X11,
                       "display", display,
                       "backend", backend,
                       NULL);
}

static void
meta_compositor_x11_constructed (GObject *object)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (object);
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  ClutterStage *stage = meta_compositor_get_stage (compositor);
  MetaDisplay *display = meta_compositor_get_display (compositor);

  compositor_x11->before_update_handler_id =
    g_signal_connect (stage, "before-update",
                      G_CALLBACK (on_before_update), compositor);
  compositor_x11->after_update_handler_id =
    g_signal_connect (stage, "after-update",
                      G_CALLBACK (on_after_update), compositor);

  compositor_x11->focus_window_handler_id =
    g_signal_connect (display, "notify::focus-window",
                      G_CALLBACK (on_focus_window_change), compositor);

  G_OBJECT_CLASS (meta_compositor_x11_parent_class)->constructed (object);
}

static void
meta_compositor_x11_dispose (GObject *object)
{
  MetaCompositorX11 *compositor_x11 = META_COMPOSITOR_X11 (object);
  MetaCompositor *compositor = META_COMPOSITOR (compositor_x11);
  ClutterStage *stage = meta_compositor_get_stage (compositor);
  MetaDisplay *display = meta_compositor_get_display (compositor);

  if (compositor_x11->have_x11_sync_object)
    {
      meta_sync_ring_destroy ();
      compositor_x11->have_x11_sync_object = FALSE;
    }

  g_clear_signal_handler (&compositor_x11->before_update_handler_id, stage);
  g_clear_signal_handler (&compositor_x11->after_update_handler_id, stage);
  g_clear_signal_handler (&compositor_x11->focus_window_handler_id, display);

  meta_compositor_x11_ungrab_root_window_keys (compositor_x11);

  G_OBJECT_CLASS (meta_compositor_x11_parent_class)->dispose (object);
}

static void
meta_compositor_x11_init (MetaCompositorX11 *compositor_x11)
{
}

static void
meta_compositor_x11_class_init (MetaCompositorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaCompositorClass *compositor_class = META_COMPOSITOR_CLASS (klass);

  object_class->constructed = meta_compositor_x11_constructed;
  object_class->dispose = meta_compositor_x11_dispose;

  compositor_class->manage = meta_compositor_x11_manage;
  compositor_class->unmanage = meta_compositor_x11_unmanage;
  compositor_class->before_paint = meta_compositor_x11_before_paint;
  compositor_class->add_window = meta_compositor_x11_add_window;
  compositor_class->remove_window = meta_compositor_x11_remove_window;
  compositor_class->monotonic_to_high_res_xserver_time =
   meta_compositor_x11_monotonic_to_high_res_xserver_time;
  compositor_class->create_view = meta_compositor_x11_create_view;
  compositor_class->handle_event = meta_compositor_x11_handle_event;
  compositor_class->notify_mapping_change =
    meta_compositor_x11_notify_mapping_change;
}
