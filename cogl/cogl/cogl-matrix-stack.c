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
 * Authors:
 *   Havoc Pennington <hp@pobox.com> for litl
 *   Robert Bragg <robert@linux.intel.com>
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-graphene.h"
#include "cogl/cogl-matrix-stack.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-offscreen.h"
#include "cogl/cogl-magazine-private.h"

G_DEFINE_BOXED_TYPE (CoglMatrixEntry, cogl_matrix_entry,
                     cogl_matrix_entry_ref,
                     cogl_matrix_entry_unref);

static CoglMagazine *cogl_matrix_stack_magazine;

/* XXX: Note: this leaves entry->parent uninitialized! */
static CoglMatrixEntry *
_cogl_matrix_entry_new (CoglMatrixOp operation)
{
  CoglMatrixEntry *entry =
    _cogl_magazine_chunk_alloc (cogl_matrix_stack_magazine);

  entry->ref_count = 1;
  entry->op = operation;

#ifdef COGL_ENABLE_DEBUG
  entry->composite_gets = 0;
#endif

  return entry;
}

G_DEFINE_FINAL_TYPE (CoglMatrixStack, cogl_matrix_stack, G_TYPE_OBJECT);

static void
cogl_matrix_stack_dispose (GObject *object)
{
  CoglMatrixStack *stack = COGL_MATRIX_STACK (object);

  cogl_matrix_entry_unref (stack->last_entry);

  G_OBJECT_CLASS (cogl_matrix_stack_parent_class)->dispose (object);
}

static void
cogl_matrix_stack_init (CoglMatrixStack *stack)
{
}

static void
cogl_matrix_stack_class_init (CoglMatrixStackClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_matrix_stack_dispose;
}


static void *
_cogl_matrix_stack_push_entry (CoglMatrixStack *stack,
                               CoglMatrixEntry *entry)
{
  /* NB: The initial reference of the entry is transferred to the
   * stack here.
   *
   * The stack only maintains a reference to the top of the stack (the
   * last entry pushed) and each entry in-turn maintains a reference
   * to its parent.
   *
   * We don't need to take a reference to the parent from the entry
   * here because the we are stealing the reference that was held by
   * the stack while that parent was previously the top of the stack.
   */
  entry->parent = stack->last_entry;
  stack->last_entry = entry;

  return entry;
}

static void *
_cogl_matrix_stack_push_operation (CoglMatrixStack *stack,
                                   CoglMatrixOp operation)
{
  CoglMatrixEntry *entry = _cogl_matrix_entry_new (operation);

  _cogl_matrix_stack_push_entry (stack, entry);

  return entry;
}

static void *
_cogl_matrix_stack_push_replacement_entry (CoglMatrixStack *stack,
                                           CoglMatrixOp operation)
{
  CoglMatrixEntry *old_top = stack->last_entry;
  CoglMatrixEntry *new_top;

  /* This would only be called for operations that completely replace
   * the matrix. In that case we don't need to keep a reference to
   * anything up to the last save entry. This optimisation could be
   * important for applications that aren't using the stack but
   * instead just perform their own matrix manipulations and load a
   * new stack every frame. If this optimisation isn't done then the
   * stack would just grow endlessly. See the comments
   * cogl_matrix_stack_pop for a description of how popping works. */
  for (new_top = old_top;
       new_top->op != COGL_MATRIX_OP_SAVE && new_top->parent;
       new_top = new_top->parent)
    ;

  cogl_matrix_entry_ref (new_top);
  cogl_matrix_entry_unref (old_top);
  stack->last_entry = new_top;

  return _cogl_matrix_stack_push_operation (stack, operation);
}

void
_cogl_matrix_entry_identity_init (CoglMatrixEntry *entry)
{
  entry->ref_count = 1;
  entry->op = COGL_MATRIX_OP_LOAD_IDENTITY;
  entry->parent = NULL;
#ifdef COGL_ENABLE_DEBUG
  entry->composite_gets = 0;
#endif
}

