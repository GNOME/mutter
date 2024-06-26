/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2012 Intel Corporation.
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
 *
 *
 * Authors:
 *   Havoc Pennington <hp@pobox.com> for litl
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-context.h"

#include <graphene.h>

/**
 * CoglMatrixStack:
 *
 * Efficiently tracking many related transformations.
 *
 * Tracks your current position within a hierarchy and lets you build
 * up a graph of transformations as you traverse through a hierarchy
 * such as a scenegraph.
 *
 * A #CoglMatrixStack always maintains a reference to a single
 * transformation at any point in time, representing the
 * transformation at the current position in the hierarchy. You can
 * get a reference to the current transformation by calling
 * cogl_matrix_stack_get_entry().
 *
 * When a #CoglMatrixStack is first created with
 * cogl_matrix_stack_new() then it is conceptually positioned at the
 * root of your hierarchy and the current transformation simply
 * represents an identity transformation.
 *
 * As you traverse your object hierarchy (your scenegraph) then you
 * should call cogl_matrix_stack_push() whenever you move down one
 * level and call cogl_matrix_stack_pop() whenever you move back up
 * one level towards the root.
 *
 * At any time you can apply a set of operations, such as "rotate",
 * "scale", "translate" on top of the current transformation of a
 * #CoglMatrixStack using functions such as
 * cogl_matrix_stack_rotate(), cogl_matrix_stack_scale() and
 * cogl_matrix_stack_translate(). These operations will derive a new
 * current transformation and will never affect a transformation
 * that you have referenced using cogl_matrix_stack_get_entry().
 *
 * Internally applying operations to a #CoglMatrixStack builds up a
 * graph of #CoglMatrixEntry structures which each represent a single
 * immutable transform.
 */
typedef struct _CoglMatrixStack CoglMatrixStack;

#define COGL_TYPE_MATRIX_STACK (cogl_matrix_stack_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglMatrixStack,
                      cogl_matrix_stack,
                      COGL,
                      MATRIX_STACK,
                      GObject)

/**
 * CoglMatrixEntry:
 *
 * Represents a single immutable transformation that was retrieved
 * from a #CoglMatrixStack using cogl_matrix_stack_get_entry().
 *
 * Internally a #CoglMatrixEntry represents a single matrix
 * operation (such as "rotate", "scale", "translate") which is applied
 * to the transform of a single parent entry.
 *
 * Using the #CoglMatrixStack api effectively builds up a graph of
 * these immutable #CoglMatrixEntry structures whereby operations
 * that can be shared between multiple transformations will result
 * in shared #CoglMatrixEntry nodes in the graph.
 *
 * When a #CoglMatrixStack is first created it references one
 * #CoglMatrixEntry that represents a single "load identity"
 * operation. This serves as the root entry and all operations
 * that are then applied to the stack will extend the graph
 * starting from this root "load identity" entry.
 *
 * Given the typical usage model for a #CoglMatrixStack and the way
 * the entries are built up while traversing a scenegraph then in most
 * cases where an application is interested in comparing two
 * transformations for equality then it is enough to simply compare
 * two #CoglMatrixEntry pointers directly. Technically this can lead
 * to false negatives that could be identified with a deeper
 * comparison but often these false negatives are unlikely and
 * don't matter anyway so this enables extremely cheap comparisons.
 *
 * `CoglMatrixEntry`s are reference counted using
 * cogl_matrix_entry_ref() and cogl_matrix_entry_unref() not with
 * g_object_ref() and g_object_unref().
 */
typedef struct _CoglMatrixEntry CoglMatrixEntry;

/**
 * cogl_matrix_entry_get_type:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_matrix_entry_get_type (void);


/**
 * cogl_matrix_stack_new:
 * @ctx: A #CoglContext
 *
 * Allocates a new #CoglMatrixStack that can be used to build up
 * transformations relating to objects in a scenegraph like hierarchy.
 * (See the description of #CoglMatrixStack and #CoglMatrixEntry for
 * more details of what a matrix stack is best suited for)
 *
 * When a #CoglMatrixStack is first allocated it is conceptually
 * positioned at the root of your scenegraph hierarchy. As you
 * traverse your scenegraph then you should call
 * cogl_matrix_stack_push() whenever you move down a level and
 * cogl_matrix_stack_pop() whenever you move back up a level towards
 * the root.
 *
 * Once you have allocated a #CoglMatrixStack you can get a reference
 * to the current transformation for the current position in the
 * hierarchy by calling cogl_matrix_stack_get_entry().
 *
 * Once you have allocated a #CoglMatrixStack you can apply operations
 * such as rotate, scale and translate to modify the current transform
 * for the current position in the hierarchy by calling
 * cogl_matrix_stack_rotate(), cogl_matrix_stack_scale() and
 * cogl_matrix_stack_translate().
 *
 * Return value: (transfer full): A newly allocated #CoglMatrixStack
 */
COGL_EXPORT CoglMatrixStack *
cogl_matrix_stack_new (CoglContext *ctx);

