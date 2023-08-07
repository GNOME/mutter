/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "core/meta-accel-parse.h"
#include "clutter/clutter-keyval.h"
#include "meta/util.h"

#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "core/keybindings-private.h"

/* This is copied from GTK+ and modified to work with mutter's
 * internal structures. Originating code comes from gtk/gtkaccelgroup.c
 */

static inline gboolean
is_alt (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'a' || string[1] == 'A') &&
          (string[2] == 'l' || string[2] == 'L') &&
          (string[3] == 't' || string[3] == 'T') &&
          (string[4] == '>'));
}

static inline gboolean
is_ctl (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 't' || string[2] == 'T') &&
          (string[3] == 'l' || string[3] == 'L') &&
          (string[4] == '>'));
}

static inline gboolean
is_modx (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'm' || string[1] == 'M') &&
          (string[2] == 'o' || string[2] == 'O') &&
          (string[3] == 'd' || string[3] == 'D') &&
          (string[4] >= '1' && string[4] <= '5') &&
          (string[5] == '>'));
}

static inline gboolean
is_ctrl (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 't' || string[2] == 'T') &&
          (string[3] == 'r' || string[3] == 'R') &&
          (string[4] == 'l' || string[4] == 'L') &&
          (string[5] == '>'));
}

static inline gboolean
is_shft (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'h' || string[2] == 'H') &&
          (string[3] == 'f' || string[3] == 'F') &&
          (string[4] == 't' || string[4] == 'T') &&
          (string[5] == '>'));
}

static inline gboolean
is_shift (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'h' || string[2] == 'H') &&
          (string[3] == 'i' || string[3] == 'I') &&
          (string[4] == 'f' || string[4] == 'F') &&
          (string[5] == 't' || string[5] == 'T') &&
          (string[6] == '>'));
}

static inline gboolean
is_control (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'c' || string[1] == 'C') &&
          (string[2] == 'o' || string[2] == 'O') &&
          (string[3] == 'n' || string[3] == 'N') &&
          (string[4] == 't' || string[4] == 'T') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == 'o' || string[6] == 'O') &&
          (string[7] == 'l' || string[7] == 'L') &&
          (string[8] == '>'));
}

static inline gboolean
is_meta (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'm' || string[1] == 'M') &&
          (string[2] == 'e' || string[2] == 'E') &&
          (string[3] == 't' || string[3] == 'T') &&
          (string[4] == 'a' || string[4] == 'A') &&
          (string[5] == '>'));
}

static inline gboolean
is_super (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 's' || string[1] == 'S') &&
          (string[2] == 'u' || string[2] == 'U') &&
          (string[3] == 'p' || string[3] == 'P') &&
          (string[4] == 'e' || string[4] == 'E') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == '>'));
}

static inline gboolean
is_hyper (const gchar *string)
{
  return ((string[0] == '<') &&
          (string[1] == 'h' || string[1] == 'H') &&
          (string[2] == 'y' || string[2] == 'Y') &&
          (string[3] == 'p' || string[3] == 'P') &&
          (string[4] == 'e' || string[4] == 'E') &&
          (string[5] == 'r' || string[5] == 'R') &&
          (string[6] == '>'));
}

static inline gboolean
is_primary (const gchar *string)
{
  return ((string[0] == '<') &&
	  (string[1] == 'p' || string[1] == 'P') &&
	  (string[2] == 'r' || string[2] == 'R') &&
	  (string[3] == 'i' || string[3] == 'I') &&
	  (string[4] == 'm' || string[4] == 'M') &&
	  (string[5] == 'a' || string[5] == 'A') &&
	  (string[6] == 'r' || string[6] == 'R') &&
	  (string[7] == 'y' || string[7] == 'Y') &&
	  (string[8] == '>'));
}

static inline gboolean
is_keycode (const gchar *string)
{
  return (string[0] == '0' &&
          string[1] == 'x' &&
          g_ascii_isxdigit (string[2]) &&
          g_ascii_isxdigit (string[3]));
}

