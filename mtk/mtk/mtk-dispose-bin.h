/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2025 Red Hat
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

#include <glib.h>

#include "mtk/mtk-macros.h"

/**
 * MtkDisposeBin: (skip)
 */
typedef struct _MtkDisposeBin MtkDisposeBin;

MTK_EXPORT
void mtk_dispose_bin_add (MtkDisposeBin  *bin,
                          gpointer        user_data,
                          GDestroyNotify  notify);

MTK_EXPORT
MtkDisposeBin * mtk_dispose_bin_new (void);

MTK_EXPORT
void mtk_dispose_bin_dispose (MtkDisposeBin *bin);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkDisposeBin, mtk_dispose_bin_dispose)
