/*
 * Copyright (C) 2020 Red Hat
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

#include "backends/meta-output.h"

#define META_TYPE_OUTPUT_NATIVE (meta_output_native_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaOutputNative, meta_output_native,
                          META, OUTPUT_NATIVE,
                          MetaOutput)

struct _MetaOutputNativeClass
{
  MetaOutputClass parent_class;

  GBytes * (* read_edid) (MetaOutputNative *output_native);
};

GBytes * meta_output_native_read_edid (MetaOutputNative *output_native);
