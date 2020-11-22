/*
 * Copyright (C) 2021 Red Hat
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

#ifndef META_THREAD_PRIVATE_H
#define META_THREAD_PRIVATE_H

#include "backends/native/meta-thread.h"

#include "core/util-private.h"

META_EXPORT_TEST
void meta_thread_class_register_impl_type (MetaThreadClass *thread_class,
                                           GType            impl_type);

#endif /* META_THREAD_PRIVATE_H */
