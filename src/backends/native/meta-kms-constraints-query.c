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

#include "backends/native/meta-kms-constraints-query.h"

#include <errno.h>
#include <gio/gio.h>

#include "backends/native/meta-drm-constraints.h"
#include "backends/native/meta-kms-constraints-decoder.h"

#define MAX_QUERY_ATTEMPTS 8

static gboolean
metadata_is_valid (uint64_t generation,
                   uint32_t size)
{
  return generation != 0 &&
         size >= sizeof (struct drm_mode_constraints_list) &&
         size <= DRM_MODE_CONSTRAINTS_MAX_BYTES;
}

static void
set_query_error (GError **error,
                 int      query_result)
{
  int error_number;

  if (query_result >= 0)
    error_number = EPROTO;
  else
    error_number = -query_result;

  g_set_error (error,
               G_IO_ERROR,
               g_io_error_from_errno (error_number),
               "Failed to list KMS constraints: %s",
               g_strerror (error_number));
}

MetaKmsConstraintsList *
meta_kms_constraints_query (uint32_t                     crtc_id,
                            MetaKmsConstraintsQueryFunc  query_func,
                            gpointer                     user_data,
                            GError                     **error)
{
  g_autofree uint8_t *buffer = NULL;
  uint64_t expected_generation = 0;
  uint32_t capacity = 0;
  unsigned int attempt;

  g_return_val_if_fail (crtc_id != 0, NULL);
  g_return_val_if_fail (query_func != NULL, NULL);

  for (attempt = 0; attempt < MAX_QUERY_ATTEMPTS; attempt++)
    {
      g_autoptr (MetaKmsConstraintsList) list = NULL;
      uint64_t generation = expected_generation;
      uint32_t size = capacity;
      int result;

      result = query_func (user_data,
                           crtc_id,
                           &generation,
                           buffer,
                           &size);
      if (result == -ESTALE)
        {
          expected_generation = 0;
          continue;
        }

      if (result == -ENOSPC)
        {
          uint8_t *resized_buffer;

          if (!metadata_is_valid (generation, size) || size <= capacity)
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_INVALID_DATA,
                                   "Invalid KMS constraints query metadata");
              return NULL;
            }

          resized_buffer = g_try_realloc (buffer, size);
          if (!resized_buffer)
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_NO_SPACE,
                                   "Allocate KMS constraints snapshot");
              return NULL;
            }
          buffer = resized_buffer;
          capacity = size;
          expected_generation = generation;
          continue;
        }

      if (result != 0)
        {
          set_query_error (error, result);
          return NULL;
        }

      if (!metadata_is_valid (generation, size) ||
          (buffer && size > capacity))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints query metadata");
          return NULL;
        }

      if (!buffer)
        {
          buffer = g_try_malloc (size);
          if (!buffer)
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_NO_SPACE,
                                   "Allocate KMS constraints snapshot");
              return NULL;
            }
          capacity = size;
          expected_generation = generation;
          continue;
        }

      list = meta_kms_constraints_decode (buffer, size, error);
      if (!list)
        return NULL;
      if (meta_kms_constraints_list_get_generation (list) != generation)
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "KMS constraints generation mismatch");
          return NULL;
        }

      return g_steal_pointer (&list);
    }

  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_BUSY,
                       "KMS constraints changed during every query attempt");
  return NULL;
}
