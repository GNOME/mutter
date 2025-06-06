/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter visual bell */

/*
 * Copyright (C) 2002 Sun Microsystems Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
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

/*
 * bell:
 *
 * Ring the bell or flash the screen
 *
 * Sometimes, X programs "ring the bell", whatever that means. Mutter lets
 * the user configure the bell to be audible or visible (aka visual), and
 * if it's visual it can be configured to be frame-flash or fullscreen-flash.
 * We never get told about audible bells; X handles them just fine by itself.
 *
 * Visual bells come in at meta_bell_notify(), which checks we are actually
 * in visual mode and calls through to bell_visual_notify(). That
 * function then checks what kind of visual flash you like, and calls either
 * bell_flash_fullscreen()-- which calls bell_flash_screen() to do
 * its work-- or bell_flash_frame(), which flashes the focused window
 * using bell_flash_window(), unless there is no such window, in
 * which case it flashes the screen instead.
 *
 * The visual bell was the result of a discussion in Bugzilla here:
 * <http://bugzilla.gnome.org/show_bug.cgi?id=99886>.
 *
 * Several of the functions in this file are ifdeffed out entirely if we are
 * found not to have the XKB extension, which is required to do these clever
 * things with bells; some others are entirely no-ops in that case.
 */

#include "config.h"

#include "core/bell.h"

#include "compositor/compositor-private.h"
#include "core/util-private.h"
#include "core/window-private.h"
#include "meta/compositor.h"
#include "mtk/mtk.h"

/* Time limits to prevent Photosensitive Seizures */
#define MIN_TIME_BETWEEN_VISUAL_ALERTS_MS 500
#define MIN_TIME_BETWEEN_DOUBLE_VISUAL_ALERT_MS 3000

G_DEFINE_TYPE (MetaBell, meta_bell, G_TYPE_OBJECT)

enum
{
  IS_AUDIBLE_CHANGED,
  LAST_SIGNAL
};

static guint bell_signals [LAST_SIGNAL] = { 0 };

static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  MetaBell *bell = data;

  if (pref == META_PREF_AUDIBLE_BELL)
    {
      g_signal_emit (bell, bell_signals[IS_AUDIBLE_CHANGED], 0,
                     meta_prefs_bell_is_audible ());
    }
}

static void
meta_bell_finalize (GObject *object)
{
  MetaBell *bell = META_BELL (object);

  meta_prefs_remove_listener (prefs_changed_callback, bell);

  G_OBJECT_CLASS (meta_bell_parent_class)->finalize (object);
}

static void
meta_bell_class_init (MetaBellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_bell_finalize;

  bell_signals[IS_AUDIBLE_CHANGED] =
    g_signal_new ("is-audible-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
}

static void
meta_bell_init (MetaBell *bell)
{
  meta_prefs_add_listener (prefs_changed_callback, bell);
}

MetaBell *
meta_bell_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_BELL, NULL);
}

/**
 * bell_flash_fullscreen:
 * @display: The display the event came in on
 * @n_flashes: The number of times to flash the screen
 *
 * Flashes one screen, or all screens, in response to a bell event.
 * If the event is on a particular window, flash the screen that
 * window is on. Otherwise, flash every screen on this display.
 *
 * If the configure script found we had no XKB, this does not exist.
 */
static void
bell_flash_fullscreen (MetaDisplay *display,
                       int          n_flashes)
{
  meta_compositor_flash_display (display->compositor, display, n_flashes);
}

static void
bell_flash_window (MetaWindow *window,
                   int         n_flashes)
{
  meta_compositor_flash_window (window->display->compositor, window, n_flashes);
}

/**
 * bell_flash_frame:
 * @display:  The display the bell event came in on
 * @xkb_ev:   The bell event we just received
 *
 * Flashes the frame of the focused window. If there is no focused window,
 * flashes the screen.
 */
static void
bell_flash_frame (MetaDisplay *display,
                  MetaWindow  *window,
                  int          n_flashes)
{
  if (window)
    bell_flash_window (window, n_flashes);
  else
    bell_flash_fullscreen (display, n_flashes);
}

/**
 * bell_visual_notify:
 * @display: The display the bell event came in on
 * @xkb_ev: The bell event we just received
 *
 * Gives the user some kind of visual bell substitute, in response to a
 * bell event. What this is depends on the "visual bell type" pref.
 */
static void
bell_visual_notify (MetaDisplay *display,
                    MetaWindow  *window)
{
  /*
   * The European Accessibility Act (EAA), in the Annex I, Section I, 2.J,
   * specifies that products "shall avoid triggering photosensitive seizures".
   *
   * According to the Web Content Accessibility Guidelines (WCAG), any
   * element that flashes in the screen must have a maximum period of
   * 3Hz to avoid the risk of Photosensitivity Seizures.
   *
   * If several alarm bells are sent fast enough, the Visual alerts could
   * flash the screen or the window at speeds about 8-9Hz (tested with a
   * simple BASH script), which is greater than the currently accepted
   * limit of 3Hz.
   *
   * To avoid this, a timeout is added to ensure that no visual alerts are
   * sent with less than 500ms of difference, to set a maximum flash speed
   * of 2Hz.
   *
   * A property in display is used to keep the last time a visual alert has been
   * sent because not only a "single flash zone" can trigger a seizure, but also
   * slower patterns combined. So a global timeout for all the desktop is the
   * safest approach.
   */
  int64_t now_us;
  int64_t time_difference_ms;
  int n_flashes;

  now_us = g_get_monotonic_time ();
  time_difference_ms = us2ms (now_us - display->last_visual_bell_time_us);

  if (time_difference_ms < MIN_TIME_BETWEEN_VISUAL_ALERTS_MS)
    return;

  display->last_visual_bell_time_us = now_us;

  n_flashes = (time_difference_ms < MIN_TIME_BETWEEN_DOUBLE_VISUAL_ALERT_MS) ? 1 : 2;

  switch (meta_prefs_get_visual_bell_type ())
    {
    case G_DESKTOP_VISUAL_BELL_FULLSCREEN_FLASH:
      bell_flash_fullscreen (display, n_flashes);
      break;
    case G_DESKTOP_VISUAL_BELL_FRAME_FLASH:
      bell_flash_frame (display, window, n_flashes);
      break;
    }
}

static gboolean
bell_audible_notify (MetaDisplay *display,
                     MetaWindow  *window)
{
  MetaSoundPlayer *player;

  player = meta_display_get_sound_player (display);
  meta_sound_player_play_from_theme (player,
                                     "bell-window-system",
                                     _("Bell event"),
                                     NULL);
  return TRUE;
}

gboolean
meta_bell_notify (MetaDisplay *display,
                  MetaWindow  *window)
{
  /* flash something */
  if (meta_prefs_get_visual_bell ())
    bell_visual_notify (display, window);

  if (meta_prefs_bell_is_audible ())
    return bell_audible_notify (display, window);

  return TRUE;
}
