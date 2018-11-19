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

#include "meta-selection.h"

typedef struct TransferRequest TransferRequest;

struct _MetaSelection
{
  GObject parent_instance;
  MetaDisplay *display;
  MetaSelectionSource *owners[META_N_SELECTION_TYPES];
};

struct TransferRequest
{
  MetaSelectionType selection;
  GInputStream  *istream;
  GOutputStream *ostream;
};

enum
{
  OWNER_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (MetaSelection, meta_selection, G_TYPE_OBJECT)

static void
meta_selection_finalize (GObject *object)
{
  G_OBJECT_CLASS (meta_selection_parent_class)->finalize (object);
}

static void
meta_selection_class_init (MetaSelectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_selection_finalize;

  signals[OWNER_CHANGED] =
    g_signal_new ("owner-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  META_TYPE_SELECTION_SOURCE);
}

static void
meta_selection_init (MetaSelection *selection)
{
}

MetaSelection *
meta_selection_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_SELECTION,
                       NULL);
}

void
meta_selection_set_owner (MetaSelection       *selection,
                          MetaSelectionType    selection_type,
                          MetaSelectionSource *owner)
{
  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);

  if (selection->owners[selection_type] == owner)
    return;

  if (selection->owners[selection_type])
    g_signal_emit_by_name (selection->owners[selection_type], "inactive");

  g_set_object (&selection->owners[selection_type], owner);
  g_signal_emit_by_name (owner, "active", selection_type);
  g_signal_emit (selection, signals[OWNER_CHANGED], 0, selection_type, owner);
}

void
meta_selection_unset_owner (MetaSelection       *selection,
                            MetaSelectionType    selection_type,
                            MetaSelectionSource *owner)
{
  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);

  if (selection->owners[selection_type] == owner)
    {
      g_signal_emit_by_name (owner, "inactive");
      g_clear_object (&selection->owners[selection_type]);
      g_signal_emit (selection, signals[OWNER_CHANGED], 0,
                     selection_type, NULL);
    }
}

GList *
meta_selection_get_mimetypes (MetaSelection        *selection,
                              MetaSelectionType     selection_type)
{
  g_return_val_if_fail (META_IS_SELECTION (selection), NULL);
  g_return_val_if_fail (selection_type < META_N_SELECTION_TYPES, NULL);

  if (!selection->owners[selection_type])
    return NULL;

  return meta_selection_source_get_mimetypes (selection->owners[selection_type]);
}

static TransferRequest *
transfer_request_new (GOutputStream     *ostream,
                      MetaSelectionType  selection)
{
  TransferRequest *request;

  request = g_new0 (TransferRequest, 1);
  request->ostream = g_object_ref (ostream);
  request->selection = selection;
  return request;
}

static void
transfer_request_free (TransferRequest *request)
{
  g_clear_object (&request->istream);
  g_clear_object (&request->ostream);
  g_free (request);
}

static void
transfer_finished_cb (GOutputStream *stream,
                      GAsyncResult  *result,
                      GTask         *task)
{
  GError *error = NULL;

  g_output_stream_splice_finish (stream, result, &error);
  if (error)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static void
source_read_cb (MetaSelectionSource *source,
                GAsyncResult        *result,
                GTask               *task)
{
  TransferRequest *request;
  GInputStream *stream;
  GError *error = NULL;

  stream = meta_selection_source_read_finish (source, result, &error);
  if (!stream)
    {
      g_task_return_error (task, error);
      return;
    }

  request = g_task_get_task_data (task);
  request->istream = stream;

  g_output_stream_splice_async (request->ostream,
                                request->istream,
                                G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                G_PRIORITY_DEFAULT,
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback) transfer_finished_cb,
                                task);
}

void
meta_selection_transfer_async (MetaSelection        *selection,
                               MetaSelectionType     selection_type,
                               const gchar          *mimetype,
                               GOutputStream        *output,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  GTask *task;

  g_return_if_fail (META_IS_SELECTION (selection));
  g_return_if_fail (selection_type < META_N_SELECTION_TYPES);
  g_return_if_fail (G_IS_OUTPUT_STREAM (output));
  g_return_if_fail (mimetype != NULL);

  task = g_task_new (selection, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_selection_transfer_async);

  g_task_set_task_data (task,
                        transfer_request_new (output, selection_type),
                        (GDestroyNotify) transfer_request_free);
  meta_selection_source_read_async (selection->owners[selection_type],
                                    mimetype,
                                    cancellable,
                                    (GAsyncReadyCallback) source_read_cb,
                                    task);
}

gboolean
meta_selection_transfer_finish (MetaSelection  *selection,
                                GAsyncResult   *result,
                                GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, selection), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        meta_selection_transfer_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
