/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_CONTEXT (mdk_context_get_type ())
G_DECLARE_FINAL_TYPE (MdkContext, mdk_context,
                      MDK, CONTEXT,
                      GObject)

MdkContext * mdk_context_new (void);

void mdk_context_activate (MdkContext *context);

MdkSession * mdk_context_get_session (MdkContext *context);

MdkPipewire * mdk_context_get_pipewire (MdkContext *context);

gboolean mdk_context_get_emulate_touch (MdkContext *context);

gboolean mdk_context_get_inhibit_system_shortcuts (MdkContext *context);
