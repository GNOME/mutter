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

#include "config.h"

#include "clutter/clutter-event-private.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-input-device-private.h"
#include "clutter/clutter-input-focus.h"
#include "clutter/clutter-input-method.h"
#include "clutter/clutter-input-method-private.h"
#include "clutter/clutter-input-focus-private.h"
#include "clutter/clutter-seat-private.h"

typedef struct _ClutterInputMethodPrivate ClutterInputMethodPrivate;

struct _ClutterInputMethodPrivate
{
  ClutterInputFocus *focus;
  ClutterInputContentHintFlags content_hints;
  ClutterInputContentPurpose content_purpose;
  ClutterInputActionFlags handled_actions;
  gboolean can_show_preedit;
};

enum
{
  COMMIT,
  DELETE_SURROUNDING,
  REQUEST_SURROUNDING,
  INPUT_PANEL_STATE,
  CURSOR_LOCATION_CHANGED,
  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_CONTENT_HINTS,
  PROP_CONTENT_PURPOSE,
  PROP_CAN_SHOW_PREEDIT,
  PROP_HANDLED_ACTIONS,
  N_PROPS
};

static guint signals[N_SIGNALS] = { 0 };
static GParamSpec *pspecs[N_PROPS] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterInputMethod, clutter_input_method, G_TYPE_OBJECT)

static void
clutter_input_method_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ClutterInputMethod *im = CLUTTER_INPUT_METHOD (object);

  switch (prop_id)
    {
    case PROP_CONTENT_HINTS:
      clutter_input_method_set_content_hints (im,
                                              g_value_get_flags (value));
      break;
    case PROP_CONTENT_PURPOSE:
      clutter_input_method_set_content_purpose (im,
                                                g_value_get_enum (value));
      break;
    case PROP_CAN_SHOW_PREEDIT:
      clutter_input_method_set_can_show_preedit (im,
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
  ClutterInputMethod *im;

  im = CLUTTER_INPUT_METHOD (object);
  priv = clutter_input_method_get_instance_private (im);

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
    case PROP_HANDLED_ACTIONS:
      g_value_set_flags (value, priv->handled_actions);
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
                  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_UINT);
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
  signals[CURSOR_LOCATION_CHANGED] =
    g_signal_new ("cursor-location-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, GRAPHENE_TYPE_RECT);

  pspecs[PROP_CONTENT_HINTS] =
    g_param_spec_flags ("content-hints", NULL, NULL,
                        CLUTTER_TYPE_INPUT_CONTENT_HINT_FLAGS, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);
  pspecs[PROP_CONTENT_PURPOSE] =
    g_param_spec_enum ("content-purpose", NULL, NULL,
                       CLUTTER_TYPE_INPUT_CONTENT_PURPOSE, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_EXPLICIT_NOTIFY);
  pspecs[PROP_CAN_SHOW_PREEDIT] =
    g_param_spec_boolean ("can-show-preedit", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);
  pspecs[PROP_HANDLED_ACTIONS] =
    g_param_spec_flags ("handled-actions", NULL, NULL,
                        CLUTTER_TYPE_INPUT_ACTION_FLAGS,
                        CLUTTER_INPUT_ACTION_FLAG_NONE,
                        G_PARAM_WRITABLE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

static void
clutter_input_method_init (ClutterInputMethod *im)
{
}

void
clutter_input_method_focus_in (ClutterInputMethod *im,
                               ClutterInputFocus  *focus)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  priv = clutter_input_method_get_instance_private (im);

  if (priv->focus == focus)
    return;

  if (priv->focus)
    clutter_input_method_focus_out (im);

  g_set_object (&priv->focus, focus);

  if (focus)
    {
      klass = CLUTTER_INPUT_METHOD_GET_CLASS (im);
      klass->focus_in (im, focus);

      clutter_input_focus_focus_in (priv->focus, im);
    }
}

void
clutter_input_method_focus_out (ClutterInputMethod *im)
{
  ClutterInputMethodPrivate *priv;
  ClutterInputMethodClass *klass;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);

  if (!priv->focus)
    return;

  clutter_input_focus_focus_out (priv->focus);
  g_clear_object (&priv->focus);

  klass = CLUTTER_INPUT_METHOD_GET_CLASS (im);
  klass->focus_out (im);
}

static void
clutter_input_method_put_im_event (ClutterInputMethod      *im,
                                   ClutterEventType         event_type,
                                   const char              *text,
                                   int32_t                  offset,
                                   int32_t                  anchor,
                                   uint32_t                 len,
                                   ClutterPreeditResetMode  mode,
                                   ClutterPreeditAttribute *preedit_hints,
                                   unsigned int             n_preedit_hints)
{
  ClutterEvent *event;
  ClutterContext *context = _clutter_context_get_default ();
  ClutterBackend *backend = clutter_context_get_backend (context);
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);

  event = clutter_event_im_new (event_type,
                                CLUTTER_EVENT_FLAG_INPUT_METHOD,
                                CLUTTER_CURRENT_TIME,
                                seat,
                                text,
                                offset,
                                anchor,
                                len,
                                mode,
                                preedit_hints, n_preedit_hints);

  clutter_event_put (event);
  clutter_event_free (event);
}

