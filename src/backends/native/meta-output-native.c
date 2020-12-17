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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-output-native.h"

G_DEFINE_ABSTRACT_TYPE (MetaOutputNative, meta_output_native,
                        META_TYPE_OUTPUT)

GBytes *
meta_output_native_read_edid (MetaOutputNative *output_native)
{
  MetaOutputNativeClass *klass = META_OUTPUT_NATIVE_GET_CLASS (output_native);

  return klass->read_edid (output_native);
}

static void
meta_output_native_init (MetaOutputNative *output_native)
{
}

static void
meta_output_native_class_init (MetaOutputNativeClass *klass)
{
}
