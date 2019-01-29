/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corp.
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
 *
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#include "clutter-build-config.h"

#include "clutter-keymap-x11.h"
#include "clutter-backend-x11.h"

#include "clutter-debug.h"
#include "clutter-event-translator.h"
#include "clutter-private.h"

#include <X11/Xatom.h>
#include <X11/XKBlib.h>

typedef struct _ClutterKeymapX11Class   ClutterKeymapX11Class;
typedef struct _DirectionCacheEntry     DirectionCacheEntry;
typedef struct _ClutterKeymapKey        ClutterKeymapKey;

struct _ClutterKeymapKey
{
  guint keycode;
  guint group;
  guint level;
};

struct _DirectionCacheEntry
{
  guint serial;
  Atom group_atom;
  PangoDirection direction;
};

struct _ClutterKeymapX11
{
  ClutterKeymap parent_instance;

  ClutterBackend *backend;

  int min_keycode;
  int max_keycode;

  ClutterModifierType modmap[8];

  ClutterModifierType num_lock_mask;
  ClutterModifierType scroll_lock_mask;
  ClutterModifierType level3_shift_mask;

  PangoDirection current_direction;

  XkbDescPtr xkb_desc;
  int xkb_event_base;
  guint xkb_map_serial;
  Atom current_group_atom;
  guint current_cache_serial;
  DirectionCacheEntry group_direction_cache[4];
  int current_group;

  GHashTable *reserved_keycodes;
  GQueue *available_keycodes;

  guint caps_lock_state : 1;
  guint num_lock_state  : 1;
  guint has_direction   : 1;
};

struct _ClutterKeymapX11Class
{
  ClutterKeymapClass parent_class;
};

enum
{
  PROP_0,

  PROP_BACKEND,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

static void clutter_event_translator_iface_init (ClutterEventTranslatorIface *iface);

#define clutter_keymap_x11_get_type     _clutter_keymap_x11_get_type

G_DEFINE_TYPE_WITH_CODE (ClutterKeymapX11, clutter_keymap_x11,
                         CLUTTER_TYPE_KEYMAP,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_EVENT_TRANSLATOR,
                                                clutter_event_translator_iface_init));

/* code adapted from gdk/x11/gdkkeys-x11.c - update_modmap */
static void
update_modmap (Display          *display,
               ClutterKeymapX11 *keymap_x11)
{
  static struct {
    const gchar *name;
    Atom atom;
    ClutterModifierType mask;
  } vmods[] = {
    { "Meta",  0, CLUTTER_META_MASK  },
    { "Super", 0, CLUTTER_SUPER_MASK },
    { "Hyper", 0, CLUTTER_HYPER_MASK },
    { NULL, 0, 0 }
  };

  int i, j, k;

  if (vmods[0].atom == 0)
    for (i = 0; vmods[i].name; i++)
      vmods[i].atom = XInternAtom (display, vmods[i].name, FALSE);

  for (i = 0; i < 8; i++)
    keymap_x11->modmap[i] = 1 << i;

  for (i = 0; i < XkbNumVirtualMods; i++)
    {
      for (j = 0; vmods[j].atom; j++)
        {
          if (keymap_x11->xkb_desc->names->vmods[i] == vmods[j].atom)
            {
              for (k = 0; k < 8; k++)
                {
                  if (keymap_x11->xkb_desc->server->vmods[i] & (1 << k))
                    keymap_x11->modmap[k] |= vmods[j].mask;
                }
            }
        }
    }
}

