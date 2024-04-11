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

#include "meta/meta-base.h"

/**
 * MetaDebugTopic:
 * @META_DEBUG_VERBOSE: verbose logging
 * @META_DEBUG_FOCUS: focus
 * @META_DEBUG_WORKAREA: workarea
 * @META_DEBUG_STACK: stack
 * @META_DEBUG_SM: session management
 * @META_DEBUG_EVENTS: events
 * @META_DEBUG_WINDOW_STATE: window state
 * @META_DEBUG_WINDOW_OPS: window operations
 * @META_DEBUG_GEOMETRY: geometry
 * @META_DEBUG_PLACEMENT: window placement
 * @META_DEBUG_PING: ping
 * @META_DEBUG_KEYBINDINGS: keybindings
 * @META_DEBUG_SYNC: sync
 * @META_DEBUG_STARTUP: startup
 * @META_DEBUG_PREFS: preferences
 * @META_DEBUG_GROUPS: groups
 * @META_DEBUG_RESIZING: resizing
 * @META_DEBUG_SHAPES: shapes
 * @META_DEBUG_EDGE_RESISTANCE: edge resistance
 * @META_DEBUG_WAYLAND: Wayland
 * @META_DEBUG_KMS: kernel mode setting
 * @META_DEBUG_SCREEN_CAST: screencasting
 * @META_DEBUG_REMOTE_DESKTOP: remote desktop
 * @META_DEBUG_BACKEND: backend
 * @META_DEBUG_RENDER: native backend rendering
 * @META_DEBUG_COLOR: color management
 * @META_DEBUG_INPUT_EVENTS: input events
 * @META_DEBUG_EIS: eis state
 */
typedef enum
{
  META_DEBUG_VERBOSE         = -1,
  META_DEBUG_FOCUS           = 1 << 0,
  META_DEBUG_WORKAREA        = 1 << 1,
  META_DEBUG_STACK           = 1 << 2,
  META_DEBUG_SM              = 1 << 3,
  META_DEBUG_EVENTS          = 1 << 4,
  META_DEBUG_WINDOW_STATE    = 1 << 5,
  META_DEBUG_WINDOW_OPS      = 1 << 6,
  META_DEBUG_GEOMETRY        = 1 << 7,
  META_DEBUG_PLACEMENT       = 1 << 8,
  META_DEBUG_PING            = 1 << 9,
  META_DEBUG_KEYBINDINGS     = 1 << 10,
  META_DEBUG_SYNC            = 1 << 11,
  META_DEBUG_STARTUP         = 1 << 12,
  META_DEBUG_PREFS           = 1 << 13,
  META_DEBUG_GROUPS          = 1 << 14,
  META_DEBUG_RESIZING        = 1 << 15,
  META_DEBUG_SHAPES          = 1 << 16,
  META_DEBUG_EDGE_RESISTANCE = 1 << 17,
  META_DEBUG_DBUS            = 1 << 18,
  META_DEBUG_INPUT           = 1 << 19,
  META_DEBUG_WAYLAND         = 1 << 20,
  META_DEBUG_KMS             = 1 << 21,
  META_DEBUG_SCREEN_CAST     = 1 << 22,
  META_DEBUG_REMOTE_DESKTOP  = 1 << 23,
  META_DEBUG_BACKEND         = 1 << 24,
  META_DEBUG_RENDER          = 1 << 25,
  META_DEBUG_COLOR           = 1 << 26,
  META_DEBUG_INPUT_EVENTS    = 1 << 27,
  META_DEBUG_EIS             = 1 << 28,
} MetaDebugTopic;

META_EXPORT
gboolean meta_is_topic_enabled (MetaDebugTopic topic);

/* To disable verbose mode, we make these functions into no-ops */
#ifdef WITH_VERBOSE_MODE

const char * meta_topic_to_string (MetaDebugTopic topic);

META_EXPORT
void meta_log (const char *format, ...) G_GNUC_PRINTF (1, 2);

#define meta_topic(debug_topic, ...) \
  G_STMT_START \
    { \
      if (meta_is_topic_enabled (debug_topic)) \
        { \
          g_autofree char *_topic_message = NULL; \
\
          _topic_message = g_strdup_printf (__VA_ARGS__); \
          meta_log ("%s: %s", meta_topic_to_string (debug_topic), \
                     _topic_message); \
        } \
    } \
  G_STMT_END

#define meta_verbose(...) meta_topic (META_DEBUG_VERBOSE, __VA_ARGS__)

#else

#  ifdef G_HAVE_ISO_VARARGS
#    define meta_verbose(...)
#    define meta_topic(...)
#  elif defined(G_HAVE_GNUC_VARARGS)
#    define meta_verbose(format...)
#    define meta_topic(format...)
#  else
#    error "This compiler does not support varargs macros and thus verbose mode can't be disabled meaningfully"
#  endif

#endif /* !WITH_VERBOSE_MODE */
