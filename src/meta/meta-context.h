/*
 * Copyright (C) 2019 Red Hat Inc.
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

#include "meta/common.h"
#include "meta/types.h"

#define META_TYPE_CONTEXT (meta_context_get_type ())
META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaContext, meta_context, META, CONTEXT, GObject)

META_EXPORT
MetaContext * meta_create_context (const char *name);

META_EXPORT
void meta_context_destroy (MetaContext *context);

META_EXPORT
void meta_context_add_option_entries (MetaContext        *context,
                                      const GOptionEntry *entries,
                                      const char         *translation_domain);

META_EXPORT
void meta_context_add_option_group (MetaContext  *context,
                                    GOptionGroup *group);

META_EXPORT
void meta_context_set_plugin_gtype (MetaContext *context,
                                    GType        plugin_gtype);

META_EXPORT
void meta_context_set_plugin_name (MetaContext *context,
                                   const char  *plugin_name);

META_EXPORT
void meta_context_set_gnome_wm_keybindings (MetaContext *context,
                                            const char  *wm_keybindings);

META_EXPORT
gboolean meta_context_configure (MetaContext   *context,
                                 int           *argc,
                                 char        ***argv,
                                 GError       **error);

META_EXPORT
gboolean meta_context_setup (MetaContext  *context,
                             GError      **error);

META_EXPORT
gboolean meta_context_start (MetaContext  *context,
                             GError      **error);

META_EXPORT
gboolean meta_context_run_main_loop (MetaContext  *context,
                                     GError      **error);

META_EXPORT
void meta_context_notify_ready (MetaContext *context);

META_EXPORT
void meta_context_terminate (MetaContext *context);

META_EXPORT
void meta_context_terminate_with_error (MetaContext *context,
                                        GError      *error);

META_EXPORT
MetaCompositorType meta_context_get_compositor_type (MetaContext *context);

META_EXPORT
gboolean meta_context_is_replacing (MetaContext *context);

META_EXPORT
MetaBackend * meta_context_get_backend (MetaContext *context);

META_EXPORT
MetaDisplay * meta_context_get_display (MetaContext *context);

META_EXPORT
gboolean meta_context_raise_rlimit_nofile (MetaContext  *context,
                                           GError      **error);

META_EXPORT
gboolean meta_context_restore_rlimit_nofile (MetaContext  *context,
                                             GError      **error);

META_EXPORT
MetaDebugControl * meta_context_get_debug_control (MetaContext *context);
