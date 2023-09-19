/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "cogl-config.h"

#include <glib.h>
#include <string.h>

#include "cogl/cogl-util.h"
#include "cogl/cogl-types.h"
#include "cogl/cogl-object-private.h"
#include "cogl/cogl-gtype-private.h"

COGL_GTYPE_DEFINE_BASE_CLASS (Object, object);

void *
cogl_object_ref (void *object)
{
  CoglObject *obj = object;

  g_return_val_if_fail (object != NULL, NULL);

  obj->ref_count++;
  return object;
}

void
_cogl_object_default_unref (void *object)
{
  CoglObject *obj = object;

  g_return_if_fail (object != NULL);
  g_return_if_fail (obj->ref_count > 0);

  if (--obj->ref_count < 1)
    {
      void (*free_func)(void *obj);

      COGL_OBJECT_DEBUG_FREE (obj);
      free_func = obj->klass->virt_free;
      free_func (obj);
    }
}

void
cogl_object_unref (void *obj)
{
  void (* unref_func) (void *);

  g_return_if_fail (obj != NULL);

  unref_func = ((CoglObject *) obj)->klass->virt_unref;
  unref_func (obj);
}

void
cogl_debug_object_foreach_type (CoglDebugObjectForeachTypeCallback func,
                                void *user_data)
{
  GHashTableIter iter;
  unsigned long *instance_count;
  CoglDebugObjectTypeInfo info;

  g_hash_table_iter_init (&iter, _cogl_debug_instances);
  while (g_hash_table_iter_next (&iter,
                                 (void *) &info.name,
                                 (void *) &instance_count))
    {
      info.instance_count = *instance_count;
      func (&info, user_data);
    }
}

static void
print_instances_cb (const CoglDebugObjectTypeInfo *info,
                    void *user_data)
{
  g_print ("\t%s: %lu\n", info->name, info->instance_count);
}

void
cogl_debug_object_print_instances (void)
{
  g_print ("Cogl instances:\n");

  cogl_debug_object_foreach_type (print_instances_cb, NULL);
}
