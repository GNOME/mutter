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

#include "clutter-build-config.h"

#include "clutter/clutter-input-focus.h"

enum {
  FOCUS_IN,
  FOCUS_OUT,
  COMMIT,
  DELETE_SURROUNDING,
  REQUEST_SURROUNDING,
  SET_PREEDIT_TEXT,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_INTERFACE (ClutterInputFocus, clutter_input_focus, G_TYPE_OBJECT)

static void
clutter_input_focus_default_init (ClutterInputFocusInterface *iface)
{
  signals[FOCUS_IN] =
    g_signal_new ("focus-in",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, focus_in),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, CLUTTER_TYPE_INPUT_METHOD);
  signals[FOCUS_OUT] =
    g_signal_new ("focus-out",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, focus_out),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[COMMIT] =
    g_signal_new ("commit",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, commit_text),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[DELETE_SURROUNDING] =
    g_signal_new ("delete-surrounding",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, delete_surrounding),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
  signals[REQUEST_SURROUNDING] =
    g_signal_new ("request-surrounding",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, request_surrounding),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[SET_PREEDIT_TEXT] =
    g_signal_new ("set-preedit-text",
                  CLUTTER_TYPE_INPUT_FOCUS,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterInputFocusInterface, set_preedit_text),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
}

void
clutter_input_focus_reset (ClutterInputFocus *focus)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) != focus)
    return;

  CLUTTER_INPUT_METHOD_GET_CLASS (method)->reset (method);
}

void
clutter_input_focus_set_cursor_location (ClutterInputFocus *focus,
                                         const ClutterRect *rect)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) != focus)
    return;

  CLUTTER_INPUT_METHOD_GET_CLASS (method)->set_cursor_location (method, rect);
}

void
clutter_input_focus_set_surrounding (ClutterInputFocus *focus,
                                     const gchar       *text,
                                     guint              cursor,
                                     guint              anchor)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) != focus)
    return;

  CLUTTER_INPUT_METHOD_GET_CLASS (method)->set_surrounding (method, text, cursor, anchor);
}

void
clutter_input_focus_set_content_hints (ClutterInputFocus            *focus,
                                       ClutterInputContentHintFlags  hints)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) != focus)
    return;

  g_object_set (G_OBJECT (method), "content-hints", hints, NULL);
}

void
clutter_input_focus_set_content_purpose (ClutterInputFocus          *focus,
                                         ClutterInputContentPurpose  purpose)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) != focus)
    return;

  g_object_set (G_OBJECT (method), "content-purpose", purpose, NULL);
}

void
clutter_input_focus_focus_in (ClutterInputFocus *focus)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  clutter_input_method_focus_in (method, focus);
}

void
clutter_input_focus_focus_out (ClutterInputFocus *focus)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  if (clutter_input_method_get_focus (method) == focus)
    clutter_input_method_focus_out (method);
}

gboolean
clutter_input_focus_filter_key_event (ClutterInputFocus     *focus,
                                      const ClutterKeyEvent *key)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  g_return_val_if_fail (CLUTTER_IS_INPUT_METHOD (method), FALSE);

  if (clutter_event_get_flags ((ClutterEvent *) key) & CLUTTER_EVENT_FLAG_INPUT_METHOD)
    return FALSE;

  if (clutter_input_method_get_focus (method) == focus)
    {
      ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_GET_CLASS (method);

      if (im_class->filter_key_event)
        return im_class->filter_key_event (method, (const ClutterEvent *) key);
    }

  return FALSE;
}

void
clutter_input_focus_set_can_show_preedit (ClutterInputFocus *focus,
                                          gboolean           can_show_preedit)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  if (clutter_input_method_get_focus (method) == focus)
    g_object_set (G_OBJECT (method), "can-show-preedit", can_show_preedit, NULL);
}

void
clutter_input_focus_request_toggle_input_panel (ClutterInputFocus *focus)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterInputMethod *method = clutter_backend_get_input_method (backend);

  if (clutter_input_method_get_focus (method) == focus)
    {
      g_signal_emit_by_name (method, "input-panel-state",
                             CLUTTER_INPUT_PANEL_STATE_TOGGLE);
    }
}
