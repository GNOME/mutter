/*
 * Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2015 Red Hat, Inc.
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

/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "config.h"

#include "wayland/meta-wayland-popup.h"

#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"

G_DEFINE_INTERFACE (MetaWaylandPopupSurface, meta_wayland_popup_surface,
                    G_TYPE_OBJECT);

struct _MetaWaylandPopupGrab
{
  MetaWaylandSeat *seat;
  MetaWaylandEventHandler *handler;

  struct wl_client       *grab_client;
  struct wl_list          all_popups;
};

struct _MetaWaylandPopup
{
  MetaWaylandPopupGrab *grab;
  MetaWaylandPopupSurface *popup_surface;
  struct wl_list link;
};

static void meta_wayland_popup_grab_finish (MetaWaylandPopupGrab *grab);

static void
meta_wayland_popup_surface_default_init (MetaWaylandPopupSurfaceInterface *iface)
{
}

static void
meta_wayland_popup_surface_done (MetaWaylandPopupSurface *popup_surface)
{
  META_WAYLAND_POPUP_SURFACE_GET_IFACE (popup_surface)->done (popup_surface);
}

static void
meta_wayland_popup_surface_dismiss (MetaWaylandPopupSurface *popup_surface)
{
  META_WAYLAND_POPUP_SURFACE_GET_IFACE (popup_surface)->dismiss (popup_surface);
}

static void
meta_wayland_popup_surface_finish (MetaWaylandPopupSurface *popup_surface)
{
  META_WAYLAND_POPUP_SURFACE_GET_IFACE (popup_surface)->finish (popup_surface);
}

static MetaWaylandSurface *
meta_wayland_popup_surface_get_surface (MetaWaylandPopupSurface *popup_surface)
{
  return META_WAYLAND_POPUP_SURFACE_GET_IFACE (popup_surface)->get_surface (popup_surface);
}

static MetaWaylandSurface *
popup_grab_get_focus_surface (MetaWaylandEventHandler *handler,
                              ClutterFocus            *focus,
                              gpointer                 user_data)
{
  MetaWaylandPopupGrab *popup_grab = user_data;
  MetaWaylandSurface *surface;

  if (CLUTTER_IS_KEY_FOCUS (focus) &&
      !wl_list_empty (&popup_grab->all_popups))
    {
      /* Keyboard focus must always go to the topmost surface */
      return meta_wayland_popup_grab_get_top_popup (popup_grab);
    }
  else
    {
      MetaWaylandInput *input = meta_wayland_seat_get_input (popup_grab->seat);

      surface = meta_wayland_event_handler_chain_up_get_focus_surface (handler,
                                                                       focus);

      if (!meta_wayland_input_is_current_handler (input, handler) ||
          (surface && surface->resource &&
           wl_resource_get_client (surface->resource) == popup_grab->grab_client))
        return surface;
    }

  return NULL;
}

static void
popup_grab_focus (MetaWaylandEventHandler *handler,
                  ClutterFocus            *focus,
                  MetaWaylandSurface      *surface,
                  gpointer                 user_data)
{
  meta_wayland_event_handler_chain_up_focus (handler, focus, surface);
}