void
cogl_matrix_stack_load_identity (CoglMatrixStack *stack)
{
  _cogl_matrix_stack_push_replacement_entry (stack,
                                             COGL_MATRIX_OP_LOAD_IDENTITY);
}

void
cogl_matrix_stack_translate (CoglMatrixStack *stack,
                              float x,
                              float y,
                              float z)
{
  CoglMatrixEntryTranslate *entry;

  entry = _cogl_matrix_stack_push_operation (stack, COGL_MATRIX_OP_TRANSLATE);

  graphene_point3d_init (&entry->translate, x, y, z);
}

void
cogl_matrix_stack_rotate (CoglMatrixStack *stack,
                           float angle,
                           float x,
                           float y,
                           float z)
{
  CoglMatrixEntryRotate *entry;

  entry = _cogl_matrix_stack_push_operation (stack, COGL_MATRIX_OP_ROTATE);

  entry->angle = angle;
  graphene_vec3_init (&entry->axis, x, y, z);
}

void
cogl_matrix_stack_rotate_euler (CoglMatrixStack        *stack,
                                const graphene_euler_t *euler)
{
  CoglMatrixEntryRotateEuler *entry;

  entry = _cogl_matrix_stack_push_operation (stack,
                                             COGL_MATRIX_OP_ROTATE_EULER);
  graphene_euler_init_from_euler (&entry->euler, euler);
}

void
cogl_matrix_stack_scale (CoglMatrixStack *stack,
                          float x,
                          float y,
                          float z)
{
  CoglMatrixEntryScale *entry;

  entry = _cogl_matrix_stack_push_operation (stack, COGL_MATRIX_OP_SCALE);

  entry->x = x;
  entry->y = y;
  entry->z = z;
}

void
cogl_matrix_stack_multiply (CoglMatrixStack         *stack,
                            const graphene_matrix_t *matrix)
{
  CoglMatrixEntryMultiply *entry;

  entry = _cogl_matrix_stack_push_operation (stack, COGL_MATRIX_OP_MULTIPLY);
  graphene_matrix_init_from_matrix (&entry->matrix, matrix);
}

void
cogl_matrix_stack_set (CoglMatrixStack         *stack,
                       const graphene_matrix_t *matrix)
{
  CoglMatrixEntryLoad *entry;

  entry =
    _cogl_matrix_stack_push_replacement_entry (stack,
                                               COGL_MATRIX_OP_LOAD);
  graphene_matrix_init_from_matrix (&entry->matrix, matrix);
}

void
cogl_matrix_stack_frustum (CoglMatrixStack *stack,
                            float left,
                            float right,
                            float bottom,
                            float top,
                            float z_near,
                            float z_far)
{
  CoglMatrixEntryLoad *entry;

  entry =
    _cogl_matrix_stack_push_replacement_entry (stack,
                                               COGL_MATRIX_OP_LOAD);

  graphene_matrix_init_frustum (&entry->matrix,
                                left, right,
                                bottom, top,
                                z_near, z_far);
}

void
cogl_matrix_stack_push (CoglMatrixStack *stack)
{
  CoglMatrixEntrySave *entry;

  entry = _cogl_matrix_stack_push_operation (stack, COGL_MATRIX_OP_SAVE);

  entry->cache_valid = FALSE;
}

CoglMatrixEntry *
cogl_matrix_entry_ref (CoglMatrixEntry *entry)
{
  /* A NULL pointer is considered a valid stack so we should accept
     that as an argument */
  if (entry)
    entry->ref_count++;

  return entry;
}

void
cogl_matrix_entry_unref (CoglMatrixEntry *entry)
{
  CoglMatrixEntry *parent;

  for (; entry && --entry->ref_count <= 0; entry = parent)
    {
      parent = entry->parent;
      _cogl_magazine_chunk_free (cogl_matrix_stack_magazine, entry);
    }
}

