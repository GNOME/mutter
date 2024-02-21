/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * PLEASE KEEP IN SYNC WITH GSETTINGS SCHEMAS!
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

#pragma once

#include <glib.h>

#include "clutter/clutter.h"
#include "meta/meta-enums.h"

/**
 * Common:
 *
 * Mutter common types
 */

/* This is set in stone and also hard-coded in GDK. */
#define META_VIRTUAL_CORE_POINTER_ID 2
#define META_VIRTUAL_CORE_KEYBOARD_ID 3

/* Replacement for X11 CurrentTime */
#define META_CURRENT_TIME 0L

#define META_EXPORT __attribute__((visibility("default"))) extern

#define MAX_BUTTONS_PER_CORNER META_BUTTON_FUNCTION_LAST

/* Keep array size in sync with MAX_BUTTONS_PER_CORNER */
/**
 * MetaButtonLayout:
 * @left_buttons: (array fixed-size=4):
 * @right_buttons: (array fixed-size=4):
 * @left_buttons_has_spacer: (array fixed-size=4):
 * @right_buttons_has_spacer: (array fixed-size=4):
 */
typedef struct _MetaButtonLayout MetaButtonLayout;
struct _MetaButtonLayout
{
  /* buttons in the group on the left side */
  MetaButtonFunction left_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean left_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];

  /* buttons in the group on the right side */
  MetaButtonFunction right_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean right_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];
};

/**
 * MetaFrameBorder:
 * @left: left border
 * @right: right border
 * @top: top border
 * @bottom: bottom border
 */
typedef struct _MetaFrameBorder
{
  int16_t left;
  int16_t right;
  int16_t top;
  int16_t bottom;
} MetaFrameBorder;

/**
 * MetaFrameBorders:
 * @visible: inner visible portion of frame border
 * @invisible: outer invisible portion of frame border
 * @total: sum of the two borders above
 */
typedef struct _MetaFrameBorders MetaFrameBorders;
struct _MetaFrameBorders
{
  /* The frame border is made up of two pieces - an inner visible portion
   * and an outer portion that is invisible but responds to events.
   */
  MetaFrameBorder visible;
  MetaFrameBorder invisible;

  /* For convenience, we have a "total" border which is equal to the sum
   * of the two borders above. */
  MetaFrameBorder total;
};

/* sets all dimensions to zero */
META_EXPORT
void meta_frame_borders_clear (MetaFrameBorders *self);

/* Main loop priorities determine when activity in the GLib
 * will take precedence over the others. Priorities are sometimes
 * used to enforce ordering: give A a higher priority than B if
 * A must occur before B. But that poses a problem since then
 * if A occurs frequently enough, B will never occur.
 *
 * Anything we want to occur more or less immediately should
 * have a priority of G_PRIORITY_DEFAULT. When we want to
 * coalesce multiple things together, the appropriate place to
 * do it is usually META_PRIORITY_BEFORE_REDRAW.
 *
 * Note that its usually better to use meta_laters_add() rather
 * than calling g_idle_add() directly; this will make sure things
 * get run when added from a clutter event handler without
 * waiting for another repaint cycle.
 *
 * If something has a priority lower than the redraw priority
 * (such as a default priority idle), then it may be arbitrarily
 * delayed. This happens if the screen is updating rapidly: we
 * are spending all our time either redrawing or waiting for a
 * vblank-synced buffer swap. (When X is improved to allow
 * clutter to do the buffer-swap asynchronously, this will get
 * better.)
 */

/* G_PRIORITY_DEFAULT:
 *  events
 *  many timeouts
 */

/* GTK_PRIORITY_RESIZE:         (G_PRIORITY_HIGH_IDLE + 10) */
#define META_PRIORITY_RESIZE    (G_PRIORITY_HIGH_IDLE + 15)
/* GTK_PRIORITY_REDRAW:         (G_PRIORITY_HIGH_IDLE + 20) */

#define META_PRIORITY_BEFORE_REDRAW  (G_PRIORITY_HIGH_IDLE + 40)
/*  calc-showing idle
 *  update-icon idle
 */

/* CLUTTER_PRIORITY_REDRAW:     (G_PRIORITY_HIGH_IDLE + 50) */
#define META_PRIORITY_REDRAW    (G_PRIORITY_HIGH_IDLE + 50)

/* ==== Anything below here can be starved arbitrarily ==== */

/* G_PRIORITY_DEFAULT_IDLE:
 *  Mutter plugin unloading
 */

#define META_PRIORITY_PREFS_NOTIFY   (G_PRIORITY_DEFAULT_IDLE + 10)

/************************************************************/