static gboolean
popup_grab_release (MetaWaylandEventHandler *handler,
                    const ClutterEvent      *event,
                    gpointer                 user_data)
{
  MetaWaylandPopupGrab *popup_grab = user_data;
  gboolean close_popup;

  close_popup = __builtin_popcount (clutter_event_get_state (event) &
				    (CLUTTER_BUTTON1_MASK |
				     CLUTTER_BUTTON2_MASK |
				     CLUTTER_BUTTON3_MASK |
				     CLUTTER_BUTTON4_MASK |
				     CLUTTER_BUTTON5_MASK)) <= 1;

  if (close_popup)
    {
      MetaWaylandSeat *seat = popup_grab->seat;
      MetaContext *context =
        meta_wayland_compositor_get_context (seat->compositor);
      MetaBackend *backend = meta_context_get_backend (context);
      ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
      ClutterBackend *clutter_backend =
        meta_backend_get_clutter_backend (backend);
      ClutterFocus *focus;
      MetaWaylandSurface *surface;

      focus = CLUTTER_FOCUS (clutter_backend_get_sprite (clutter_backend,
                                                         stage, event));
      surface = meta_wayland_seat_get_current_surface (popup_grab->seat, focus);

      if (!surface ||
          wl_resource_get_client (surface->resource) != popup_grab->grab_client)
        {
          meta_wayland_popup_grab_finish (popup_grab);
          return CLUTTER_EVENT_STOP;
        }
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static MetaWaylandEventInterface popup_event_interface = {
  popup_grab_get_focus_surface,
  popup_grab_focus,
  NULL, /* motion */
  NULL, /* press */
  popup_grab_release,
};

MetaWaylandPopupGrab *
meta_wayland_popup_grab_create (MetaWaylandSeat         *seat,
                                MetaWaylandPopupSurface *popup_surface)
{
  MetaWaylandSurface *surface =
    meta_wayland_popup_surface_get_surface (popup_surface);
  struct wl_client *client = wl_resource_get_client (surface->resource);
  MetaWaylandInput *input = meta_wayland_seat_get_input (seat);
  MetaWaylandPopupGrab *grab;

  grab = g_new0 (MetaWaylandPopupGrab, 1);
  grab->seat = seat;
  grab->grab_client = client;
  wl_list_init (&grab->all_popups);

  grab->handler =
    meta_wayland_input_attach_event_handler (input,
                                             &popup_event_interface,
                                             TRUE, grab);

  return grab;
}

void
meta_wayland_popup_grab_finish (MetaWaylandPopupGrab *grab)
{
  MetaWaylandPopup *popup, *tmp;

  wl_list_for_each_safe (popup, tmp, &grab->all_popups, link)
    {
      MetaWaylandPopupSurface *popup_surface = popup->popup_surface;

      meta_wayland_popup_surface_done (popup_surface);
      meta_wayland_popup_destroy (popup);
      meta_wayland_popup_surface_finish (popup_surface);
    }
}

void
meta_wayland_popup_grab_destroy (MetaWaylandPopupGrab *grab)
{
  g_assert (wl_list_empty (&grab->all_popups));

  if (grab->handler)
    {
      MetaWaylandInput *input = meta_wayland_seat_get_input (grab->seat);

      meta_wayland_input_detach_event_handler (input, grab->handler);
      grab->handler = NULL;
    }

  g_free (grab);
}

gboolean
meta_wayland_popup_grab_has_popups (MetaWaylandPopupGrab *grab)
{
  return !wl_list_empty (&grab->all_popups);
}

MetaWaylandSurface *
meta_wayland_popup_grab_get_top_popup (MetaWaylandPopupGrab *grab)
{
  MetaWaylandPopup *popup;

  g_assert (!wl_list_empty (&grab->all_popups));
  popup = wl_container_of (grab->all_popups.next, popup, link);

  return meta_wayland_popup_surface_get_surface (popup->popup_surface);
}

void
meta_wayland_popup_destroy (MetaWaylandPopup *popup)
{
  meta_wayland_popup_surface_dismiss (popup->popup_surface);
  wl_list_remove (&popup->link);
  g_free (popup);
}

static void
meta_wayland_popup_grab_repick_keyboard_focus (MetaWaylandPopupGrab *popup_grab)
{
  MetaWaylandSeat *seat = popup_grab->seat;
  MetaContext *context =
    meta_wayland_compositor_get_context (seat->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterKeyFocus *key_focus;
  MetaWaylandInput *input;

  input = meta_wayland_seat_get_input (seat);
  key_focus = clutter_backend_get_key_focus (clutter_backend, stage);
  meta_wayland_input_invalidate_focus (input, CLUTTER_FOCUS (key_focus));
}

void
meta_wayland_popup_dismiss (MetaWaylandPopup *popup)
{
  MetaWaylandPopupSurface *popup_surface = popup->popup_surface;
  MetaWaylandPopupGrab *popup_grab = popup->grab;

  meta_wayland_popup_destroy (popup);

  if (wl_list_empty (&popup_grab->all_popups))
    meta_wayland_popup_surface_finish (popup_surface);
  else
    meta_wayland_popup_grab_repick_keyboard_focus (popup_grab);
}

MetaWaylandSurface *
meta_wayland_popup_get_top_popup (MetaWaylandPopup *popup)
{
  return meta_wayland_popup_grab_get_top_popup (popup->grab);
}

MetaWaylandPopup *
meta_wayland_popup_create (MetaWaylandPopupSurface *popup_surface,
                           MetaWaylandPopupGrab    *grab)
{
  MetaWaylandSurface *surface =
    meta_wayland_popup_surface_get_surface (popup_surface);
  MetaWaylandPopup *popup;

  /* Don't allow creating popups if the grab has a different client. */
  if (grab->grab_client != wl_resource_get_client (surface->resource))
    return NULL;

  popup = g_new0 (MetaWaylandPopup, 1);
  popup->grab = grab;
  popup->popup_surface = popup_surface;

  wl_list_insert (&grab->all_popups, &popup->link);

  meta_wayland_popup_grab_repick_keyboard_focus (grab);

  return popup;
}
