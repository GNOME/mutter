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

#include "config.h"

#include "backends/meta-keymap-description-private.h"

#include <glib-object.h>

#include "backends/meta-keymap-utils.h"

struct _MetaKeymapDescription
{
  gatomicrefcount ref_count;

  char *rules;
  char *model;
  char *layout;
  char *variant;
  char *options;
};

#define DEFAULT_XKB_RULES_FILE "evdev"
#define DEFAULT_XKB_MODEL "pc105+inet"

G_DEFINE_BOXED_TYPE (MetaKeymapDescription, meta_keymap_description,
                     meta_keymap_description_ref,
                     meta_keymap_description_unref)

static char *
strdup_or_empty (const char *string)
{
  return string ? g_strdup (string) : g_strdup ("");
}

MetaKeymapDescription *
meta_keymap_description_new_from_rules (const char *model,
                                        const char *layout,
                                        const char *variant,
                                        const char *options)
{
  MetaKeymapDescription *keymap_description;

  keymap_description = g_new0 (MetaKeymapDescription, 1);
  g_atomic_ref_count_init (&keymap_description->ref_count);
  keymap_description->model = model ? g_strdup (model)
                                    : g_strdup (DEFAULT_XKB_MODEL);
  keymap_description->layout = strdup_or_empty (layout);
  keymap_description->variant = strdup_or_empty (variant);
  keymap_description->options = strdup_or_empty (options);

  return keymap_description;
}

MetaKeymapDescription *
meta_keymap_description_ref (MetaKeymapDescription *keymap_description)
{
  g_atomic_ref_count_inc (&keymap_description->ref_count);
  return keymap_description;
}

void
meta_keymap_description_unref (MetaKeymapDescription *keymap_description)
{
  if (g_atomic_ref_count_dec (&keymap_description->ref_count))
    {
      g_free (keymap_description->model);
      g_free (keymap_description->layout);
      g_free (keymap_description->variant);
      g_free (keymap_description->options);
      g_free (keymap_description);
    }
}

void
meta_keymap_description_get_rules (MetaKeymapDescription  *keymap_description,
                                   char                  **model,
                                   char                  **layout,
                                   char                  **variant,
                                   char                  **options)
{
  *model = g_strdup (keymap_description->model);
  *layout = g_strdup (keymap_description->layout);
  *variant = g_strdup (keymap_description->variant);
  *options = g_strdup (keymap_description->options);
}
