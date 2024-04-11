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
#include "meta/meta-debug.h"
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
