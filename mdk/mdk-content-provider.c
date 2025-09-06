/*
 * Copyright (C) 2025 Red Hat
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-content-provider.h"

struct _MdkContentProvider
{
  GdkContentProvider parent;

  const char *mime_type;

  MdkContentWriter *writer;
};

G_DEFINE_INTERFACE (MdkContentWriter, mdk_content_writer, G_TYPE_OBJECT)

G_DEFINE_FINAL_TYPE (MdkContentProvider, mdk_content_provider,
                     GDK_TYPE_CONTENT_PROVIDER)

static void
mdk_content_writer_default_init (MdkContentWriterInterface *iface)
{
}

static void
mdk_content_writer_write_async (MdkContentWriter    *writer,
                                const char          *mime_type,
                                GOutputStream       *stream,
                                int                  io_priority,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  MdkContentWriterInterface *iface = MDK_CONTENT_WRITER_GET_IFACE (writer);

  iface->write_async (writer, mime_type, stream, io_priority,
                      cancellable, callback, user_data);
}

static gboolean
mdk_content_writer_write_finish (MdkContentWriter  *writer,
                                 GAsyncResult      *result,
                                 GError           **error)
{
  MdkContentWriterInterface *iface = MDK_CONTENT_WRITER_GET_IFACE (writer);

  return iface->write_finish (writer, result, error);
}

static void
mdk_content_provider_finalize (GObject *object)
{
  MdkContentProvider *content = MDK_CONTENT_PROVIDER (object);

  g_clear_object (&content->writer);

  G_OBJECT_CLASS (mdk_content_provider_parent_class)->finalize (object);
}

static GdkContentFormats *
mdk_content_provider_ref_formats (GdkContentProvider *provider)
{
  MdkContentProvider *content = MDK_CONTENT_PROVIDER (provider);
  GdkContentFormatsBuilder *builder;

  builder = gdk_content_formats_builder_new ();
  gdk_content_formats_builder_add_mime_type (builder, content->mime_type);
  return gdk_content_formats_builder_free_to_formats (builder);
}

static void
writer_write_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MdkContentWriter *writer = MDK_CONTENT_WRITER (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  GError *error = NULL;

  if (!mdk_content_writer_write_finish (writer, result, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

static void
mdk_content_provider_write_mime_type_async (GdkContentProvider  *provider,
                                            const char          *mime_type,
                                            GOutputStream       *stream,
                                            int                  io_priority,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data)
{
  MdkContentProvider *content = MDK_CONTENT_PROVIDER (provider);
  g_autoptr (GTask) task = NULL;

  task = g_task_new (content, cancellable, callback, user_data);
  g_task_set_priority (task, io_priority);
  g_task_set_source_tag (task, mdk_content_provider_write_mime_type_async);

  if (mime_type != content->mime_type)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Cannot provide contents as “%s”", mime_type);
      return;
    }

  mdk_content_writer_write_async (content->writer,
                                  mime_type,
                                  stream,
                                  io_priority,
                                  cancellable,
                                  writer_write_cb,
                                  g_object_ref (task));
}

static gboolean
mdk_content_provider_write_mime_type_finish (GdkContentProvider  *provider,
                                             GAsyncResult        *result,
                                             GError             **error)
{
  g_return_val_if_fail (g_task_is_valid (result, provider), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        mdk_content_provider_write_mime_type_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
mdk_content_provider_class_init (MdkContentProviderClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GdkContentProviderClass *provider_class = GDK_CONTENT_PROVIDER_CLASS (class);

  object_class->finalize = mdk_content_provider_finalize;

  provider_class->ref_formats = mdk_content_provider_ref_formats;
  provider_class->write_mime_type_async =
    mdk_content_provider_write_mime_type_async;
  provider_class->write_mime_type_finish =
    mdk_content_provider_write_mime_type_finish;
}

static void
mdk_content_provider_init (MdkContentProvider *content)
{
}

MdkContentProvider *
mdk_content_provider_new (const char       *mime_type,
                          MdkContentWriter *writer)
{
  MdkContentProvider *content;

  g_return_val_if_fail (mime_type != NULL, NULL);
  g_return_val_if_fail (writer != NULL, NULL);

  content = g_object_new (MDK_TYPE_CONTENT_PROVIDER, NULL);
  content->mime_type = g_intern_string (mime_type);
  g_set_object (&content->writer, writer);

  return content;
}