/**
 * cogl_matrix_stack_push:
 * @stack: A #CoglMatrixStack
 *
 * Saves the current transform and starts a new transform that derives
 * from the current transform.
 *
 * This is usually called while traversing a scenegraph whenever you
 * traverse one level deeper. cogl_matrix_stack_pop() can then be
 * called when going back up one layer to restore the previous
 * transform of an ancestor.
 */
COGL_EXPORT void
cogl_matrix_stack_push (CoglMatrixStack *stack);

/**
 * cogl_matrix_stack_pop:
 * @stack: A #CoglMatrixStack
 *
 * Restores the previous transform that was last saved by calling
 * cogl_matrix_stack_push().
 *
 * This is usually called while traversing a scenegraph whenever you
 * return up one level in the graph towards the root node.
 */
COGL_EXPORT void
cogl_matrix_stack_pop (CoglMatrixStack *stack);

/**
 * cogl_matrix_stack_load_identity:
 * @stack: A #CoglMatrixStack
 *
 * Resets the current matrix to the identity matrix.
 */
COGL_EXPORT void
cogl_matrix_stack_load_identity (CoglMatrixStack *stack);

/**
 * cogl_matrix_stack_scale:
 * @stack: A #CoglMatrixStack
 * @x: Amount to scale along the x-axis
 * @y: Amount to scale along the y-axis
 * @z: Amount to scale along the z-axis
 *
 * Multiplies the current matrix by one that scales the x, y and z
 * axes by the given values.
 */
COGL_EXPORT void
cogl_matrix_stack_scale (CoglMatrixStack *stack,
                         float x,
                         float y,
                         float z);

/**
 * cogl_matrix_stack_translate:
 * @stack: A #CoglMatrixStack
 * @x: Distance to translate along the x-axis
 * @y: Distance to translate along the y-axis
 * @z: Distance to translate along the z-axis
 *
 * Multiplies the current matrix by one that translates along all
 * three axes according to the given values.
 */
COGL_EXPORT void
cogl_matrix_stack_translate (CoglMatrixStack *stack,
                             float x,
                             float y,
                             float z);

/**
 * cogl_matrix_stack_rotate:
 * @stack: A #CoglMatrixStack
 * @angle: Angle in degrees to rotate.
 * @x: X-component of vertex to rotate around.
 * @y: Y-component of vertex to rotate around.
 * @z: Z-component of vertex to rotate around.
 *
 * Multiplies the current matrix by one that rotates the around the
 * axis-vector specified by @x, @y and @z. The rotation follows the
 * right-hand thumb rule so for example rotating by 10 degrees about
 * the axis-vector (0, 0, 1) causes a small counter-clockwise
 * rotation.
 */
COGL_EXPORT void
cogl_matrix_stack_rotate (CoglMatrixStack *stack,
                          float angle,
                          float x,
                          float y,
                          float z);

/**
 * cogl_matrix_stack_rotate_euler:
 * @stack: A #CoglMatrixStack
 * @euler: A #graphene_euler_t
 *
 * Multiplies the current matrix by one that rotates according to the
 * rotation described by @euler.
 */
COGL_EXPORT void
cogl_matrix_stack_rotate_euler (CoglMatrixStack *stack,
                                const graphene_euler_t *euler);

/**
 * cogl_matrix_stack_multiply:
 * @stack: A #CoglMatrixStack
 * @matrix: the matrix to multiply with the current model-view
 *
 * Multiplies the current matrix by the given matrix.
 */
COGL_EXPORT void
cogl_matrix_stack_multiply (CoglMatrixStack         *stack,
                            const graphene_matrix_t *matrix);

/**
 * cogl_matrix_stack_frustum:
 * @stack: A #CoglMatrixStack
 * @left: X position of the left clipping plane where it
 *   intersects the near clipping plane
 * @right: X position of the right clipping plane where it
 *   intersects the near clipping plane
 * @bottom: Y position of the bottom clipping plane where it
 *   intersects the near clipping plane
 * @top: Y position of the top clipping plane where it intersects
 *   the near clipping plane
 * @z_near: The distance to the near clipping plane (Must be positive)
 * @z_far: The distance to the far clipping plane (Must be positive)
 *
 * Replaces the current matrix with a perspective matrix for a given
 * viewing frustum defined by 4 side clip planes that all cross
 * through the origin and 2 near and far clip planes.
 */
COGL_EXPORT void
cogl_matrix_stack_frustum (CoglMatrixStack *stack,
                           float left,
                           float right,
                           float bottom,
                           float top,
                           float z_near,
                           float z_far);

/**
 * cogl_matrix_stack_get_entry:
 * @stack: A #CoglMatrixStack
 *
 * Gets a reference to the current transform represented by a
 * #CoglMatrixEntry pointer.
 *
 * The transform represented by a #CoglMatrixEntry is
 * immutable.
 *
 * `CoglMatrixEntry`s are reference counted using
 * cogl_matrix_entry_ref() and cogl_matrix_entry_unref() and you
 * should call cogl_matrix_entry_unref() when you are finished with
 * and entry you get via cogl_matrix_stack_get_entry().
 *
 * Return value: (transfer none): A pointer to the #CoglMatrixEntry
 *               representing the current matrix stack transform.
 */
