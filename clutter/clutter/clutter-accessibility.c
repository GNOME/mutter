/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * Based on GailUtil from GAIL
 * Copyright 2001, 2002, 2003 Sun Microsystems Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "clutter/clutter-accessibility-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-stage-manager-accessible-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-private.h"
#ifdef HAVE_FONTS
#include "clutter/clutter-pango.h"
#endif

#define DEFAULT_PASSWORD_CHAR '*'

/* This is just a copy of the Gail one, a shared library or place to
   define it could be a good idea. */
typedef struct _KeyEventInfo
{
  AtkKeySnoopFunc listener;
  gpointer func_data;
} KeyEventInfo;

static AtkObject*root = NULL;
static GHashTable *key_listener_list = NULL;

/* ------------------------------ ATK UTIL METHODS -------------------------- */

static AtkObject*
clutter_accessibility_get_root (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterStageManager *stage_manager =
    clutter_context_get_stage_manager (context);

  if (!root)
    root = clutter_stage_manager_accessible_new (stage_manager);

  return root;
}

static const gchar *
clutter_accessibility_get_toolkit_name (void)
{
  return "clutter";
}

static const gchar *
clutter_accessibility_get_toolkit_version (void)
{
  return VERSION;
}

static guint
clutter_accessibility_add_key_event_listener (AtkKeySnoopFunc listener,
                                              gpointer        data)
{
  static guint key = 1;
  KeyEventInfo *event_info = NULL;

  if (!key_listener_list)
    key_listener_list = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  event_info = g_new (KeyEventInfo, 1);
  event_info->listener = listener;
  event_info->func_data = data;

  g_hash_table_insert (key_listener_list, GUINT_TO_POINTER (key++), event_info);
  /* XXX: we don't check to see if n_listeners > MAXUINT */
  return key - 1;
}

static void
clutter_accessibility_remove_key_event_listener (guint remove_listener)
{
  if (!g_hash_table_remove (key_listener_list, GUINT_TO_POINTER (remove_listener))) {
      g_warning ("Not able to remove listener with id %i", remove_listener);
    }

  if (g_hash_table_size (key_listener_list) == 0)
    {
      g_hash_table_destroy (key_listener_list);
      key_listener_list = NULL;
    }
}

/* ------------------------------ PRIVATE FUNCTIONS ------------------------- */

static AtkKeyEventStruct *
atk_key_event_from_clutter_event_key (ClutterKeyEvent *clutter_event,
                                      gunichar         password_char)
{
  AtkKeyEventStruct *atk_event = g_new0 (AtkKeyEventStruct, 1);
  gunichar key_unichar;

  switch (clutter_event_type ((ClutterEvent *) clutter_event))
    {
    case CLUTTER_KEY_PRESS:
      atk_event->type = ATK_KEY_EVENT_PRESS;
      break;
    case CLUTTER_KEY_RELEASE:
      atk_event->type = ATK_KEY_EVENT_RELEASE;
      break;
    default:
      g_assert_not_reached ();
      return NULL;
    }

  if (password_char)
    atk_event->state = 0;
  else
    atk_event->state = clutter_event_get_state ((ClutterEvent *) clutter_event);

  /* We emit the clutter keyval. This is not exactly the one expected
     by AtkKeyEventStruct, as it expects a Gdk-like event, with the
     modifiers applied. But to avoid a dependency to gdk, we delegate
     that on the AT application.
     More information: Bug 1952 and bug 2072
  */
  if (password_char)
    atk_event->keyval = clutter_unicode_to_keysym (password_char);
  else
    atk_event->keyval = clutter_event_get_key_symbol ((ClutterEvent *) clutter_event);

  /* It is expected to store a key defining string here (ie "Space" in
     case you press a space). Anyway, there are no function on clutter
     to obtain that, and we want to avoid a gdk dependency here, so we
     delegate on the AT application to obtain that string using the
     rest of the data on the ATK event struct.

     More information: Bug 1952 and 2072
  */

  if (password_char)
    key_unichar = password_char;
  else
    key_unichar = clutter_event_get_key_unicode ((ClutterEvent *) clutter_event);

  if (g_unichar_validate (key_unichar) && !g_unichar_iscntrl (key_unichar))
    {
      GString *new = NULL;

      new = g_string_new ("");
      new = g_string_insert_unichar (new, 0, key_unichar);
      atk_event->string = g_string_free (new, FALSE);
    }
  else
    atk_event->string = NULL;

  atk_event->length = 0;

  /* Computing the hardware keycode from the password-char is
     difficult. But we are in a password situation. We are already a
     unichar that it is not the original one. Providing a "almost
     real" keycode is irrelevant */
  if (password_char)
    atk_event->keycode = 0;
  else
    atk_event->keycode = clutter_event_get_key_code ((ClutterEvent *) clutter_event);

  atk_event->timestamp = clutter_event_get_time ((ClutterEvent *) clutter_event);

#ifdef CLUTTER_ENABLE_DEBUG

  g_debug ("KeyEvent:\tsym 0x%x\n\t\tmods %x\n\t\tcode %u\n\t\ttime %lx \n\t\tstring %s\n",
           (unsigned int) atk_event->keyval,
           (unsigned int) atk_event->state,
           (unsigned int) atk_event->keycode,
           (unsigned long int) atk_event->timestamp,
           atk_event->string);
#endif

  return atk_event;
}


