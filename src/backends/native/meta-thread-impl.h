/*
 * Copyright (C) 2018-2020 Red Hat
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
 */

#ifndef META_THREAD_IMPL_H
#define META_THREAD_IMPL_H

#include <glib-object.h>

typedef struct _MetaThread MetaThread;

#define META_TYPE_THREAD_IMPL (meta_thread_impl_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaThreadImpl, meta_thread_impl,
                          META, THREAD_IMPL, GObject)

struct _MetaThreadImplClass
{
  GObjectClass parent_class;
};

MetaThread * meta_thread_impl_get_thread (MetaThreadImpl *thread_impl);

GMainContext * meta_thread_impl_get_main_context (MetaThreadImpl *thread_impl);

#endif /* META_THREAD_IMPL_H */
