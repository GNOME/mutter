/*
 * Copyright (C) 2017,2018 Red Hat
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

#pragma once

#define CLUTTER_TYPE_INPUT_METHOD (clutter_input_method_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterInputMethod, clutter_input_method,
                          CLUTTER, INPUT_METHOD, GObject)

typedef struct _ClutterInputMethodClass ClutterInputMethodClass;

struct _ClutterInputMethodClass
{
  GObjectClass parent_class;

  void (* focus_in) (ClutterInputMethod *im,
                     ClutterInputFocus  *actor);
  void (* focus_out) (ClutterInputMethod *im);

  void (* reset) (ClutterInputMethod *im);

  void (* set_cursor_location) (ClutterInputMethod    *im,
                                const graphene_rect_t *rect);
  void (* set_surrounding) (ClutterInputMethod *im,
                            const gchar        *text,
                            guint               cursor,
                            guint               anchor);
  void (* update_content_hints) (ClutterInputMethod           *im,
                                 ClutterInputContentHintFlags  hint);
  void (* update_content_purpose) (ClutterInputMethod         *im,
                                   ClutterInputContentPurpose  purpose);

  gboolean (* filter_key_event) (ClutterInputMethod *im,
                                 const ClutterEvent *key);
};

CLUTTER_EXPORT
void clutter_input_method_focus_in  (ClutterInputMethod *im,
                                     ClutterInputFocus  *focus);
CLUTTER_EXPORT
void clutter_input_method_focus_out (ClutterInputMethod *im);

CLUTTER_EXPORT
void clutter_input_method_commit (ClutterInputMethod *im,
                                  const gchar        *text);
CLUTTER_EXPORT
void clutter_input_method_delete_surrounding (ClutterInputMethod *im,
                                              int                 offset,
                                              guint               len);
CLUTTER_EXPORT
void clutter_input_method_request_surrounding (ClutterInputMethod *im);

CLUTTER_EXPORT
void clutter_input_method_set_preedit_text (ClutterInputMethod      *im,
                                            const gchar             *preedit,
                                            unsigned int             cursor,
                                            unsigned int             anchor,
                                            ClutterPreeditResetMode  mode);

CLUTTER_EXPORT
void clutter_input_method_notify_key_event (ClutterInputMethod *im,
                                            const ClutterEvent *event,
                                            gboolean            filtered);
CLUTTER_EXPORT
void clutter_input_method_set_input_panel_state (ClutterInputMethod     *im,
                                                 ClutterInputPanelState  state);

CLUTTER_EXPORT
void clutter_input_method_forward_key (ClutterInputMethod *im,
                                       uint32_t            keyval,
                                       uint32_t            keycode,
                                       uint32_t            state,
                                       uint64_t            time_,
                                       gboolean            press);