void
cogl_matrix_stack_pop (CoglMatrixStack *stack)
{
  CoglMatrixEntry *old_top;
  CoglMatrixEntry *new_top;

  g_return_if_fail (stack != NULL);

  old_top = stack->last_entry;
  g_return_if_fail (old_top != NULL);

  /* To pop we are moving the top of the stack to the old top's parent
   * node. The stack always needs to have a reference to the top entry
   * so we must take a reference to the new top. The stack would have
   * previously had a reference to the old top so we need to decrease
   * the ref count on that. We need to ref the new head first in case
   * this stack was the only thing referencing the old top. In that
   * case the call to cogl_matrix_entry_unref will unref the parent.
   */

  /* Find the last save operation and remove it */

  /* XXX: it would be an error to pop to the very beginning of the
   * stack so we don't need to check for NULL pointer dereferencing. */
  for (new_top = old_top;
       new_top->op != COGL_MATRIX_OP_SAVE;
       new_top = new_top->parent)
    ;

  new_top = new_top->parent;
  cogl_matrix_entry_ref (new_top);

  cogl_matrix_entry_unref (old_top);

  stack->last_entry = new_top;
}

/* In addition to writing the stack matrix into the give @matrix
 * argument this function *may* sometimes also return a pointer
 * to a matrix too so if we are querying the inverse matrix we
 * should query from the return matrix so that the result can
 * be cached within the stack. */
graphene_matrix_t *
cogl_matrix_entry_get (CoglMatrixEntry   *entry,
                       graphene_matrix_t *matrix)
{
  CoglMatrixEntry *current;
  int depth;

  graphene_matrix_init_identity (matrix);

  for (current = entry, depth = 0;
       current;
       current = current->parent, depth++)
    {
      switch (current->op)
        {
        case COGL_MATRIX_OP_TRANSLATE:
          {
            CoglMatrixEntryTranslate *translate =
              (CoglMatrixEntryTranslate *) current;
            graphene_matrix_translate (matrix, &translate->translate);
            break;
          }
        case COGL_MATRIX_OP_ROTATE:
          {
            CoglMatrixEntryRotate *rotate =
              (CoglMatrixEntryRotate *) current;
            graphene_matrix_rotate (matrix, rotate->angle, &rotate->axis);
            break;
          }
        case COGL_MATRIX_OP_ROTATE_EULER:
          {
            CoglMatrixEntryRotateEuler *rotate =
              (CoglMatrixEntryRotateEuler *) current;
            graphene_matrix_rotate_euler (matrix, &rotate->euler);
            break;
          }
        case COGL_MATRIX_OP_SCALE:
          {
            CoglMatrixEntryScale *scale =
              (CoglMatrixEntryScale *) current;
            graphene_matrix_scale (matrix, scale->x, scale->y, scale->z);
            break;
          }
        case COGL_MATRIX_OP_MULTIPLY:
          {
            CoglMatrixEntryMultiply *multiply =
              (CoglMatrixEntryMultiply *) current;
            graphene_matrix_multiply (matrix, &multiply->matrix, matrix);
            break;
          }

        case COGL_MATRIX_OP_LOAD_IDENTITY:
          goto applied;

        case COGL_MATRIX_OP_LOAD:
          {
            CoglMatrixEntryLoad *load = (CoglMatrixEntryLoad *) current;
            graphene_matrix_multiply (matrix, &load->matrix, matrix);
            goto applied;
          }
        case COGL_MATRIX_OP_SAVE:
          {
            CoglMatrixEntrySave *save = (CoglMatrixEntrySave *) current;
            if (!save->cache_valid)
              {
                cogl_matrix_entry_get (current->parent, &save->cache);
                save->cache_valid = TRUE;
              }
            graphene_matrix_multiply (matrix, &save->cache, matrix);
            goto applied;
          }
        }
    }

applied:

#ifdef COGL_ENABLE_DEBUG
  if (!current)
    {
      g_warning ("Inconsistent matrix stack");
      return NULL;
    }

  entry->composite_gets++;

  if (COGL_DEBUG_ENABLED (COGL_DEBUG_PERFORMANCE) &&
      entry->composite_gets >= 2)
    {
      COGL_NOTE (PERFORMANCE,
                 "Re-composing a matrix stack entry multiple times");
    }
#endif

  if (depth == 0)
    {
      switch (entry->op)
        {
        case COGL_MATRIX_OP_LOAD_IDENTITY:
        case COGL_MATRIX_OP_TRANSLATE:
        case COGL_MATRIX_OP_ROTATE:
        case COGL_MATRIX_OP_ROTATE_EULER:
        case COGL_MATRIX_OP_SCALE:
        case COGL_MATRIX_OP_MULTIPLY:
          return NULL;

        case COGL_MATRIX_OP_LOAD:
          {
            CoglMatrixEntryLoad *load = (CoglMatrixEntryLoad *)entry;
            return &load->matrix;
          }
        case COGL_MATRIX_OP_SAVE:
          {
            CoglMatrixEntrySave *save = (CoglMatrixEntrySave *)entry;
            return &save->cache;
          }
        }
      g_warn_if_reached ();
      return NULL;
    }

  return NULL;
}

