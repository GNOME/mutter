/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2023 Red Hat
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

#if !defined(__MTK_H_INSIDE__) && !defined(MTK_COMPILATION)
#error "Only <mtk/mtk.h> can be included directly."
#endif

#define MTK_EXPORT __attribute__((visibility ("default"))) extern

/* MTK_EXPORT_TEST should be used to export symbols that are exported only
 * for testability purposes
 */
#define MTK_EXPORT_TEST MTK_EXPORT
