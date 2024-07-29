/*
 * Copyright (C) 2024 Red Hat Inc.
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

#pragma once

#include <sys/mman.h>

#include "core/meta-session-state.h"
#include "core/util-private.h"
#include "meta/window.h"

#define META_TYPE_SESSION_MANAGER (meta_session_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaSessionManager,
                      meta_session_manager,
                      META, SESSION_MANAGER,
                      GObject)

MetaSessionManager * meta_session_manager_new (const gchar  *name,
                                               GError      **error);

META_EXPORT_TEST
MetaSessionManager * meta_session_manager_new_for_fd (const gchar  *name,
                                                      int           fd,
                                                      GError      **error);

int meta_session_manager_get_fd (MetaSessionManager *manager);

gboolean meta_session_manager_get_session_exists (MetaSessionManager *manager,
                                                  const char         *name);

MetaSessionState * meta_session_manager_get_session (MetaSessionManager  *manager,
                                                     GType                type,
                                                     const char          *name);

void meta_session_manager_save (MetaSessionManager  *manager,
                                GAsyncReadyCallback  cb,
                                gpointer             user_data);

gboolean meta_session_manager_save_finish (MetaSessionManager  *manager,
                                           GAsyncResult        *res,
                                           GError             **error);

gboolean meta_session_manager_save_sync (MetaSessionManager  *manager,
                                         GError             **error);

void meta_session_manager_delete_session (MetaSessionManager *manager,
                                          const char         *name);
