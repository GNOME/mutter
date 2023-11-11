/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright 2015  Emmanuele Bassi
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
 *
 *
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __GI_SCANNER__

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterClone, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterInputDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterScrollActor, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterStage, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterText, g_object_unref)

#endif /* __GI_SCANNER__ */
