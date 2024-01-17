/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 *
 *
 */
#include "config.h"

#include <fribidi.h>
#include <math.h>

#include "clutter/clutter-debug.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-interval.h"
#include "clutter/clutter-private.h"

/* Help macros to scale from OpenGL <-1,1> coordinates system to
 * window coordinates ranging [0,window-size]
 */
#define MTX_GL_SCALE_X(x,w,v1,v2) ((((((x) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))
#define MTX_GL_SCALE_Y(y,w,v1,v2) ((v1) - (((((y) / (w)) + 1.0f) / 2.0f) * (v1)) + (v2))
#define MTX_GL_SCALE_Z(z,w,v1,v2) (MTX_GL_SCALE_X ((z), (w), (v1), (v2)))

typedef struct
{
  float x;
  float y;
  float z;
  float w;
} ClutterVertex4;

void
_clutter_util_fully_transform_vertices (const graphene_matrix_t  *modelview,
                                        const graphene_matrix_t  *projection,
                                        const float              *viewport,
                                        const graphene_point3d_t *vertices_in,
                                        graphene_point3d_t       *vertices_out,
                                        int                       n_vertices)
{
  graphene_matrix_t modelview_projection;
  ClutterVertex4 *vertices_tmp;
  int i;

  vertices_tmp = g_alloca (sizeof (ClutterVertex4) * n_vertices);

  if (n_vertices >= 4)
    {
      /* XXX: we should find a way to cache this per actor */
      graphene_matrix_multiply (modelview, projection, &modelview_projection);

      cogl_graphene_matrix_project_points (&modelview_projection,
                                           3,
                                           sizeof (graphene_point3d_t),
                                           vertices_in,
                                           sizeof (ClutterVertex4),
                                           vertices_tmp,
                                           n_vertices);
    }
  else
    {
      cogl_graphene_matrix_transform_points (modelview,
                                             3,
                                             sizeof (graphene_point3d_t),
                                             vertices_in,
                                             sizeof (ClutterVertex4),
                                             vertices_tmp,
                                             n_vertices);

      cogl_graphene_matrix_project_points (projection,
                                           3,
                                           sizeof (ClutterVertex4),
                                           vertices_tmp,
                                           sizeof (ClutterVertex4),
                                           vertices_tmp,
                                           n_vertices);
    }

  for (i = 0; i < n_vertices; i++)
    {
      ClutterVertex4 vertex_tmp = vertices_tmp[i];
      graphene_point3d_t *vertex_out = &vertices_out[i];
      /* Finally translate from OpenGL coords to window coords */
      vertex_out->x = MTX_GL_SCALE_X (vertex_tmp.x,
                                      vertex_tmp.w,
                                      viewport[2],
                                      viewport[0]);
      vertex_out->y = MTX_GL_SCALE_Y (vertex_tmp.y,
                                      vertex_tmp.w,
                                      viewport[3],
                                      viewport[1]);
      clutter_round_to_256ths (&vertex_out->x);
      clutter_round_to_256ths (&vertex_out->y);
    }
}

typedef struct
{
  GType value_type;
  ClutterProgressFunc func;
} ProgressData;

G_LOCK_DEFINE_STATIC (progress_funcs);
static GHashTable *progress_funcs = NULL;

gboolean
_clutter_has_progress_function (GType gtype)
{
  const char *type_name = g_type_name (gtype);

  if (progress_funcs == NULL)
    return FALSE;

  return g_hash_table_lookup (progress_funcs, type_name) != NULL;
}

gboolean
_clutter_run_progress_function (GType gtype,
                                const GValue *initial,
                                const GValue *final,
                                gdouble progress,
                                GValue *retval)
{
  ProgressData *pdata;
  gboolean res;

  G_LOCK (progress_funcs);

  if (G_UNLIKELY (progress_funcs == NULL))
    {
      res = FALSE;
      goto out;
    }

  pdata = g_hash_table_lookup (progress_funcs, g_type_name (gtype));
  if (G_UNLIKELY (pdata == NULL))
    {
      res = FALSE;
      goto out;
    }

  res = pdata->func (initial, final, progress, retval);

out:
  G_UNLOCK (progress_funcs);

  return res;
}

static void
progress_data_destroy (gpointer data_)
{
  g_free (data_);
}

