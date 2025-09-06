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

#pragma once

#include <gdk/gdk.h>

#define MDK_TYPE_CONTENT_WRITER (mdk_content_writer_get_type ())
G_DECLARE_INTERFACE (MdkContentWriter, mdk_content_writer,
                     MDK, CONTENT_WRITER,
                     GObject)

struct _MdkContentWriterInterface
{
  GTypeInterface parent_iface;

  void (* write_async) (MdkContentWriter    *writer,
                        const char          *mime_type,
                        GOutputStream       *stream,
                        int                  io_priority,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data);

  gboolean (* write_finish) (MdkContentWriter  *content,
                             GAsyncResult      *result,
                             GError           **error);
};

#define MDK_TYPE_CONTENT_PROVIDER (mdk_content_provider_get_type ())
G_DECLARE_FINAL_TYPE (MdkContentProvider, mdk_content_provider,
                      MDK, CONTENT_PROVIDER,
                      GdkContentProvider)

MdkContentProvider * mdk_content_provider_new (const char       *mime_type,
                                               MdkContentWriter *writer);
