/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc
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
 */

#include "config.h"
#include "meta-startup-notification-x11.h"

#include <gio/gdesktopappinfo.h>

#include "core/display-private.h"
#include "core/startup-notification-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

#define MAX_MESSAGE_LENGTH 4096
#define CLIENT_MESSAGE_DATA_LENGTH 20

enum
{
  MESSAGE_TYPE_NEW,
  MESSAGE_TYPE_REMOVE,
};

struct _MetaX11StartupNotification
{
  Atom atom_net_startup_info_begin;
  Atom atom_net_startup_info;
  GHashTable *messages;
  MetaX11Display *x11_display;
};

typedef struct
{
  Window xwindow;
  GString *data;
} StartupMessage;


static StartupMessage *
startup_message_new (Window window)
{
  StartupMessage *message;

  message = g_new0 (StartupMessage, 1);
  message->xwindow = window;
  message->data = g_string_new (NULL);

  return message;
}

static void
startup_message_free (StartupMessage *message)
{
  g_string_free (message->data, TRUE);
  g_free (message);
}

static void
skip_whitespace (const gchar **str)
{
  while ((*str)[0] == ' ')
    (*str)++;
}

static gchar *
parse_key (const gchar **str)
{
  const gchar *start = *str;

  while (*str[0] != '\0' && *str[0] != '=')
    (*str)++;

  if (start == *str)
    return NULL;

  return g_strndup (start, *str - start);
}

static gchar *
parse_value (const gchar **str)
{
  const gchar *start, *end;
  gboolean escaped = FALSE, quoted = FALSE;
  GString *value;

  start = end = *str;
  value = g_string_new (NULL);

  if (start[0] == '"')
    {
      quoted = TRUE;
      start++;
      end++;
    }

  while (end[0] != '\0')
    {
      if (escaped)
        {
          g_string_append_c (value, end[0]);
          escaped = FALSE;
        }
      else
        {
          if (!quoted && end[0] == ' ')
            break;
          else if (end[0] == '"')
            quoted = !quoted;
          else if (end[0] == '\\')
            escaped = TRUE;
          else
            g_string_append_c (value, end[0]);
        }

      end++;
    }

  if (value->len == 0)
    {
      g_string_free (value, TRUE);
      return NULL;
    }

  return g_string_free (value, FALSE);
}

static gboolean
startup_message_parse (StartupMessage  *message,
                       int             *type,
                       gchar          **id,
                       GHashTable     **data)
{
  const gchar *str = message->data->str;

  if (strncmp (str, "new:", 4) == 0)
    {
      *type = MESSAGE_TYPE_NEW;
      str += 4;
    }
  else if (strncmp (str, "remove:", 7) == 0)
    {
      *type = MESSAGE_TYPE_REMOVE;
      str += 7;
    }
  else
    {
      return FALSE;
    }

  *data = g_hash_table_new_full (g_str_hash,
                                 g_str_equal,
                                 g_free,
                                 g_free);
  while (str[0])
    {
      gchar *key, *value;

      skip_whitespace (&str);
      key = parse_key (&str);
      if (!key)
        break;

      str++;
      value = parse_value (&str);

      if (!value)
        {
          g_free (key);
          break;
        }

      g_hash_table_insert (*data, key, value);
    }

  *id = g_strdup (g_hash_table_lookup (*data, "ID"));

  return TRUE;
}

static gboolean
startup_message_add_data (StartupMessage *message,
                          const gchar    *data)
{
  int len;

  len = strnlen (data, CLIENT_MESSAGE_DATA_LENGTH);
  g_string_append_len (message->data, data, len);

  return (message->data->len > MAX_MESSAGE_LENGTH ||
          len < CLIENT_MESSAGE_DATA_LENGTH);
}

