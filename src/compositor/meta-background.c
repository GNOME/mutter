/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background.c: Utilities for drawing the background
 *
 * Copyright 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "meta-background-actor-private.h"
#include <core/screen-private.h>

/* From and To */
#define CACHE_SIZE 2

typedef struct _SizedUri {
  char *picture_uri;
  int width, height;

  struct _SizedUri *next;
} SizedUri;

typedef struct {
  SizedUri *from;
  SizedUri *to;

  time_t starttime;
  time_t endtime;
} Slide;

typedef struct {
  char *uri;
  GdkPixbuf *pixbuf;

  int lru_age;
} CacheEntry;

struct _MetaBackgroundSlideshow {
  GObject parent_instance;

  /* Immutable once created */
  MetaScreen *screen;
  char *picture_uri;

  GMutex cache_mutex;
  CacheEntry cache[CACHE_SIZE];

  GMutex slides_lock;
  GQueue slides;
  time_t total_duration;
};

struct _MetaBackgroundSlideshowClass {
  GObjectClass parent_class;
};

G_DEFINE_TYPE (MetaBackgroundSlideshow, meta_background_slideshow, G_TYPE_OBJECT);

static void
slide_free (Slide *slide)
{
  SizedUri *cur, *next;

  for (cur = slide->from; cur; cur = next)
    {
      next = cur->next;

      g_free (cur->picture_uri);
      g_slice_free (SizedUri, cur);
    }
  for (cur = slide->to; cur; cur = next)
    {
      next = cur->next;

      g_free (cur->picture_uri);
      g_slice_free (SizedUri, cur);
    }

  g_slice_free (Slide, slide);
}

static void
clear_cache_entry (CacheEntry *entry)
{
  g_free (entry->uri);
  entry->uri = NULL;
  g_clear_object (&entry->pixbuf);
}

static void
insert_cache (MetaBackgroundSlideshow *slideshow,
              const char              *pixbuf_uri,
              GdkPixbuf               *pixbuf)
{
  int i;
  int min_age, max_age;
  CacheEntry *candidate;

  g_mutex_lock (&slideshow->cache_mutex);

  candidate = NULL;
  min_age = INT_MAX;
  max_age = INT_MIN;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      CacheEntry *entry = &slideshow->cache[i];

      if (entry->uri == NULL)
        {
          candidate = entry;
          min_age = INT_MIN;
        }
      else if (entry->lru_age < min_age)
        {
          candidate = entry;
          min_age = entry->lru_age;
        }

      if (entry->lru_age > max_age)
        max_age = entry->lru_age;
    }

  g_assert (candidate != NULL);

  clear_cache_entry (candidate);
  candidate->uri = g_strdup (pixbuf_uri);
  candidate->pixbuf = g_object_ref (pixbuf);
  candidate->lru_age = max_age + 1;

  g_mutex_unlock (&slideshow->cache_mutex);
}

static GdkPixbuf *
hit_cache (MetaBackgroundSlideshow *slideshow,
           const char              *pixbuf_uri)
{
  GdkPixbuf *hit;
  int i;

  g_mutex_lock (&slideshow->cache_mutex);

  hit = NULL;

  for (i = 0; i < CACHE_SIZE; i++)
    {
      CacheEntry *entry = &slideshow->cache[i];

      if (entry->uri != NULL &&
          strcmp (entry->uri, pixbuf_uri) == 0)
        {
          entry->lru_age ++;
          hit = g_object_ref (entry->pixbuf);
        }
    }

  g_mutex_unlock (&slideshow->cache_mutex);

  return hit;
}

typedef enum {
  STATE_INITIAL,
  STATE_BACKGROUND,
  STATE_STARTTIME,
  STATE_STATIC_SLIDE,
  STATE_TRANSITION_SLIDE,
  STATE_FILE,
  STATE_FILE_SIZE,
} ParserState;

/* initial -> background -> transition/static -> to/from/file -> size */
#define STATE_STACK_SIZE 5

typedef struct {
  GQueue *slides_queue;
  Slide *current_slide;
  SizedUri **current_size_list;
  SizedUri *current_size;

  time_t starttime;
  struct tm starttime_tm;

  ParserState state_stack[STATE_STACK_SIZE];
  int         state_stack_len;
} SlideshowParseContext;

static inline ParserState
parse_context_get_state (SlideshowParseContext *parser)
{
  return parser->state_stack[parser->state_stack_len-1];
}

