/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter utilities */

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
#include <glib-object.h>

#include "meta/common.h"
#include "meta/meta-later.h"

META_EXPORT
gboolean meta_is_verbose  (void);

META_EXPORT
gboolean meta_is_wayland_compositor (void);

META_EXPORT
void meta_bug        (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_warning    (const char *format,
                      ...) G_GNUC_PRINTF (1, 2);

META_EXPORT
void meta_fatal      (const char *format,
                      ...) G_GNUC_PRINTF (1, 2) G_GNUC_NORETURN G_ANALYZER_NORETURN;

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

/**
 * MetaDebugPaintFlag:
 * @META_DEBUG_PAINT_NONE: default
 * @META_DEBUG_PAINT_OPAQUE_REGION: paint opaque regions
 */
typedef enum
{
  META_DEBUG_PAINT_NONE          = 0,
  META_DEBUG_PAINT_OPAQUE_REGION = 1 << 0,
} MetaDebugPaintFlag;

META_EXPORT
gboolean meta_is_topic_enabled (MetaDebugTopic topic);

META_EXPORT
void meta_add_verbose_topic    (MetaDebugTopic topic);

META_EXPORT
void meta_remove_verbose_topic (MetaDebugTopic topic);

META_EXPORT
void meta_push_no_msg_prefix (void);

META_EXPORT
void meta_pop_no_msg_prefix  (void);

META_EXPORT
gint  meta_unsigned_long_equal (gconstpointer v1,
                                gconstpointer v2);

META_EXPORT
guint meta_unsigned_long_hash  (gconstpointer v);

META_EXPORT
const char* meta_frame_type_to_string (MetaFrameType type);
META_EXPORT
const char* meta_gravity_to_string (MetaGravity gravity);

META_EXPORT
char* meta_external_binding_name_for_action (guint keybinding_action);

META_EXPORT
char* meta_g_utf8_strndup (const gchar *src, gsize n);

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

typedef enum
{
  META_LOCALE_DIRECTION_LTR,
  META_LOCALE_DIRECTION_RTL,
} MetaLocaleDirection;

META_EXPORT
MetaLocaleDirection meta_get_locale_direction (void);

META_EXPORT
void meta_add_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                                   ClutterDrawDebugFlag draw_flags,
                                   ClutterPickDebugFlag pick_flags);

META_EXPORT
void meta_remove_clutter_debug_flags (ClutterDebugFlag     debug_flags,
                                      ClutterDrawDebugFlag draw_flags,
                                      ClutterPickDebugFlag pick_flags);

META_EXPORT
void meta_get_clutter_debug_flags (ClutterDebugFlag     *debug_flags,
                                   ClutterDrawDebugFlag *draw_flags,
                                   ClutterPickDebugFlag *pick_flags);

META_EXPORT
void meta_add_debug_paint_flag (MetaDebugPaintFlag flag);

META_EXPORT
void meta_remove_debug_paint_flag (MetaDebugPaintFlag flag);

META_EXPORT
MetaDebugPaintFlag meta_get_debug_paint_flags (void);

META_EXPORT
char * meta_accelerator_name (ClutterModifierType accelerator_mods,
                              unsigned int        accelerator_key);