void
meta_x11_startup_notification_init (MetaX11Display *x11_display)
{
  MetaX11StartupNotification *x11_sn;

  x11_sn = g_new0 (MetaX11StartupNotification, 1);
  x11_sn->atom_net_startup_info_begin = XInternAtom (x11_display->xdisplay,
                                                     "_NET_STARTUP_INFO_BEGIN",
                                                     False);
  x11_sn->atom_net_startup_info = XInternAtom (x11_display->xdisplay,
                                               "_NET_STARTUP_INFO",
                                               False);
  x11_sn->messages = g_hash_table_new_full (NULL, NULL, NULL,
                                            (GDestroyNotify) startup_message_free);
  x11_sn->x11_display = x11_display;
  x11_display->startup_notification = x11_sn;
}

void
meta_x11_startup_notification_release (MetaX11Display *x11_display)
{
  MetaX11StartupNotification *x11_sn = x11_display->startup_notification;

  x11_display->startup_notification = NULL;

  if (x11_sn)
    {
      g_hash_table_unref (x11_sn->messages);
      g_free (x11_sn);
    }
}

static void
handle_message (MetaX11StartupNotification *x11_sn,
                StartupMessage             *message)
{
  MetaStartupNotification *sn = x11_sn->x11_display->display->startup_notification;
  MetaStartupSequence *seq;
  GHashTable *data;
  char *id;
  int type;

  if (message->data->len <= MAX_MESSAGE_LENGTH &&
      startup_message_parse (message, &type, &id, &data))
    {
      if (type == MESSAGE_TYPE_NEW)
        {
          seq = g_object_new (META_TYPE_STARTUP_SEQUENCE,
                              "id", id,
                              "icon-name", g_hash_table_lookup (data, "ICON_NAME"),
                              "application-id", g_hash_table_lookup (data, "APPLICATION_ID"),
                              "wmclass", g_hash_table_lookup (data, "WMCLASS"),
                              "name", g_hash_table_lookup (data, "NAME"),
                              "workspace", g_hash_table_lookup (data, "WORKSPACE"),
                              "timestamp", g_hash_table_lookup (data, "TIMESTAMP"),
                              NULL);

          meta_topic (META_DEBUG_STARTUP,
                      "Received startup initiated for %s wmclass %s\n",
                      id, (gchar*) g_hash_table_lookup (data, "WMCLASS"));

          meta_startup_notification_add_sequence (sn, seq);
          g_object_unref (seq);
        }
      else if (type == MESSAGE_TYPE_REMOVE)
        {
          meta_topic (META_DEBUG_STARTUP,
                      "Received startup completed for %s\n", id);
          seq = meta_startup_notification_lookup_sequence (sn, id);

          if (seq)
            {
              meta_startup_sequence_complete (seq);
              meta_startup_notification_remove_sequence (sn, seq);
            }
        }

      g_hash_table_unref (data);
      g_free (id);
    }

  g_hash_table_remove (x11_sn->messages, GINT_TO_POINTER (message->xwindow));
}

static gboolean
handle_startup_notification_event (MetaX11StartupNotification *x11_sn,
                                   XClientMessageEvent        *client_event)
{
  StartupMessage *message;

  if (client_event->message_type == x11_sn->atom_net_startup_info_begin)
    {
      message = startup_message_new (client_event->window);
      g_hash_table_insert (x11_sn->messages,
                           GINT_TO_POINTER (client_event->window),
                           message);
      if (startup_message_add_data (message, client_event->data.b))
        handle_message (x11_sn, message);
      return TRUE;
    }
  else if (client_event->message_type == x11_sn->atom_net_startup_info)
    {
      message = g_hash_table_lookup (x11_sn->messages,
                                     GINT_TO_POINTER (client_event->window));
      if (message)
        {
          if (startup_message_add_data (message, client_event->data.b))
            handle_message (x11_sn, message);
          return TRUE;
        }
    }

  return FALSE;
}

gboolean
meta_x11_startup_notification_handle_xevent (MetaX11Display *x11_display,
                                             XEvent         *xevent)
{
  MetaX11StartupNotification *x11_sn = x11_display->startup_notification;

  if (!x11_sn)
    return FALSE;

  if (xevent->xany.type != ClientMessage)
    return FALSE;

  return handle_startup_notification_event (x11_sn, &xevent->xclient);
}