COGL_EXPORT CoglMatrixEntry *
cogl_matrix_stack_get_entry (CoglMatrixStack *stack);

/**
 * cogl_matrix_stack_get:
 * @stack: A #CoglMatrixStack
 * @matrix: (out): The potential destination for the current matrix
 *
 * Resolves the current @stack transform into a #graphene_matrix_t by
 * combining the operations that have been applied to build up the
 * current transform.
 *
 * There are two possible ways that this function may return its
 * result depending on whether the stack is able to directly point
 * to an internal #graphene_matrix_t or whether the result needs to be
 * composed of multiple operations.
 *
 * If an internal matrix contains the required result then this
 * function will directly return a pointer to that matrix, otherwise
 * if the function returns %NULL then @matrix will be initialized
 * to match the current transform of @stack.
 *
 * @matrix will be left untouched if a direct pointer is
 * returned.
 *
 * Return value: A direct pointer to the current transform or %NULL
 *               and in that case @matrix will be initialized with
 *               the value of the current transform.
 */
COGL_EXPORT graphene_matrix_t *
cogl_matrix_stack_get (CoglMatrixStack   *stack,
                       graphene_matrix_t *matrix);

/**
 * cogl_matrix_entry_get:
 * @entry: A #CoglMatrixEntry
 * @matrix: (out): The potential destination for the transform as
 *                 a matrix
 *
 * Resolves the current @entry transform into a #graphene_matrix_t by
 * combining the sequence of operations that have been applied to
 * build up the current transform.
 *
 * There are two possible ways that this function may return its
 * result depending on whether it's possible to directly point
 * to an internal #graphene_matrix_t or whether the result needs to be
 * composed of multiple operations.
 *
 * If an internal matrix contains the required result then this
 * function will directly return a pointer to that matrix, otherwise
 * if the function returns %NULL then @matrix will be initialized
 * to match the transform of @entry.
 *
 * @matrix will be left untouched if a direct pointer is
 * returned.
 *
 * Return value: A direct pointer to a #graphene_matrix_t transform or %NULL
 *               and in that case @matrix will be initialized with
 *               the effective transform represented by @entry.
 */
COGL_EXPORT graphene_matrix_t *
cogl_matrix_entry_get (CoglMatrixEntry   *entry,
                       graphene_matrix_t *matrix);

/**
 * cogl_matrix_stack_set:
 * @stack: A #CoglMatrixStack
 * @matrix: A #graphene_matrix_t replace the current matrix value with
 *
 * Replaces the current @stack matrix value with the value of @matrix.
 * This effectively discards any other operations that were applied
 * since the last time cogl_matrix_stack_push() was called or since
 * the stack was initialized.
 */
COGL_EXPORT void
cogl_matrix_stack_set (CoglMatrixStack         *stack,
                       const graphene_matrix_t *matrix);

/**
 * cogl_matrix_entry_calculate_translation:
 * @entry0: The first reference transform
 * @entry1: A second reference transform
 * @x: (out): The destination for the x-component of the translation
 * @y: (out): The destination for the y-component of the translation
 * @z: (out): The destination for the z-component of the translation
 *
 * Determines if the only difference between two transforms is a
 * translation and if so returns what the @x, @y, and @z components of
 * the translation are.
 *
 * If the difference between the two translations involves anything
 * other than a translation then the function returns %FALSE.
 *
 * Return value: %TRUE if the only difference between the transform of
 *                @entry0 and the transform of @entry1 is a translation,
 *                otherwise %FALSE.
 */
COGL_EXPORT gboolean
cogl_matrix_entry_calculate_translation (CoglMatrixEntry *entry0,
                                         CoglMatrixEntry *entry1,
                                         float *x,
                                         float *y,
                                         float *z);

/**
 * cogl_matrix_entry_is_identity:
 * @entry: A #CoglMatrixEntry
 *
 * Determines whether @entry is known to represent an identity
 * transform.
 *
 * If this returns %TRUE then the entry is definitely the identity
 * matrix. If it returns %FALSE it may or may not be the identity
 * matrix but no expensive comparison is performed to verify it.
 *
 * Return value: %TRUE if @entry is definitely an identity transform,
 *               otherwise %FALSE.
 */
COGL_EXPORT gboolean
cogl_matrix_entry_is_identity (CoglMatrixEntry *entry);

/**
 * cogl_matrix_entry_ref:
 * @entry: A #CoglMatrixEntry
 *
 * Takes a reference on the given @entry to ensure the @entry stays
 * alive and remains valid. When you are finished with the @entry then
 * you should call cogl_matrix_entry_unref().
 */
COGL_EXPORT CoglMatrixEntry *
cogl_matrix_entry_ref (CoglMatrixEntry *entry);

/**
 * cogl_matrix_entry_unref:
 * @entry: A #CoglMatrixEntry
 *
 * Releases a reference on @entry either taken by calling
 * cogl_matrix_entry_unref() or to release the reference given when
 * calling cogl_matrix_stack_get_entry().
 */
COGL_EXPORT void
cogl_matrix_entry_unref (CoglMatrixEntry *entry);
