/*
 * Copyright (C) 2024 Bilal Elmoussaoui
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

/* Grab/ungrab, ignoring all annoying modifiers like NumLock etc. */

#include "config.h"

#include "meta-x11-keybindings-private.h"

#ifdef HAVE_X11
static GArray *
calc_grab_modifiers (MetaKeyBindingManager *keys,
                     unsigned int           modmask)
{
  unsigned int ignored_mask;
  XIGrabModifiers mods;
  GArray *mods_array = g_array_new (FALSE, TRUE, sizeof (XIGrabModifiers));

  /* The X server crashes if XIAnyModifier gets passed in with any
     other bits. It doesn't make sense to ask for a grab of
     XIAnyModifier plus other bits anyway so we avoid that. */
  if (modmask & XIAnyModifier)
    {
      mods = (XIGrabModifiers) { XIAnyModifier, 0 };
      g_array_append_val (mods_array, mods);
      return mods_array;
    }

  mods = (XIGrabModifiers) { modmask, 0 };
  g_array_append_val (mods_array, mods);

  for (ignored_mask = 1;
       ignored_mask <= keys->ignored_modifier_mask;
       ++ignored_mask)
    {
      if (ignored_mask & keys->ignored_modifier_mask)
        {
          mods = (XIGrabModifiers) { modmask | ignored_mask, 0 };
          g_array_append_val (mods_array, mods);
        }
    }

  return mods_array;
}
#endif

static void
meta_change_button_grab (MetaKeyBindingManager *keys,
                         MetaWindow            *window,
                         gboolean               grab,
                         gboolean               sync,
                         int                    button,
                         int                    modmask)
{
#ifdef HAVE_X11
  MetaBackendX11 *backend;
  Display *xdisplay;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };
  Window xwindow;
  GArray *mods;
  MetaFrame *frame;

  if (meta_is_wayland_compositor ())
    return;
  if (window->client_type != META_WINDOW_CLIENT_TYPE_X11)
    return;

  backend = META_BACKEND_X11 (keys->backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend);

  XISetMask (mask.mask, XI_ButtonPress);
  XISetMask (mask.mask, XI_ButtonRelease);
  XISetMask (mask.mask, XI_Motion);

  mods = calc_grab_modifiers (keys, modmask);

  mtk_x11_error_trap_push (xdisplay);
  frame = meta_window_x11_get_frame (window);
  if (frame)
    xwindow = frame->xwindow;
  else
    xwindow = meta_window_x11_get_xwindow (window);

  /* GrabModeSync means freeze until XAllowEvents */
  if (grab)
    XIGrabButton (xdisplay,
                  META_VIRTUAL_CORE_POINTER_ID,
                  button, xwindow, None,
                  sync ? XIGrabModeSync : XIGrabModeAsync,
                  XIGrabModeAsync, False,
                  &mask, mods->len, (XIGrabModifiers *)mods->data);
  else
    XIUngrabButton (xdisplay,
                    META_VIRTUAL_CORE_POINTER_ID,
                    button, xwindow, mods->len, (XIGrabModifiers *)mods->data);

  XSync (xdisplay, False);

  mtk_x11_error_trap_pop (xdisplay);

  g_array_free (mods, TRUE);
#endif
}

static void
meta_change_buttons_grab (MetaKeyBindingManager *keys,
                          MetaWindow            *window,
                          gboolean               grab,
                          gboolean               sync,
                          int                    modmask)
{
#define MAX_BUTTON 3

  int i;
  for (i = 1; i <= MAX_BUTTON; i++)
    meta_change_button_grab (keys, window, grab, sync, i, modmask);
}

void
meta_x11_keybindings_change_keygrab (MetaKeyBindingManager *keys,
                                     Window                 xwindow,
                                     gboolean               grab,
                                     MetaResolvedKeyCombo  *resolved_combo)
{
#ifdef HAVE_X11
  MetaBackendX11 *backend_x11;
  Display *xdisplay;
  GArray *mods;
  int i;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_KeyPress);
  XISetMask (mask.mask, XI_KeyRelease);

  if (meta_is_wayland_compositor ())
    return;

  backend_x11 = META_BACKEND_X11 (keys->backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  /* Grab keycode/modmask, together with
   * all combinations of ignored modifiers.
   * X provides no better way to do this.
   */

  mods = calc_grab_modifiers (keys, resolved_combo->mask);

  mtk_x11_error_trap_push (xdisplay);

  for (i = 0; i < resolved_combo->len; i++)
    {
      xkb_keycode_t keycode = resolved_combo->keycodes[i];

      meta_topic (META_DEBUG_KEYBINDINGS,
                  "%s keybinding keycode %d mask 0x%x on 0x%lx",
                  grab ? "Grabbing" : "Ungrabbing",
                  keycode, resolved_combo->mask, xwindow);

      if (grab)
        XIGrabKeycode (xdisplay,
                       META_VIRTUAL_CORE_KEYBOARD_ID,
                       keycode, xwindow,
                       XIGrabModeSync, XIGrabModeAsync,
                       False, &mask, mods->len, (XIGrabModifiers *)mods->data);
      else
        XIUngrabKeycode (xdisplay,
                         META_VIRTUAL_CORE_KEYBOARD_ID,
                         keycode, xwindow,
                         mods->len, (XIGrabModifiers *)mods->data);
    }

  XSync (xdisplay, False);

  mtk_x11_error_trap_pop (xdisplay);

  g_array_free (mods, TRUE);
#endif
}

