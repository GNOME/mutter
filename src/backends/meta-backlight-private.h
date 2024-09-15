/*
 * Copyright (C) 2024 Red Hat
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

#include "meta/meta-backlight.h"

#include <glib-object.h>

struct _MetaBacklightClass
{
  GObjectClass parent_class;

  void (* set_brightness) (MetaBacklight       *backlight,
                           int                  brightness_target,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data);

  int (* set_brightness_finish) (MetaBacklight  *backlight,
                                 GAsyncResult   *result,
                                 GError        **error);
};

MetaBackend * meta_backlight_get_backend (MetaBacklight *backlight);

const char * meta_backlight_get_name (MetaBacklight *backlight);

void meta_backlight_update_brightness_target (MetaBacklight *backlight,
                                              int            brightness);

META_EXPORT_TEST
gboolean meta_backlight_has_pending (MetaBacklight *backlight);
