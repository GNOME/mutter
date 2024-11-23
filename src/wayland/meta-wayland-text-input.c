/*
 * Copyright (C) 2017, 2018 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland/meta-wayland-text-input.h"

#include <wayland-server.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-versions.h"

#include "text-input-unstable-v3-server-protocol.h"

#define META_TYPE_WAYLAND_TEXT_INPUT_FOCUS (meta_wayland_text_input_focus_get_type ())

typedef enum
{
  META_WAYLAND_PENDING_STATE_NONE             = 0,
  META_WAYLAND_PENDING_STATE_INPUT_RECT       = 1 << 0,
  META_WAYLAND_PENDING_STATE_CONTENT_TYPE     = 1 << 1,
  META_WAYLAND_PENDING_STATE_SURROUNDING_TEXT = 1 << 2,
  META_WAYLAND_PENDING_STATE_CHANGE_CAUSE     = 1 << 3,
  META_WAYLAND_PENDING_STATE_ENABLED          = 1 << 4,
} MetaWaylandTextInputPendingState;

typedef struct _MetaWaylandTextInput MetaWaylandTextInput;

struct _MetaWaylandTextInput
{
  MetaWaylandSeat *seat;
  ClutterInputFocus *input_focus;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;
  MetaWaylandSurface *surface;
  struct wl_listener surface_listener;

  MetaWaylandTextInputPendingState pending_state;

  GHashTable *resource_serials;

  /* This saves the uncommitted middle state of surrounding text from client
   * between `set_surrounding_text` and `commit`, will be cleared after
   * committed.
   */
  struct
  {
    char *text;
    uint32_t cursor;
    uint32_t anchor;
  } pending_surrounding;

  /* This is the actual committed surrounding text after `commit`, we need this
   * to convert between char based offset and byte based offset.
   */
  struct
  {
    char *text;
    uint32_t cursor;
    uint32_t anchor;
  } surrounding;

  MtkRectangle cursor_rect;

  uint32_t content_type_hint;
  uint32_t content_type_purpose;
  uint32_t text_change_cause;
  gboolean enabled;

  struct
  {
    char *string;
    uint32_t cursor;
    uint32_t anchor;
    gboolean changed;
  } preedit;

  guint done_idle_id;
};

struct _MetaWaylandTextInputFocus
{
  ClutterInputFocus parent_instance;
  MetaWaylandTextInput *text_input;
};

G_DECLARE_FINAL_TYPE (MetaWaylandTextInputFocus, meta_wayland_text_input_focus,
                      META, WAYLAND_TEXT_INPUT_FOCUS, ClutterInputFocus)
G_DEFINE_TYPE (MetaWaylandTextInputFocus, meta_wayland_text_input_focus,
               CLUTTER_TYPE_INPUT_FOCUS)

