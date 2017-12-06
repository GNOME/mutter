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

#include "clutter-private.h"
#include "clutter/clutter-input-method.h"

typedef struct _ClutterInputMethodPrivate ClutterInputMethodPrivate;

struct _ClutterInputMethodPrivate
{
  ClutterInputFocus *focus;
  ClutterInputContentHintFlags content_hints;
  ClutterInputContentPurpose content_purpose;
  gboolean can_show_preedit;
};

enum {
  COMMIT,
  DELETE_SURROUNDING,
  REQUEST_SURROUNDING,
  INPUT_PANEL_STATE,
  N_SIGNALS,
};

enum {
  PROP_0,
  PROP_CONTENT_HINTS,
  PROP_CONTENT_PURPOSE,
  PROP_CAN_SHOW_PREEDIT,
  N_PROPS
};

static guint signals[N_SIGNALS] = { 0 };
static GParamSpec *pspecs[N_PROPS] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterInputMethod, clutter_input_method, G_TYPE_OBJECT)

static void
set_content_hints (ClutterInputMethod           *method,
                   ClutterInputContentHintFlags  content_hints)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (method);
  priv->content_hints = content_hints;
  CLUTTER_INPUT_METHOD_GET_CLASS (method)->update_content_hints (method,
                                                                 content_hints);
}

static void
set_content_purpose (ClutterInputMethod         *method,
                     ClutterInputContentPurpose  content_purpose)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (method);
  priv->content_purpose = content_purpose;
  CLUTTER_INPUT_METHOD_GET_CLASS (method)->update_content_purpose (method,
                                                                   content_purpose);
}

static void
set_can_show_preedit (ClutterInputMethod *method,
                      gboolean            can_show_preedit)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (method);
  priv->can_show_preedit = can_show_preedit;
}

static void
clutter_input_method_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_CONTENT_HINTS:
      set_content_hints (CLUTTER_INPUT_METHOD (object),
                         g_value_get_flags (value));
      break;
    case PROP_CONTENT_PURPOSE:
      set_content_purpose (CLUTTER_INPUT_METHOD (object),
                           g_value_get_enum (value));
      break;
    case PROP_CAN_SHOW_PREEDIT:
      set_can_show_preedit (CLUTTER_INPUT_METHOD (object),
                            g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_input_method_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethod *method;

  method = CLUTTER_INPUT_METHOD (object);
  priv = clutter_input_method_get_instance_private (method);

  switch (prop_id)
    {
    case PROP_CONTENT_HINTS:
      g_value_set_flags (value, priv->content_hints);
      break;
    case PROP_CONTENT_PURPOSE:
      g_value_set_enum (value, priv->content_purpose);
      break;
    case PROP_CAN_SHOW_PREEDIT:
      g_value_set_boolean (value, priv->can_show_preedit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_input_method_class_init (ClutterInputMethodClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = clutter_input_method_set_property;
  object_class->get_property = clutter_input_method_get_property;

  signals[COMMIT] =
    g_signal_new ("commit",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[DELETE_SURROUNDING] =
    g_signal_new ("delete-surrounding",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
  signals[REQUEST_SURROUNDING] =
    g_signal_new ("request-surrounding",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[INPUT_PANEL_STATE] =
    g_signal_new ("input-panel-state",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_PANEL_STATE);

  pspecs[PROP_CONTENT_HINTS] =
    g_param_spec_flags ("content-hints",
                        P_("Content hints"),
                        P_("Content hints"),
                        CLUTTER_TYPE_INPUT_CONTENT_HINT_FLAGS, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);
  pspecs[PROP_CONTENT_PURPOSE] =
    g_param_spec_enum ("content-purpose",
                       P_("Content purpose"),
                       P_("Content purpose"),
                       CLUTTER_TYPE_INPUT_CONTENT_PURPOSE, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);
  pspecs[PROP_CAN_SHOW_PREEDIT] =
    g_param_spec_boolean ("can-show-preedit",
                          P_("Can show preedit"),
                          P_("Can show preedit"),
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

static void
clutter_input_method_init (ClutterInputMethod *method)
{
}

void
clutter_input_method_focus_in (ClutterInputMethod *method,
                               ClutterInputFocus  *focus)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  priv = clutter_input_method_get_instance_private (method);

  if (priv->focus == focus)
    return;

  if (priv->focus)
    clutter_input_method_focus_out (method);

  g_set_object (&priv->focus, focus);

  if (focus)
    {
      klass = CLUTTER_INPUT_METHOD_GET_CLASS (method);
      klass->focus_in (method, focus);

      g_signal_emit_by_name (priv->focus, "focus-in", method);
    }
}

void
clutter_input_method_focus_out (ClutterInputMethod *method)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  priv = clutter_input_method_get_instance_private (method);

  if (!priv->focus)
    return;

  g_signal_emit_by_name (priv->focus, "focus-out");
  g_clear_object (&priv->focus);

  klass = CLUTTER_INPUT_METHOD_GET_CLASS (method);
  klass->focus_out (method);

  g_signal_emit (method, signals[INPUT_PANEL_STATE],
                 0, CLUTTER_INPUT_PANEL_STATE_OFF);
}

/**
 * clutter_input_method_get_focus:
 * @method: the #ClutterInputMethod
 *
 * Retrieves the current focus of the input method, or %NULL
 * if there is none.
 *
 * Returns: (transfer none) (nullable): the current focus
 **/
ClutterInputFocus *
clutter_input_method_get_focus (ClutterInputMethod *method)
{
  ClutterInputMethodPrivate *priv;

  priv = clutter_input_method_get_instance_private (method);
  return priv->focus;
}

void
clutter_input_method_commit (ClutterInputMethod *method,
                             const gchar        *text)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  priv = clutter_input_method_get_instance_private (method);
  if (priv->focus)
    g_signal_emit_by_name (priv->focus, "commit", text);
}

void
clutter_input_method_delete_surrounding (ClutterInputMethod *method,
                                         guint               offset,
                                         guint               len)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  priv = clutter_input_method_get_instance_private (method);
  if (priv->focus)
    g_signal_emit_by_name (priv->focus, "delete-surrounding", offset, len);
}

void
clutter_input_method_request_surrounding (ClutterInputMethod *method)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  priv = clutter_input_method_get_instance_private (method);
  if (priv->focus)
    g_signal_emit_by_name (priv->focus, "request-surrounding");
}

/**
 * clutter_input_method_set_preedit_text:
 * @method: a #ClutterInputMethod
 * @preedit: (nullable): the preedit text, or %NULL
 *
 * Sets the preedit text on the current input focus.
 **/
void
clutter_input_method_set_preedit_text (ClutterInputMethod *method,
                                       const gchar        *preedit,
                                       guint               cursor)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (method));

  priv = clutter_input_method_get_instance_private (method);
  if (priv->focus)
    g_signal_emit_by_name (priv->focus, "set-preedit-text", preedit, cursor);
}

void
clutter_input_method_notify_key_event (ClutterInputMethod *method,
                                       const ClutterEvent *event,
                                       gboolean            filtered)
{
  if (!filtered)
    {
      ClutterEvent *copy;

      /* XXX: we rely on the IM implementation to notify back of
       * key events in the exact same order they were given.
       */
      copy = clutter_event_copy (event);
      clutter_event_set_flags (copy, clutter_event_get_flags (event) |
                               CLUTTER_EVENT_FLAG_INPUT_METHOD);
      clutter_event_put (copy);
      clutter_event_free (copy);
    }
}
