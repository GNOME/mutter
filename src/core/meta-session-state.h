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

#include <glib-object.h>

#include <gvdb/gvdb-builder.h>
#include <gvdb/gvdb-reader.h>

#include "meta/window.h"

#define META_TYPE_SESSION_STATE (meta_session_state_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaSessionState,
                          meta_session_state,
                          META, SESSION_STATE,
                          GObject)

struct _MetaSessionStateClass
{
  GObjectClass parent_class;

  gboolean (* serialize) (MetaSessionState *state,
                          GHashTable       *gvdb_data);

  gboolean (* parse) (MetaSessionState  *state,
                      GvdbTable         *data,
                      GError           **error);

  void (* save_window) (MetaSessionState *state,
                        const char       *name,
                        MetaWindow       *window);

  gboolean (* restore_window) (MetaSessionState *state,
                               const char       *name,
                               MetaWindow       *window);

  void (* remove_window) (MetaSessionState *state,
                          const char       *name);
};

const char * meta_session_state_get_name (MetaSessionState *state);

gboolean meta_session_state_serialize (MetaSessionState *state,
                                       GHashTable       *gvdb_data);

gboolean meta_session_state_parse (MetaSessionState  *state,
                                   GvdbTable         *data,
                                   GError           **error);

void meta_session_state_save_window (MetaSessionState *state,
                                     const char       *name,
                                     MetaWindow       *window);

gboolean meta_session_state_restore_window (MetaSessionState *state,
                                            const char       *name,
                                            MetaWindow       *window);

void meta_session_state_remove_window (MetaSessionState *state,
                                       const char       *name);
