/*
 * Copyright (C) 2016-2024 Red Hat, Inc.
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

#include "backends/native/meta-output-native.h"

struct _MetaOutputTest
{
  MetaOutput parent;

  float scale;
};

#define META_TYPE_OUTPUT_TEST (meta_output_test_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaOutputTest, meta_output_test,
                      META, OUTPUT_TEST,
                      MetaOutputNative)
