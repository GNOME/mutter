/*
 * Copyright (C) 2017 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <wayland-server.h>

#include "gtk-text-input-server-protocol.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-text-input.h"
#include "wayland/meta-wayland-versions.h"

static void meta_wayland_text_input_input_focus_iface_init (ClutterInputFocusInterface *iface);

typedef struct _MetaWaylandTextInput MetaWaylandTextInput;

struct _MetaWaylandTextInput
{
  GObject parent_instance;

  MetaWaylandSeat *seat;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;
  MetaWaylandSurface *surface;
  struct wl_listener surface_listener;
  uint32_t focus_serial;

  guint pending_state;

  struct {
    gchar *text;
    guint cursor;
    guint anchor;
  } surrounding;

  cairo_rectangle_int_t cursor_rect;

  guint content_type_hint;
  guint content_type_purpose;
};

enum {
  PROP_0,
  PROP_SEAT,
  N_PROPS
};

enum {
  PENDING_STATE_NONE             = 0,
  PENDING_STATE_INPUT_RECT       = 1 << 0,
  PENDING_STATE_CONTENT_TYPE     = 1 << 1,
  PENDING_STATE_SURROUNDING_TEXT = 1 << 2,
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_TYPE_WITH_CODE (MetaWaylandTextInput, meta_wayland_text_input,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_INPUT_FOCUS,
                                                meta_wayland_text_input_input_focus_iface_init))

static void
text_input_handle_focus_surface_destroy (struct wl_listener *listener,
					 void               *data)
{
  MetaWaylandTextInput *text_input = wl_container_of (listener, text_input,
						      surface_listener);

  meta_wayland_text_input_set_focus (text_input, NULL);
}

static void
meta_wayland_text_input_focus_in (ClutterInputFocus  *focus,
                                  ClutterInputMethod *method)
{
}

static void
meta_wayland_text_input_focus_out (ClutterInputFocus *focus)
{
}

static void
meta_wayland_text_input_request_surrounding (ClutterInputFocus *focus)
{
  MetaWaylandTextInput *text_input = META_WAYLAND_TEXT_INPUT (focus);

  clutter_input_focus_set_surrounding (focus,
				       text_input->surrounding.text,
				       text_input->surrounding.cursor,
                                       text_input->surrounding.anchor);
}

static void
meta_wayland_text_input_delete_surrounding (ClutterInputFocus *focus,
                                            guint              cursor,
                                            guint              len)
{
  MetaWaylandTextInput *input = META_WAYLAND_TEXT_INPUT (focus);
  struct wl_resource *resource;

  wl_resource_for_each (resource, &input->focus_resource_list)
    {
      gtk_text_input_send_delete_surrounding_text (resource, cursor, len);
    }
}

static void
meta_wayland_text_input_commit_text (ClutterInputFocus *focus,
                                     const gchar       *text)
{
  MetaWaylandTextInput *input = META_WAYLAND_TEXT_INPUT (focus);
  struct wl_resource *resource;

  wl_resource_for_each (resource, &input->focus_resource_list)
    {
      gtk_text_input_send_preedit_string (resource, NULL, 0);
      gtk_text_input_send_commit (resource, text);
    }

  clutter_input_focus_reset (focus);
}

static void
meta_wayland_text_input_set_preedit_text (ClutterInputFocus *focus,
                                          const gchar       *text,
                                          guint              cursor)
{
  MetaWaylandTextInput *input = META_WAYLAND_TEXT_INPUT (focus);
  struct wl_resource *resource;

  wl_resource_for_each (resource, &input->focus_resource_list)
    {
      gtk_text_input_send_preedit_string (resource, text, cursor);
    }
}

static void
meta_wayland_text_input_input_focus_iface_init (ClutterInputFocusInterface *iface)
{
  iface->focus_in = meta_wayland_text_input_focus_in;
  iface->focus_out = meta_wayland_text_input_focus_out;
  iface->request_surrounding = meta_wayland_text_input_request_surrounding;
  iface->delete_surrounding = meta_wayland_text_input_delete_surrounding;
  iface->commit_text = meta_wayland_text_input_commit_text;
  iface->set_preedit_text = meta_wayland_text_input_set_preedit_text;
}

static void
meta_wayland_text_input_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  MetaWaylandTextInput *text_input = META_WAYLAND_TEXT_INPUT (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      text_input->seat = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_wayland_text_input_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  MetaWaylandTextInput *text_input = META_WAYLAND_TEXT_INPUT (object);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_pointer (value, text_input->seat);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_wayland_text_input_finalize (GObject *object)
{
  MetaWaylandTextInput *text_input = META_WAYLAND_TEXT_INPUT (object);

  meta_wayland_text_input_set_focus (text_input, NULL);

  G_OBJECT_CLASS (meta_wayland_text_input_parent_class)->finalize (object);
}

static void
meta_wayland_text_input_class_init (MetaWaylandTextInputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_wayland_text_input_set_property;
  object_class->get_property = meta_wayland_text_input_get_property;
  object_class->finalize = meta_wayland_text_input_finalize;

  props[PROP_SEAT] = g_param_spec_pointer ("seat",
                                           "MetaWaylandSeat",
                                           "The seat",
                                           G_PARAM_READWRITE |
                                           G_PARAM_STATIC_STRINGS |
                                           G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_wayland_text_input_init (MetaWaylandTextInput *text_input)
{
  wl_list_init (&text_input->resource_list);
  wl_list_init (&text_input->focus_resource_list);
  text_input->surface_listener.notify = text_input_handle_focus_surface_destroy;
}

static void
move_resources (struct wl_list *destination, struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list *destination,
			   struct wl_list *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;
  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

void
meta_wayland_text_input_set_focus (MetaWaylandTextInput *text_input,
				   MetaWaylandSurface   *surface)
{
  if (text_input->surface == surface)
    return;

  text_input->pending_state = PENDING_STATE_NONE;

  if (text_input->surface)
    {
      if (!wl_list_empty (&text_input->focus_resource_list))
        {
          struct wl_resource *resource;
          uint32_t serial;

          clutter_input_focus_focus_out (CLUTTER_INPUT_FOCUS (text_input));
          serial = wl_display_next_serial (text_input->seat->wl_display);

          wl_resource_for_each (resource, &text_input->focus_resource_list)
            {
              gtk_text_input_send_preedit_string (resource, NULL, 0);
              gtk_text_input_send_leave (resource, serial,
                                         text_input->surface->resource);
            }

          move_resources (&text_input->resource_list,
                          &text_input->focus_resource_list);
        }

      wl_list_remove (&text_input->surface_listener.link);
      text_input->surface = NULL;
    }

  if (surface)
    {
      struct wl_resource *focus_surface_resource;

      text_input->surface = surface;
      focus_surface_resource = text_input->surface->resource;
      wl_resource_add_destroy_listener (focus_surface_resource,
                                        &text_input->surface_listener);

      move_resources_for_client (&text_input->focus_resource_list,
                                 &text_input->resource_list,
                                 wl_resource_get_client (focus_surface_resource));

      if (!wl_list_empty (&text_input->focus_resource_list))
        {
          struct wl_resource *resource;

          text_input->focus_serial =
            wl_display_next_serial (text_input->seat->wl_display);

          wl_resource_for_each (resource, &text_input->focus_resource_list)
            {
              gtk_text_input_send_enter (resource, text_input->focus_serial,
                                         surface->resource);
            }
        }
    }
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
text_input_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
text_input_enable (struct wl_client   *client,
                   struct wl_resource *resource,
                   uint32_t            serial,
                   uint32_t            flags)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);
  ClutterInputFocus *focus = CLUTTER_INPUT_FOCUS (text_input);
  gboolean show_preedit;

  if (serial != text_input->focus_serial)
    return;

  show_preedit = (flags & GTK_TEXT_INPUT_ENABLE_FLAGS_CAN_SHOW_PREEDIT) != 0;
  clutter_input_focus_set_can_show_preedit (focus, show_preedit);

  clutter_input_focus_focus_in (CLUTTER_INPUT_FOCUS (text_input));

  if (flags & GTK_TEXT_INPUT_ENABLE_FLAGS_TOGGLE_INPUT_PANEL)
    clutter_input_focus_request_toggle_input_panel (focus);
}

static void
text_input_disable (struct wl_client   *client,
                    struct wl_resource *resource,
                    uint32_t            serial)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (serial != text_input->focus_serial)
    return;

  clutter_input_focus_reset (CLUTTER_INPUT_FOCUS (text_input));
  clutter_input_focus_focus_out (CLUTTER_INPUT_FOCUS (text_input));
  text_input->pending_state = PENDING_STATE_NONE;
}

static void
text_input_set_surrounding_text (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 const char         *text,
                                 int32_t             cursor,
                                 int32_t             anchor)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  g_free (text_input->surrounding.text);
  text_input->surrounding.text = g_strdup (text);
  text_input->surrounding.cursor = cursor;
  text_input->surrounding.anchor = anchor;
  text_input->pending_state |= PENDING_STATE_SURROUNDING_TEXT;
}

static ClutterInputContentHintFlags
translate_hints (uint32_t hints)
{
  ClutterInputContentHintFlags clutter_hints = 0;

  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_COMPLETION)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_COMPLETION;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_SPELLCHECK)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_SPELLCHECK;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_AUTO_CAPITALIZATION)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_AUTO_CAPITALIZATION;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_LOWERCASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_LOWERCASE;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_UPPERCASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_UPPERCASE;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_TITLECASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_TITLECASE;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_HIDDEN_TEXT)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_HIDDEN_TEXT;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_SENSITIVE_DATA)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_SENSITIVE_DATA;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_LATIN)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_LATIN;
  if (hints & GTK_TEXT_INPUT_CONTENT_HINT_MULTILINE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_MULTILINE;

  return clutter_hints;
}

static ClutterInputContentPurpose
translate_purpose (uint32_t purpose)
{
  switch (purpose)
    {
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_NORMAL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NORMAL;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_ALPHA:
      return CLUTTER_INPUT_CONTENT_PURPOSE_ALPHA;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_DIGITS:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DIGITS;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_NUMBER:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NUMBER;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_PHONE:
      return CLUTTER_INPUT_CONTENT_PURPOSE_PHONE;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_URL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_URL;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_EMAIL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_EMAIL;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_NAME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NAME;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_PASSWORD:
      return CLUTTER_INPUT_CONTENT_PURPOSE_PASSWORD;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_DATE:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DATE;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_TIME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_TIME;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_DATETIME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DATETIME;
    case GTK_TEXT_INPUT_CONTENT_PURPOSE_TERMINAL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_TERMINAL;
    }

  g_warn_if_reached ();
  return CLUTTER_INPUT_CONTENT_PURPOSE_NORMAL;
}

static void
text_input_set_content_type (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            hint,
                             uint32_t            purpose)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (!text_input->surface)
    return;

  text_input->content_type_hint = hint;
  text_input->content_type_purpose = purpose;
  text_input->pending_state |= PENDING_STATE_CONTENT_TYPE;
}

static void
text_input_set_cursor_rectangle (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 int32_t             x,
                                 int32_t             y,
                                 int32_t             width,
                                 int32_t             height)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (!text_input->surface)
    return;

  text_input->cursor_rect = (cairo_rectangle_int_t) { x, y, width, height };
  text_input->pending_state |= PENDING_STATE_INPUT_RECT;
}

static void
text_input_commit_state (struct wl_client   *client,
                         struct wl_resource *resource,
                         uint32_t            serial)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (text_input->surface == NULL)
    return;
  if (serial != text_input->focus_serial)
    return;

  if (text_input->pending_state & PENDING_STATE_CONTENT_TYPE)
    {
      clutter_input_focus_set_content_hints (CLUTTER_INPUT_FOCUS (text_input),
                                             translate_hints (text_input->content_type_hint));
      clutter_input_focus_set_content_purpose (CLUTTER_INPUT_FOCUS (text_input),
                                               translate_purpose (text_input->content_type_purpose));
    }

  if (text_input->pending_state & PENDING_STATE_SURROUNDING_TEXT)
    {
      clutter_input_focus_set_surrounding (CLUTTER_INPUT_FOCUS (text_input),
                                           text_input->surrounding.text,
                                           text_input->surrounding.cursor,
                                           text_input->surrounding.anchor);
    }

  if (text_input->pending_state & PENDING_STATE_INPUT_RECT)
    {
      ClutterRect cursor_rect;
      float x1, y1, x2, y2;
      cairo_rectangle_int_t rect;

      rect = text_input->cursor_rect;
      meta_wayland_surface_get_absolute_coordinates (text_input->surface,
                                                     rect.x, rect.y, &x1, &y1);
      meta_wayland_surface_get_absolute_coordinates (text_input->surface,
                                                     rect.x + rect.width,
                                                     rect.y + rect.height,
                                                     &x2, &y2);

      clutter_rect_init (&cursor_rect, x1, y1, x2 - x1, y2 - y1);
      clutter_input_focus_set_cursor_location (CLUTTER_INPUT_FOCUS (text_input),
                                               &cursor_rect);
    }

  text_input->pending_state = PENDING_STATE_NONE;
}

static struct gtk_text_input_interface meta_text_input_interface = {
  text_input_destroy,
  text_input_enable,
  text_input_disable,
  text_input_set_surrounding_text,
  text_input_set_content_type,
  text_input_set_cursor_rectangle,
  text_input_commit_state,
};

MetaWaylandTextInput *
meta_wayland_text_input_new (MetaWaylandSeat *seat)
{
  return g_object_new (META_TYPE_WAYLAND_TEXT_INPUT,
                       "seat", seat,
                       NULL);
}

static void
meta_wayland_text_input_create_new_resource (MetaWaylandTextInput *text_input,
                                             struct wl_client     *client,
                                             struct wl_resource   *seat_resource,
                                             uint32_t              id)
{
  struct wl_resource *text_input_resource;

  text_input_resource = wl_resource_create (client,
                                            &gtk_text_input_interface,
                                            META_GTK_TEXT_INPUT_VERSION,
                                            id);

  wl_resource_set_implementation (text_input_resource,
                                  &meta_text_input_interface,
                                  text_input, unbind_resource);

  if (text_input->surface &&
      wl_resource_get_client (text_input->surface->resource) == client)
    {
      wl_list_insert (&text_input->focus_resource_list,
                      wl_resource_get_link (text_input_resource));

      gtk_text_input_send_enter (text_input_resource,
                                 text_input->focus_serial,
                                 text_input->surface->resource);
    }
  else
    {
      wl_list_insert (&text_input->resource_list,
                      wl_resource_get_link (text_input_resource));
    }
}

static void
text_input_manager_destroy (struct wl_client   *client,
                            struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
text_input_manager_get_text_input (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   uint32_t            id,
                                   struct wl_resource *seat_resource)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);

  meta_wayland_text_input_create_new_resource (seat->text_input,
                                               client, seat_resource, id);
}

static struct gtk_text_input_manager_interface meta_text_input_manager_interface = {
  text_input_manager_destroy,
  text_input_manager_get_text_input,
};

static void
bind_text_input (struct wl_client *client,
		 void             *data,
		 uint32_t          version,
		 uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &gtk_text_input_manager_interface,
				 META_GTK_TEXT_INPUT_VERSION,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_text_input_manager_interface,
                                  NULL, NULL);
}

gboolean
meta_wayland_text_input_init_global (MetaWaylandCompositor *compositor)
{
  return (wl_global_create (compositor->wayland_display,
                            &gtk_text_input_manager_interface,
                            META_GTK_TEXT_INPUT_VERSION, NULL,
                            bind_text_input) != NULL);
}