typedef struct
{
  MetaKeyBindingManager *keys;
  Window xwindow;
  gboolean only_per_window;
  gboolean grab;
} ChangeKeygrabData;

static void
change_keygrab_foreach (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  ChangeKeygrabData *data = user_data;
  MetaKeyBinding *binding = value;
  gboolean binding_is_per_window = (binding->flags & META_KEY_BINDING_PER_WINDOW) != 0;

  if (data->only_per_window != binding_is_per_window)
    return;

  /* Ignore the key bindings marked as META_KEY_BINDING_NO_AUTO_GRAB,
   * those are handled separately
   */
  if (binding->flags & META_KEY_BINDING_NO_AUTO_GRAB)
    return;

  if (binding->resolved_combo.len == 0)
    return;

  meta_x11_keybindings_change_keygrab (data->keys, data->xwindow,
                                       data->grab, &binding->resolved_combo);
}

static void
change_binding_keygrabs (MetaKeyBindingManager *keys,
                         Window                 xwindow,
                         gboolean               only_per_window,
                         gboolean               grab)
{
  ChangeKeygrabData data;

  data.keys = keys;
  data.xwindow = xwindow;
  data.only_per_window = only_per_window;
  data.grab = grab;

  g_hash_table_foreach (keys->key_bindings, change_keygrab_foreach, &data);
}

void
meta_x11_keybindings_maybe_update_locate_pointer_keygrab (MetaDisplay *display,
                                                          gboolean     grab)
{
  MetaKeyBindingManager *keys = &display->key_binding_manager;

  if (!display->x11_display)
    return;

  if (keys->locate_pointer_resolved_key_combo.len != 0)
    meta_x11_keybindings_change_keygrab (keys, display->x11_display->xroot,
                                         (!!grab & !!meta_prefs_is_locate_pointer_enabled ()),
                                         &keys->locate_pointer_resolved_key_combo);
}

static void
change_window_keygrabs (MetaKeyBindingManager *keys,
                        Window                 xwindow,
                        gboolean               grab)
{
  change_binding_keygrabs (keys, xwindow, TRUE, grab);
}

void
meta_window_grab_keys (MetaWindow  *window)
{
  MetaDisplay *display = window->display;
  MetaKeyBindingManager *keys = &display->key_binding_manager;
  MetaWindowX11Private *priv;

  if (meta_is_wayland_compositor ())
    return;

  priv = meta_window_x11_get_private (META_WINDOW_X11 (window));
  if (window->type == META_WINDOW_DOCK
      || window->override_redirect)
    {
      if (priv->keys_grabbed)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);
      priv->keys_grabbed = FALSE;
      return;
    }

  if (priv->keys_grabbed)
    {
      if (priv->frame && !priv->grab_on_frame)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);
      else if (priv->frame == NULL &&
               priv->grab_on_frame)
        ; /* continue to regrab on client window */
      else
        return; /* already all good */
    }

  change_window_keygrabs (keys,
                          meta_window_x11_get_toplevel_xwindow (window),
                          TRUE);

  priv->keys_grabbed = TRUE;
  priv->grab_on_frame = priv->frame != NULL;
}

void
meta_window_ungrab_keys (MetaWindow  *window)
{
  MetaWindowX11Private *priv;

  if (meta_is_wayland_compositor ())
    return;

  priv = meta_window_x11_get_private (META_WINDOW_X11 (window));

  if (priv->keys_grabbed)
    {
      MetaDisplay *display = window->display;
      MetaKeyBindingManager *keys = &display->key_binding_manager;
      MetaFrame *frame = meta_window_x11_get_frame (window);

      if (priv->grab_on_frame && frame != NULL)
        change_window_keygrabs (keys, frame->xwindow, FALSE);
      else if (!priv->grab_on_frame)
        change_window_keygrabs (keys, meta_window_x11_get_xwindow (window), FALSE);

      priv->keys_grabbed = FALSE;
    }
}

