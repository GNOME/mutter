/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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


#define _POSIX_C_SOURCE 200112L /* for fdopen() */

#include "config.h"

#include "core/display-private.h"
#include "core/util-private.h"

#include <gio/gunixinputstream.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef HAVE_SYS_PRCTL
#include <sys/prctl.h>
#endif

#include "clutter/clutter-mutter.h"
#include "cogl/cogl.h"
#include "meta/common.h"
#include "meta/main.h"

static const GDebugKey meta_debug_keys[] = {
  { "focus", META_DEBUG_FOCUS },
  { "workarea", META_DEBUG_WORKAREA },
  { "stack", META_DEBUG_STACK },
  { "sm", META_DEBUG_SM },
  { "events", META_DEBUG_EVENTS },
  { "window-state", META_DEBUG_WINDOW_STATE },
  { "window-ops", META_DEBUG_WINDOW_OPS },
  { "geometry", META_DEBUG_GEOMETRY },
  { "placement", META_DEBUG_PLACEMENT },
  { "display", META_DEBUG_DISPLAY },
  { "keybindings", META_DEBUG_KEYBINDINGS },
  { "sync", META_DEBUG_SYNC },
  { "startup", META_DEBUG_STARTUP },
  { "prefs", META_DEBUG_PREFS },
  { "edge-resistance", META_DEBUG_EDGE_RESISTANCE },
  { "dbus", META_DEBUG_DBUS },
  { "input", META_DEBUG_INPUT },
  { "wayland", META_DEBUG_WAYLAND },
  { "kms", META_DEBUG_KMS },
  { "screen-cast", META_DEBUG_SCREEN_CAST },
  { "remote-desktop", META_DEBUG_REMOTE_DESKTOP },
  { "backend", META_DEBUG_BACKEND },
  { "render", META_DEBUG_RENDER },
  { "color", META_DEBUG_COLOR },
  { "input-events", META_DEBUG_INPUT_EVENTS },
  { "eis", META_DEBUG_EIS },
  { "kms-deadline", META_DEBUG_KMS_DEADLINE },
  { "session-management", META_DEBUG_SESSION_MANAGEMENT },
  { "x11", META_DEBUG_X11 },
  { "workspaces", META_DEBUG_WORKSPACES },
};

typedef struct _MetaReadBytesContext
{
  int fd;
  uint32_t offset;
  uint32_t length;
  uint8_t *bytes;
} MetaReadBytesContext;

static gint verbose_topics = 0;
static gboolean is_wayland_compositor = FALSE;
static int debug_paint_flags = 0;
static GLogLevelFlags mutter_log_level = G_LOG_LEVEL_MESSAGE;

#ifdef WITH_VERBOSE_MODE
static FILE* logfile = NULL;

static void
ensure_logfile (void)
{
  if (logfile == NULL && g_getenv ("MUTTER_USE_LOGFILE"))
    {
      char *filename = NULL;
      char *tmpl;
      int fd;
      GError *err;

      tmpl = g_strdup_printf ("mutter-%d-debug-log-XXXXXX",
                              (int) getpid ());

      err = NULL;
      fd = g_file_open_tmp (tmpl,
                            &filename,
                            &err);

      g_free (tmpl);

      if (err != NULL)
        {
          g_warning ("Failed to open debug log: %s",
                     err->message);
          g_error_free (err);
          return;
        }

      logfile = fdopen (fd, "w");

      if (logfile == NULL)
        {
          g_warning ("Failed to fdopen() log file %s: %s",
                     filename, strerror (errno));
          close (fd);
        }
      else
        {
          g_printerr ("Opened log file %s", filename);
        }

      g_free (filename);
    }
}
#endif

gboolean
meta_is_verbose (void)
{
  return verbose_topics != 0;
}

