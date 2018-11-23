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
#ifndef META_SELECTION_H
#define META_SELECTION_H

#include <gio/gio.h>

#include "meta/display.h"
#include "meta/meta-selection-source.h"

#define META_TYPE_SELECTION (meta_selection_get_type ())

G_DECLARE_FINAL_TYPE (MetaSelection,
                      meta_selection,
                      META, SELECTION,
                      GObject)

MetaSelection *
         meta_selection_new                  (MetaDisplay *display);

void     meta_selection_set_owner            (MetaSelection        *selection,
                                              MetaSelectionType     selection_type,
                                              MetaSelectionSource  *owner);
void     meta_selection_unset_owner          (MetaSelection        *selection,
                                              MetaSelectionType     selection_type,
                                              MetaSelectionSource  *owner);

GList *  meta_selection_get_mimetypes        (MetaSelection        *selection,
                                              MetaSelectionType     selection_type);

void     meta_selection_transfer_async       (MetaSelection        *selection,
                                              MetaSelectionType     selection_type,
                                              const gchar          *mimetype,
                                              GOutputStream        *output,
                                              GCancellable         *cancellable,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);
gboolean meta_selection_transfer_finish      (MetaSelection        *selection,
                                              GAsyncResult         *result,
                                              GError              **error);

#endif /* META_SELECTION_H */