static inline void
parse_context_push_state (SlideshowParseContext *parser,
                          ParserState            state)
{
  g_assert (parser->state_stack_len < STATE_STACK_SIZE);

  parser->state_stack[parser->state_stack_len] = state;
  parser->state_stack_len++;
}

static inline void
parse_context_pop_state (SlideshowParseContext *parser)
{
  parser->state_stack_len--;
}

static void
slideshow_start_element (GMarkupParseContext  *context,
                         const char           *element_name,
                         const char          **attribute_names,
                         const char          **attribute_values,
                         gpointer              user_data,
                         GError              **error)
{
  SlideshowParseContext *parser = user_data;

  switch (parse_context_get_state (parser)) {
  case STATE_INITIAL:
    if (strcmp (element_name, "background"))
      g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                  "Invalid root element %s", element_name);
    else
      parse_context_push_state (parser, STATE_BACKGROUND);
    break;

  case STATE_BACKGROUND:
    if (strcmp (element_name, "starttime") == 0)
      parse_context_push_state (parser, STATE_STARTTIME);
    else if (strcmp (element_name, "static") == 0 ||
             strcmp (element_name, "transition") == 0)
      {
        if (strcmp (element_name, "static") == 0)
          parse_context_push_state (parser, STATE_STATIC_SLIDE);
        else
          parse_context_push_state (parser, STATE_TRANSITION_SLIDE);

        parser->current_slide = g_slice_new0 (Slide);
        parser->current_slide->starttime = parser->starttime;

        g_queue_push_tail (parser->slides_queue, parser->current_slide);
      }
    else
      g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                  "Invalid element %s in state <background>", element_name);
    break;

  case STATE_STARTTIME:
    if (strcmp (element_name, "year") &&
        strcmp (element_name, "month") &&
        strcmp (element_name, "day") &&
        strcmp (element_name, "hour") &&
        strcmp (element_name, "minute") &&
        strcmp (element_name, "second"))
      g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                  "Invalid element %s in state <starttime>", element_name);
    break;

  case STATE_STATIC_SLIDE:
    if (strcmp (element_name, "file") == 0)
      {
        parse_context_push_state (parser, STATE_FILE);

        parser->current_size_list = &parser->current_slide->from;
      }
    else if (strcmp (element_name, "duration"))
      g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                  "Invalid element %s in state <static>", element_name);
    break;

  case STATE_TRANSITION_SLIDE:
    if (strcmp (element_name, "from") == 0 ||
        strcmp (element_name, "to") == 0)
      {
        parse_context_push_state (parser, STATE_FILE);

        if (strcmp (element_name, "from") == 0)
          parser->current_size_list = &parser->current_slide->from;
        else
          parser->current_size_list = &parser->current_slide->to;
      }
    else if (strcmp (element_name, "duration"))
      g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                  "Invalid element %s in state <transition>", element_name);
    break;

  case STATE_FILE:
    if (strcmp (element_name, "size") == 0)
      {
        const char *width, *height;

        if (g_markup_collect_attributes (element_name,
                                         attribute_names,
                                         attribute_values,
                                         error,
                                         G_MARKUP_COLLECT_STRING,
                                         "width", &width,
                                         G_MARKUP_COLLECT_STRING,
                                         "height", &height,
                                         G_MARKUP_COLLECT_INVALID))
          {
            SizedUri *new_size = g_slice_new (SizedUri);

            new_size->picture_uri = NULL;
            new_size->width = atoi(width);
            new_size->height = atoi(height);
            new_size->next = NULL;

            if (parser->current_size == NULL)
              {
                *parser->current_size_list = new_size;
                parser->current_size = new_size;
              }
            else
              parser->current_size->next = new_size;

            parse_context_push_state (parser, STATE_FILE_SIZE);
          }
      }
    else
      {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                    "Invalid element %s in state <file>", element_name);
      }
    break;

  case STATE_FILE_SIZE:
    g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                "Invalid element %s in state <size>", element_name);
    break;
  }
};

