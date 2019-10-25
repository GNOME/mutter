/*
 * MetaSubprocessLauncher
 *
 * Copyright (C) 2019 Sergio Costas (rastersoft@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __META_SUBPROCESS_LAUNCHER_H__
#define __META_SUBPROCESS_LAUNCHER__

#include <glib-object.h>
#include <gio/gio.h>
#include <meta/meta-window-actor.h>

typedef enum {
    META_SUBPROCESS_LAUNCHER_ERROR_ALREADY_LAUNCHED,
    META_SUBPROCESS_LAUNCHER_ERROR_NO_SOCKET_PAIR,
    META_SUBPROCESS_LAUNCHER_ERROR_NOT_WAYLAND,
    META_SUBPROCESS_LAUNCHER_ERROR_SUBPROCESS_NOT_LAUNCHED,
    META_SUBPROCESS_LAUNCHER_ERROR_SUBPROCESS_DIED
} MetaSubprocessLauncherErrorEnum;

G_BEGIN_DECLS

#define META_TYPE_SUBPROCESS_LAUNCHER (meta_subprocess_launcher_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaSubprocessLauncher, meta_subprocess_launcher, META, SUBPROCESS_LAUNCHER, GObject)

#define META_SUBPROCESS_LAUNCHER_ERROR meta_subprocess_launcher_error_quark ()

META_EXPORT
MetaSubprocessLauncher *meta_subprocess_launcher_new         (GSubprocessFlags flags);

META_EXPORT
GSubprocess *meta_subprocess_launcher_spawn                  (MetaSubprocessLauncher   *self,
                                                              GError                  **error,
                                                              const gchar              *argv0,
                                                              ...) G_GNUC_NULL_TERMINATED;

META_EXPORT
GSubprocess *meta_subprocess_launcher_spawnv                 (MetaSubprocessLauncher  *self,
                                                              const gchar * const     *argv,
                                                              GError                 **error);

META_EXPORT
gboolean meta_subprocess_launcher_query_window_belongs_to    (MetaSubprocessLauncher  *self,
                                                              MetaWindow              *window,
                                                              GError                 **error);

META_EXPORT
void meta_subprocess_launcher_set_environ                    (MetaSubprocessLauncher   *self,
                                                              gchar                   **env);

META_EXPORT
void meta_subprocess_launcher_setenv                         (MetaSubprocessLauncher   *self,
                                                              const gchar              *variable,
                                                              const gchar              *value,
                                                              gboolean                  overwrite);

META_EXPORT
void meta_subprocess_launcher_unsetenv                       (MetaSubprocessLauncher *self,
                                                              const gchar            *variable);

META_EXPORT
const gchar * meta_subprocess_launcher_getenv                (MetaSubprocessLauncher   *self,
                                                              const gchar              *variable);

META_EXPORT
void meta_subprocess_launcher_set_cwd                        (MetaSubprocessLauncher   *self,
                                                              const gchar              *cwd);
META_EXPORT
void meta_subprocess_launcher_set_flags                      (MetaSubprocessLauncher   *self,
                                                              GSubprocessFlags          flags);

META_EXPORT
GIOStream *meta_subprocess_launcher_create_env_socket        (MetaSubprocessLauncher   *self,
                                                              const gchar              *variable,
                                                              GError                  **error);

META_EXPORT
GIOStream *meta_subprocess_launcher_create_param_socket      (MetaSubprocessLauncher   *self,
                                                              const gchar              *parameter,
                                                              const gboolean            single,
                                                              GError                  **error);

G_END_DECLS

#endif /* __VIEWER_FILE_H__ */
