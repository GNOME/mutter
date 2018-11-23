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

#include "meta/meta-selection.h"
#include "meta/meta-selection-source.h"

typedef struct MetaSelectionSourcePrivate MetaSelectionSourcePrivate;

struct MetaSelectionSourcePrivate
{
  MetaSelectionType selection;
  guint active : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaSelectionSource,
                            meta_selection_source,
                            G_TYPE_OBJECT)

enum
{
  ACTIVE,
  INACTIVE,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static void
meta_selection_source_active (MetaSelectionSource *source,
                              MetaSelectionType    selection)
{
  MetaSelectionSourcePrivate *priv =
    meta_selection_source_get_instance_private (source);

  priv->selection = selection;
  priv->active = TRUE;
}

static void
meta_selection_source_inactive (MetaSelectionSource *source)
{
  MetaSelectionSourcePrivate *priv =
    meta_selection_source_get_instance_private (source);

  priv->active = FALSE;
}

static void
meta_selection_source_class_init (MetaSelectionSourceClass *klass)
{
  klass->active = meta_selection_source_active;
  klass->inactive = meta_selection_source_inactive;

  signals[ACTIVE] =
    g_signal_new ("active",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaSelectionSourceClass, active),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_UINT);
  signals[INACTIVE] =
    g_signal_new ("inactive",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaSelectionSourceClass, inactive),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_selection_source_init (MetaSelectionSource *source)
{
}

void
meta_selection_source_read_async (MetaSelectionSource  *source,
                                  const gchar          *mimetype,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data)
{
  META_SELECTION_SOURCE_GET_CLASS (source)->read_async (source,
                                                        mimetype,
                                                        cancellable,
                                                        callback,
                                                        user_data);
}

/**
 * meta_selection_source_read_finish:
 * @source: The selection source
 * @result: The async result
 * @error: Location for returned error
 *
 * Finishes a read from the selection source.
 *
 * Returns: (transfer full): The resulting #GInputStream
 */
GInputStream *
meta_selection_source_read_finish (MetaSelectionSource  *source,
                                   GAsyncResult         *result,
                                   GError              **error)
{
  return META_SELECTION_SOURCE_GET_CLASS (source)->read_finish (source,
                                                                result,
                                                                error);
}

/**
 * meta_selection_source_get_mimetypes:
 * @source: The selection source
 *
 * Returns the list of supported mimetypes.
 *
 * Returns: (element-type utf8) (transfer full): The supported mimetypes
 */
GList *
meta_selection_source_get_mimetypes (MetaSelectionSource  *source)
{
  return META_SELECTION_SOURCE_GET_CLASS (source)->get_mimetypes (source);
}

/**
 * meta_selection_source_get_selection:
 * @source: the selection source
 * @selection: (out): return location for the current selection type
 *
 * If the source is currently the owner of a selection, returns #TRUE
 * and fills in @selection with the currently owned selection. Otherwise
 * returns #FALSE.
 *
 * Returns: #TRUE if the source owns a selection.
 **/
gboolean
meta_selection_source_get_selection (MetaSelectionSource *source,
                                     MetaSelectionType   *selection)
{
  MetaSelectionSourcePrivate *priv =
    meta_selection_source_get_instance_private (source);

  if (!priv->active)
    return FALSE;

  *selection = priv->selection;
  return TRUE;
}