static MetaBackend *
backend_from_text_input (MetaWaylandTextInput *text_input)
{
  MetaWaylandSeat *seat = text_input->seat;
  MetaWaylandCompositor *compositor = meta_wayland_seat_get_compositor (seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_backend (context);
}

static void
meta_wayland_text_input_focus_request_surrounding (ClutterInputFocus *focus)
{
  MetaWaylandTextInput *text_input;
  long cursor, anchor;

  /* Clutter uses char offsets but text-input-v3 uses byte offsets. */
  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;
  cursor = g_utf8_strlen (text_input->surrounding.text,
                          text_input->surrounding.cursor);
  anchor = g_utf8_strlen (text_input->surrounding.text,
                          text_input->surrounding.anchor);
  clutter_input_focus_set_surrounding (focus,
                                       text_input->surrounding.text,
                                       cursor,
                                       anchor);
}

static uint32_t
lookup_serial (MetaWaylandTextInput *text_input,
               struct wl_resource   *resource)
{
  return GPOINTER_TO_UINT (g_hash_table_lookup (text_input->resource_serials,
                                                resource));
}

static void
increment_serial (MetaWaylandTextInput *text_input,
                  struct wl_resource   *resource)
{
  uint32_t serial;

  serial = lookup_serial (text_input, resource);
  g_hash_table_insert (text_input->resource_serials, resource,
                       GUINT_TO_POINTER (serial + 1));
}

static void
clutter_input_focus_send_done (ClutterInputFocus *focus)
{
  MetaWaylandTextInput *text_input;
  struct wl_resource *resource;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;

  wl_resource_for_each (resource, &text_input->focus_resource_list)
    {
      if (text_input->preedit.string || text_input->preedit.changed)
        {
          zwp_text_input_v3_send_preedit_string (resource,
                                                 text_input->preedit.string,
                                                 text_input->preedit.cursor,
                                                 text_input->preedit.anchor);
          text_input->preedit.changed = FALSE;
        }

      zwp_text_input_v3_send_done (resource,
                                   lookup_serial (text_input, resource));
    }
}

static gboolean
done_idle_cb (gpointer user_data)
{
  ClutterInputFocus *focus = user_data;
  MetaWaylandTextInput *text_input;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;
  clutter_input_focus_send_done (focus);

  text_input->done_idle_id = 0;
  return G_SOURCE_REMOVE;
}

static void
meta_wayland_text_input_focus_defer_done (ClutterInputFocus *focus)
{
  MetaWaylandTextInput *text_input;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;

  if (text_input->done_idle_id != 0)
    return;

  /* This operates on 2 principles:
   * - IM operations come as individual ClutterEvents
   * - We want to run .done after them all. The slightly lower
   *   CLUTTER_PRIORITY_EVENTS + 1 priority should ensure we at least group
   *   all events seen so far.
   *
   * FIXME: .done may be delayed indefinitely if there's a high enough
   *        priority idle source in the main loop. It's unlikely that
   *        recurring idles run at this high priority though.
   */
  text_input->done_idle_id = g_idle_add_full (CLUTTER_PRIORITY_EVENTS + 1,
                                              done_idle_cb, focus, NULL);
}

static void
meta_wayland_text_input_focus_flush_done (ClutterInputFocus *focus)
{
  MetaWaylandTextInput *text_input;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;

  if (text_input->done_idle_id == 0)
    return;

  g_clear_handle_id (&text_input->done_idle_id, g_source_remove);
  clutter_input_focus_send_done (focus);
}

static void
meta_wayland_text_input_focus_delete_surrounding (ClutterInputFocus *focus,
                                                  int                offset,
                                                  guint              len)
{
  MetaWaylandTextInput *text_input;
  const char *start, *end;
  const char *before, *after;
  const char *cursor;
  uint32_t before_length;
  uint32_t after_length;
  struct wl_resource *resource;

  /* offset and len are counted by UTF-8 chars, but text_input_v3's lengths are
   * counted by bytes, so we convert UTF-8 char offsets to pointers here, this
   * needs the surrounding text
   */
  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;
  offset = MIN (offset, 0);

  start = text_input->surrounding.text;
  end = start + strlen (text_input->surrounding.text);
  cursor = start + text_input->surrounding.cursor;

  before = g_utf8_offset_to_pointer (cursor, offset);
  g_return_if_fail (before >= start);

  after = g_utf8_offset_to_pointer (cursor, offset + len);
  g_return_if_fail (after <= end);

  before_length = cursor - before;
  after_length = after - cursor;

  wl_resource_for_each (resource, &text_input->focus_resource_list)
    {
      zwp_text_input_v3_send_delete_surrounding_text (resource,
                                                      before_length,
                                                      after_length);
    }

  meta_wayland_text_input_focus_defer_done (focus);
}

static void
meta_wayland_text_input_focus_commit_text (ClutterInputFocus *focus,
                                           const gchar       *text)
{
  MetaWaylandTextInput *text_input;
  struct wl_resource *resource;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;

  wl_resource_for_each (resource, &text_input->focus_resource_list)
    {
      zwp_text_input_v3_send_preedit_string (resource, NULL, 0, 0);
      zwp_text_input_v3_send_commit_string (resource, text);
    }

  meta_wayland_text_input_focus_defer_done (focus);
}

static void
meta_wayland_text_input_focus_set_preedit_text (ClutterInputFocus *focus,
                                                const gchar       *text,
                                                unsigned int       cursor,
                                                unsigned int       anchor)
{
  MetaWaylandTextInput *text_input;
  gsize cursor_pos = 0, anchor_pos = 0;

  text_input = META_WAYLAND_TEXT_INPUT_FOCUS (focus)->text_input;

  g_clear_pointer (&text_input->preedit.string, g_free);
  text_input->preedit.string = g_strdup (text);

  if (text)
    {
      cursor_pos = g_utf8_offset_to_pointer (text, cursor) - text;
      anchor_pos = g_utf8_offset_to_pointer (text, anchor) - text;
    }

  text_input->preedit.cursor = cursor_pos;
  text_input->preedit.anchor = anchor_pos;
  text_input->preedit.changed = TRUE;

  meta_wayland_text_input_focus_defer_done (focus);
}

static void
meta_wayland_text_input_focus_class_init (MetaWaylandTextInputFocusClass *klass)
{
  ClutterInputFocusClass *focus_class = CLUTTER_INPUT_FOCUS_CLASS (klass);

  focus_class->request_surrounding = meta_wayland_text_input_focus_request_surrounding;
  focus_class->delete_surrounding = meta_wayland_text_input_focus_delete_surrounding;
  focus_class->commit_text = meta_wayland_text_input_focus_commit_text;
  focus_class->set_preedit_text = meta_wayland_text_input_focus_set_preedit_text;
}

static void
meta_wayland_text_input_focus_init (MetaWaylandTextInputFocus *focus)
{
}

static ClutterInputFocus *
meta_wayland_text_input_focus_new (MetaWaylandTextInput *text_input)
{
  MetaWaylandTextInputFocus *focus;

  focus = g_object_new (META_TYPE_WAYLAND_TEXT_INPUT_FOCUS, NULL);
  focus->text_input = text_input;

  return CLUTTER_INPUT_FOCUS (focus);
}

static void
text_input_handle_focus_surface_destroy (struct wl_listener *listener,
					 void               *data)
{
  MetaWaylandTextInput *text_input = wl_container_of (listener, text_input,
						      surface_listener);

  meta_wayland_text_input_set_focus (text_input, NULL);
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

  text_input->pending_state = META_WAYLAND_PENDING_STATE_NONE;

  if (text_input->surface)
    {
      if (!wl_list_empty (&text_input->focus_resource_list))
        {
          ClutterInputFocus *focus = text_input->input_focus;
          MetaBackend *backend = backend_from_text_input (text_input);
          ClutterBackend *clutter_backend =
            meta_backend_get_clutter_backend (backend);
          ClutterInputMethod *input_method;
          struct wl_resource *resource;

          if (clutter_input_focus_is_focused (focus))
            {
              input_method = clutter_backend_get_input_method (clutter_backend);
              clutter_input_focus_reset (focus);
              meta_wayland_text_input_focus_flush_done (focus);
              clutter_input_method_focus_out (input_method);
            }

          wl_resource_for_each (resource, &text_input->focus_resource_list)
            {
              zwp_text_input_v3_send_leave (resource,
                                            text_input->surface->resource);
            }

          move_resources (&text_input->resource_list,
                          &text_input->focus_resource_list);
        }

      wl_list_remove (&text_input->surface_listener.link);
      text_input->surface = NULL;
      /* Wayland set_surrounding_text() does not support to set null string
       * for applications with the non-supported surrounding text feature
       * and reset the values here with focus changes.
       */
      g_clear_pointer (&text_input->surrounding.text, g_free);
      text_input->surrounding.cursor = 0;
      text_input->surrounding.anchor = 0;
    }

  if (surface && surface->resource)
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

          wl_resource_for_each (resource, &text_input->focus_resource_list)
            {
              zwp_text_input_v3_send_enter (resource, surface->resource);
            }
        }
    }
}