static void
slideshow_end_element (GMarkupParseContext *context,
                       const gchar         *element_name,
                       gpointer             user_data,
                       GError             **error)
{
  SlideshowParseContext *parser = user_data;

  switch (parse_context_get_state (parser)) {
  case STATE_STARTTIME:
    if (strcmp (element_name, "starttime") == 0)
      {
        parser->starttime = mktime (&parser->starttime_tm);
        parse_context_pop_state (parser);
      }
    break;

  case STATE_STATIC_SLIDE:
  case STATE_TRANSITION_SLIDE:
    if (strcmp (element_name, "duration"))
      {
        parse_context_pop_state (parser);
        parser->current_slide = NULL;
        parser->current_size_list = NULL;
      }
    break;

  case STATE_FILE:
    parse_context_pop_state (parser);
    parser->current_size = NULL;
    break;

  case STATE_FILE_SIZE:
  case STATE_BACKGROUND:
    parse_context_pop_state (parser);
    break;

  case STATE_INITIAL:
    g_assert_not_reached ();
  }
}

static int
strntoi(const char *text, size_t text_len)
{
  char *v;
  int i;

  v = g_strndup (text, text_len);
  i = atoi(v);

  g_free (v);
  return i;
}

static gboolean
is_all_white (const char *text, size_t text_len)
{
  size_t i;

  for (i = 0; i < text_len; i++)
    if (!g_ascii_isspace(text[i]))
      return FALSE;

  return TRUE;
}

static void
slideshow_text (GMarkupParseContext *context,
                const gchar         *text,
                gsize                text_len,
                gpointer             user_data,
                GError             **error)
{
  SlideshowParseContext *parser = user_data;
  const char *current_element = g_markup_parse_context_get_element (context);

  switch (parse_context_get_state (parser)) {
  case STATE_STARTTIME:
    if (strcmp (current_element, "year") == 0)
      parser->starttime_tm.tm_year = strntoi (text, text_len) - 1900;
    else if (strcmp (current_element, "month") == 0)
      parser->starttime_tm.tm_mon = strntoi (text, text_len) - 1;
    else if (strcmp (current_element, "day") == 0)
      parser->starttime_tm.tm_mday = strntoi (text, text_len);
    else if (strcmp (current_element, "hour") == 0)
      parser->starttime_tm.tm_hour = strntoi (text, text_len);
    else if (strcmp (current_element, "minute") == 0)
      parser->starttime_tm.tm_min = strntoi (text, text_len);
    else if (strcmp (current_element, "second") == 0)
      parser->starttime_tm.tm_sec = strntoi (text, text_len);
    else if (!is_all_white (text, text_len))
      g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                   "Unexpected content in element <%s>", current_element);
    break;

  case STATE_STATIC_SLIDE:
  case STATE_TRANSITION_SLIDE:
    if (strcmp (current_element, "duration") == 0)
      {
        /* Values are floating point in the XML, but we only
           handle integers.
           Also, some XML files represent infinity as a very high value,
           so we don't want to overflow reading.
        */
        char *value = g_strndup (text, text_len);
        long long duration = g_ascii_strtod (value, NULL);

        /* We use milliseconds for timeouts, so anything greater than that
           is impossible (and means infinite)
        */
        if ((duration * 1000) > (long long)INT_MAX)
          {
            parser->current_slide->endtime = parser->starttime = -1;
          }
        else
          {
            parser->current_slide->endtime = parser->current_slide->starttime + (int)duration;
            parser->starttime = parser->current_slide->endtime;
          }

        g_free (value);
      }
    else if (!is_all_white (text, text_len))
      {
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                     "Unexpected content in element <%s>", current_element);
      }
    break;

  case STATE_FILE:
    {
      if (parser->current_size != NULL &&
          !is_all_white (text, text_len))
        {
          g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                       "Unexpected content in element <%s>", current_element);
          break;
        }
      else
        {
          SizedUri *new_size = g_slice_new (SizedUri);

          new_size->picture_uri = NULL;
          new_size->width = -1;
          new_size->height = -1;
          new_size->next = NULL;

          *parser->current_size_list = new_size;
          parser->current_size = new_size;
        }
    }
    /* Fall through */

  case STATE_FILE_SIZE:
    parser->current_size->picture_uri = g_strdup_printf("file://%.*s", (int)text_len, text);
    break;

  case STATE_INITIAL:
  case STATE_BACKGROUND:
    if (!is_all_white (text, text_len))
      g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                   "Unexpected content in element <%s>", current_element);
  }
}

static GMarkupParser slideshow_parser = {
  slideshow_start_element,
  slideshow_end_element,
  slideshow_text,
  NULL,
  NULL,
};