static XkbDescPtr
get_xkb (ClutterKeymapX11 *keymap_x11)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (keymap_x11->backend);

  if (keymap_x11->max_keycode == 0)
    XDisplayKeycodes (backend_x11->xdpy,
                      &keymap_x11->min_keycode,
                      &keymap_x11->max_keycode);

  if (keymap_x11->xkb_desc == NULL)
    {
      int flags = XkbKeySymsMask
                | XkbKeyTypesMask
                | XkbModifierMapMask
                | XkbVirtualModsMask;

      keymap_x11->xkb_desc = XkbGetMap (backend_x11->xdpy, flags, XkbUseCoreKbd);
      if (G_UNLIKELY (keymap_x11->xkb_desc == NULL))
        {
          g_error ("Failed to get the keymap from XKB");
          return NULL;
        }

      flags = XkbGroupNamesMask | XkbVirtualModNamesMask;
      XkbGetNames (backend_x11->xdpy, flags, keymap_x11->xkb_desc);

      update_modmap (backend_x11->xdpy, keymap_x11);
    }
  else if (keymap_x11->xkb_map_serial != backend_x11->keymap_serial)
    {
      int flags = XkbKeySymsMask
                | XkbKeyTypesMask
                | XkbModifierMapMask
                | XkbVirtualModsMask;

      CLUTTER_NOTE (BACKEND, "Updating XKB keymap");

      XkbGetUpdatedMap (backend_x11->xdpy, flags, keymap_x11->xkb_desc);

      flags = XkbGroupNamesMask | XkbVirtualModNamesMask;
      XkbGetNames (backend_x11->xdpy, flags, keymap_x11->xkb_desc);

      update_modmap (backend_x11->xdpy, keymap_x11);

      keymap_x11->xkb_map_serial = backend_x11->keymap_serial;
    }

  if (keymap_x11->num_lock_mask == 0)
    keymap_x11->num_lock_mask = XkbKeysymToModifiers (backend_x11->xdpy,
                                                      XK_Num_Lock);

  if (keymap_x11->scroll_lock_mask == 0)
    keymap_x11->scroll_lock_mask = XkbKeysymToModifiers (backend_x11->xdpy,
                                                         XK_Scroll_Lock);
  if (keymap_x11->level3_shift_mask == 0)
    keymap_x11->level3_shift_mask = XkbKeysymToModifiers (backend_x11->xdpy,
                                                          XK_ISO_Level3_Shift);

  return keymap_x11->xkb_desc;
}

static void
update_locked_mods (ClutterKeymapX11 *keymap_x11,
                    gint              locked_mods)
{
  gboolean old_caps_lock_state, old_num_lock_state;

  old_caps_lock_state = keymap_x11->caps_lock_state;
  old_num_lock_state  = keymap_x11->num_lock_state;

  keymap_x11->caps_lock_state = (locked_mods & CLUTTER_LOCK_MASK) != 0;
  keymap_x11->num_lock_state  = (locked_mods & keymap_x11->num_lock_mask) != 0;

  CLUTTER_NOTE (BACKEND, "Locks state changed - Num: %s, Caps: %s",
                keymap_x11->num_lock_state ? "set" : "unset",
                keymap_x11->caps_lock_state ? "set" : "unset");

  if ((keymap_x11->caps_lock_state != old_caps_lock_state) ||
      (keymap_x11->num_lock_state != old_num_lock_state))
    g_signal_emit_by_name (keymap_x11, "state-changed");
}

/* the code to retrieve the keymap direction and cache it
 * is taken from GDK:
 *      gdk/x11/gdkkeys-x11.c
 */
static PangoDirection
get_direction (XkbDescPtr xkb,
               int        group)
{
  int rtl_minus_ltr = 0; /* total number of RTL keysyms minus LTR ones */
  int code;

  for (code = xkb->min_key_code;
       code <= xkb->max_key_code;
       code += 1)
    {
      int level = 0;
      KeySym sym = XkbKeySymEntry (xkb, code, level, group);
      PangoDirection dir = pango_unichar_direction (clutter_keysym_to_unicode (sym));

      switch (dir)
        {
        case PANGO_DIRECTION_RTL:
          rtl_minus_ltr++;
          break;

        case PANGO_DIRECTION_LTR:
          rtl_minus_ltr--;
          break;

        default:
          break;
        }
    }

  if (rtl_minus_ltr > 0)
    return PANGO_DIRECTION_RTL;

  return PANGO_DIRECTION_LTR;
}