static void
text_input_destructor (struct wl_resource *resource)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  g_hash_table_remove (text_input->resource_serials, resource);
  wl_list_remove (wl_resource_get_link (resource));
}

static void
text_input_destroy (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static gboolean
client_matches_focus (MetaWaylandTextInput *text_input,
                      struct wl_client     *client)
{
  if (!text_input->surface)
    return FALSE;

  return client == wl_resource_get_client (text_input->surface->resource);
}

static void
text_input_enable (struct wl_client   *client,
                   struct wl_resource *resource)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (!client_matches_focus (text_input, client))
    return;

  text_input->enabled = TRUE;
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_ENABLED;
}

static void
text_input_disable (struct wl_client   *client,
                    struct wl_resource *resource)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (!client_matches_focus (text_input, client))
    return;

  text_input->enabled = FALSE;
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_ENABLED;
}

static void
text_input_set_surrounding_text (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 const char         *text,
                                 int32_t             cursor,
                                 int32_t             anchor)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);
  size_t text_len = strlen (text);

  if (!client_matches_focus (text_input, client))
    return;

  if (cursor < 0 || anchor < 0 || cursor > text_len || anchor > text_len)
    {
      g_warning ("Client sent invalid surrounding text (text_len=%lu, cursor=%d, "
                 "anchor=%d), ignoring", text_len, cursor, anchor);
      return;
    }

  g_free (text_input->pending_surrounding.text);
  text_input->pending_surrounding.text = g_strdup (text);
  text_input->pending_surrounding.cursor = cursor;
  text_input->pending_surrounding.anchor = anchor;
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_SURROUNDING_TEXT;
}

