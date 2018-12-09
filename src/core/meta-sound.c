/*
 * Copyright (C) 2018 Red Hat
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

#include <canberra-gtk.h>
#include "meta/meta-sound.h"

#define EVENT_SOUNDS_KEY "event-sounds"
#define THEME_NAME_KEY   "theme-name"

typedef struct _MetaSound MetaSound;
typedef struct _MetaPlayRequest MetaPlayRequest;

struct _MetaSound
{
  GObject parent_instance;
  GThreadPool *queue;
  GSettings *settings;
  ca_context *context;
  uint32_t id_pool;
};

struct _MetaPlayRequest
{
  ca_proplist *props;
  uint32_t id;
  guint cancel_id;
  GCancellable *cancellable;
  MetaSound *sound;
};

const gchar * const cache_whitelist[] = {
  "bell-window-system",
  NULL
};

G_DEFINE_TYPE (MetaSound, meta_sound, G_TYPE_OBJECT)

static MetaPlayRequest *
meta_play_request_new (MetaSound    *sound,
                       ca_proplist  *props,
                       GCancellable *cancellable)
{
  MetaPlayRequest *req;

  req = g_new0 (MetaPlayRequest, 1);
  req->props = props;
  req->sound = sound;
  g_set_object (&req->cancellable, cancellable);

  return req;
}

static void
meta_play_request_free (MetaPlayRequest *req)
{
  g_clear_object (&req->cancellable);
  ca_proplist_destroy (req->props);
  g_free (req);
}

static void
meta_sound_finalize (GObject *object)
{
  MetaSound *sound = META_SOUND (object);

  g_object_unref (sound->settings);
  g_thread_pool_free (sound->queue, TRUE, TRUE);
  ca_context_destroy (sound->context);

  G_OBJECT_CLASS (meta_sound_parent_class)->finalize (object);
}

static void
meta_sound_class_init (MetaSoundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_sound_finalize;
}

static void
cancelled_cb (GCancellable    *cancellable,
              MetaPlayRequest *req)
{
  ca_context_cancel (req->sound->context, req->id);
}

static void
finish_cb (ca_context *context,
           uint32_t    id,
           int         error_code,
           gpointer    user_data)
{
  MetaPlayRequest *req = user_data;

  g_cancellable_disconnect (req->cancellable, req->cancel_id);
  meta_play_request_free (req);
}

static void
play_sound (MetaPlayRequest *req,
            MetaSound       *sound)
{
  req->id = sound->id_pool++;

  if (ca_context_play_full (sound->context, req->id, req->props,
                            finish_cb, req) != CA_SUCCESS)
    {
      meta_play_request_free (req);
      return;
    }

  if (req->cancellable)
    {
      req->cancel_id =
        g_cancellable_connect (req->cancellable,
                               G_CALLBACK (cancelled_cb), req, NULL);
    }
}

static void
settings_changed_cb (GSettings   *settings,
                     const gchar *key,
                     MetaSound   *sound)
{
  if (strcmp (key, EVENT_SOUNDS_KEY) == 0)
    {
      gboolean enabled;

      enabled = g_settings_get_boolean (settings, EVENT_SOUNDS_KEY);
      ca_context_change_props (sound->context, CA_PROP_CANBERRA_ENABLE,
                               enabled ? "1" : "0", NULL);
    }
  else if (strcmp (key, THEME_NAME_KEY) == 0)
    {
      gchar *theme_name;

      theme_name = g_settings_get_string (settings, THEME_NAME_KEY);
      ca_context_change_props (sound->context, CA_PROP_CANBERRA_XDG_THEME_NAME,
                               theme_name, NULL);
      g_free (theme_name);
    }
}

static ca_context *
create_context (GSettings *settings)
{
  ca_context *context;
  ca_proplist *props;
  gboolean enabled;
  gchar *theme_name;

  if (ca_context_create (&context) != CA_SUCCESS)
    return NULL;

  if (ca_proplist_create (&props) != CA_SUCCESS)
    {
      ca_context_destroy (context);
      return NULL;
    }

  ca_proplist_sets (props, CA_PROP_APPLICATION_NAME, "Mutter");

  enabled = g_settings_get_boolean (settings, EVENT_SOUNDS_KEY);
  ca_proplist_sets (props, CA_PROP_CANBERRA_ENABLE, enabled ? "1" : "0");

  theme_name = g_settings_get_string (settings, THEME_NAME_KEY);
  ca_proplist_sets (props, CA_PROP_CANBERRA_XDG_THEME_NAME, theme_name);
  g_free (theme_name);

  ca_context_change_props_full (context, props);
  ca_proplist_destroy (props);

  return context;
}

static void
meta_sound_init (MetaSound *sound)
{
  sound->queue = g_thread_pool_new ((GFunc) play_sound,
                                    sound, 1, FALSE, NULL);
  sound->settings = g_settings_new ("org.gnome.desktop.sound");
  sound->context = create_context (sound->settings);

  g_signal_connect (sound->settings, "changed",
                    G_CALLBACK (settings_changed_cb), sound);
}

static void
build_ca_proplist (ca_proplist  *props,
                   const char   *event_property,
                   const char   *event_id,
                   const char   *event_description)
{
  ca_proplist_sets (props, event_property, event_id);
  ca_proplist_sets (props, CA_PROP_EVENT_DESCRIPTION, event_description);
}

/**
 * meta_sound_play_from_theme:
 * @sound: a #MetaSound
 * @name: sound theme name of the event
 * @description: description of the event
 * @cancellable: cancellable for the request
 *
 * Plays a sound from the sound theme.
 **/
void
meta_sound_play_from_theme (MetaSound    *sound,
                            const char   *name,
                            const char   *description,
                            GCancellable *cancellable)
{
  MetaPlayRequest *req;
  ca_proplist *props;

  ca_proplist_create (&props);
  build_ca_proplist (props, CA_PROP_EVENT_ID, name, description);

  if (g_strv_contains (cache_whitelist, name))
    ca_proplist_sets (props, CA_PROP_CANBERRA_CACHE_CONTROL, "permanent");
  else
    ca_proplist_sets (props, CA_PROP_CANBERRA_CACHE_CONTROL, "volatile");

  req = meta_play_request_new (sound, props, cancellable);
  g_thread_pool_push (sound->queue, req, NULL);
}

/**
 * meta_sound_play_from_file:
 * @sound: a #MetaSound
 * @file: file to play
 * @description: description of the played sound
 * @cancellable: cancellable for the request
 *
 * Plays a sound from a file.
 **/
void
meta_sound_play_from_file (MetaSound    *sound,
                           GFile        *file,
                           const char   *description,
                           GCancellable *cancellable)
{
  MetaPlayRequest *req;
  ca_proplist *props;
  gchar *path;

  g_return_if_fail (META_IS_SOUND (sound));
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  path = g_file_get_path (file);
  g_return_if_fail (path != NULL);

  ca_proplist_create (&props);
  build_ca_proplist (props, CA_PROP_MEDIA_FILENAME, path, description);
  ca_proplist_sets (props, CA_PROP_CANBERRA_CACHE_CONTROL, "volatile");
  g_free (path);

  req = meta_play_request_new (sound, props, cancellable);
  g_thread_pool_push (sound->queue, req, NULL);
}
