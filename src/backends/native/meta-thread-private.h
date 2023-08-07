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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "backends/native/meta-thread.h"

#include "core/util-private.h"

META_EXPORT_TEST
void meta_thread_class_register_impl_type (MetaThreadClass *thread_class,
                                           GType            impl_type);

META_EXPORT_TEST
MetaThreadType meta_thread_get_thread_type (MetaThread *thread);

GThread * meta_thread_get_thread (MetaThread *thread);

void meta_thread_dispatch_callbacks (MetaThread   *thread,
                                     GMainContext *main_context);