void
meta_set_verbose (gboolean setting)
{
#ifndef WITH_VERBOSE_MODE
  if (setting)
    meta_fatal (_("Mutter was compiled without support for verbose mode"));
#endif

  if (setting)
    meta_add_verbose_topic (META_DEBUG_VERBOSE);
  else
    meta_remove_verbose_topic (META_DEBUG_VERBOSE);
}

/**
 * meta_add_verbose_topic:
 * @topic: Topic for which logging will be started
 *
 * Ensure log messages for the given topic @topic
 * will be printed.
 */
void
meta_add_verbose_topic (MetaDebugTopic topic)
{
  if (verbose_topics == META_DEBUG_VERBOSE)
    return;

#ifdef WITH_VERBOSE_MODE
  ensure_logfile ();
#endif

  if (topic == META_DEBUG_VERBOSE)
    verbose_topics = META_DEBUG_VERBOSE;
  else
    verbose_topics |= topic;
}

/**
 * meta_remove_verbose_topic:
 * @topic: Topic for which logging will be stopped
 *
 * Stop printing log messages for the given topic @topic.
 *
 * Note that this method does not stack with [func@Meta.add_verbose_topic];
 * i.e. if two calls to [func@Meta.add_verbose_topic] for the same
 * topic are made, one call to [func@Meta.remove_verbose_topic]  will
 * remove it.
 */
void
meta_remove_verbose_topic (MetaDebugTopic topic)
{
  if (topic == META_DEBUG_VERBOSE)
    verbose_topics = 0;
  else
    verbose_topics &= ~topic;
}

void
meta_init_debug_utils (void)
{
  const char *debug_env;

#ifdef HAVE_SYS_PRCTL
  prctl (PR_SET_DUMPABLE, 1);
#endif

  if (g_getenv ("MUTTER_VERBOSE"))
    meta_set_verbose (TRUE);

  debug_env = g_getenv ("MUTTER_DEBUG");
  if (debug_env)
    {
      MetaDebugTopic topics;

      topics = g_parse_debug_string (debug_env,
                                     meta_debug_keys,
                                     G_N_ELEMENTS (meta_debug_keys));
      meta_add_verbose_topic (topics);
    }

  if (g_test_initialized ())
    mutter_log_level = G_LOG_LEVEL_DEBUG;
}

gboolean
meta_is_wayland_compositor (void)
{
  return is_wayland_compositor;
}

void
meta_set_is_wayland_compositor (gboolean value)
{
  is_wayland_compositor = value;
}

char *
meta_g_utf8_strndup (const gchar *src,
                     gsize        n)
{
  const gchar *s = src;
  while (n && *s)
    {
      s = g_utf8_next_char (s);
      n--;
    }

  return g_strndup (src, s - src);
}

static void
meta_read_bytes_context_free (MetaReadBytesContext *context)
{
  g_clear_fd (&context->fd, NULL);
  g_clear_pointer (&context->bytes, g_free);
  g_free (context);
}