static gboolean
accelerator_parse (const gchar         *accelerator,
                   MetaKeyCombo        *combo)
{
  guint keyval, keycode;
  ClutterModifierType mods;
  gint len;

  combo->keysym = 0;
  combo->keycode = 0;
  combo->modifiers = 0;

  if (accelerator == NULL)
    return FALSE;

  keyval = 0;
  keycode = 0;
  mods = 0;
  len = strlen (accelerator);
  while (len)
    {
      if (*accelerator == '<')
        {
          if (len >= 9 && is_primary (accelerator))
            {
              /* Primary is treated the same as Control */
              accelerator += 9;
              len -= 9;
              mods |= CLUTTER_CONTROL_MASK;
            }
          else if (len >= 9 && is_control (accelerator))
            {
              accelerator += 9;
              len -= 9;
              mods |= CLUTTER_CONTROL_MASK;
            }
          else if (len >= 7 && is_shift (accelerator))
            {
              accelerator += 7;
              len -= 7;
              mods |= CLUTTER_SHIFT_MASK;
            }
          else if (len >= 6 && is_shft (accelerator))
            {
              accelerator += 6;
              len -= 6;
              mods |= CLUTTER_SHIFT_MASK;
            }
          else if (len >= 6 && is_ctrl (accelerator))
            {
              accelerator += 6;
              len -= 6;
              mods |= CLUTTER_CONTROL_MASK;
            }
          else if (len >= 6 && is_modx (accelerator))
            {
              static const guint mod_vals[] = {
                CLUTTER_MOD1_MASK,
                CLUTTER_MOD2_MASK,
                CLUTTER_MOD3_MASK,
                CLUTTER_MOD4_MASK,
                CLUTTER_MOD5_MASK,
              };

              len -= 6;
              accelerator += 4;
              mods |= mod_vals[*accelerator - '1'];
              accelerator += 2;
            }
          else if (len >= 5 && is_ctl (accelerator))
            {
              accelerator += 5;
              len -= 5;
              mods |= CLUTTER_CONTROL_MASK;
            }
          else if (len >= 5 && is_alt (accelerator))
            {
              accelerator += 5;
              len -= 5;
              mods |= CLUTTER_MOD1_MASK;
            }
          else if (len >= 6 && is_meta (accelerator))
            {
              accelerator += 6;
              len -= 6;
              mods |= CLUTTER_META_MASK;
            }
          else if (len >= 7 && is_hyper (accelerator))
            {
              accelerator += 7;
              len -= 7;
              mods |= CLUTTER_HYPER_MASK;
            }
          else if (len >= 7 && is_super (accelerator))
            {
              accelerator += 7;
              len -= 7;
              mods |= CLUTTER_SUPER_MASK;
            }
          else
            {
              gchar last_ch;

              last_ch = *accelerator;
              while (last_ch && last_ch != '>')
                {
                  last_ch = *accelerator;
                  accelerator += 1;
                  len -= 1;
                }
            }
        }
      else
        {
          if (len >= 4 && is_keycode (accelerator))
            {
              keycode = strtoul (accelerator, NULL, 16);
              goto out;
            }
          else if (strcmp (accelerator, "Above_Tab") == 0)
            {
              keyval = META_KEY_ABOVE_TAB;
              goto out;
            }
          else
            {
              keyval = xkb_keysym_from_name (accelerator, XKB_KEYSYM_CASE_INSENSITIVE);
              if (keyval == XKB_KEY_NoSymbol)
                {
                  char *with_xf86 = g_strconcat ("XF86", accelerator, NULL);
                  keyval = xkb_keysym_from_name (with_xf86, XKB_KEYSYM_CASE_INSENSITIVE);
                  g_free (with_xf86);

                  if (keyval == XKB_KEY_NoSymbol)
                    return FALSE;
                }
            }

          accelerator += len;
          len -= len;
        }
    }

out:
  combo->keysym = keyval;
  combo->keycode = keycode;
  combo->modifiers = mods;
  return TRUE;
}

gboolean
meta_parse_accelerator (const char   *accel,
                        MetaKeyCombo *combo)
{
  g_return_val_if_fail (combo != NULL, FALSE);

  *combo = (MetaKeyCombo) { 0 };

  if (!accel[0] || strcmp (accel, "disabled") == 0)
    return TRUE;

  return accelerator_parse (accel, combo);
}

gboolean
meta_parse_modifier (const char          *accel,
                     ClutterModifierType *mask)
{
  MetaKeyCombo combo = { 0 };

  g_return_val_if_fail (mask != NULL, FALSE);

  *mask = 0;

  if (accel == NULL || !accel[0] || strcmp (accel, "disabled") == 0)
    return TRUE;

  if (!accelerator_parse (accel, &combo))
    return FALSE;

  *mask = combo.modifiers;
  return TRUE;
}

/**
 * meta_accelerator_name:
 * @accelerator_mods: Accelerator modifier mask.
 * @accelerator_key: Accelerator keyval.
 *
 * Convert an accelerator keyval and modifier mask into a string parsable by `meta_parse_accelerator`.
 *
 * Returns: The accelerator name.
 */
char *
meta_accelerator_name (ClutterModifierType accelerator_mods,
                       unsigned int        accelerator_key)
{
#define TXTLEN(s) sizeof (s) - 1
  static const struct {
    guint mask;
    const char *text;
    gsize text_len;
  } mask_text[] = {
    { CLUTTER_SHIFT_MASK,   "<Shift>",   TXTLEN ("<Shift>") },
    { CLUTTER_CONTROL_MASK, "<Control>", TXTLEN ("<Control>") },
    { CLUTTER_MOD1_MASK,    "<Alt>",     TXTLEN ("<Alt>") },
    { CLUTTER_META_MASK,    "<Meta>",    TXTLEN ("<Meta>") },
    { CLUTTER_SUPER_MASK,   "<Super>",   TXTLEN ("<Super>") },
    { CLUTTER_HYPER_MASK,   "<Hyper>",   TXTLEN ("<Hyper>") }
  };
#undef TXTLEN

  ClutterModifierType saved_mods;
  guint l;
  guint name_len;
  const char *keyval_name;
  char *accelerator;
  int i;
  unsigned int lower_key;

  accelerator_mods &= CLUTTER_MODIFIER_MASK;

  clutter_keyval_convert_case (accelerator_key, &lower_key, NULL);
  keyval_name = clutter_keyval_name (lower_key);
  if (!keyval_name)
    keyval_name = "";

  name_len = strlen (keyval_name);

  saved_mods = accelerator_mods;
  for (i = 0; i < G_N_ELEMENTS (mask_text); i++)
    {
      if (accelerator_mods & mask_text[i].mask)
        name_len += mask_text[i].text_len;
    }

  if (name_len == 0)
    return g_strdup (keyval_name);

  name_len += 1; /* NUL byte */
  accelerator = g_new (char, name_len);

  accelerator_mods = saved_mods;
  l = 0;
  for (i = 0; i < G_N_ELEMENTS (mask_text); i++)
    {
      if (accelerator_mods & mask_text[i].mask)
        {
          strcpy (accelerator + l, mask_text[i].text);
          l += mask_text[i].text_len;
        }
    }

  strcpy (accelerator + l, keyval_name);
  accelerator[name_len - 1] = '\0';

  return accelerator;
}