void
meta_x11_keybindings_ungrab_key_bindings (MetaDisplay *display)
{
  GSList *windows, *l;

  if (display->x11_display)
    meta_x11_display_ungrab_keys (display->x11_display);

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *w = l->data;
      meta_window_ungrab_keys (w);
    }

  g_slist_free (windows);
}

void
meta_x11_keybindings_grab_key_bindings (MetaDisplay *display)
{
  GSList *windows, *l;

  if (display->x11_display)
    meta_x11_display_grab_keys (display->x11_display);

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *w = l->data;
      meta_window_grab_keys (w);
    }

  g_slist_free (windows);
}

void
meta_x11_keybindings_grab_window_buttons (MetaKeyBindingManager *keys,
                                          MetaWindow            *window)
{
  /* Grab Alt + button1 for moving window.
   * Grab Alt + button2 for resizing window.
   * Grab Alt + button3 for popping up window menu.
   * Grab Alt + Shift + button1 for snap-moving window.
   */
  meta_verbose ("Grabbing window buttons for %s", window->desc);

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  if (keys->window_grab_modifiers != 0)
    {
      meta_change_buttons_grab (keys, window, TRUE, FALSE,
                                keys->window_grab_modifiers);

      /* In addition to grabbing Alt+Button1 for moving the window,
       * grab Alt+Shift+Button1 for snap-moving the window.  See bug
       * 112478.  Unfortunately, this doesn't work with
       * Shift+Alt+Button1 for some reason; so at least part of the
       * order still matters, which sucks (please FIXME).
       */
      meta_change_button_grab (keys, window,
                               TRUE,
                               FALSE,
                               1, keys->window_grab_modifiers | CLUTTER_SHIFT_MASK);
    }
}

void
meta_x11_keybindings_ungrab_window_buttons (MetaKeyBindingManager *keys,
                                            MetaWindow            *window)
{
  if (keys->window_grab_modifiers == 0)
    return;

  meta_change_buttons_grab (keys, window, FALSE, FALSE,
                            keys->window_grab_modifiers);
}

void
meta_x11_keybindings_grab_focus_window_button (MetaKeyBindingManager *keys,
                                               MetaWindow            *window)
{
  /* Grab button 1 for activating unfocused windows */
  meta_verbose ("Grabbing unfocused window buttons for %s", window->desc);

  if (window->have_focus_click_grab)
    {
      meta_verbose (" (well, not grabbing since we already have the grab)");
      return;
    }

  /* FIXME If we ignored errors here instead of spewing, we could
   * put one big error trap around the loop and avoid a bunch of
   * XSync()
   */

  meta_change_buttons_grab (keys, window, TRUE, TRUE, XIAnyModifier);
  window->have_focus_click_grab = TRUE;
}

void
meta_x11_keybindings_ungrab_focus_window_button (MetaKeyBindingManager *keys,
                                                 MetaWindow            *window)
{
  meta_verbose ("Ungrabbing unfocused window buttons for %s", window->desc);

  if (!window->have_focus_click_grab)
    return;

  meta_change_buttons_grab (keys, window, FALSE, FALSE, XIAnyModifier);
  window->have_focus_click_grab = FALSE;
}

static void
meta_x11_display_change_keygrabs (MetaX11Display *x11_display,
                                  gboolean        grab)
{
  MetaKeyBindingManager *keys = &x11_display->display->key_binding_manager;
  int i;

  if (keys->overlay_resolved_key_combo.len != 0)
    meta_x11_keybindings_change_keygrab (keys, x11_display->xroot,
                                         grab, &keys->overlay_resolved_key_combo);

  meta_x11_keybindings_maybe_update_locate_pointer_keygrab (x11_display->display,
                                                            grab);

  for (i = 0; i < keys->n_iso_next_group_combos; i++)
    meta_x11_keybindings_change_keygrab (keys, x11_display->xroot,
                                         grab, &keys->iso_next_group_combo[i]);

  change_binding_keygrabs (keys, x11_display->xroot,
                           FALSE, grab);
}


void
meta_x11_display_grab_keys (MetaX11Display *x11_display)
{
  if (x11_display->keys_grabbed)
    return;

  meta_x11_display_change_keygrabs (x11_display, TRUE);

  x11_display->keys_grabbed = TRUE;
}

void
meta_x11_display_ungrab_keys (MetaX11Display *x11_display)
{
  if (!x11_display->keys_grabbed)
    return;

  meta_x11_display_change_keygrabs (x11_display, FALSE);

  x11_display->keys_grabbed = FALSE;
}
