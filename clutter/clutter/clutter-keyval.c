/*
 * Copyright (C) 2021 Red Hat
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
 * Author: Bilal Elmoussaoui <belmous@redhat.com>
 * 
 * The code is a modified version of the GDK implementation
 */

#include <glib.h>
#include <glib/gprintf.h>

#include "config.h"

#include "clutter/clutter-keyval.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-keysyms.h"
#include "clutter/clutter-keyname-table.h"

#define CLUTTER_NUM_KEYS G_N_ELEMENTS (clutter_keys_by_keyval)

/**
 * clutter_keyval_convert_case: 
 * @symbol: a keyval.
 * @lower: (out): return location of the lowercase version of @symbol.
 * @upper: (out): return location of the uppercase version of @symbol.
 */
void
clutter_keyval_convert_case (unsigned int  symbol,
                             unsigned int *lower,
                             unsigned int *upper)
{
  unsigned int xlower, xupper;

  xlower = symbol;
  xupper = symbol;

  /* Check for directly encoded 24-bit UCS characters: */
  if ((symbol & 0xff000000) == 0x01000000)
    {
      if (lower)
        *lower = clutter_unicode_to_keysym (g_unichar_tolower (symbol & 0x00ffffff));
      if (upper)
        *upper = clutter_unicode_to_keysym (g_unichar_toupper (symbol & 0x00ffffff));
      return;
    }

  switch (symbol >> 8)
    {
    case 0: /* Latin 1 */
      if ((symbol >= CLUTTER_KEY_A) && (symbol <= CLUTTER_KEY_Z))
        xlower += (CLUTTER_KEY_a - CLUTTER_KEY_A);
      else if ((symbol >= CLUTTER_KEY_a) && (symbol <= CLUTTER_KEY_z))
        xupper -= (CLUTTER_KEY_a - CLUTTER_KEY_A);
      else if ((symbol >= CLUTTER_KEY_Agrave) && (symbol <= CLUTTER_KEY_Odiaeresis))
        xlower += (CLUTTER_KEY_agrave - CLUTTER_KEY_Agrave);
      else if ((symbol >= CLUTTER_KEY_agrave) && (symbol <= CLUTTER_KEY_odiaeresis))
        xupper -= (CLUTTER_KEY_agrave - CLUTTER_KEY_Agrave);
      else if ((symbol >= CLUTTER_KEY_Ooblique) && (symbol <= CLUTTER_KEY_Thorn))
        xlower += (CLUTTER_KEY_oslash - CLUTTER_KEY_Ooblique);
      else if ((symbol >= CLUTTER_KEY_oslash) && (symbol <= CLUTTER_KEY_thorn))
        xupper -= (CLUTTER_KEY_oslash - CLUTTER_KEY_Ooblique);
      break;

    case 1: /* Latin 2 */
      /* Assume the KeySym is a legal value (ignore discontinuities) */
      if (symbol == CLUTTER_KEY_Aogonek)
        xlower = CLUTTER_KEY_aogonek;
      else if (symbol >= CLUTTER_KEY_Lstroke && symbol <= CLUTTER_KEY_Sacute)
        xlower += (CLUTTER_KEY_lstroke - CLUTTER_KEY_Lstroke);
      else if (symbol >= CLUTTER_KEY_Scaron && symbol <= CLUTTER_KEY_Zacute)
        xlower += (CLUTTER_KEY_scaron - CLUTTER_KEY_Scaron);
      else if (symbol >= CLUTTER_KEY_Zcaron && symbol <= CLUTTER_KEY_Zabovedot)
        xlower += (CLUTTER_KEY_zcaron - CLUTTER_KEY_Zcaron);
      else if (symbol == CLUTTER_KEY_aogonek)
        xupper = CLUTTER_KEY_Aogonek;
      else if (symbol >= CLUTTER_KEY_lstroke && symbol <= CLUTTER_KEY_sacute)
        xupper -= (CLUTTER_KEY_lstroke - CLUTTER_KEY_Lstroke);
      else if (symbol >= CLUTTER_KEY_scaron && symbol <= CLUTTER_KEY_zacute)
        xupper -= (CLUTTER_KEY_scaron - CLUTTER_KEY_Scaron);
      else if (symbol >= CLUTTER_KEY_zcaron && symbol <= CLUTTER_KEY_zabovedot)
        xupper -= (CLUTTER_KEY_zcaron - CLUTTER_KEY_Zcaron);
      else if (symbol >= CLUTTER_KEY_Racute && symbol <= CLUTTER_KEY_Tcedilla)
        xlower += (CLUTTER_KEY_racute - CLUTTER_KEY_Racute);
      else if (symbol >= CLUTTER_KEY_racute && symbol <= CLUTTER_KEY_tcedilla)
        xupper -= (CLUTTER_KEY_racute - CLUTTER_KEY_Racute);
      break;

    case 2: /* Latin 3 */
      /* Assume the KeySym is a legal value (ignore discontinuities) */
      if (symbol >= CLUTTER_KEY_Hstroke && symbol <= CLUTTER_KEY_Hcircumflex)
        xlower += (CLUTTER_KEY_hstroke - CLUTTER_KEY_Hstroke);
      else if (symbol >= CLUTTER_KEY_Gbreve && symbol <= CLUTTER_KEY_Jcircumflex)
        xlower += (CLUTTER_KEY_gbreve - CLUTTER_KEY_Gbreve);
      else if (symbol >= CLUTTER_KEY_hstroke && symbol <= CLUTTER_KEY_hcircumflex)
        xupper -= (CLUTTER_KEY_hstroke - CLUTTER_KEY_Hstroke);
      else if (symbol >= CLUTTER_KEY_gbreve && symbol <= CLUTTER_KEY_jcircumflex)
        xupper -= (CLUTTER_KEY_gbreve - CLUTTER_KEY_Gbreve);
      else if (symbol >= CLUTTER_KEY_Cabovedot && symbol <= CLUTTER_KEY_Scircumflex)
        xlower += (CLUTTER_KEY_cabovedot - CLUTTER_KEY_Cabovedot);
      else if (symbol >= CLUTTER_KEY_cabovedot && symbol <= CLUTTER_KEY_scircumflex)
        xupper -= (CLUTTER_KEY_cabovedot - CLUTTER_KEY_Cabovedot);
      break;

    case 3: /* Latin 4 */
      /* Assume the KeySym is a legal value (ignore discontinuities) */
      if (symbol >= CLUTTER_KEY_Rcedilla && symbol <= CLUTTER_KEY_Tslash)
        xlower += (CLUTTER_KEY_rcedilla - CLUTTER_KEY_Rcedilla);
      else if (symbol >= CLUTTER_KEY_rcedilla && symbol <= CLUTTER_KEY_tslash)
        xupper -= (CLUTTER_KEY_rcedilla - CLUTTER_KEY_Rcedilla);
      else if (symbol == CLUTTER_KEY_ENG)
        xlower = CLUTTER_KEY_eng;
      else if (symbol == CLUTTER_KEY_eng)
        xupper = CLUTTER_KEY_ENG;
      else if (symbol >= CLUTTER_KEY_Amacron && symbol <= CLUTTER_KEY_Umacron)
        xlower += (CLUTTER_KEY_amacron - CLUTTER_KEY_Amacron);
      else if (symbol >= CLUTTER_KEY_amacron && symbol <= CLUTTER_KEY_umacron)
        xupper -= (CLUTTER_KEY_amacron - CLUTTER_KEY_Amacron);
      break;

    case 6: /* Cyrillic */
      /* Assume the KeySym is a legal value (ignore discontinuities) */
      if (symbol >= CLUTTER_KEY_Serbian_DJE && symbol <= CLUTTER_KEY_Serbian_DZE)
        xlower -= (CLUTTER_KEY_Serbian_DJE - CLUTTER_KEY_Serbian_dje);
      else if (symbol >= CLUTTER_KEY_Serbian_dje && symbol <= CLUTTER_KEY_Serbian_dze)
        xupper += (CLUTTER_KEY_Serbian_DJE - CLUTTER_KEY_Serbian_dje);
      else if (symbol >= CLUTTER_KEY_Cyrillic_YU && symbol <= CLUTTER_KEY_Cyrillic_HARDSIGN)
        xlower -= (CLUTTER_KEY_Cyrillic_YU - CLUTTER_KEY_Cyrillic_yu);
      else if (symbol >= CLUTTER_KEY_Cyrillic_yu && symbol <= CLUTTER_KEY_Cyrillic_hardsign)
        xupper += (CLUTTER_KEY_Cyrillic_YU - CLUTTER_KEY_Cyrillic_yu);
      break;

    case 7: /* Greek */
      /* Assume the KeySym is a legal value (ignore discontinuities) */
      if (symbol >= CLUTTER_KEY_Greek_ALPHAaccent && symbol <= CLUTTER_KEY_Greek_OMEGAaccent)
        xlower += (CLUTTER_KEY_Greek_alphaaccent - CLUTTER_KEY_Greek_ALPHAaccent);
      else if (symbol >= CLUTTER_KEY_Greek_alphaaccent && symbol <= CLUTTER_KEY_Greek_omegaaccent &&
               symbol != CLUTTER_KEY_Greek_iotaaccentdieresis &&
               symbol != CLUTTER_KEY_Greek_upsilonaccentdieresis)
        xupper -= (CLUTTER_KEY_Greek_alphaaccent - CLUTTER_KEY_Greek_ALPHAaccent);
      else if (symbol >= CLUTTER_KEY_Greek_ALPHA && symbol <= CLUTTER_KEY_Greek_OMEGA)
        xlower += (CLUTTER_KEY_Greek_alpha - CLUTTER_KEY_Greek_ALPHA);
      else if (symbol == CLUTTER_KEY_Greek_finalsmallsigma)
        xupper = CLUTTER_KEY_Greek_SIGMA;
      else if (symbol >= CLUTTER_KEY_Greek_alpha && symbol <= CLUTTER_KEY_Greek_omega)
        xupper -= (CLUTTER_KEY_Greek_alpha - CLUTTER_KEY_Greek_ALPHA);
      break;

    default:
      break;
    }

  if (lower)
    *lower = xlower;
  if (upper)
    *upper = xupper;
}

static int
clutter_keys_keyval_compare (const void *pkey, const void *pbase)
{
  return (*(int *) pkey) - ((clutter_key *) pbase)->keyval;
}

/**
 * clutter_keyval_name:
 * @keyval: A key value.
 *  
 * Returns: (nullable) (transfer none): The corresponding symbolic name.
 */
const char *
clutter_keyval_name (unsigned int keyval)
{
  static char buf[100];
  clutter_key *found;

  /* Check for directly encoded 24-bit UCS characters: */
  if ((keyval & 0xff000000) == 0x01000000)
    {
      g_sprintf (buf, "U+%.04X", (keyval & 0x00ffffff));
      return buf;
    }

  found = bsearch (&keyval, clutter_keys_by_keyval,
		   CLUTTER_NUM_KEYS, sizeof (clutter_key),
		   clutter_keys_keyval_compare);

  if (found != NULL)
    {
      while ((found > clutter_keys_by_keyval) &&
             ((found - 1)->keyval == keyval))
        found--;

      return (char *) (keynames + found->offset);
    }
  else if (keyval != 0)
    {
      g_sprintf (buf, "%#x", keyval);
      return buf;
    }

  return NULL;
}
