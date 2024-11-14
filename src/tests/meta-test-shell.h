/*
 * Copyright (c) 2023 Red Hat
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

#include "meta/meta-plugin.h"

#define META_TYPE_TEST_SHELL (meta_test_shell_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaTestShell, meta_test_shell,
                      META, TEST_SHELL,
                      MetaPlugin)

META_EXPORT
void meta_test_shell_set_background_color (MetaTestShell *test_shell,
                                           CoglColor      color);

META_EXPORT
void meta_test_shell_disable_animations (MetaTestShell *test_shell);