static PangoDirection
get_direction_from_cache (ClutterKeymapX11 *keymap_x11,
                          XkbDescPtr        xkb,
                          int               group)
{
  Atom group_atom = xkb->names->groups[group];
  gboolean cache_hit = FALSE;
  DirectionCacheEntry *cache = keymap_x11->group_direction_cache;
  PangoDirection direction = PANGO_DIRECTION_NEUTRAL;
  int i;

  if (keymap_x11->has_direction)
    {
      /* look up in the cache */
      for (i = 0; i < G_N_ELEMENTS (keymap_x11->group_direction_cache); i++)
        {
          if (cache[i].group_atom == group_atom)
            {
              cache_hit = TRUE;
              cache[i].serial = keymap_x11->current_cache_serial++;
              direction = cache[i].direction;
              group_atom = cache[i].group_atom;
              break;
            }
        }
    }
  else
    {
      /* initialize the cache */
      for (i = 0; i < G_N_ELEMENTS (keymap_x11->group_direction_cache); i++)
        {
          cache[i].group_atom = 0;
          cache[i].direction = PANGO_DIRECTION_NEUTRAL;
          cache[i].serial = keymap_x11->current_cache_serial;
        }

      keymap_x11->current_cache_serial += 1;
    }

  /* insert the new entry in the cache */
  if (!cache_hit)
    {
      int oldest = 0;

      direction = get_direction (xkb, group);

      /* replace the oldest entry */
      for (i = 0; i < G_N_ELEMENTS (keymap_x11->group_direction_cache); i++)
        {
          if (cache[i].serial < cache[oldest].serial)
            oldest = i;
        }

      cache[oldest].group_atom = group_atom;
      cache[oldest].direction = direction;
      cache[oldest].serial = keymap_x11->current_cache_serial++;
    }

  return direction;
}

static void
update_direction (ClutterKeymapX11 *keymap_x11,
                  int               group)
{
  XkbDescPtr xkb = get_xkb (keymap_x11);
  Atom group_atom;

  group_atom = xkb->names->groups[group];

  if (!keymap_x11->has_direction || keymap_x11->current_group_atom != group_atom)
    {
      keymap_x11->current_direction = get_direction_from_cache (keymap_x11, xkb, group);
      keymap_x11->current_group_atom = group_atom;
      keymap_x11->has_direction = TRUE;
    }
}

static void
clutter_keymap_x11_constructed (GObject *gobject)
{
  ClutterKeymapX11 *keymap_x11 = CLUTTER_KEYMAP_X11 (gobject);
  ClutterBackendX11 *backend_x11;
  gint xkb_major = XkbMajorVersion;
  gint xkb_minor = XkbMinorVersion;

  g_assert (keymap_x11->backend != NULL);
  backend_x11 = CLUTTER_BACKEND_X11 (keymap_x11->backend);

  if (XkbLibraryVersion (&xkb_major, &xkb_minor))
    {
      xkb_major = XkbMajorVersion;
      xkb_minor = XkbMinorVersion;

      if (XkbQueryExtension (backend_x11->xdpy,
                             NULL,
                             &keymap_x11->xkb_event_base,
                             NULL,
                             &xkb_major, &xkb_minor))
        {
          Bool detectable_autorepeat_supported;

          backend_x11->use_xkb = TRUE;

          XkbSelectEvents (backend_x11->xdpy,
                           XkbUseCoreKbd,
                           XkbNewKeyboardNotifyMask | XkbMapNotifyMask | XkbStateNotifyMask,
                           XkbNewKeyboardNotifyMask | XkbMapNotifyMask | XkbStateNotifyMask);

          XkbSelectEventDetails (backend_x11->xdpy,
                                 XkbUseCoreKbd, XkbStateNotify,
                                 XkbAllStateComponentsMask,
                                 XkbGroupLockMask | XkbModifierLockMask);

          /* enable XKB autorepeat */
          XkbSetDetectableAutoRepeat (backend_x11->xdpy,
                                      True,
                                      &detectable_autorepeat_supported);

          backend_x11->have_xkb_autorepeat = detectable_autorepeat_supported;

          CLUTTER_NOTE (BACKEND, "Detectable autorepeat: %s",
                        backend_x11->have_xkb_autorepeat ? "supported"
                                                         : "not supported");
        }
    }
}

