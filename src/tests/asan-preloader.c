/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include <link.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char *program_invocation_name;

void asan_auto_loader () __attribute__((constructor));

typedef struct _DlIteratorState
{
  char asan_lib_path[PATH_MAX];
  bool loader_found;
} DlIteratorState;

static int
dl_iterate_cb (struct dl_phdr_info *info,
               size_t               size,
               void                *user_data)
{
  DlIteratorState *state = user_data;

  if (info->dlpi_name)
    {
      char lib_path[PATH_MAX];
      char *lib_name;

      strcpy (lib_path, info->dlpi_name);
      lib_name = basename (lib_path);

      if (strstr (lib_name, "libasan.so") == lib_name &&
          state->loader_found)
        {
          strcpy (state->asan_lib_path, lib_path);
          return 1;
        }
      else if (strstr (lib_name, "libasan-preloader.so") == lib_name)
        {
          state->loader_found = true;
        }
      else if (strstr (lib_name, "lib") == lib_name &&
               strstr (lib_name, ".so") &&
               !state->loader_found)
        {
          fprintf (stderr,
                   "libasan-preloader.so must be the first loaded library\n");
          _exit (EXIT_FAILURE);
        }
    }

  return 0;
}

void
asan_auto_loader ()
{
  DlIteratorState state = {};

  dl_iterate_phdr (dl_iterate_cb, &state);

  if (state.asan_lib_path[0] != '\0')
    {
      fprintf (stderr, "Loading %s for %s\n",
               state.asan_lib_path, program_invocation_name);

      if (dlopen (state.asan_lib_path, RTLD_NOW | RTLD_GLOBAL) == NULL)
        fprintf (stderr, "Failed to load: %s", dlerror ());
    }
}
