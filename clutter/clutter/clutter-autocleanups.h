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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterActor, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBackend, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBlurEffect, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBoxLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterBrightnessContrastEffect, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterClone, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterDeformEffect, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterDesaturateEffect, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterFixedLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterFlowLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterGridLayout, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterInputDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterInterval, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPageTurnEffect, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterScrollActor, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterStage, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterText, g_object_unref)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterActorBox, clutter_actor_box_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterColor, clutter_color_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterMargin, clutter_margin_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPaintContext, clutter_paint_context_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPaintNode, clutter_paint_node_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPaintVolume, clutter_paint_volume_free)

#endif /* __GI_SCANNER__ */
