/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

/* some structures are meant to be opaque and still be allocated on the stack;
 * in order to avoid people poking at their internals, we use this macro to
 * ensure that users don't accidentally access a struct private members.
 *
 * we use the CLUTTER_COMPILATION define to allow us easier access, though.
 */
#ifdef CLUTTER_COMPILATION
#define CLUTTER_PRIVATE_FIELD(x)        x
#else
#define CLUTTER_PRIVATE_FIELD(x)        clutter_private_ ## x
#endif

#define _CLUTTER_EXTERN __attribute__((visibility("default"))) extern

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || \
  __clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 4)
#define _CLUTTER_GNUC_DO_PRAGMA(x) _Pragma(G_STRINGIFY (x))
#define _CLUTTER_DEPRECATED_MACRO _CLUTTER_GNUC_DO_PRAGMA(GCC warning "Deprecated macro")
#define _CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_GNUC_DO_PRAGMA(GCC warning #f)
#else
#define _CLUTTER_DEPRECATED_MACRO
#define _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

/* these macros are used to mark deprecated functions, and thus have to be
 * exposed in a public header.
 *
 * do *not* use them in other libraries depending on Clutter: use G_DEPRECATED
 * and G_DEPRECATED_FOR, or use your own wrappers around them.
 */
#ifdef CLUTTER_DISABLE_DEPRECATION_WARNINGS
#define CLUTTER_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f)
#else
#define CLUTTER_DEPRECATED G_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) G_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO _CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

#define CLUTTER_MACRO_DEPRECATED CLUTTER_DEPRECATED_MACRO
#define CLUTTER_MACRO_DEPRECATED_FOR(f) CLUTTER_DEPRECATED_MACRO_FOR(f)

#define CLUTTER_EXPORT _CLUTTER_EXTERN

/* CLUTTER_EXPORT_TEST should be used to export symbols that are exported only
 * for testability purposes
 */
#define CLUTTER_EXPORT_TEST CLUTTER_EXPORT