/* Based on the default size for GBufferedInputStream */
#define BUFFER_SIZE 4096

static gboolean
parse_slideshow (MetaBackgroundSlideshow  *slideshow,
                 GInputStream             *stream,
                 GCancellable             *cancellable,
                 GError                  **error)
{
  SlideshowParseContext parser = {
    NULL, NULL, NULL, NULL, 0, { 0 }, { STATE_INITIAL }, 1
  };
  GMarkupParseContext *context;
  char buffer[BUFFER_SIZE];
  GError *local_error;
  gsize read;
  GList *iter;
  time_t total_duration;

  parser.slides_queue = &slideshow->slides;
  parser.starttime_tm.tm_isdst = -1;

  if (!g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, cancellable, error))
    return FALSE;

  context = g_markup_parse_context_new (&slideshow_parser,
                                        G_MARKUP_TREAT_CDATA_AS_TEXT | G_MARKUP_PREFIX_ERROR_POSITION,
                                        &parser, NULL);

  local_error = NULL;

  do
    {
      g_input_stream_read_all (stream, buffer, sizeof(buffer),
                               &read, cancellable, error);
    }
  while (read > 0 &&
         g_markup_parse_context_parse (context, buffer, read, &local_error));

  g_markup_parse_context_free (context);

  if (read > 0)
    {
      g_message ("Failed to parse slideshow file: %s", local_error->message);
      g_set_error (error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
                   _("File format not recognized"));

      g_clear_error (&local_error);
      g_queue_foreach (&slideshow->slides, (GFunc) slide_free, NULL);
      g_queue_clear (&slideshow->slides);
      return FALSE;
    }

  total_duration = 0;

  for (iter = slideshow->slides.head; iter; iter = iter->next)
    {
      Slide *slide = iter->data;

      if (slide->endtime < 0)
        {
          slideshow->total_duration = -1;
          return TRUE;
        }

      total_duration += slide->endtime - slide->starttime;
    }

  slideshow->total_duration = total_duration;
  return TRUE;
}

static Slide *
make_single_pixbuf_slide (const char *picture_uri,
                          GdkPixbuf  *pixbuf)
{
  Slide *slide;
  SizedUri *uri;

  slide = g_slice_new (Slide);
  uri = g_slice_new (SizedUri);

  uri->picture_uri = g_strdup (picture_uri);
  uri->width = gdk_pixbuf_get_width (pixbuf);
  uri->height = gdk_pixbuf_get_height (pixbuf);
  uri->next = NULL;
  slide->from = uri;
  slide->to = NULL;
  slide->starttime = -1;
  slide->endtime = -1;

  return slide;
}

static gboolean
ensure_slideshow (MetaBackgroundSlideshow  *slideshow,
                  GCancellable             *cancellable,
                  GError                  **error)
{
  GFile *file;
  GInputStream *stream;
  GdkPixbuf *pixbuf;
  GError *local_error;
  gboolean ok;

  if (!g_queue_is_empty (&slideshow->slides))
    return TRUE;

  pixbuf = hit_cache (slideshow, slideshow->picture_uri);
  if (pixbuf != NULL)
    {
      g_queue_push_tail (&slideshow->slides,
                         make_single_pixbuf_slide (slideshow->picture_uri, pixbuf));
      slideshow->total_duration = -1;
      return TRUE;
    }

  file = g_file_new_for_uri (slideshow->picture_uri);

  stream = G_INPUT_STREAM (g_file_read (file, cancellable, error));
  if (stream == NULL)
    {
      g_object_unref (file);
      return FALSE;
    }

  local_error = NULL;
  pixbuf = gdk_pixbuf_new_from_stream (stream, cancellable, &local_error);

  if (pixbuf != NULL)
    {
      g_queue_push_tail (&slideshow->slides,
                         make_single_pixbuf_slide (slideshow->picture_uri, pixbuf));
      insert_cache (slideshow, slideshow->picture_uri, pixbuf);
      slideshow->total_duration = -1;

      ok = TRUE;
    }
  else if (g_error_matches (local_error, GDK_PIXBUF_ERROR,
                            GDK_PIXBUF_ERROR_UNKNOWN_TYPE))
    {
      g_clear_error (&local_error);
      ok = parse_slideshow (slideshow, stream, cancellable, error);
    }
  else
    {
      g_propagate_error (error, local_error);

      ok = FALSE;
    }

  if (pixbuf)
    g_object_unref (pixbuf);
  g_object_unref (file);
  g_object_unref (stream);

  return ok;
}