static gboolean
notify_hf (gpointer key,
           gpointer value,
           gpointer data)
{
  KeyEventInfo *info = (KeyEventInfo *) value;
  AtkKeyEventStruct *key_event = (AtkKeyEventStruct *)data;

  return (*(AtkKeySnoopFunc) info->listener) (key_event, info->func_data) ? TRUE : FALSE;
}

static void
insert_hf (gpointer key,
           gpointer value,
           gpointer data)
{
  GHashTable *new_table = (GHashTable *) data;
  g_hash_table_insert (new_table, key, value);
}


/*
 * 0 if the key of that event is visible, in other case the password
 * char
 */
static gunichar
check_key_visibility (ClutterStage *stage)
{
  AtkObject *accessible;
  ClutterActor *focus;

  focus = clutter_stage_get_key_focus (stage);
  if (focus == NULL)
    focus = CLUTTER_ACTOR (stage);
  accessible = clutter_actor_get_accessible (focus);

  g_return_val_if_fail (accessible != NULL, 0);

  if (atk_object_get_role (accessible) != ATK_ROLE_PASSWORD_TEXT)
    return 0;

  /* If it is a clutter text, we use his password char.  Note that
     although at Clutter toolkit itself, only ClutterText exposes a
     password role, nothing prevents on any derived toolkit (like st)
     to create a new actor that can behave like a password entry. And
     the key event will still be emitted here. Although in that case
     we would lose any password char from the derived toolkit, it is
     still better fill this with a default unichar that the original
     one */

#ifdef HAVE_FONTS
  if (CLUTTER_IS_TEXT (focus))
    return clutter_text_get_password_char (CLUTTER_TEXT (focus));
  else
#endif
    return DEFAULT_PASSWORD_CHAR;
}

gboolean
clutter_accessibility_snoop_key_event (ClutterStage    *stage,
                                       ClutterKeyEvent *key)
{
  ClutterEvent *event = (ClutterEvent *) key;
  AtkKeyEventStruct *key_event = NULL;
  ClutterEventType event_type;
  gboolean consumed = FALSE;
  gunichar password_char = 0;

  event_type = clutter_event_type (event);

  /* filter key events */
  if ((event_type != CLUTTER_KEY_PRESS) && (event_type != CLUTTER_KEY_RELEASE))
    return FALSE;

  if (key_listener_list)
    {
      GHashTable *new_hash = g_hash_table_new (NULL, NULL);

      g_hash_table_foreach (key_listener_list, insert_hf, new_hash);
      password_char = check_key_visibility (stage);
      key_event = atk_key_event_from_clutter_event_key (key, password_char);
      /* func data is inside the hash table */
      consumed = g_hash_table_foreach_steal (new_hash, notify_hf, key_event) > 0;
      g_hash_table_destroy (new_hash);

      g_free (key_event->string);
      g_free (key_event);
    }

  return consumed;
}

void
_clutter_accessibility_override_atk_util (void)
{
  AtkUtilClass *atk_class = ATK_UTIL_CLASS (g_type_class_ref (ATK_TYPE_UTIL));

  if (atk_class->get_root)
    return;

  atk_class->add_key_event_listener = clutter_accessibility_add_key_event_listener;
  atk_class->remove_key_event_listener = clutter_accessibility_remove_key_event_listener;
  atk_class->get_root = clutter_accessibility_get_root;
  atk_class->get_toolkit_name = clutter_accessibility_get_toolkit_name;
  atk_class->get_toolkit_version = clutter_accessibility_get_toolkit_version;
}