static void
text_input_set_text_change_cause (struct wl_client   *client,
                                  struct wl_resource *resource,
                                  uint32_t            cause)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);

  if (!client_matches_focus (text_input, client))
    return;

  text_input->text_change_cause = cause;
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_CHANGE_CAUSE;
}

static ClutterInputContentHintFlags
translate_hints (uint32_t hints)
{
  ClutterInputContentHintFlags clutter_hints = 0;

  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_COMPLETION;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_SPELLCHECK;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_AUTO_CAPITALIZATION)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_AUTO_CAPITALIZATION;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_LOWERCASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_LOWERCASE;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_UPPERCASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_UPPERCASE;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_TITLECASE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_TITLECASE;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_HIDDEN_TEXT;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_SENSITIVE_DATA;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_LATIN)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_LATIN;
  if (hints & ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE)
    clutter_hints |= CLUTTER_INPUT_CONTENT_HINT_MULTILINE;

  return clutter_hints;
}

static ClutterInputContentPurpose
translate_purpose (uint32_t purpose)
{
  switch (purpose)
    {
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NORMAL;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_ALPHA:
      return CLUTTER_INPUT_CONTENT_PURPOSE_ALPHA;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DIGITS;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NUMBER;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE:
      return CLUTTER_INPUT_CONTENT_PURPOSE_PHONE;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_URL;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL:
      return CLUTTER_INPUT_CONTENT_PURPOSE_EMAIL;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NAME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_NAME;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD:
      return CLUTTER_INPUT_CONTENT_PURPOSE_PASSWORD;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATE:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DATE;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TIME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_TIME;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DATETIME:
      return CLUTTER_INPUT_CONTENT_PURPOSE_DATETIME;
    case ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL:
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

  if (!client_matches_focus (text_input, client))
    return;

  text_input->content_type_hint = hint;
  text_input->content_type_purpose = purpose;
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_CONTENT_TYPE;
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

  if (!client_matches_focus (text_input, client))
    return;

  text_input->cursor_rect = (MtkRectangle) { x, y, width, height };
  text_input->pending_state |= META_WAYLAND_PENDING_STATE_INPUT_RECT;
}

static void
meta_wayland_text_input_reset (MetaWaylandTextInput *text_input)
{
  g_clear_pointer (&text_input->pending_surrounding.text, g_free);
  text_input->content_type_hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;
  text_input->content_type_purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
  text_input->text_change_cause = ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD;
  text_input->cursor_rect = (MtkRectangle) { 0, 0, 0, 0 };
  text_input->pending_state = META_WAYLAND_PENDING_STATE_NONE;
}

static void
text_input_commit_state (struct wl_client   *client,
                         struct wl_resource *resource)
{
  MetaWaylandTextInput *text_input = wl_resource_get_user_data (resource);
  ClutterInputFocus *focus = text_input->input_focus;
  gboolean enable_panel = FALSE;
  MetaBackend *backend = backend_from_text_input (text_input);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterInputMethod *input_method;

  increment_serial (text_input, resource);

  if (!client_matches_focus (text_input, client))
    return;

  input_method = clutter_backend_get_input_method (clutter_backend);

  if (input_method &&
      text_input->pending_state & META_WAYLAND_PENDING_STATE_ENABLED)
    {
      if (text_input->enabled)
        {
          if (!clutter_input_focus_is_focused (focus))
            clutter_input_method_focus_in (input_method, focus);
          else
            enable_panel = TRUE;

          clutter_input_focus_set_can_show_preedit (focus, TRUE);
        }
      else if (clutter_input_focus_is_focused (focus))
        {
          text_input->pending_state = META_WAYLAND_PENDING_STATE_NONE;
          clutter_input_focus_reset (text_input->input_focus);
          clutter_input_method_focus_out (input_method);
        }
    }

  if (!clutter_input_focus_is_focused (focus))
    {
      meta_wayland_text_input_reset (text_input);
      return;
    }

  if (text_input->pending_state & META_WAYLAND_PENDING_STATE_CONTENT_TYPE)
    {
      clutter_input_focus_set_content_hints (text_input->input_focus,
                                             translate_hints (text_input->content_type_hint));
      clutter_input_focus_set_content_purpose (text_input->input_focus,
                                               translate_purpose (text_input->content_type_purpose));
    }

  if (text_input->pending_state & META_WAYLAND_PENDING_STATE_SURROUNDING_TEXT)
    {
      long cursor, anchor;

      /* Save the surrounding text for `delete_surrounding_text`. */
      g_free (text_input->surrounding.text);
      text_input->surrounding.text = g_steal_pointer (&text_input->pending_surrounding.text);
      text_input->surrounding.cursor = text_input->pending_surrounding.cursor;
      text_input->surrounding.anchor = text_input->pending_surrounding.anchor;

      /* Pass the surrounding text to Clutter to handle it with input method. */
      /* Clutter uses char offsets but text-input-v3 uses byte offsets. */
      cursor = g_utf8_strlen (text_input->surrounding.text,
                              text_input->surrounding.cursor);
      anchor = g_utf8_strlen (text_input->surrounding.text,
                              text_input->surrounding.anchor);
      clutter_input_focus_set_surrounding (text_input->input_focus,
                                           text_input->surrounding.text,
                                           cursor,
                                           anchor);
    }

  if (text_input->pending_state & META_WAYLAND_PENDING_STATE_INPUT_RECT)
    {
      graphene_rect_t cursor_rect;
      float x1, y1, x2, y2;
      MtkRectangle rect;

      rect = text_input->cursor_rect;
      meta_wayland_surface_get_absolute_coordinates (text_input->surface,
                                                     rect.x, rect.y, &x1, &y1);
      meta_wayland_surface_get_absolute_coordinates (text_input->surface,
                                                     rect.x + rect.width,
                                                     rect.y + rect.height,
                                                     &x2, &y2);

      graphene_rect_init (&cursor_rect, x1, y1, x2 - x1, y2 - y1);
      clutter_input_focus_set_cursor_location (text_input->input_focus,
                                               &cursor_rect);
    }

  meta_wayland_text_input_reset (text_input);

  if (enable_panel)
    clutter_input_focus_set_input_panel_state (focus, CLUTTER_INPUT_PANEL_STATE_ON);

  meta_wayland_text_input_focus_defer_done (focus);
}

static struct zwp_text_input_v3_interface meta_text_input_interface = {
  text_input_destroy,
  text_input_enable,
  text_input_disable,
  text_input_set_surrounding_text,
  text_input_set_text_change_cause,
  text_input_set_content_type,
  text_input_set_cursor_rectangle,
  text_input_commit_state,
};

MetaWaylandTextInput *
meta_wayland_text_input_new (MetaWaylandSeat *seat)
{
  MetaWaylandTextInput *text_input;

  text_input = g_new0 (MetaWaylandTextInput, 1);
  text_input->input_focus = meta_wayland_text_input_focus_new (text_input);
  text_input->seat = seat;

  wl_list_init (&text_input->resource_list);
  wl_list_init (&text_input->focus_resource_list);
  text_input->surface_listener.notify = text_input_handle_focus_surface_destroy;

  text_input->resource_serials = g_hash_table_new (NULL, NULL);

  return text_input;
}

void
meta_wayland_text_input_destroy (MetaWaylandTextInput *text_input)
{
  meta_wayland_text_input_set_focus (text_input, NULL);
  g_object_unref (text_input->input_focus);
  g_hash_table_destroy (text_input->resource_serials);
  g_clear_pointer (&text_input->preedit.string, g_free);
  g_clear_pointer (&text_input->pending_surrounding.text, g_free);
  g_clear_pointer (&text_input->surrounding.text, g_free);
  g_free (text_input);
}

static void
meta_wayland_text_input_create_new_resource (MetaWaylandTextInput *text_input,
                                             struct wl_client     *client,
                                             struct wl_resource   *seat_resource,
                                             uint32_t              id)
{
  struct wl_resource *text_input_resource;

  text_input_resource = wl_resource_create (client,
                                            &zwp_text_input_v3_interface,
                                            META_ZWP_TEXT_INPUT_V3_VERSION,
                                            id);

  wl_resource_set_implementation (text_input_resource,
                                  &meta_text_input_interface,
                                  text_input, text_input_destructor);

  if (text_input->surface &&
      wl_resource_get_client (text_input->surface->resource) == client)
    {
      wl_list_insert (&text_input->focus_resource_list,
                      wl_resource_get_link (text_input_resource));

      zwp_text_input_v3_send_enter (text_input_resource,
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

  meta_wayland_text_input_create_new_resource (seat->text_input, client,
                                               seat_resource, id);
}

static struct zwp_text_input_manager_v3_interface meta_text_input_manager_interface = {
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
                                 &zwp_text_input_manager_v3_interface,
                                 META_ZWP_TEXT_INPUT_V3_VERSION,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_text_input_manager_interface,
                                  NULL, NULL);
}

gboolean
meta_wayland_text_input_init (MetaWaylandCompositor *compositor)
{
  return (wl_global_create (compositor->wayland_display,
                            &zwp_text_input_manager_v3_interface,
                            META_ZWP_TEXT_INPUT_V3_VERSION,
                            compositor->seat->text_input,
                            bind_text_input) != NULL);
}

gboolean
meta_wayland_text_input_update (MetaWaylandTextInput *text_input,
                                const ClutterEvent   *event)
{
  ClutterEventType event_type;

  if (!text_input->surface ||
      !clutter_input_focus_is_focused (text_input->input_focus))
    return FALSE;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_KEY_PRESS ||
      event_type == CLUTTER_KEY_RELEASE)
    {
      gboolean filtered = FALSE;

      filtered = clutter_input_focus_filter_event (text_input->input_focus, event);
      if (!filtered)
        meta_wayland_text_input_focus_flush_done (text_input->input_focus);

      return filtered;
    }

  return FALSE;
}

gboolean
meta_wayland_text_input_handle_event (MetaWaylandTextInput *text_input,
                                      const ClutterEvent   *event)
{
  ClutterEventType event_type;
  gboolean retval;

  if (!text_input->surface ||
      !clutter_input_focus_is_focused (text_input->input_focus))
    return FALSE;

  event_type = clutter_event_type (event);

  retval = clutter_input_focus_process_event (text_input->input_focus, event);

  if (event_type == CLUTTER_BUTTON_PRESS ||
      event_type == CLUTTER_TOUCH_BEGIN)
    {
      MetaWaylandSurface *surface = NULL;
      MetaBackend *backend;
      ClutterStage *stage;
      ClutterActor *actor;

      backend = backend_from_text_input (text_input);
      stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

      actor = clutter_stage_get_device_actor (stage,
                                              clutter_event_get_device (event),
                                              clutter_event_get_event_sequence (event));

      if (META_IS_SURFACE_ACTOR_WAYLAND (actor))
        {
          MetaSurfaceActorWayland *actor_wayland =
            META_SURFACE_ACTOR_WAYLAND (actor);

          surface = meta_surface_actor_wayland_get_surface (actor_wayland);

          if (surface == text_input->surface)
            {
              clutter_input_focus_reset (text_input->input_focus);
              meta_wayland_text_input_focus_flush_done (text_input->input_focus);
            }
        }
    }

  return retval;
}