static Slide *
find_current_slide (MetaBackgroundSlideshow *slideshow,
                    time_t                   current_time)
{
  Slide *first_slide;
  GList *iter;

  g_assert (slideshow->slides.head != NULL);

  first_slide = slideshow->slides.head->data;

  if (slideshow->total_duration < 0)
    return first_slide;

  g_assert (first_slide->starttime >= 0);

  /* Account for loops, looking for the difference from the
     starttime indicated in the XML */
  current_time = first_slide->starttime +
    (current_time - first_slide->starttime) % slideshow->total_duration;

  for (iter = slideshow->slides.head; iter; iter = iter->next)
    {
      Slide *slide = iter->data;

      g_assert (slide->endtime >= 0);
      if (current_time >= slide->starttime &&
          current_time < slide->endtime)
        return slide;
    }

  g_assert_not_reached ();
}

/*
 * Find the FileSize that best matches the given size.
 * Do two passes; the first pass only considers FileSizes
 * that are larger than the given size.
 * We are looking for the image that best matches the aspect ratio.
 * When two images have the same aspect ratio, prefer the one whose
 * width is closer to the given width.
 *
 * Shamelessly taken from gnome-bg.c
 */
static SizedUri *
find_best_size (SizedUri *sizes, gint width, gint height)
{
  SizedUri *s, *best;
  gdouble a, d, distance;
  gint pass;

  a = width/(gdouble)height;
  distance = 10000.0;
  best = NULL;

  for (pass = 0; pass < 2; pass++)
    {
      for (s = sizes; s; s = s->next)
        {
          if (pass == 0 && (s->width < width || s->height < height))
            continue;

          d = fabs (a - s->width/(gdouble)s->height);
          if (d < distance)
            {
              distance = d;
              best = s;
            }
          else if (d == distance)
            {
              if (abs (s->width - width) < abs (best->width - width))
                {
                  best = s;
                }
            }
        }

      if (best)
        break;
    }

  return best;
}

static GdkPixbuf *
load_best_pixbuf (MetaBackgroundSlideshow  *slideshow,
                  SizedUri                 *size,
                  GCancellable             *cancellable,
                  GError                  **error)
{
  GFile *file;
  GInputStream *stream;
  GdkPixbuf *pixbuf;
  int width, height;

  meta_screen_get_size (slideshow->screen, &width, &height);
  size = find_best_size (size, width, height);
  pixbuf = hit_cache (slideshow, size->picture_uri);

  if (pixbuf)
    return pixbuf;

  file = g_file_new_for_uri (size->picture_uri);

  stream = G_INPUT_STREAM (g_file_read (file, cancellable, error));
  if (stream == NULL)
    {
      g_object_unref (file);
      return NULL;
    }

  pixbuf = gdk_pixbuf_new_from_stream (stream, cancellable, error);

  if (pixbuf)
    insert_cache (slideshow, size->picture_uri, pixbuf);

  g_object_unref (file);
  g_object_unref (stream);
  return pixbuf;
}

static void
meta_background_slideshow_draw_thread (GTask        *task,
                                       gpointer      source_object,
                                       gpointer      task_data,
                                       GCancellable *cancellable)
{
  MetaBackgroundSlideshow *self = source_object;
  GError *error;
  Slide *slide;
  time_t current_time;

  g_mutex_lock (&self->slides_lock);

  error = NULL;
  if (!ensure_slideshow (self, cancellable, &error))
    goto error;

  current_time = time (NULL);
  slide = find_current_slide (self, current_time);

  if (slide->to != NULL)
    {
      GdkPixbuf *from, *to, *blended;
      int transition_steps, current_step;
      Slide *first_slide = self->slides.head->data;

      from = load_best_pixbuf (self, slide->from, cancellable, &error);
      if (!from)
        goto error;

      to = load_best_pixbuf (self, slide->to, cancellable, &error);
      if (!to)
        goto error;

      g_mutex_unlock (&self->slides_lock);

      /* Round to five minute granularity */
      current_time = first_slide->starttime +
        (current_time - first_slide->starttime) % self->total_duration;
      transition_steps = (slide->endtime - slide->starttime) / 300 * 300;
      current_step = (current_time - slide->starttime) / 300 * 300;

      blended = gdk_pixbuf_copy (from);
      gdk_pixbuf_composite (to, blended,
                            0, 0,
                            gdk_pixbuf_get_width (blended),
                            gdk_pixbuf_get_height (blended),
                            0.0, 0.0, 1.0, 1.0,
                            GDK_INTERP_BILINEAR,
                            (255 * ((double)current_step/transition_steps)) + 0.5);

      g_object_unref (from);
      g_object_unref (to);
      g_task_return_pointer (task, blended, g_object_unref);
    }
  else
    {
      GdkPixbuf *static_pixbuf;

      static_pixbuf = load_best_pixbuf (self, slide->from, cancellable, &error);
      if (!static_pixbuf)
        goto error;

      g_mutex_unlock (&self->slides_lock);
      g_task_return_pointer (task, static_pixbuf, g_object_unref);
    }

  return;

 error:
  g_task_return_error (task, error);
  g_mutex_unlock (&self->slides_lock);
}

