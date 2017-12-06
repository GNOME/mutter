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

#ifndef __CLUTTER_INPUT_METHOD_H__
#define __CLUTTER_INPUT_METHOD_H__

#include <clutter/clutter.h>

#define CLUTTER_TYPE_INPUT_METHOD (clutter_input_method_get_type ())

CLUTTER_AVAILABLE_IN_MUTTER
G_DECLARE_DERIVABLE_TYPE (ClutterInputMethod, clutter_input_method,
                          CLUTTER, INPUT_METHOD, GObject)

typedef struct _ClutterInputMethodClass ClutterInputMethodClass;

struct _ClutterInputMethodClass
{
  GObjectClass parent_class;

  void (* focus_in) (ClutterInputMethod *method,
                     ClutterInputFocus  *actor);
  void (* focus_out) (ClutterInputMethod *method);

  void (* reset) (ClutterInputMethod *method);

  void (* set_cursor_location) (ClutterInputMethod          *method,
                                const ClutterRect           *rect);
  void (* set_surrounding) (ClutterInputMethod *method,
                            const gchar        *text,
                            guint               cursor,
                            guint               anchor);
  void (* update_content_hints) (ClutterInputMethod           *method,
                                 ClutterInputContentHintFlags  hint);
  void (* update_content_purpose) (ClutterInputMethod         *method,
                                   ClutterInputContentPurpose  purpose);

  gboolean (* filter_key_event) (ClutterInputMethod *method,
                                 const ClutterEvent *key);
};

CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_focus_in  (ClutterInputMethod *method,
                                     ClutterInputFocus  *focus);
CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_focus_out (ClutterInputMethod *method);

CLUTTER_AVAILABLE_IN_MUTTER
ClutterInputFocus * clutter_input_method_get_focus (ClutterInputMethod *method);

CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_commit (ClutterInputMethod *method,
                                  const gchar        *text);
CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_delete_surrounding (ClutterInputMethod *method,
                                              guint               offset,
                                              guint               len);
CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_request_surrounding (ClutterInputMethod *method);

CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_set_preedit_text (ClutterInputMethod *method,
                                            const gchar        *preedit,
                                            guint               cursor);

CLUTTER_AVAILABLE_IN_MUTTER
void clutter_input_method_notify_key_event (ClutterInputMethod *method,
                                            const ClutterEvent *event,
                                            gboolean            filtered);

#endif /* __CLUTTER_INPUT_METHOD_H__ */
