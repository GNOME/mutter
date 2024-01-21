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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta/meta-selection-source-memory.h"

#include <gio/gunixinputstream.h>

#include "core/meta-anonymous-file.h"

struct _MetaSelectionSourceMemory
{
  MetaSelectionSource parent_instance;
  char *mimetype;
  MetaAnonymousFile *content;
};

G_DEFINE_TYPE (MetaSelectionSourceMemory,
               meta_selection_source_memory,
               META_TYPE_SELECTION_SOURCE)

struct _MetaUnixInputStream
{
  GUnixInputStream parent_instance;
};

#define META_TYPE_UNIX_INPUT_STREAM (meta_unix_input_stream_get_type ())
G_DECLARE_FINAL_TYPE (MetaUnixInputStream,
                      meta_unix_input_stream,
                      META, UNIX_INPUT_STREAM,
                      GUnixInputStream)

G_DEFINE_FINAL_TYPE (MetaUnixInputStream,
                     meta_unix_input_stream,
                     G_TYPE_UNIX_INPUT_STREAM)

static gboolean
meta_unix_input_stream_close_fn (GInputStream  *input_stream,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  GUnixInputStream *stream = G_UNIX_INPUT_STREAM (input_stream);
  int fd;

  if (!g_unix_input_stream_get_close_fd (stream))
    return TRUE;

  fd = g_unix_input_stream_get_fd (stream);
  meta_anonymous_file_close_fd (fd);

  return TRUE;
}

static void
meta_unix_input_stream_class_init (MetaUnixInputStreamClass *klass)
{
  GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS (klass);

  input_stream_class->close_fn = meta_unix_input_stream_close_fn;
}

static void
meta_unix_input_stream_init (MetaUnixInputStream *stream)
{
}

static GInputStream *
meta_unix_input_stream_new (int fd)
{
  return G_INPUT_STREAM (g_object_new (META_TYPE_UNIX_INPUT_STREAM,
                                       "fd", fd,
                                       "close-fd", true,
                                       NULL));
}

static void
meta_selection_source_memory_read_async (MetaSelectionSource *source,
                                         const char          *mimetype,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (source);
  GInputStream *stream;
  g_autoptr (GTask) task = NULL;
  int fd;

  if (g_strcmp0 (mimetype, source_mem->mimetype) != 0)
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_memory_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Mimetype not in selection");
      return;
    }

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_source_memory_read_async);

  fd = meta_anonymous_file_open_fd (source_mem->content,
                                    META_ANONYMOUS_FILE_MAPMODE_SHARED);

  if (fd == -1)
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_selection_source_memory_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed to open MetaAnonymousFile");
      return;
    }

  stream = meta_unix_input_stream_new (fd);

  g_task_return_pointer (task, stream, g_object_unref);
}

static GInputStream *
meta_selection_source_memory_read_finish (MetaSelectionSource  *source,
                                          GAsyncResult         *result,
                                          GError              **error)
{
  g_assert (g_task_get_source_tag (G_TASK (result)) ==
            meta_selection_source_memory_read_async);
  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_selection_source_memory_get_mimetypes (MetaSelectionSource *source)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (source);

  if (!source_mem->mimetype)
    return NULL;

  return g_list_prepend (NULL, g_strdup (source_mem->mimetype));
}

static void
meta_selection_source_memory_finalize (GObject *object)
{
  MetaSelectionSourceMemory *source_mem = META_SELECTION_SOURCE_MEMORY (object);

  g_clear_pointer (&source_mem->content, meta_anonymous_file_free);
  g_free (source_mem->mimetype);

  G_OBJECT_CLASS (meta_selection_source_memory_parent_class)->finalize (object);
}

static void
meta_selection_source_memory_class_init (MetaSelectionSourceMemoryClass *klass)
{
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_selection_source_memory_finalize;

  source_class->read_async = meta_selection_source_memory_read_async;
  source_class->read_finish = meta_selection_source_memory_read_finish;
  source_class->get_mimetypes = meta_selection_source_memory_get_mimetypes;
}

static void
meta_selection_source_memory_init (MetaSelectionSourceMemory *source)
{
}

MetaSelectionSource *
meta_selection_source_memory_new (const char  *mimetype,
                                  GBytes      *content,
                                  GError     **error)
{
  MetaSelectionSourceMemory *source;
  MetaAnonymousFile *anon_file;
  const uint8_t *data;
  size_t size;

  g_return_val_if_fail (mimetype != NULL, NULL);
  g_return_val_if_fail (content != NULL, NULL);

  data = g_bytes_get_data (content, &size);
  anon_file = meta_anonymous_file_new (size, data);

  if (anon_file == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create MetaAnonymousFile");
      return NULL;
    }

  source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
  source->mimetype = g_strdup (mimetype);
  source->content = anon_file;

  return META_SELECTION_SOURCE (source);
}
