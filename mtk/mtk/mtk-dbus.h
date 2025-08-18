/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2025 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "mtk/mtk-macros.h"

typedef struct _MtkDbusPidfd MtkDbusPidfd;

MTK_EXPORT
void mtk_dbus_pidfd_free (MtkDbusPidfd *pfd);

MTK_EXPORT
pid_t mtk_dbus_pidfd_get_pid (MtkDbusPidfd *dbus_pidfd);

MTK_EXPORT
int mtk_dbus_pidfd_get_pidfd (MtkDbusPidfd *dbus_pidfd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkDbusPidfd, mtk_dbus_pidfd_free);

MTK_EXPORT
MtkDbusPidfd * mtk_dbus_pidfd_new_for_connection_finish (GDBusConnection  *connection,
                                                         GAsyncResult     *result,
                                                         GError          **error);

MTK_EXPORT
void mtk_dbus_pidfd_new_for_connection_async (GDBusConnection     *connection,
                                              const char          *sender,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data);