GTask *
meta_background_slideshow_draw_async (MetaBackgroundSlideshow *slideshow,
                                      GCancellable            *cancellable,
                                      GAsyncReadyCallback      callback,
                                      gpointer                 user_data)
{
  GTask *task;

  task = g_task_new (slideshow, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_background_slideshow_draw_async);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_set_check_cancellable (task, TRUE);

  g_task_run_in_thread (task, meta_background_slideshow_draw_thread);

  return task;
}

CoglHandle
meta_background_slideshow_draw_finish (MetaBackgroundSlideshow  *slideshow,
                                       GAsyncResult             *result,
                                       GError                  **error)
{
  GdkPixbuf *pixbuf;
  CoglHandle handle;

  pixbuf = g_task_propagate_pointer (G_TASK (result), error);
  if (pixbuf == NULL)
    return COGL_INVALID_HANDLE;

  handle = cogl_texture_new_from_data (gdk_pixbuf_get_width (pixbuf),
                                       gdk_pixbuf_get_height (pixbuf),
                                       COGL_TEXTURE_NO_ATLAS | COGL_TEXTURE_NO_SLICING,
                                       COGL_PIXEL_FORMAT_RGB_888,
                                       COGL_PIXEL_FORMAT_RGB_888,
                                       gdk_pixbuf_get_rowstride (pixbuf),
                                       gdk_pixbuf_get_pixels (pixbuf));

  g_object_unref (pixbuf);
  return handle;
}

static void
meta_background_slideshow_init (MetaBackgroundSlideshow *self)
{
  g_mutex_init (&self->cache_mutex);
  g_mutex_init (&self->slides_lock);
}

static void
meta_background_slideshow_finalize (GObject *object)
{
  MetaBackgroundSlideshow *self = META_BACKGROUND_SLIDESHOW (object);
  int i = 0;

  for (i = 0; i < CACHE_SIZE; i++)
    clear_cache_entry (&self->cache[i]);

  g_queue_foreach (&self->slides, (GFunc) slide_free, NULL);
  g_queue_clear (&self->slides);

  g_free (self->picture_uri);
}

static void
meta_background_slideshow_class_init (MetaBackgroundSlideshowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_background_slideshow_finalize;
}

MetaBackgroundSlideshow *
meta_background_slideshow_new (MetaScreen          *screen,
                               const char          *picture_uri)
{
  MetaBackgroundSlideshow *self;

  self = g_object_new (META_TYPE_BACKGROUND_SLIDESHOW, NULL);

  self->screen = screen;
  self->picture_uri = g_strdup (picture_uri);

  return self;
}

const char *
meta_background_slideshow_get_uri (MetaBackgroundSlideshow *self)
{
  return self->picture_uri;
}

int
meta_background_slideshow_get_next_timeout (MetaBackgroundSlideshow *slideshow)
{
  time_t current_time;
  Slide *current_slide;
  int retval;

  g_mutex_lock (&slideshow->slides_lock);

  g_assert (!g_queue_is_empty (&slideshow->slides));

  current_time = time(NULL);
  current_slide = find_current_slide (slideshow, current_time);

  if (current_slide->endtime < 0)
    retval = -1;
  else if (current_slide->to != NULL)
    /* Translition slides have five minute granularity */
    retval = 300;
  else
    retval = current_slide->endtime - current_time;

  g_mutex_unlock (&slideshow->slides_lock);

  return retval;
}
