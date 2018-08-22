/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc
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

#ifndef META_STARTUP_NOTIFICATION_H
#define META_STARTUP_NOTIFICATION_H

#define META_TYPE_STARTUP_SEQUENCE (meta_startup_sequence_get_type ())
#define META_TYPE_STARTUP_NOTIFICATION (meta_startup_notification_get_type ())

typedef struct _MetaStartupNotification MetaStartupNotification;
typedef struct _MetaStartupSequence MetaStartupSequence;

GType         meta_startup_notification_get_type      (void) G_GNUC_CONST;

GSList *      meta_startup_notification_get_sequences (MetaStartupNotification *sn);

GType         meta_startup_sequence_get_type          (void) G_GNUC_CONST;

const gchar * meta_startup_sequence_get_id             (MetaStartupSequence *sequence);
gboolean      meta_startup_sequence_get_completed      (MetaStartupSequence *sequence);
const gchar * meta_startup_sequence_get_name           (MetaStartupSequence *sequence);
int           meta_startup_sequence_get_workspace      (MetaStartupSequence *sequence);
gint64        meta_startup_sequence_get_timestamp      (MetaStartupSequence *sequence);
const gchar * meta_startup_sequence_get_icon_name      (MetaStartupSequence *sequence);
const gchar * meta_startup_sequence_get_application_id (MetaStartupSequence *sequence);
const gchar * meta_startup_sequence_get_wmclass        (MetaStartupSequence *sequence);

void        meta_startup_sequence_complete           (MetaStartupSequence *sequence);

#endif /* META_STARTUP_NOTIFICATION_H */
