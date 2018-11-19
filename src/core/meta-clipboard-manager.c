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

#include "core/meta-clipboard-manager.h"
#include "core/meta-memory-selection-source.h"

static void
transfer_cb (MetaSelection *selection,
             GAsyncResult  *result,
             GOutputStream *stream)
{
  MetaDisplay *display = meta_get_display ();
  GError *error = NULL;

  if (!meta_selection_transfer_finish (selection, result, &error))
    {
      g_error_free (error);
      return;
    }

  g_output_stream_close (stream, NULL, NULL);
  display->saved_clipboard =
    g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (stream));
  g_object_unref (stream);
}

static void
owner_changed_cb (MetaSelection       *selection,
                  MetaSelectionType    selection_type,
                  MetaSelectionSource *source,
                  MetaDisplay         *display)
{
  /* Only track CLIPBOARD selection changed */
  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  if (source && source != display->selection_source)
    {
      GOutputStream *output;
      GList *mimetypes, *l;
      const gchar *best = NULL;

      /* New selection source, initiate inspection */
      g_clear_object (&display->selection_source);
      g_clear_pointer (&display->saved_clipboard, g_bytes_unref);

      mimetypes = meta_selection_get_mimetypes (selection, selection_type);

      for (l = mimetypes; l; l = l->next)
        {
          if (g_str_equal (l->data, "text/plain;charset=utf-8"))
            best = l->data;
          else if (!best && g_str_equal (l->data, "text/plain"))
            best = l->data;
        }

      if (!best)
        return;

      output = g_memory_output_stream_new_resizable ();
      meta_selection_transfer_async (selection,
                                     META_SELECTION_CLIPBOARD,
                                     best,
                                     output,
                                     NULL,
                                     (GAsyncReadyCallback) transfer_cb,
                                     output);
    }
  else if (!source && display->saved_clipboard)
    {
      /* Selection source is gone, time to take over */
      source = meta_memory_selection_source_new ("text/plain;charset=utf-8",
                                                 display->saved_clipboard);
      g_set_object (&display->selection_source, source);
      meta_selection_set_owner (selection, selection_type, source);
    }
}

void
meta_clipboard_manager_init (MetaDisplay *display)
{
  MetaSelection *selection;

  selection = meta_display_get_selection (display);
  g_signal_connect_after (selection, "owner-changed",
                          G_CALLBACK (owner_changed_cb), display);
}

void
meta_clipboard_manager_shutdown (MetaDisplay *display)
{
  MetaSelection *selection;

  g_clear_pointer (&display->saved_clipboard, g_bytes_unref);
  selection = meta_display_get_selection (display);
  g_signal_handlers_disconnect_by_func (selection, owner_changed_cb, display);
}
