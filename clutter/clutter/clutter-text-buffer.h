/* clutter-text-buffer.h
 * Copyright (C) 2011 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Stef Walter <stefw@collabora.co.uk>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_TEXT_BUFFER            (clutter_text_buffer_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterTextBuffer,
                          clutter_text_buffer,
                          CLUTTER,
                          TEXT_BUFFER,
                          GObject)

/**
 * CLUTTER_TEXT_BUFFER_MAX_SIZE:
 *
 * Maximum size of text buffer, in bytes.
 */
#define CLUTTER_TEXT_BUFFER_MAX_SIZE        G_MAXUSHORT

/**
 * ClutterTextBufferClass:
 * @inserted_text: default handler for the #ClutterTextBuffer::inserted-text signal
 * @deleted_text: default handler for the #ClutterTextBuffer::deleted-text signal
 * @get_text: virtual function
 * @get_length: virtual function
 * @insert_text: virtual function
 * @delete_text: virtual function
 *
 * The #ClutterTextBufferClass structure contains
 * only private data.
 */
struct _ClutterTextBufferClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  /* Signals */
  void         (*inserted_text)          (ClutterTextBuffer *buffer,
                                          guint              position,
                                          const gchar       *chars,
                                          guint              n_chars);

  void         (*deleted_text)           (ClutterTextBuffer *buffer,
                                          guint              position,
                                          guint              n_chars);

  /* Virtual Methods */
  const gchar* (*get_text)               (ClutterTextBuffer *buffer,
                                          gsize             *n_bytes);

  guint        (*get_length)             (ClutterTextBuffer *buffer);

  guint        (*insert_text)            (ClutterTextBuffer *buffer,
                                          guint              position,
                                          const gchar       *chars,
                                          guint              n_chars);

  guint        (*delete_text)            (ClutterTextBuffer *buffer,
                                          guint              position,
                                          guint              n_chars);
};

CLUTTER_EXPORT
ClutterTextBuffer*  clutter_text_buffer_new                 (void);
CLUTTER_EXPORT
ClutterTextBuffer*  clutter_text_buffer_new_with_text       (const gchar       *text,
                                                             gssize             text_len);

CLUTTER_EXPORT
gsize               clutter_text_buffer_get_bytes           (ClutterTextBuffer *buffer);
CLUTTER_EXPORT
guint               clutter_text_buffer_get_length          (ClutterTextBuffer *buffer);
CLUTTER_EXPORT
const gchar*        clutter_text_buffer_get_text            (ClutterTextBuffer *buffer);
CLUTTER_EXPORT
void                clutter_text_buffer_set_text            (ClutterTextBuffer *buffer,
                                                             const gchar       *chars,
                                                             gint               n_chars);
CLUTTER_EXPORT
void                clutter_text_buffer_set_max_length      (ClutterTextBuffer *buffer,
                                                             gint               max_length);
CLUTTER_EXPORT
gint                clutter_text_buffer_get_max_length      (ClutterTextBuffer  *buffer);

CLUTTER_EXPORT
guint               clutter_text_buffer_insert_text         (ClutterTextBuffer *buffer,
                                                             guint              position,
                                                             const gchar       *chars,
                                                             gint               n_chars);
CLUTTER_EXPORT
guint               clutter_text_buffer_delete_text         (ClutterTextBuffer *buffer,
                                                             guint              position,
                                                             gint               n_chars);
CLUTTER_EXPORT
void                clutter_text_buffer_emit_inserted_text  (ClutterTextBuffer *buffer,
                                                             guint              position,
                                                             const gchar       *chars,
                                                             guint              n_chars);
CLUTTER_EXPORT
void                clutter_text_buffer_emit_deleted_text   (ClutterTextBuffer *buffer,
                                                             guint              position,
                                                             guint              n_chars);

G_END_DECLS