void
clutter_input_method_commit (ClutterInputMethod *im,
                             const gchar        *text)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_COMMIT, text, 0, 0, 0,
                                     CLUTTER_PREEDIT_RESET_CLEAR,
                                     NULL, 0);
}

void
clutter_input_method_delete_surrounding (ClutterInputMethod *im,
                                         int                 offset,
                                         guint               len)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_DELETE, NULL,
                                     offset, offset, len,
                                     CLUTTER_PREEDIT_RESET_CLEAR,
                                     NULL, 0);
}

void
clutter_input_method_request_surrounding (ClutterInputMethod *im)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);
  if (priv->focus)
    clutter_input_focus_request_surrounding (priv->focus);
}

/**
 * clutter_input_method_set_preedit_text_with_attrs:
 * @im: a #ClutterInputMethod
 * @preedit: (nullable): the preedit text, or %NULL
 * @cursor: the cursor offset in characters
 * @anchor: the anchor offset in characters
 * @mode: the reset mode
 * @preedit_hints: (array length=n_preedit_hints) (nullable): the preedit hints
 * @n_preedit_hints: the number of preedit hints
 *
 * Sets the preedit text on the current input focus. The @preedit_hints array
 * specifies the style hints applying to specific regions of the text.
 **/
void
clutter_input_method_set_preedit_text_with_attrs (ClutterInputMethod      *im,
                                                  const gchar             *preedit,
                                                  unsigned int             cursor,
                                                  unsigned int             anchor,
                                                  ClutterPreeditResetMode  mode,
                                                  ClutterPreeditAttribute *preedit_hints,
                                                  unsigned int             n_preedit_hints)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  clutter_input_method_put_im_event (im, CLUTTER_IM_PREEDIT, preedit,
                                     cursor, anchor, 0, mode,
                                     preedit_hints, n_preedit_hints);
}

/**
 * clutter_input_method_set_preedit_text:
 * @im: a #ClutterInputMethod
 * @preedit: (nullable): the preedit text, or %NULL
 * @cursor: the cursor offset in characters
 * @anchor: the anchor offset in characters
 * @mode: the reset mode
 *
 * Sets the preedit text on the current input focus.
 **/
void
clutter_input_method_set_preedit_text (ClutterInputMethod      *im,
                                       const gchar             *preedit,
                                       unsigned int             cursor,
                                       unsigned int             anchor,
                                       ClutterPreeditResetMode  mode)
{
  ClutterPreeditAttribute style;
  int n_style = 0;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  if (preedit)
    {
      style = (ClutterPreeditAttribute) {
        .hint = CLUTTER_PREEDIT_STYLE_WHOLE,
        .start = 0,
        .end = g_utf8_strlen (preedit, -1),
      };
      n_style++;
    }

  clutter_input_method_set_preedit_text_with_attrs (im, preedit,
                                                    cursor, anchor, mode,
                                                    &style, n_style);
}

void
clutter_input_method_notify_key_event (ClutterInputMethod *im,
                                       const ClutterEvent *event,
                                       gboolean            filtered)
{
  if (!filtered)
    {
      ClutterModifierSet raw_modifiers;
      ClutterEvent *copy;

      clutter_event_get_key_state (event,
                                   &raw_modifiers.pressed,
                                   &raw_modifiers.latched,
                                   &raw_modifiers.locked);

      /* XXX: we rely on the IM implementation to notify back of
       * key events in the exact same order they were given.
       */
      copy = clutter_event_key_new (clutter_event_type (event),
                                    clutter_event_get_flags (event) |
                                    CLUTTER_EVENT_FLAG_INPUT_METHOD,
                                    clutter_event_get_time_us (event),
                                    clutter_event_get_source_device (event),
                                    raw_modifiers,
                                    clutter_event_get_state (event),
                                    clutter_event_get_key_symbol (event),
                                    clutter_event_get_event_code (event),
                                    clutter_event_get_key_code (event),
                                    clutter_event_get_key_unicode (event));
      clutter_event_put (copy);
      clutter_event_free (copy);
    }
}