static void
clutter_keymap_x11_set_property (GObject      *gobject,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  ClutterKeymapX11 *keymap = CLUTTER_KEYMAP_X11 (gobject);

  switch (prop_id)
    {
    case PROP_BACKEND:
      keymap->backend = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_keymap_x11_refresh_reserved_keycodes (ClutterKeymapX11 *keymap_x11)
{
  Display *dpy = clutter_x11_get_default_display ();
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, keymap_x11->reserved_keycodes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      guint reserved_keycode = GPOINTER_TO_UINT (key);
      guint reserved_keysym = GPOINTER_TO_UINT (value);
      guint actual_keysym = XkbKeycodeToKeysym (dpy, reserved_keycode, 0, 0);

      /* If an available keycode is no longer mapped to the stored keysym, then
       * the keycode should not be considered available anymore and should be
       * removed both from the list of available and reserved keycodes.
       */
      if (reserved_keysym != actual_keysym)
        {
          g_hash_table_iter_remove (&iter);
          g_queue_remove (keymap_x11->available_keycodes, key);
        }
    }
}

static gboolean
clutter_keymap_x11_replace_keycode (ClutterKeymapX11 *keymap_x11,
                                    KeyCode           keycode,
                                    KeySym            keysym)
{
  if (CLUTTER_BACKEND_X11 (keymap_x11->backend)->use_xkb)
    {
      Display *dpy = clutter_x11_get_default_display ();
      XkbDescPtr xkb = get_xkb (keymap_x11);
      XkbMapChangesRec changes;

      XFlush (dpy);

      xkb->device_spec = XkbUseCoreKbd;
      memset (&changes, 0, sizeof(changes));

      if (keysym != NoSymbol)
        {
          int types[XkbNumKbdGroups] = { XkbOneLevelIndex };
          XkbChangeTypesOfKey (xkb, keycode, 1, XkbGroup1Mask, types, &changes);
          XkbKeySymEntry (xkb, keycode, 0, 0) = keysym;
        }
      else
        {
          /* Reset to NoSymbol */
          XkbChangeTypesOfKey (xkb, keycode, 0, XkbGroup1Mask, NULL, &changes);
        }

      changes.changed = XkbKeySymsMask | XkbKeyTypesMask;
      changes.first_key_sym = keycode;
      changes.num_key_syms = 1;
      changes.first_type = 0;
      changes.num_types = xkb->map->num_types;
      XkbChangeMap (dpy, xkb, &changes);

      XFlush (dpy);

      return TRUE;
    }

  return FALSE;
}

static void
clutter_keymap_x11_finalize (GObject *gobject)
{
  ClutterKeymapX11 *keymap;
  ClutterEventTranslator *translator;
  GHashTableIter iter;
  gpointer key, value;

  keymap = CLUTTER_KEYMAP_X11 (gobject);
  translator = CLUTTER_EVENT_TRANSLATOR (keymap);

  clutter_keymap_x11_refresh_reserved_keycodes (keymap);
  g_hash_table_iter_init (&iter, keymap->reserved_keycodes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      guint keycode = GPOINTER_TO_UINT (key);
      clutter_keymap_x11_replace_keycode (keymap, keycode, NoSymbol);
    }

  g_hash_table_destroy (keymap->reserved_keycodes);
  g_queue_free (keymap->available_keycodes);

  _clutter_backend_remove_event_translator (keymap->backend, translator);

  if (keymap->xkb_desc != NULL)
    XkbFreeKeyboard (keymap->xkb_desc, XkbAllComponentsMask, True);

  G_OBJECT_CLASS (clutter_keymap_x11_parent_class)->finalize (gobject);
}

static gboolean
clutter_keymap_x11_get_num_lock_state (ClutterKeymap *keymap)
{
  ClutterKeymapX11 *keymap_x11 = CLUTTER_KEYMAP_X11 (keymap);

  return keymap_x11->num_lock_state;
}

static gboolean
clutter_keymap_x11_get_caps_lock_state (ClutterKeymap *keymap)
{
  ClutterKeymapX11 *keymap_x11 = CLUTTER_KEYMAP_X11 (keymap);

  return keymap_x11->caps_lock_state;
}

static void
clutter_keymap_x11_class_init (ClutterKeymapX11Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterKeymapClass *keymap_class = CLUTTER_KEYMAP_CLASS (klass);

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         P_("Backend"),
                         P_("The Clutter backend"),
                         CLUTTER_TYPE_BACKEND,
                         CLUTTER_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  gobject_class->constructed = clutter_keymap_x11_constructed;
  gobject_class->set_property = clutter_keymap_x11_set_property;
  gobject_class->finalize = clutter_keymap_x11_finalize;

  keymap_class->get_num_lock_state = clutter_keymap_x11_get_num_lock_state;
  keymap_class->get_caps_lock_state = clutter_keymap_x11_get_caps_lock_state;

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_keymap_x11_init (ClutterKeymapX11 *keymap)
{
  keymap->current_direction = PANGO_DIRECTION_NEUTRAL;
  keymap->current_group = -1;
  keymap->reserved_keycodes = g_hash_table_new (NULL, NULL);
  keymap->available_keycodes = g_queue_new ();
}

static ClutterTranslateReturn
clutter_keymap_x11_translate_event (ClutterEventTranslator *translator,
                                    gpointer                native,
                                    ClutterEvent           *event)
{
  ClutterKeymapX11 *keymap_x11 = CLUTTER_KEYMAP_X11 (translator);
  ClutterBackendX11 *backend_x11;
  ClutterTranslateReturn retval;
  XEvent *xevent;

  backend_x11 = CLUTTER_BACKEND_X11 (keymap_x11->backend);
  if (!backend_x11->use_xkb)
    return CLUTTER_TRANSLATE_CONTINUE;

  xevent = native;

  retval = CLUTTER_TRANSLATE_CONTINUE;

  if (xevent->type == keymap_x11->xkb_event_base)
    {
      XkbEvent *xkb_event = (XkbEvent *) xevent;

      switch (xkb_event->any.xkb_type)
        {
        case XkbStateNotify:
          CLUTTER_NOTE (EVENT, "Updating keyboard state");
          keymap_x11->current_group = XkbStateGroup (&xkb_event->state);
          update_direction (keymap_x11, keymap_x11->current_group);
          update_locked_mods (keymap_x11, xkb_event->state.locked_mods);
          retval = CLUTTER_TRANSLATE_REMOVE;
          break;

        case XkbNewKeyboardNotify:
        case XkbMapNotify:
          CLUTTER_NOTE (EVENT, "Updating keyboard mapping");
          XkbRefreshKeyboardMapping (&xkb_event->map);
          backend_x11->keymap_serial += 1;
          retval = CLUTTER_TRANSLATE_REMOVE;
          break;

        default:
          break;
        }
    }

  return retval;
}

static void
clutter_event_translator_iface_init (ClutterEventTranslatorIface *iface)
{
  iface->translate_event = clutter_keymap_x11_translate_event;
}

gint
_clutter_keymap_x11_get_key_group (ClutterKeymapX11    *keymap,
                                   ClutterModifierType  state)
{
  return XkbGroupForCoreState (state);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

/* XXX - yes, I know that XKeycodeToKeysym() has been deprecated; hopefully,
 * this code will never get run on any decent system that is also able to
 * run Clutter. I just don't want to copy the implementation inside GDK for
 * a fallback path.
 */
static int
translate_keysym (ClutterKeymapX11 *keymap,
                  guint             hardware_keycode)
{
  ClutterBackendX11 *backend_x11;
  gint retval;

  backend_x11 = CLUTTER_BACKEND_X11 (keymap->backend);

  retval = XKeycodeToKeysym (backend_x11->xdpy, hardware_keycode, 0);

  return retval;
}

G_GNUC_END_IGNORE_DEPRECATIONS

gint
_clutter_keymap_x11_translate_key_state (ClutterKeymapX11    *keymap,
                                         guint                hardware_keycode,
                                         ClutterModifierType *modifier_state_p,
                                         ClutterModifierType *mods_p)
{
  ClutterBackendX11 *backend_x11;
  ClutterModifierType unconsumed_modifiers = 0;
  ClutterModifierType modifier_state = *modifier_state_p;
  gint retval;

  g_return_val_if_fail (CLUTTER_IS_KEYMAP_X11 (keymap), 0);

  backend_x11 = CLUTTER_BACKEND_X11 (keymap->backend);

  if (backend_x11->use_xkb)
    {
      XkbDescRec *xkb = get_xkb (keymap);
      KeySym tmp_keysym;

      if (XkbTranslateKeyCode (xkb, hardware_keycode, modifier_state,
                               &unconsumed_modifiers,
                               &tmp_keysym))
        {
          retval = tmp_keysym;
        }
      else
        retval = 0;
    }
  else
    retval = translate_keysym (keymap, hardware_keycode);

  if (mods_p)
    *mods_p = unconsumed_modifiers;

  *modifier_state_p = modifier_state & ~(keymap->num_lock_mask |
                                         keymap->scroll_lock_mask |
                                         LockMask);

  return retval;
}

gboolean
_clutter_keymap_x11_get_is_modifier (ClutterKeymapX11 *keymap,
                                     gint              keycode)
{
  g_return_val_if_fail (CLUTTER_IS_KEYMAP_X11 (keymap), FALSE);

  if (keycode < keymap->min_keycode || keycode > keymap->max_keycode)
    return FALSE;

  if (CLUTTER_BACKEND_X11 (keymap->backend)->use_xkb)
    {
      XkbDescRec *xkb = get_xkb (keymap);

      if (xkb->map->modmap && xkb->map->modmap[keycode] != 0)
        return TRUE;
    }

  return FALSE;
}

PangoDirection
_clutter_keymap_x11_get_direction (ClutterKeymapX11 *keymap)
{
  g_return_val_if_fail (CLUTTER_IS_KEYMAP_X11 (keymap), PANGO_DIRECTION_NEUTRAL);

  if (CLUTTER_BACKEND_X11 (keymap->backend)->use_xkb)
    {
      if (!keymap->has_direction)
        {
          Display *xdisplay = CLUTTER_BACKEND_X11 (keymap->backend)->xdpy;
          XkbStateRec state_rec;

          XkbGetState (xdisplay, XkbUseCoreKbd, &state_rec);
          update_direction (keymap, XkbStateGroup (&state_rec));
        }

      return keymap->current_direction;
    }
  else
    return PANGO_DIRECTION_NEUTRAL;
}

static gboolean
clutter_keymap_x11_get_entries_for_keyval (ClutterKeymapX11  *keymap_x11,
                                           guint              keyval,
                                           ClutterKeymapKey **keys,
                                           gint              *n_keys)
{
  if (CLUTTER_BACKEND_X11 (keymap_x11->backend)->use_xkb)
    {
      XkbDescRec *xkb = get_xkb (keymap_x11);
      GArray *retval;
      gint keycode;

      keycode = keymap_x11->min_keycode;
      retval = g_array_new (FALSE, FALSE, sizeof (ClutterKeymapKey));

      while (keycode <= keymap_x11->max_keycode)
        {
          gint max_shift_levels = XkbKeyGroupsWidth (xkb, keycode);
          gint group = 0;
          gint level = 0;
          gint total_syms = XkbKeyNumSyms (xkb, keycode);
          gint i = 0;
          KeySym *entry;

          /* entry is an array with all syms for group 0, all
           * syms for group 1, etc. and for each group the
           * shift level syms are in order
           */
          entry = XkbKeySymsPtr (xkb, keycode);

          while (i < total_syms)
            {
              g_assert (i == (group * max_shift_levels + level));

              if (entry[i] == keyval)
                {
                  ClutterKeymapKey key;

                  key.keycode = keycode;
                  key.group = group;
                  key.level = level;

                  g_array_append_val (retval, key);

                  g_assert (XkbKeySymEntry (xkb, keycode, level, group) ==
                            keyval);
                }

              ++level;

              if (level == max_shift_levels)
                {
                  level = 0;
                  ++group;
                }

              ++i;
            }

          ++keycode;
        }

      if (retval->len > 0)
        {
          *keys = (ClutterKeymapKey*) retval->data;
          *n_keys = retval->len;
        }
      else
        {
          *keys = NULL;
          *n_keys = 0;
        }

      g_array_free (retval, retval->len > 0 ? FALSE : TRUE);

      return *n_keys > 0;
    }
  else
    {
      return FALSE;
    }
}

static guint
clutter_keymap_x11_get_available_keycode (ClutterKeymapX11 *keymap_x11)
{
  if (CLUTTER_BACKEND_X11 (keymap_x11->backend)->use_xkb)
    {
      clutter_keymap_x11_refresh_reserved_keycodes (keymap_x11);

      if (g_hash_table_size (keymap_x11->reserved_keycodes) < 5)
        {
          Display *dpy = clutter_x11_get_default_display ();
          XkbDescPtr xkb = get_xkb (keymap_x11);
          guint i;

          for (i = xkb->max_key_code; i >= xkb->min_key_code; --i)
            {
              if (XkbKeycodeToKeysym (dpy, i, 0, 0) == NoSymbol)
                return i;
            }
        }

      return GPOINTER_TO_UINT (g_queue_pop_head (keymap_x11->available_keycodes));
    }

  return 0;
}

gboolean clutter_keymap_x11_reserve_keycode (ClutterKeymapX11 *keymap_x11,
                                             guint             keyval,
                                             guint            *keycode_out)
{
  g_return_val_if_fail (CLUTTER_IS_KEYMAP_X11 (keymap_x11), FALSE);
  g_return_val_if_fail (keyval != 0, FALSE);
  g_return_val_if_fail (keycode_out != NULL, FALSE);

  *keycode_out = clutter_keymap_x11_get_available_keycode (keymap_x11);

  if (*keycode_out == NoSymbol)
    {
      g_warning ("Cannot reserve a keycode for keyval %d: no available keycode", keyval);
      return FALSE;
    }

  if (!clutter_keymap_x11_replace_keycode (keymap_x11, *keycode_out, keyval))
    {
      g_warning ("Failed to remap keycode %d to keyval %d", *keycode_out, keyval);
      return FALSE;
    }

  g_hash_table_insert (keymap_x11->reserved_keycodes, GUINT_TO_POINTER (*keycode_out), GUINT_TO_POINTER (keyval));
  g_queue_remove (keymap_x11->available_keycodes, GUINT_TO_POINTER (*keycode_out));

  return TRUE;
}

void clutter_keymap_x11_release_keycode_if_needed (ClutterKeymapX11 *keymap_x11,
                                                   guint             keycode)
{
  g_return_if_fail (CLUTTER_IS_KEYMAP_X11 (keymap_x11));

  if (!g_hash_table_contains (keymap_x11->reserved_keycodes, GUINT_TO_POINTER (keycode)) ||
      g_queue_index (keymap_x11->available_keycodes, GUINT_TO_POINTER (keycode)) != -1)
    return;

  g_queue_push_tail (keymap_x11->available_keycodes, GUINT_TO_POINTER (keycode));
}

void
clutter_keymap_x11_latch_modifiers (ClutterKeymapX11 *keymap_x11,
                                    uint32_t          level,
                                    gboolean          enable)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (keymap_x11->backend);
  uint32_t modifiers[] = {
    0,
    ShiftMask,
    keymap_x11->level3_shift_mask,
    keymap_x11->level3_shift_mask | ShiftMask,
  };
  uint32_t value = 0;

  if (!backend_x11->use_xkb)
    return;

  level = CLAMP (level, 0, G_N_ELEMENTS (modifiers) - 1);

  if (enable)
    value = modifiers[level];
  else
    value = 0;

  XkbLatchModifiers (clutter_x11_get_default_display (),
                     XkbUseCoreKbd, modifiers[level],
                     value);
}

static uint32_t
clutter_keymap_x11_get_current_group (ClutterKeymapX11 *keymap_x11)
{
  ClutterBackendX11 *backend_x11 = CLUTTER_BACKEND_X11 (keymap_x11->backend);
  XkbStateRec state_rec;

  if (keymap_x11->current_group >= 0)
    return keymap_x11->current_group;

  XkbGetState (backend_x11->xdpy, XkbUseCoreKbd, &state_rec);
  return XkbStateGroup (&state_rec);
}

gboolean
clutter_keymap_x11_keycode_for_keyval (ClutterKeymapX11 *keymap_x11,
                                       guint             keyval,
                                       guint            *keycode_out,
                                       guint            *level_out)
{
  ClutterKeymapKey *keys;
  gint i, n_keys, group;
  gboolean found = FALSE;

  g_return_val_if_fail (keycode_out != NULL, FALSE);
  g_return_val_if_fail (level_out != NULL, FALSE);

  group = clutter_keymap_x11_get_current_group (keymap_x11);

  if (!clutter_keymap_x11_get_entries_for_keyval (keymap_x11, keyval, &keys, &n_keys))
    return FALSE;

  for (i = 0; i < n_keys && !found; i++)
    {
      if (keys[i].group == group)
        {
          *keycode_out = keys[i].keycode;
          *level_out = keys[i].level;
          found = TRUE;
        }
    }

  g_free (keys);
  return found;
}
