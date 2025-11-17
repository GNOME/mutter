/*
 * Copyright (C) 2025 Red Hat
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
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"

#include "meta-dbus-clipboard.h"

#define META_TYPE_CLIPBOARD_SESSION (meta_clipboard_session_get_type ())
G_DECLARE_FINAL_TYPE (MetaClipboardSession, meta_clipboard_session,
                      META, CLIPBOARD_SESSION,
                      MetaDBusClipboardSkeleton)

gboolean meta_clipboard_session_enable (MetaClipboardSession  *session,
                                        GVariant              *arg_options,
                                        GError               **error);

gboolean meta_clipboard_session_disable (MetaClipboardSession  *session,
                                         GError               **error);

gboolean meta_clipboard_session_set_selection (MetaClipboardSession  *session,
                                               GVariant              *options,
                                               GError               **error);

gboolean meta_clipboard_session_selection_write (MetaClipboardSession  *session,
                                                 unsigned int           serial,
                                                 GUnixFDList          **out_fd_list,
                                                 GVariant             **out_fd_variant,
                                                 GError               **error);

gboolean meta_clipboard_session_selection_read (MetaClipboardSession  *session,
                                                const char            *mime_type,
                                                GUnixFDList          **out_fd_list,
                                                GVariant             **out_fd_variant,
                                                GError               **error);

MetaClipboardSession * meta_clipboard_session_new (MetaBackend      *backend,
                                                   const char       *peer_name,
                                                   GDBusConnection  *connection,
                                                   const char       *object_path,
                                                   GError          **error);

void meta_clipboard_session_request_transfer (MetaClipboardSession *session,
                                              const char           *mime_type,
                                              GTask                *task);