static void
meta_read_bytes_in_thread (GTask        *task,
                           gpointer      source_object,
                           gpointer      task_data,
                           GCancellable *cancellable)
{
  MetaReadBytesContext *context = task_data;
  g_autoptr (GInputStream) input_stream = NULL;
  g_autofree uint8_t *bytes = NULL;
  g_autoptr (GError) error = NULL;
  int skipped;

  input_stream = G_INPUT_STREAM (g_unix_input_stream_new (context->fd, FALSE));

  skipped = g_input_stream_skip (input_stream,
                                 context->offset,
                                 NULL,
                                 &error);
  if (skipped < 0)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  bytes = g_malloc (context->length);
  if (!g_input_stream_read_all (input_stream,
                                bytes,
                                context->length,
                                NULL,
                                NULL,
                                &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  context->bytes = g_steal_pointer (&bytes);

  g_task_return_boolean (task, TRUE);
}

void
meta_read_bytes (int                 fd,
                 uint32_t            offset,
                 uint32_t            length,
                 GAsyncReadyCallback callback,
                 gpointer            user_data)
{
  g_autoptr (GTask) task = NULL;
  MetaReadBytesContext *context;

  task = g_task_new (NULL, NULL, callback, user_data);

  context = g_new0 (MetaReadBytesContext, 1);
  context->fd = dup (fd);
  context->offset = offset;
  context->length = length;

  g_task_set_task_data (task, context,
                        (GDestroyNotify) meta_read_bytes_context_free);
  g_task_run_in_thread (task, meta_read_bytes_in_thread);
}

gboolean
meta_read_bytes_finish (GAsyncResult  *result,
                        uint8_t      **bytes,
                        uint32_t      *length,
                        GError       **error)
{
  MetaReadBytesContext *context = g_task_get_task_data (G_TASK (result));

  *bytes = g_steal_pointer (&context->bytes);

  if (length)
    *length = context->length;

  return g_task_propagate_boolean (G_TASK (result), error);
}

static int
utf8_fputs (const char *str,
            FILE       *f)
{
  char *l;
  int retval;

  l = g_locale_from_utf8 (str, -1, NULL, NULL, NULL);

  if (l == NULL)
    retval = fputs (str, f); /* just print it anyway, better than nothing */
  else
    retval = fputs (l, f);

  g_free (l);

  return retval;
}

#ifdef WITH_VERBOSE_MODE
const char *
meta_topic_to_string (MetaDebugTopic topic)
{
  switch (topic)
    {
    case META_DEBUG_FOCUS:
      return "FOCUS";
    case META_DEBUG_WORKAREA:
      return "WORKAREA";
    case META_DEBUG_STACK:
      return "STACK";
    case META_DEBUG_SM:
      return "SM";
    case META_DEBUG_EVENTS:
      return "EVENTS";
    case META_DEBUG_WINDOW_STATE:
      return "WINDOW_STATE";
    case META_DEBUG_WINDOW_OPS:
      return "WINDOW_OPS";
    case META_DEBUG_PLACEMENT:
      return "PLACEMENT";
    case META_DEBUG_DISPLAY:
      return "DISPLAY";
    case META_DEBUG_GEOMETRY:
      return "GEOMETRY";
    case META_DEBUG_KEYBINDINGS:
      return "KEYBINDINGS";
    case META_DEBUG_SYNC:
      return "SYNC";
    case META_DEBUG_STARTUP:
      return "STARTUP";
    case META_DEBUG_PREFS:
      return "PREFS";
    case META_DEBUG_EDGE_RESISTANCE:
      return "EDGE_RESISTANCE";
    case META_DEBUG_DBUS:
      return "DBUS";
    case META_DEBUG_INPUT:
      return "INPUT";
    case META_DEBUG_WAYLAND:
      return "WAYLAND";
    case META_DEBUG_KMS:
      return "KMS";
    case META_DEBUG_SCREEN_CAST:
      return "SCREEN_CAST";
    case META_DEBUG_REMOTE_DESKTOP:
      return "REMOTE_DESKTOP";
    case META_DEBUG_BACKEND:
      return "BACKEND";
    case META_DEBUG_RENDER:
      return "RENDER";
    case META_DEBUG_COLOR:
      return "COLOR";
    case META_DEBUG_VERBOSE:
      return "VERBOSE";
    case META_DEBUG_INPUT_EVENTS:
      return "INPUT_EVENTS";
    case META_DEBUG_EIS:
      return "EIS";
    case META_DEBUG_KMS_DEADLINE:
      return "KMS_DEADLINE";
    case META_DEBUG_SESSION_MANAGEMENT:
      return "SESSION_MANAGEMENT";
    case META_DEBUG_X11:
      return "X11";
    case META_DEBUG_WORKSPACES:
      return "WORKSPACES";
    }

  return "WM";
}

gboolean
meta_is_topic_enabled (MetaDebugTopic topic)
{
  if (verbose_topics == 0)
    return FALSE;

  if (topic == META_DEBUG_VERBOSE && verbose_topics != META_DEBUG_VERBOSE)
    return FALSE;

  return !!(verbose_topics & topic);
}
#endif /* WITH_VERBOSE_MODE */

void
meta_bug (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_return_if_fail (format != NULL);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

#ifdef WITH_VERBOSE_MODE
  out = logfile ? logfile : stderr;
#else
  out = stderr;
#endif

  utf8_fputs ("Bug in window manager: ", out);
  utf8_fputs (str, out);
  utf8_fputs ("\n", out);

  fflush (out);

  g_free (str);

  /* stop us in a debugger */
  abort ();
}

void
meta_fatal (const char *format, ...)
{
  va_list args;
  gchar *str;
  FILE *out;

  g_warn_if_fail (format);
  if (!format)
    meta_exit (META_EXIT_ERROR);

  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

#ifdef WITH_VERBOSE_MODE
  out = logfile ? logfile : stderr;
#else
  out = stderr;
#endif

  utf8_fputs ("Window manager error: ", out);
  utf8_fputs (str, out);
  utf8_fputs ("\n", out);

  fflush (out);

  g_free (str);

  meta_exit (META_EXIT_ERROR);
}

void
meta_exit (MetaExitCode code)
{

  exit (code);
}

gint
meta_unsigned_long_equal (gconstpointer v1,
                          gconstpointer v2)
{
  return *((const gulong*) v1) == *((const gulong*) v2);
}

guint
meta_unsigned_long_hash  (gconstpointer v)
{
  gulong val = * (const gulong *) v;

  /* I'm not sure this works so well. */
#if GLIB_SIZEOF_LONG > 4
  return (guint) (val ^ (val >> 32));
#else
  return val;
#endif
}

const char*
meta_gravity_to_string (MetaGravity gravity)
{
  switch (gravity)
    {
    case META_GRAVITY_NORTH_WEST:
      return "META_GRAVITY_NORTH_WEST";
      break;
    case META_GRAVITY_NORTH:
      return "META_GRAVITY_NORTH";
      break;
    case META_GRAVITY_NORTH_EAST:
      return "META_GRAVITY_NORTH_EAST";
      break;
    case META_GRAVITY_WEST:
      return "META_GRAVITY_WEST";
      break;
    case META_GRAVITY_CENTER:
      return "META_GRAVITY_CENTER";
      break;
    case META_GRAVITY_EAST:
      return "META_GRAVITY_EAST";
      break;
    case META_GRAVITY_SOUTH_WEST:
      return "META_GRAVITY_SOUTH_WEST";
      break;
    case META_GRAVITY_SOUTH:
      return "META_GRAVITY_SOUTH";
      break;
    case META_GRAVITY_SOUTH_EAST:
      return "META_GRAVITY_SOUTH_EAST";
      break;
    case META_GRAVITY_STATIC:
      return "META_GRAVITY_STATIC";
      break;
    default:
      return "META_GRAVITY_NORTH_WEST";
      break;
    }
}

char*
meta_external_binding_name_for_action (guint keybinding_action)
{
  return g_strdup_printf ("external-grab-%u", keybinding_action);
}

char *
meta_generate_random_id (GRand *rand,
                         int    length)
{
  char *id;
  int i;

  /* Generate a random string of printable ASCII characters. */

  id = g_new0 (char, length + 1);
  for (i = 0; i < length; i++)
    id[i] = (char) g_rand_int_range (rand, 32, 127);

  return id;
}

void
meta_add_debug_paint_flag (MetaDebugPaintFlag flag)
{
  debug_paint_flags |= flag;
}

void
meta_remove_debug_paint_flag (MetaDebugPaintFlag flag)
{
  debug_paint_flags &= ~flag;
}

MetaDebugPaintFlag
meta_get_debug_paint_flags (void)
{
  return debug_paint_flags;
}

void
meta_log (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  g_logv (G_LOG_DOMAIN, mutter_log_level, format, args);
  va_end (args);
}
