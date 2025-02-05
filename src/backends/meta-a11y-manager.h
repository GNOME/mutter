/*
 * Copyright 2024 GNOME Foundation
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
 *
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "clutter/clutter.h"

#define META_TYPE_A11Y_MANAGER (meta_a11y_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaA11yManager, meta_a11y_manager, META, A11Y_MANAGER, GObject)

MetaA11yManager * meta_a11y_manager_new (MetaBackend *backend);

gboolean meta_a11y_manager_notify_clients (MetaA11yManager    *a11y_manager,
                                           const ClutterEvent *event);

uint32_t * meta_a11y_manager_get_modifier_keysyms (MetaA11yManager *a11y_manager,
                                                   int             *n_modifiers);
