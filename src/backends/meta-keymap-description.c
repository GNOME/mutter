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

struct _MetaKeymapDescriptionOwner
{
  gatomicrefcount ref_count;
};

struct _MetaKeymapDescription
{
  gatomicrefcount ref_count;

  MetaKeymapDescriptionSource source;

  gboolean is_locked;
  MetaKeymapDescriptionOwner *owner;
  MetaKeymapDescriptionOwner *resets_owner;

  union {
    struct {
      char *rules;
      char *model;
      char *layout;
      char *variant;
      char *options;
      GStrv display_names;
      GStrv short_names;
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

MetaKeymapDescriptionOwner *
meta_keymap_description_owner_new (void)
{
  MetaKeymapDescriptionOwner *owner;

  owner = g_new0 (MetaKeymapDescriptionOwner, 1);
  g_atomic_ref_count_init (&owner->ref_count);

  return owner;
}

MetaKeymapDescriptionOwner *
meta_keymap_description_owner_ref (MetaKeymapDescriptionOwner *owner)
{
  g_atomic_ref_count_inc (&owner->ref_count);
  return owner;
}

void
meta_keymap_description_owner_unref (MetaKeymapDescriptionOwner *owner)
{
  if (g_atomic_ref_count_dec (&owner->ref_count))
    g_free (owner);
}

MetaKeymapDescription *
meta_keymap_description_new_from_rules (const char *model,
                                        const char *layout,
                                        const char *variant,
                                        const char *options,
                                        GStrv       display_names,
                                        GStrv       short_names)
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
  keymap_description->rules.display_names = g_strdupv (display_names);
  keymap_description->rules.short_names = g_strdupv (short_names);

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
          g_strfreev (keymap_description->rules.display_names);
          g_strfreev (keymap_description->rules.short_names);
          break;
        case META_KEYMAP_DESCRIPTION_SOURCE_FD:
          g_clear_object (&keymap_description->fd.sealed_fd);
          break;
        }

      g_clear_pointer (&keymap_description->owner,
                       meta_keymap_description_owner_unref);
      g_clear_pointer (&keymap_description->resets_owner,
                       meta_keymap_description_owner_unref);

      g_free (keymap_description);
    }
}

MetaKeymapDescriptionSource
meta_keymap_description_get_source (MetaKeymapDescription *keymap_description)
{
  return keymap_description->source;
}

gboolean
meta_keymap_description_direct_equal (MetaKeymapDescription *keymap_description,
                                      MetaKeymapDescription *other)
{
  return keymap_description == other;
}

static char *
maybe_derive_short_name (struct rxkb_context *rxkb_context,
                         const char          *layout_name)
{
  struct rxkb_layout *rxkb_layout;
  if (!layout_name)
    return NULL;

  for (rxkb_layout = rxkb_layout_first (rxkb_context);
       rxkb_layout;
       rxkb_layout = rxkb_layout_next (rxkb_layout))
    {
      if (g_strcmp0 (layout_name,
                     rxkb_layout_get_description (rxkb_layout)) == 0)
        return g_strdup (rxkb_layout_get_brief (rxkb_layout));
    }

  return NULL;
}

struct xkb_keymap *
meta_keymap_description_create_xkb_keymap (MetaKeymapDescription  *keymap_description,
                                           GStrv                  *out_display_names,
                                           GStrv                  *out_short_names,
                                           GError                **error)
{
  g_auto (GStrv) display_names = NULL;
  g_auto (GStrv) short_names = NULL;
  struct xkb_keymap *xkb_keymap = NULL;

  switch (keymap_description->source)
    {
    case META_KEYMAP_DESCRIPTION_SOURCE_RULES:
      {
        struct xkb_rule_names names;
        struct xkb_context *xkb_context;

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

        if (out_display_names)
          display_names = g_strdupv (keymap_description->rules.display_names);
        if (out_short_names)
          short_names = g_strdupv (keymap_description->rules.short_names);

        break;
      }
    case META_KEYMAP_DESCRIPTION_SOURCE_FD:
      {
        g_autoptr (GBytes) keymap_bytes = NULL;
        struct xkb_context *xkb_context;

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

        break;
      }
    }

  g_assert (xkb_keymap);

  if (out_display_names && !display_names)
    {
      g_autoptr (GStrvBuilder) display_names_builder = NULL;
      xkb_layout_index_t n_layouts, i;

      display_names_builder = g_strv_builder_new ();
      n_layouts = xkb_keymap_num_layouts (xkb_keymap);
      for (i = 0; i < n_layouts; i++)
        {
          const char *display_name;

          display_name = xkb_keymap_layout_get_name (xkb_keymap, i);
          g_strv_builder_add (display_names_builder,
                              display_name ? display_name : "");
        }

      display_names = g_strv_builder_end (display_names_builder);
    }

  if (out_short_names && !short_names)
    {
      g_autoptr (GStrvBuilder) short_names_builder = NULL;
      struct rxkb_context *rxkb_context = NULL;

      short_names_builder = g_strv_builder_new ();

      rxkb_context = rxkb_context_new (RXKB_CONTEXT_LOAD_EXOTIC_RULES);
      if (rxkb_context_parse (rxkb_context, "evdev"))
        {
          xkb_layout_index_t n_layouts, i;

          n_layouts = xkb_keymap_num_layouts (xkb_keymap);
          for (i = 0; i < n_layouts; i++)
            {
              g_autofree char *short_name = NULL;

              short_name = maybe_derive_short_name (rxkb_context,
                                                    display_names[i]);
              g_strv_builder_add (short_names_builder,
                                  short_name ? short_name : "");
            }

          short_names = g_strv_builder_end (short_names_builder);
        }
      rxkb_context_unref (rxkb_context);
    }

  if (out_display_names)
    *out_display_names = g_steal_pointer (&display_names);
  if (out_short_names)
    *out_short_names = g_steal_pointer (&short_names);

  return xkb_keymap;
}

void
meta_keymap_description_lock (MetaKeymapDescription      *keymap_description,
                              MetaKeymapDescriptionOwner *owner)
{
  g_return_if_fail (!keymap_description->owner);

  keymap_description->is_locked = TRUE;
  keymap_description->owner = meta_keymap_description_owner_ref (owner);
}

void
meta_keymap_description_unlock (MetaKeymapDescription      *keymap_description,
                                MetaKeymapDescriptionOwner *owner)
{
  g_return_if_fail (!keymap_description->owner);
  g_return_if_fail (!keymap_description->is_locked);

  keymap_description->owner = meta_keymap_description_owner_ref (owner);
}

void
meta_keymap_description_reset_owner (MetaKeymapDescription      *keymap_description,
                                     MetaKeymapDescriptionOwner *owner)
{
  g_clear_pointer (&keymap_description->resets_owner,
                   meta_keymap_description_owner_unref);
  keymap_description->resets_owner = meta_keymap_description_owner_ref (owner);
}

gboolean
meta_keymap_description_is_locked (MetaKeymapDescription *keymap_description)
{
  return keymap_description->is_locked;
}

MetaKeymapDescriptionOwner *
meta_keymap_description_get_owner (MetaKeymapDescription *keymap_description)
{
  return keymap_description->owner;
}

MetaKeymapDescriptionOwner *
meta_keymap_description_resets_owner (MetaKeymapDescription *keymap_description)
{
  return keymap_description->resets_owner;
}
