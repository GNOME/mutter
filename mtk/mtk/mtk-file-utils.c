/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2026 Red Hat
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

#include "mtk-file-utils.h"

gboolean
mtk_is_fd_readable (int fd)
{
  GPollFD poll_fd;

  poll_fd.fd = fd;
  poll_fd.events = G_IO_IN;
  poll_fd.revents = 0;

  if (!g_poll (&poll_fd, 1, 0))
    return FALSE;

  return (poll_fd.revents & (G_IO_IN | G_IO_NVAL)) != 0;
}
