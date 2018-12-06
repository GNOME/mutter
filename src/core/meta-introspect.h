/*
 * Copyright (C) 2018 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_INTROSPECT_H
#define META_INTROSPECT_H

#include <glib-object.h>

#include "meta-dbus-introspect.h"

typedef struct _MetaIntrospectSession MetaIntrospectSession;

#define META_TYPE_INTROSPECT (meta_introspect_get_type ())
G_DECLARE_FINAL_TYPE (MetaIntrospect, meta_introspect,
                      META, INTROSPECT,
                      MetaDBusIntrospectSkeleton)

MetaIntrospectSession * meta_introspect_get_session (MetaIntrospect *introspect,
                                                      const char     *session_id);

GDBusConnection * meta_introspect_get_connection (MetaIntrospect *introspect);

MetaIntrospect * meta_introspect_new (void);

#endif /* META_INTROSPECT_H */