/**
 * clutter_interval_register_progress_func: (skip)
 * @value_type: a #GType
 * @func: a #ClutterProgressFunc, or %NULL to unset a previously
 *   set progress function
 *
 * Sets the progress function for a given @value_type, like:
 *
 * ```c
 *   clutter_interval_register_progress_func (MY_TYPE_FOO,
 *                                            my_foo_progress);
 * ```
 *
 * Whenever a [class@Interval] instance using the default
 * [method@Interval.compute_value] implementation is set as an
 * interval between two [struct@GObject.Value] of type @value_type, it will call
 * @func to establish the value depending on the given progress,
 * for instance:
 *
 * ```c
 *   static gboolean
 *   my_int_progress (const GValue *a,
 *                    const GValue *b,
 *                    gdouble       progress,
 *                    GValue       *retval)
 *   {
 *     gint ia = g_value_get_int (a);
 *     gint ib = g_value_get_int (b);
 *     gint res = factor * (ib - ia) + ia;
 *
 *     g_value_set_int (retval, res);
 *
 *     return TRUE;
 *   }
 *
 *   clutter_interval_register_progress_func (G_TYPE_INT, my_int_progress);
 * ```
 *
 * To unset a previously set progress function of a [alias@GObject.Type], pass %NULL
 * for @func.
 */
void
clutter_interval_register_progress_func (GType               value_type,
                                         ClutterProgressFunc func)
{
  ProgressData *progress_func;
  const char *type_name;

  g_return_if_fail (value_type != G_TYPE_INVALID);

  type_name = g_type_name (value_type);

  G_LOCK (progress_funcs);

  if (G_UNLIKELY (progress_funcs == NULL))
    progress_funcs = g_hash_table_new_full (NULL, NULL,
                                            NULL,
                                            progress_data_destroy);

  progress_func =
    g_hash_table_lookup (progress_funcs, type_name);

  if (G_UNLIKELY (progress_func))
    {
      if (func == NULL)
        {
          g_hash_table_remove (progress_funcs, type_name);
          g_free (progress_func);
        }
      else
        progress_func->func = func;
    }
  else
    {
      progress_func = g_new0 (ProgressData, 1);
      progress_func->value_type = value_type;
      progress_func->func = func;

      g_hash_table_replace (progress_funcs,
                            (gpointer) type_name,
                            progress_func);
    }

  G_UNLOCK (progress_funcs);
}

ClutterTextDirection
clutter_unichar_direction (gunichar ch)
{
  FriBidiCharType fribidi_ch_type;

  G_STATIC_ASSERT (sizeof (FriBidiChar) == sizeof (gunichar));

  fribidi_ch_type = fribidi_get_bidi_type (ch);

  if (!FRIBIDI_IS_STRONG (fribidi_ch_type))
    return CLUTTER_TEXT_DIRECTION_DEFAULT;
  else if (FRIBIDI_IS_RTL (fribidi_ch_type))
    return CLUTTER_TEXT_DIRECTION_RTL;
  else
    return CLUTTER_TEXT_DIRECTION_LTR;
}

ClutterTextDirection
_clutter_find_base_dir (const gchar *text,
                        gint         length)
{
  ClutterTextDirection dir = CLUTTER_TEXT_DIRECTION_DEFAULT;
  const gchar *p;

  g_return_val_if_fail (text != NULL || length == 0, CLUTTER_TEXT_DIRECTION_DEFAULT);

  p = text;
  while ((length < 0 || p < text + length) && *p)
    {
      gunichar wc = g_utf8_get_char (p);

      dir = clutter_unichar_direction (wc);

      if (dir != CLUTTER_TEXT_DIRECTION_DEFAULT)
        break;

      p = g_utf8_next_char (p);
    }

  return dir;
}

PangoDirection
clutter_text_direction_to_pango_direction (ClutterTextDirection dir)
{
  switch (dir)
    {
    case CLUTTER_TEXT_DIRECTION_RTL:
      return PANGO_DIRECTION_RTL;
    case CLUTTER_TEXT_DIRECTION_LTR:
      return PANGO_DIRECTION_LTR;
    default:
    case CLUTTER_TEXT_DIRECTION_DEFAULT:
      return PANGO_DIRECTION_NEUTRAL;
    }
}