CoglMatrixEntry *
cogl_matrix_stack_get_entry (CoglMatrixStack *stack)
{
  return stack->last_entry;
}

/* In addition to writing the stack matrix into the give @matrix
 * argument this function *may* sometimes also return a pointer
 * to a matrix too so if we are querying the inverse matrix we
 * should query from the return matrix so that the result can
 * be cached within the stack. */
graphene_matrix_t *
cogl_matrix_stack_get (CoglMatrixStack   *stack,
                       graphene_matrix_t *matrix)
{
  return cogl_matrix_entry_get (stack->last_entry, matrix);
}

CoglMatrixStack *
cogl_matrix_stack_new (CoglContext *ctx)
{
  CoglMatrixStack *stack = g_object_new (COGL_TYPE_MATRIX_STACK, NULL);

  if (G_UNLIKELY (cogl_matrix_stack_magazine == NULL))
    {
      cogl_matrix_stack_magazine =
        _cogl_magazine_new (sizeof (CoglMatrixEntryFull), 20);
    }

  stack->context = ctx;
  stack->last_entry = NULL;

  cogl_matrix_entry_ref (&ctx->identity_entry);
  _cogl_matrix_stack_push_entry (stack, &ctx->identity_entry);

  return stack;
}

gboolean
cogl_matrix_entry_calculate_translation (CoglMatrixEntry *entry0,
                                         CoglMatrixEntry *entry1,
                                         float *x,
                                         float *y,
                                         float *z)
{
  GSList *head0 = NULL;
  GSList *head1 = NULL;
  CoglMatrixEntry *node0;
  CoglMatrixEntry *node1;
  int len0 = 0;
  int len1 = 0;
  int count;
  GSList *common_ancestor0;
  GSList *common_ancestor1;

  /* Algorithm:
   *
   * 1) Ignoring _OP_SAVE entries walk the ancestors of each entry to
   *    the root node or any non-translation node, adding a pointer to
   *    each ancestor node to two linked lists.
   *
   * 2) Compare the lists to find the nodes where they start to
   *    differ marking the common_ancestor node for each list.
   *
   * 3) For the list corresponding to entry0, start iterating after
   *    the common ancestor applying the negative of all translations
   *    to x, y and z.
   *
   * 4) For the list corresponding to entry1, start iterating after
   *    the common ancestor applying the positive of all translations
   *    to x, y and z.
   *
   * If we come across any non-translation operations during 3) or 4)
   * then bail out returning FALSE.
   */

  for (node0 = entry0; node0; node0 = node0->parent)
    {
      GSList *link;

      if (node0->op == COGL_MATRIX_OP_SAVE)
        continue;

      link = alloca (sizeof (GSList));
      link->next = head0;
      link->data = node0;
      head0 = link;
      len0++;

      if (node0->op != COGL_MATRIX_OP_TRANSLATE)
        break;
    }
  for (node1 = entry1; node1; node1 = node1->parent)
    {
      GSList *link;

      if (node1->op == COGL_MATRIX_OP_SAVE)
        continue;

      link = alloca (sizeof (GSList));
      link->next = head1;
      link->data = node1;
      head1 = link;
      len1++;

      if (node1->op != COGL_MATRIX_OP_TRANSLATE)
        break;
    }

  if (head0->data != head1->data)
    return FALSE;

  common_ancestor0 = head0;
  common_ancestor1 = head1;
  head0 = head0->next;
  head1 = head1->next;
  count = MIN (len0, len1) - 1;
  while (count--)
    {
      if (head0->data != head1->data)
        break;
      common_ancestor0 = head0;
      common_ancestor1 = head1;
      head0 = head0->next;
      head1 = head1->next;
    }

  *x = 0;
  *y = 0;
  *z = 0;

  for (head0 = common_ancestor0->next; head0; head0 = head0->next)
    {
      CoglMatrixEntryTranslate *translate;

      node0 = head0->data;

      if (node0->op != COGL_MATRIX_OP_TRANSLATE)
        return FALSE;

      translate = (CoglMatrixEntryTranslate *)node0;

      *x = *x - translate->translate.x;
      *y = *y - translate->translate.y;
      *z = *z - translate->translate.z;
    }
  for (head1 = common_ancestor1->next; head1; head1 = head1->next)
    {
      CoglMatrixEntryTranslate *translate;

      node1 = head1->data;

      if (node1->op != COGL_MATRIX_OP_TRANSLATE)
        return FALSE;

      translate = (CoglMatrixEntryTranslate *)node1;

      *x = *x + translate->translate.x;
      *y = *y + translate->translate.y;
      *z = *z + translate->translate.z;
    }

  return TRUE;
}

