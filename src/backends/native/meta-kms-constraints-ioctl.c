/*
 * Copyright (C) 2026 Red Hat
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
 */

#include "config.h"

#include "backends/native/meta-kms-constraints-ioctl.h"

#include <errno.h>
#include <stdint.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-constraints.h"
#include "backends/native/meta-kms-constraints-query.h"

G_STATIC_ASSERT (sizeof (struct drm_mode_list_constraints) == 48);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_list_constraints,
                                  generation) == 8);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_list_constraints,
                                  data) == 16);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_list_constraints,
                                  size) == 24);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_list_constraints,
                                  reserved) == 32);

static int
query_constraints_ioctl (gpointer  user_data,
                         uint32_t  crtc_id,
                         uint64_t *generation,
                         void     *data,
                         uint32_t *size)
{
  int fd = *(int *) user_data;
  struct drm_mode_list_constraints request = {
    .crtc_id = crtc_id,
    .generation = *generation,
    .data = (uintptr_t) data,
    .size = *size,
  };
  int result;
  int error_number;

  errno = 0;
  result = drmIoctl (fd, DRM_IOCTL_MODE_LIST_CONSTRAINTS, &request);
  if (result == 0)
    error_number = 0;
  else if (result == -1 && errno != 0)
    error_number = errno;
  else
    error_number = EPROTO;

  if (error_number == 0 || error_number == ENOSPC)
    {
      *generation = request.generation;
      *size = request.size;
    }

  return -error_number;
}

MetaKmsConstraintsList *
meta_kms_constraints_query_fd (int       fd,
                               uint32_t  crtc_id,
                               GError  **error)
{
  g_return_val_if_fail (fd >= 0, NULL);

  return meta_kms_constraints_query (crtc_id,
                                     query_constraints_ioctl,
                                     &fd,
                                     error);
}
