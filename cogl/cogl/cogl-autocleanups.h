/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009  Intel Corporation.
 * Copyright (C) 2019  Canonical Ltd.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_AUTOCLEANUPS_H__
#define __COGL_AUTOCLEANUPS_H__

#ifndef __GI_SCANNER__

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglAtlasTexture, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglAttribute, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglAttributeBuffer, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglBitmap, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglColor, cogl_color_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglDisplay, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglEuler, cogl_euler_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglFramebuffer, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglIndices, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglHandle, cogl_handle_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglMatrix, cogl_matrix_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglMatrixEntry, cogl_matrix_entry_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglMatrixStack, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglObject, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglOffscreen, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglOnscreenTemplate, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglPath, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglPipeline, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglQuaternion, cogl_quaternion_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglRenderer, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglSnippet, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglTexture, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglTexture2D, cogl_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglTexture2DSliced, cogl_object_unref)

#endif /* __GI_SCANNER__ */

#endif /* __COGL_AUTOCLEANUPS_H__ */