void
clutter_input_method_set_input_panel_state (ClutterInputMethod     *im,
                                            ClutterInputPanelState  state)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  g_signal_emit (im, signals[INPUT_PANEL_STATE], 0, state);
}

void
clutter_input_method_reset (ClutterInputMethod *im)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->reset (im);
}

void
clutter_input_method_set_cursor_location (ClutterInputMethod    *im,
                                          const graphene_rect_t *rect)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->set_cursor_location (im, rect);

  g_signal_emit (im, signals[CURSOR_LOCATION_CHANGED], 0, rect);
}

void
clutter_input_method_set_surrounding (ClutterInputMethod *im,
                                      const gchar        *text,
                                      guint               cursor,
                                      guint               anchor)
{
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_METHOD_GET_CLASS (im)->set_surrounding (im, text,
                                                        cursor, anchor);
}

void
clutter_input_method_set_content_hints (ClutterInputMethod           *im,
                                        ClutterInputContentHintFlags  hints)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);

  if (priv->content_hints == hints)
    return;

  priv->content_hints = hints;
  CLUTTER_INPUT_METHOD_GET_CLASS (im)->update_content_hints (im, hints);

  g_object_notify_by_pspec (G_OBJECT (im), pspecs[PROP_CONTENT_HINTS]);
}

void
clutter_input_method_set_content_purpose (ClutterInputMethod         *im,
                                          ClutterInputContentPurpose  purpose)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);

  if (priv->content_purpose == purpose)
    return;

  priv->content_purpose = purpose;
  CLUTTER_INPUT_METHOD_GET_CLASS (im)->update_content_purpose (im, purpose);

  g_object_notify_by_pspec (G_OBJECT (im), pspecs[PROP_CONTENT_PURPOSE]);
}

void
clutter_input_method_set_can_show_preedit (ClutterInputMethod *im,
                                           gboolean            can_show_preedit)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);

  if (priv->can_show_preedit == can_show_preedit)
    return;

  priv->can_show_preedit = can_show_preedit;

  g_object_notify_by_pspec (G_OBJECT (im), pspecs[PROP_CAN_SHOW_PREEDIT]);
}

gboolean
clutter_input_method_filter_key_event (ClutterInputMethod    *im,
                                       const ClutterKeyEvent *key)
{
  ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_GET_CLASS (im);

  g_return_val_if_fail (CLUTTER_IS_INPUT_METHOD (im), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  if (clutter_event_get_flags ((ClutterEvent *) key) & CLUTTER_EVENT_FLAG_INPUT_METHOD)
    return FALSE;
  if (!im_class->filter_key_event)
    return FALSE;

  return im_class->filter_key_event (im, (const ClutterEvent *) key);
}

void
clutter_input_method_forward_key (ClutterInputMethod *im,
                                  uint32_t            keyval,
                                  uint32_t            keycode,
                                  uint32_t            state,
                                  uint64_t            time_,
                                  gboolean            press)
{
  ClutterInputMethodPrivate *priv;
  ClutterEvent *event;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  priv = clutter_input_method_get_instance_private (im);
  if (!priv->focus)
    return;

  event = clutter_event_key_new (press ? CLUTTER_KEY_PRESS : CLUTTER_KEY_RELEASE,
                                 CLUTTER_EVENT_FLAG_INPUT_METHOD,
                                 time_,
                                 NULL,
                                 (ClutterModifierSet) { 0, },
                                 state,
                                 keyval,
                                 keycode - 8,
                                 keycode,
                                 clutter_keysym_to_unicode (keyval));

  clutter_event_put (event);
  clutter_event_free (event);
}

void
clutter_input_method_set_handled_actions (ClutterInputMethod      *im,
                                          ClutterInputActionFlags  actions)
{
  ClutterInputMethodClass *im_class = CLUTTER_INPUT_METHOD_GET_CLASS (im);
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));
  g_return_if_fail ((actions & ~(CLUTTER_INPUT_ACTION_FLAG_ALL)) == 0);

  priv = clutter_input_method_get_instance_private (im);

  if (priv->handled_actions == actions)
    return;

  if (im_class->set_handled_actions)
    im_class->set_handled_actions (im, actions);

  g_object_notify_by_pspec (G_OBJECT (im), pspecs[PROP_HANDLED_ACTIONS]);
}

void
clutter_input_method_trigger_action (ClutterInputMethod *im,
                                     ClutterInputAction  action)
{
  ClutterInputMethodPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));
  g_return_if_fail (action < CLUTTER_INPUT_ACTION_LAST);

  priv = clutter_input_method_get_instance_private (im);
  if (!priv->focus)
    return;

  if ((priv->handled_actions & (1 << action)) == 0)
    return;

  clutter_input_focus_trigger_action (priv->focus, action);
}
