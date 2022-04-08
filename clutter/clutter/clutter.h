/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

#define __CLUTTER_H_INSIDE__

#include "clutter/clutter-types.h"

#include "clutter/clutter-action.h"
#include "clutter/clutter-actor.h"
#include "clutter/clutter-actor-meta.h"
#include "clutter/clutter-align-constraint.h"
#include "clutter/clutter-animatable.h"
#include "clutter/clutter-backend.h"
#include "clutter/clutter-bind-constraint.h"
#include "clutter/clutter-binding-pool.h"
#include "clutter/clutter-bin-layout.h"
#include "clutter/clutter-blur-effect.h"
#include "clutter/clutter-box-layout.h"
#include "clutter/clutter-brightness-contrast-effect.h"
#include "clutter/clutter-click-action.h"
#include "clutter/clutter-clone.h"
#include "clutter/clutter-color.h"
#include "clutter/clutter-color-state.h"
#include "clutter/clutter-colorize-effect.h"
#include "clutter/clutter-constraint.h"
#include "clutter/clutter-content.h"
#include "clutter/clutter-deform-effect.h"
#include "clutter/clutter-desaturate-effect.h"
#include "clutter/clutter-effect.h"
#include "clutter/clutter-enums.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-fixed-layout.h"
#include "clutter/clutter-flow-layout.h"
#include "clutter/clutter-frame-clock.h"
#include "clutter/clutter-frame.h"
#include "clutter/clutter-gesture-action.h"
#include "clutter/clutter-gesture.h"
#include "clutter/clutter-grab.h"
#include "clutter/clutter-grid-layout.h"
#include "clutter/clutter-image.h"
#include "clutter/clutter-input-device.h"
#include "clutter/clutter-input-device-tool.h"
#include "clutter/clutter-input-method.h"
#include "clutter/clutter-input-focus.h"
#include "clutter/clutter-interval.h"
#include "clutter/clutter-keyframe-transition.h"
#include "clutter/clutter-keymap.h"
#include "clutter/clutter-keysyms.h"
#include "clutter/clutter-keyval.h"
#include "clutter/clutter-layout-manager.h"
#include "clutter/clutter-layout-meta.h"
#include "clutter/clutter-macros.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-offscreen-effect.h"
#include "clutter/clutter-page-turn-effect.h"
#include "clutter/clutter-paint-nodes.h"
#include "clutter/clutter-paint-node.h"
#include "clutter/clutter-pan-action.h"
#include "clutter/clutter-property-transition.h"
#include "clutter/clutter-rotate-action.h"
#include "clutter/clutter-scroll-actor.h"
#include "clutter/clutter-settings.h"
#include "clutter/clutter-shader-effect.h"
#include "clutter/clutter-shader-types.h"
#include "clutter/clutter-swipe-action.h"
#include "clutter/clutter-snap-constraint.h"
#include "clutter/clutter-stage.h"
#include "clutter/clutter-stage-manager.h"
#include "clutter/clutter-stage-view.h"
#include "clutter/clutter-tap-action.h"
#include "clutter/clutter-text.h"
#include "clutter/clutter-texture-content.h"
#include "clutter/clutter-timeline.h"
#include "clutter/clutter-transition-group.h"
#include "clutter/clutter-transition.h"
#include "clutter/clutter-virtual-input-device.h"
#include "clutter/clutter-zoom-action.h"

#undef __CLUTTER_H_INSIDE__
