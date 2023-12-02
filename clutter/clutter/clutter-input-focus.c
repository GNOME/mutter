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

#include "clutter/clutter-input-focus.h"
#include "clutter/clutter-input-focus-private.h"
#include "clutter/clutter-input-method-private.h"

typedef struct _ClutterInputFocusPrivate ClutterInputFocusPrivate;

static void clutter_input_focus_delete_surrounding (ClutterInputFocus *focus,
                                                    int                offset,
                                                    guint              len);
static void clutter_input_focus_set_preedit_text (ClutterInputFocus *focus,
                                                  const gchar       *preedit,
                                                  unsigned int       cursor,
                                                  unsigned int       anchor);

struct _ClutterInputFocusPrivate
{
  ClutterInputMethod *im;
  char *preedit;
  ClutterPreeditResetMode mode;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterInputFocus, clutter_input_focus, G_TYPE_OBJECT)

static void
clutter_input_focus_real_focus_in (ClutterInputFocus  *focus,
                                   ClutterInputMethod *im)
{
  ClutterInputFocusPrivate *priv;

  priv = clutter_input_focus_get_instance_private (focus);
  priv->im = im;
}

static void
clutter_input_focus_real_focus_out (ClutterInputFocus  *focus)
{
  ClutterInputFocusPrivate *priv;

  priv = clutter_input_focus_get_instance_private (focus);
  priv->im = NULL;
}

static void
clutter_input_focus_finalize (GObject *object)
{
  ClutterInputFocus *focus = CLUTTER_INPUT_FOCUS (object);
  ClutterInputFocusPrivate *priv =
    clutter_input_focus_get_instance_private (focus);

  g_clear_pointer (&priv->preedit, g_free);

  G_OBJECT_CLASS (clutter_input_focus_parent_class)->finalize (object);
}

static void
clutter_input_focus_class_init (ClutterInputFocusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_input_focus_finalize;

  klass->focus_in = clutter_input_focus_real_focus_in;
  klass->focus_out = clutter_input_focus_real_focus_out;
}

static void
clutter_input_focus_init (ClutterInputFocus *focus)
{
}

gboolean
clutter_input_focus_is_focused (ClutterInputFocus *focus)
{
  ClutterInputFocusPrivate *priv;

  priv = clutter_input_focus_get_instance_private (focus);

  return !!priv->im;
}

void
clutter_input_focus_reset (ClutterInputFocus *focus)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  if (priv->preedit)
    {
      if (priv->mode == CLUTTER_PREEDIT_RESET_COMMIT)
        clutter_input_focus_commit (focus, priv->preedit);

      clutter_input_focus_set_preedit_text (focus, NULL, 0, 0);
      g_clear_pointer (&priv->preedit, g_free);
    }

  priv->mode = CLUTTER_PREEDIT_RESET_CLEAR;
  clutter_input_method_reset (priv->im);
}

void
clutter_input_focus_set_cursor_location (ClutterInputFocus     *focus,
                                         const graphene_rect_t *rect)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_cursor_location (priv->im, rect);
}

void
clutter_input_focus_set_surrounding (ClutterInputFocus *focus,
                                     const gchar       *text,
                                     guint              cursor,
                                     guint              anchor)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_surrounding (priv->im, text, cursor, anchor);
}

void
clutter_input_focus_set_content_hints (ClutterInputFocus            *focus,
                                       ClutterInputContentHintFlags  hints)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_content_hints (priv->im, hints);
}

void
clutter_input_focus_set_content_purpose (ClutterInputFocus          *focus,
                                         ClutterInputContentPurpose  purpose)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_content_purpose (priv->im, purpose);
}

gboolean
clutter_input_focus_filter_event (ClutterInputFocus  *focus,
                                  const ClutterEvent *event)
{
  ClutterInputFocusPrivate *priv;
  ClutterEventType event_type;

  g_return_val_if_fail (CLUTTER_IS_INPUT_FOCUS (focus), FALSE);
  g_return_val_if_fail (clutter_input_focus_is_focused (focus), FALSE);

  priv = clutter_input_focus_get_instance_private (focus);

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_KEY_PRESS ||
      event_type == CLUTTER_KEY_RELEASE)
    {
      return clutter_input_method_filter_key_event (priv->im, (ClutterKeyEvent *) event);
    }

  return FALSE;
}

gboolean
clutter_input_focus_process_event (ClutterInputFocus  *focus,
                                   const ClutterEvent *event)
{
  ClutterInputFocusPrivate *priv;
  ClutterEventType event_type;

  g_return_val_if_fail (CLUTTER_IS_INPUT_FOCUS (focus), FALSE);
  g_return_val_if_fail (clutter_input_focus_is_focused (focus), FALSE);

  priv = clutter_input_focus_get_instance_private (focus);

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_IM_COMMIT)
    {
      clutter_input_focus_commit (focus,
                                  clutter_event_get_im_text (event));
      return TRUE;
    }
  else if (event_type == CLUTTER_IM_DELETE)
    {
      int32_t offset;
      uint32_t len;

      clutter_event_get_im_location (event, &offset, NULL);
      len = clutter_event_get_im_delete_length (event);
      clutter_input_focus_delete_surrounding (focus, offset, len);
      return TRUE;
    }
  else if (event_type == CLUTTER_IM_PREEDIT)
    {
      int32_t offset, anchor;

      g_clear_pointer (&priv->preedit, g_free);
      priv->preedit = g_strdup (clutter_event_get_im_text (event));
      priv->mode = clutter_event_get_im_preedit_reset_mode (event);
      clutter_event_get_im_location (event, &offset, &anchor);
      clutter_input_focus_set_preedit_text (focus,
                                            priv->preedit,
                                            offset, anchor);
      return TRUE;
    }

  return FALSE;
}

void
clutter_input_focus_set_can_show_preedit (ClutterInputFocus *focus,
                                          gboolean           can_show_preedit)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_can_show_preedit (priv->im, can_show_preedit);
}

void
clutter_input_focus_set_input_panel_state (ClutterInputFocus      *focus,
                                           ClutterInputPanelState  state)
{
  ClutterInputFocusPrivate *priv;

  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (clutter_input_focus_is_focused (focus));

  priv = clutter_input_focus_get_instance_private (focus);

  clutter_input_method_set_input_panel_state (priv->im, state);
}

void
clutter_input_focus_focus_in (ClutterInputFocus  *focus,
                              ClutterInputMethod *im)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));
  g_return_if_fail (CLUTTER_IS_INPUT_METHOD (im));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->focus_in (focus, im);
}

void
clutter_input_focus_focus_out (ClutterInputFocus  *focus)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->focus_out (focus);
}

void
clutter_input_focus_commit (ClutterInputFocus *focus,
                            const gchar       *text)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->commit_text (focus, text);
}

void
clutter_input_focus_delete_surrounding (ClutterInputFocus *focus,
                                        int                offset,
                                        guint              len)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->delete_surrounding (focus, offset, len);
}

void
clutter_input_focus_request_surrounding (ClutterInputFocus *focus)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->request_surrounding (focus);
}

void
clutter_input_focus_set_preedit_text (ClutterInputFocus *focus,
                                      const gchar       *preedit,
                                      unsigned int       cursor,
                                      unsigned int       anchor)
{
  g_return_if_fail (CLUTTER_IS_INPUT_FOCUS (focus));

  CLUTTER_INPUT_FOCUS_GET_CLASS (focus)->set_preedit_text (focus, preedit,
                                                           cursor, anchor);
}
