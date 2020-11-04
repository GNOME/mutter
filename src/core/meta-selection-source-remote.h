/*
 * Copyright (C) 2020 Red Hat
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

#ifndef META_SELECTION_SOURCE_REMOTE_H
#define META_SELECTION_SOURCE_REMOTE_H

#include "backends/meta-remote-desktop.h"
#include "meta/meta-selection-source.h"

#define META_TYPE_SELECTION_SOURCE_REMOTE (meta_selection_source_remote_get_type ())
G_DECLARE_FINAL_TYPE (MetaSelectionSourceRemote,
                      meta_selection_source_remote,
                      META, SELECTION_SOURCE_REMOTE,
                      MetaSelectionSource)

void meta_selection_source_remote_complete_transfer (MetaSelectionSourceRemote *source_remote,
                                                     int                        fd,
                                                     GTask                     *task);

void meta_selection_source_remote_cancel_transfer (MetaSelectionSourceRemote *source_remote,
                                                   GTask                     *task);

MetaSelectionSourceRemote * meta_selection_source_remote_new (MetaRemoteDesktopSession *session,
                                                              GList                    *mime_types);

#endif /* META_SELECTION_SOURCE_REMOTE_H */