gboolean
cogl_matrix_entry_is_identity (CoglMatrixEntry *entry)
{
  return entry ? entry->op == COGL_MATRIX_OP_LOAD_IDENTITY : FALSE;
}

void
_cogl_matrix_entry_cache_init (CoglMatrixEntryCache *cache)
{
  cache->entry = NULL;
  cache->flushed_identity = FALSE;
  cache->flipped = FALSE;
}

/* NB: This function can report false negatives since it never does a
 * deep comparison of the stack matrices. */
gboolean
_cogl_matrix_entry_cache_maybe_update (CoglMatrixEntryCache *cache,
                                       CoglMatrixEntry *entry,
                                       gboolean flip)
{
  gboolean is_identity;
  gboolean updated = FALSE;

  if (cache->flipped != flip)
    {
      cache->flipped = flip;
      updated = TRUE;
    }

  is_identity = (entry->op == COGL_MATRIX_OP_LOAD_IDENTITY);
  if (cache->flushed_identity != is_identity)
    {
      cache->flushed_identity = is_identity;
      updated = TRUE;
    }

  if (cache->entry != entry)
    {
      cogl_matrix_entry_ref (entry);
      if (cache->entry)
        cogl_matrix_entry_unref (cache->entry);
      cache->entry = entry;

      /* We want to make sure here that if the cache->entry and the
       * given @entry are both identity matrices then even though they
       * are different entries we don't want to consider this an
       * update...
       */
      updated |= !is_identity;
    }

  return updated;
}

void
_cogl_matrix_entry_cache_destroy (CoglMatrixEntryCache *cache)
{
  if (cache->entry)
    cogl_matrix_entry_unref (cache->entry);
}
