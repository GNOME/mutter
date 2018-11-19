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

#include "core/meta-memory-selection-source.h"

struct _MetaMemorySelectionSource
{
  MetaSelectionSource parent_instance;
  char *mimetype;
  GBytes *content;
};

G_DEFINE_TYPE (MetaMemorySelectionSource,
               meta_memory_selection_source,
               META_TYPE_SELECTION_SOURCE)

static void
meta_memory_selection_source_read_async (MetaSelectionSource *source,
                                         const gchar         *mimetype,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  MetaMemorySelectionSource *source_mem = META_MEMORY_SELECTION_SOURCE (source);
  GInputStream *stream;
  GTask *task;

  if (g_strcmp0 (mimetype, source_mem->mimetype) != 0)
    {
      g_task_report_new_error (source, callback, user_data,
                               meta_memory_selection_source_read_async,
                               G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Mimetype not in selection");
      return;
    }

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_memory_selection_source_read_async);

  stream = g_memory_input_stream_new_from_bytes (source_mem->content);
  g_task_return_pointer (task, stream, g_object_unref);
}

static GInputStream *
meta_memory_selection_source_read_finish (MetaSelectionSource  *source,
                                          GAsyncResult         *result,
                                          GError              **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_memory_selection_source_read_async, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static GList *
meta_memory_selection_source_get_mimetypes (MetaSelectionSource  *source)
{
  MetaMemorySelectionSource *source_mem = META_MEMORY_SELECTION_SOURCE (source);

  return g_list_prepend (NULL, g_strdup (source_mem->mimetype));
}

static void
meta_memory_selection_source_class_init (MetaMemorySelectionSourceClass *klass)
{
  MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

  source_class->read_async = meta_memory_selection_source_read_async;
  source_class->read_finish = meta_memory_selection_source_read_finish;
  source_class->get_mimetypes = meta_memory_selection_source_get_mimetypes;
}

static void
meta_memory_selection_source_init (MetaMemorySelectionSource *source)
{
}

MetaSelectionSource *
meta_memory_selection_source_new (const gchar *mimetype,
                                  GBytes      *content)
{
  MetaMemorySelectionSource *source;

  g_return_val_if_fail (mimetype != NULL, NULL);
  g_return_val_if_fail (content != NULL, NULL);

  source = g_object_new (META_TYPE_MEMORY_SELECTION_SOURCE, NULL);
  source->mimetype = g_strdup (mimetype);
  source->content = g_bytes_ref (content);

  return META_SELECTION_SOURCE (source);
}
