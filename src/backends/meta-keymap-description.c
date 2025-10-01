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
#include <xkbcommon/xkbregistry.h>

#include "backends/meta-keymap-utils.h"
#include "core/meta-sealed-fd.h"

struct _MetaKeymapDescription
{
  gatomicrefcount ref_count;

  MetaKeymapDescriptionSource source;

  union {
    struct {
      char *rules;
      char *model;
      char *layout;
      char *variant;
      char *options;
    } rules;
    struct {
      MetaSealedFd *sealed_fd;
      enum xkb_keymap_format format;
    } fd;
  };
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
  keymap_description->source = META_KEYMAP_DESCRIPTION_SOURCE_RULES;
  keymap_description->rules.model = model ? g_strdup (model)
                                          : g_strdup (DEFAULT_XKB_MODEL);
  keymap_description->rules.layout = strdup_or_empty (layout);
  keymap_description->rules.variant = strdup_or_empty (variant);
  keymap_description->rules.options = strdup_or_empty (options);

  return keymap_description;
}

MetaKeymapDescription *
meta_keymap_description_new_from_fd (MetaSealedFd           *sealed_fd,
                                     enum xkb_keymap_format  format)
{
  MetaKeymapDescription *keymap_description;

  keymap_description = g_new0 (MetaKeymapDescription, 1);
  g_atomic_ref_count_init (&keymap_description->ref_count);
  keymap_description->source = META_KEYMAP_DESCRIPTION_SOURCE_FD;
  g_set_object (&keymap_description->fd.sealed_fd, sealed_fd);
  keymap_description->fd.format = format;

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
      switch (keymap_description->source)
        {
        case META_KEYMAP_DESCRIPTION_SOURCE_RULES:
          g_free (keymap_description->rules.model);
          g_free (keymap_description->rules.layout);
          g_free (keymap_description->rules.variant);
          g_free (keymap_description->rules.options);
          break;
        case META_KEYMAP_DESCRIPTION_SOURCE_FD:
          g_clear_object (&keymap_description->fd.sealed_fd);
          break;
        }

      g_free (keymap_description);
    }
}

MetaKeymapDescriptionSource
meta_keymap_description_get_source (MetaKeymapDescription *keymap_description)
{
  return keymap_description->source;
}

struct xkb_keymap *
meta_keymap_description_create_xkb_keymap (MetaKeymapDescription  *keymap_description,
                                           GError                **error)
{
  switch (keymap_description->source)
    {
    case META_KEYMAP_DESCRIPTION_SOURCE_RULES:
      {
        struct xkb_rule_names names;
        struct xkb_context *xkb_context;
        struct xkb_keymap *xkb_keymap;

        names.rules = DEFAULT_XKB_RULES_FILE;
        names.model = keymap_description->rules.model;
        names.layout = keymap_description->rules.layout;
        names.variant = keymap_description->rules.variant;
        names.options = keymap_description->rules.options;

        xkb_context = meta_create_xkb_context ();
        xkb_keymap = xkb_keymap_new_from_names (xkb_context,
                                                &names,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_context_unref (xkb_context);

        if (!xkb_keymap)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Failed to create XKB keymap with "
                         "rules=%s, model=%s, layout=%s, "
                         "variant=%s, options=%s",
                         keymap_description->rules.rules,
                         keymap_description->rules.model,
                         keymap_description->rules.layout,
                         keymap_description->rules.variant,
                         keymap_description->rules.options);
            return NULL;
          }

        return xkb_keymap;
      }
    case META_KEYMAP_DESCRIPTION_SOURCE_FD:
      {
        g_autoptr (GBytes) keymap_bytes = NULL;
        struct xkb_context *xkb_context;
        struct xkb_keymap *xkb_keymap;

        keymap_bytes =
          meta_sealed_fd_get_bytes (keymap_description->fd.sealed_fd, error);
        if (!keymap_bytes)
          return NULL;

        xkb_context = meta_create_xkb_context ();
        xkb_keymap =
          xkb_keymap_new_from_string (xkb_context,
                                      g_bytes_get_data (keymap_bytes, NULL),
                                      keymap_description->fd.format,
                                      XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_context_unref (xkb_context);

        if (!xkb_keymap)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Failed to create XKB keymap from file descriptor");
            return NULL;
          }

        return xkb_keymap;
      }
    }

  g_assert_not_reached ();
}
